# Favorites from BROWSE SD CARD — implementation plan

**Goal.** Button 3 on a file in BROWSE SD CARD marks it as a favorite, or unmarks it, as it does on a game in the menu.
The file then shows in the FAVORITES folder even when no ini entry or folder rule lists it, which is the usual case
for a file in a subfolder.

**Confirmed facts (checked by the orchestrator, build on them):**
- Favorites live in `menu/favorites.ini` (`file = <path>` per section). `mt_favs_read` reads them into `MtFavs`
  (`src/menu_tree.h`): `typedef struct { int n; char key[MT_FAV_MAX][NM_KEY_LEN]; } MtFavs;` with MT_FAV_MAX 100 and
  NM_KEY_LEN 96. It keeps only each path's `nmPathKey`, so the path itself is lost.
- `mt_build` puts a game in FAVORITES only when it reaches the tree some other way: `add_everywhere()` checks
  `mt_favs_has()` and adds the FAVORITES copy. A favorite whose file nothing lists never appears.
- `file_title()` (static, `src/menu_tree.c`) turns a file name into a menu title (`star_wars_v2.um2` -> `STAR WARS V2`).
- `src/main.c`: `toggle_favorite()` works on the tree's cursor. It reads `menu/favorites.ini` into the first 16 KB of
  `s_text`, calls `mt_fav_toggle`, writes the file, then rebuilds with `build_menu()` and keeps the place with
  `mt_place_save` / `mt_place_restore`. Its messages go in `s_error`.
- The browser (`s_br`, `browse_open(path, select)`, `browse_draw()`) keeps its names in `s_text`, so anything that
  uses `s_text` (the favorites file, `build_menu`) empties the list: call `browse_open(s_br.path copy, name copy)`
  again afterwards, as the failed-load path in `update()` already does.
- RAM: about 30 KB is free (`__bss_end__` 0x200789c0, limit 0x20080000). This plan may add at most 2 KB.

**Files.** Task 1: `src/menu_tree.c`, `src/menu_tree.h`, `tests/test_menu.c`. Task 2: `src/main.c`, `README.md`.

**Gate (all tasks):** `make test` prints `0 failed` for every test program with no warnings, and `make uvm2`
succeeds with no `warning:` or `error:` line for a file under `src/`, `tests/` or `tools/`.

### Task 1: A favorite that nothing else lists still shows in FAVORITES

- `MtFavs` keeps each favorite's path as written in the file (trimmed), not only its key. Store the path where the key
  was (`char path[MT_FAV_MAX][NM_KEY_LEN]`), so the struct does not grow, and compare with `nmPathKey` of both sides
  in `mt_favs_has` (case and punctuation still do not matter). `mt_favs_read`'s duplicate check compares keys too.
- At the end of `mt_build`, before `apply_categories`: every favorite that is a card path (not `@fw/...`), is not
  already in the FAVORITES folder, and exists on the card (`card->exists`) is added to `MT_FAV_FOLDER` as an entry:
  title `file_title()` of the path's last part, image the path, no record (`-1`), marked as a favorite (`mark_fav`).
  The FAVORITES folder is created if needed. Nothing else changes for favorites that do reach the tree.
- A missing file is skipped silently (it may be on another card).

Tests (`test_fav_standalone()`, called from `main`), written first and seen failing:
1. Favorites `sub/deep_game.bin` (add it to what the test card says exists; no rule lists `sub/`):
   FAVORITES has one entry titled `DEEP GAME` with image `sub/deep_game.bin`, and it is marked as a favorite.
2. A favorite that is also listed by a rule (`Zeta_game.um2`) appears once in FAVORITES, as before.
3. A favorite whose file does not exist: no entry, no FAVORITES folder if it was the only one.
4. `SUB/Deep_Game.BIN` in favorites.ini matches the file `sub/deep_game.bin` (one entry, the image as the file on the
   card reports it or as written: say which in the test, and keep it consistent).
5. `sizeof(MtFavs)` is unchanged from before (print it; compare with `MT_FAV_MAX * NM_KEY_LEN + sizeof(int)`).
6. Every existing favorites test passes unchanged.

Commit title: `A favorite that no rule lists still shows in FAVORITES`

### Task 2: Button 3 in BROWSE SD CARD

In `src/main.c`:
- While browsing, button 3 on a file (not a folder) toggles it as a favorite. Path: `br_child_path` of the cursor
  entry; title: `br_title` of it. Share the read / `mt_fav_toggle` / write / rebuild code with `toggle_favorite()`
  (one function taking a path and a title); the tree's place is kept as now. Then reopen the browser on the same
  folder with the cursor on the same file (copy the folder path and the name out first: `s_text` is overwritten).
  The messages are the tree's: ADDED TO FAVORITES, REMOVED FROM FAVORITES, FAVORITES FULL, FAVORITES FILE TOO BIG,
  COULD NOT SAVE FAVORITES. Button 3 on a folder does nothing.
- In the browser list, a favorite file gets the `'` mark in `MARK_X`, as in the tree (`mt_favs_has(&s_favs, path)`).
- The hint row: `4 OPEN  1 BACK  3 FAVORITE` when the cursor is on a file, `4 OPEN  1 BACK` on a folder.
- RAM: put `arm-none-eabi-size` (`~/.local/opt/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin/`) before and
  after in the commit body; no new static buffer over 512 bytes.

`README.md`, Browse SD card section: one sentence that button 3 marks a file as a favorite, and that it then shows in
FAVORITES even if nothing else lists it.

Checks: the gate. There is no console here; the orchestrator tests on hardware.

Commit title: `Button 3 in BROWSE SD CARD marks a file as a favorite`
