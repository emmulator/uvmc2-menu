# Favorites (button 3) — implementation plan

**Goal.** Button 3 on a game marks it as a favorite, or unmarks it. A favorite stays where it is and is also listed
in a FAVORITES folder at the top level, the way `also = FAVORITES` worked on the PiTrex menu. The choice is kept on
the card in `menu/favorites.ini`, so it survives power-off, and the menu shows the change at once.

**Confirmed facts (checked by the orchestrator, build on them):**
- Buttons: `src/main.c` `update()` reads `pressed(3)` and throws it away (`(void) pressed(3);`). Button 1 is back,
  2 opens the notes page (`s_notes`), 4 starts or opens. Nothing else uses 3.
- `add_to()` in `src/menu_tree.c` already refuses the same image twice in one category, so a game whose ini already
  says `also = FAVORITES` cannot appear twice.
- Card files reach the tree through `folder_add()` (path = `dir/name`, or `name` in the root) and image entries
  through `mt_build()`'s `NM_REC_ENTRY` branch (`e->image`). Built-ins come through `mt_add_builtin()` with the path
  `@fw/<name>` (keyed with `nmPathKey`, exactly as the `file = @fw/<name>` sections are). Their image is `@fw:<index>`.
- Ini records are scarce: `NM_MAX_RECS` is 192 and the user's card already uses about 156. RAM is also scarce: about
  30 KB is left (the records alone are 168 KB). So favorites must NOT become ini records. They get their own small
  list, read from `menu/favorites.ini` by a separate reader.
- `uvm2_sd_write(path, data, n)` creates or replaces a whole file (it is how `settings.ini` is written today, and
  that works on hardware). Do not use `uvm2_sd_overwrite` (it pads the sector and puts '\n' at byte 511).
- `s_text` (32 KB, `main.c`) is free whenever the menu is not reading its files or serving a `.bin`.

**The file.** `menu/favorites.ini`, one section per favorite, in the PiTrex format, so it reads like the other menu
files and can be edited by hand:

    ; menu/favorites.ini: your favorites. Button 3 in the menu adds or removes a game; you can also edit this file.
    [VECTOR KONG]
    file = dkong.um2
    also = FAVORITES

    [RIP OFF]
    file = @fw/Rip Off
    also = FAVORITES

The menu itself only reads the `file =` lines (the section names and the `also` lines are for people reading it). It
is not read as a menu layer: `mt_layer_sort` must leave it out like `menu.ini` and `settings.ini`.

**Review focus.** Matching is by `nmPathKey`, the same key the rest of the menu uses, so case and punctuation do not
matter (`PORTS/UM2/Enduro.UM2` is `PORTS/UM2/enduro.um2`). Nothing in this plan may add an `NmRec`, raise
`NM_MAX_RECS`, or add a static buffer over 12 KB.

**Files.** Tasks 1–3: `src/menu_tree.c`, `src/menu_tree.h`, `tests/test_menu.c`. Task 4: `src/main.c`,
`tools/preview.c`, `README.md`.

**Gate (all tasks):** `make test` prints `0 failed` for both test programs with no warnings, and `make uvm2`
succeeds with no `warning:` or `error:` line for a file under `src/`, `tests/` or `tools/`.

### Task 1: The favorites list and the FAVORITES folder

Add to `menu_tree.h` and implement in `menu_tree.c`:

```c
#define MT_FAVORITES "favorites.ini"     /* in menu/; read on its own, never as a layer */
#define MT_FAV_FOLDER "FAVORITES"
#define MT_FAV_MAX 100
typedef struct { int n; char key[MT_FAV_MAX][NM_KEY_LEN]; } MtFavs;
/* The favorites in the text of favorites.ini: the nmPathKey of every "file =" value, in file order. Lines that are
 * not "file = ..." (sections, also, comments, blank) are skipped; a key already listed is not added twice; past
 * MT_FAV_MAX the rest are ignored. */
void mt_favs_read(MtFavs *f, const char *text);
int  mt_favs_has(const MtFavs *f, const char *path);   /* 1 if path (a card path or @fw/<name>) is a favorite */
```

- `MtCard` gets a last field `const MtFavs *favs;` (0: no favorites). Existing initializers with four fields stay
  valid; do not reorder the fields.
- During `mt_build`, a game whose path is a favorite is also added to `MT_FAV_FOLDER` at the top level, with the
  same title, image and record as its other copies. That covers the three ways games arrive: `folder_add` (its
  `path`), `NM_REC_ENTRY` entries (their `image`, unless it starts with `@`), and `mt_add_builtin` (`@fw/<name>`).
  `mt_add` (used by tests and nothing else for games) does not take part. The folder sorts like any other.
- `mt_layer_sort` drops `favorites.ini` (any case), the way it drops `menu.ini` and `settings.ini`.

Tests in `tests/test_menu.c` (a new `test_favorites()`, called from `main`), written first and seen failing:
1. `mt_favs_read` on a text with three sections (one `@fw/` path, one with different case from the card file, one
   repeated) gives 2 keys; comments, `also` lines and a `file` line inside a comment (`; file = x.um2`) are ignored.
2. With the test card (`fx`/`fl`) and favorites `Zeta_game.um2` and `PORTS/UM2/DEMON.UM2`: FAVORITES exists at the
   top level with exactly those two, both still in ALL GAMES, and each pair shares its title and image offsets.
3. A built-in favorite (`builtins()` card from `test_builtins`, favorite `@fw/rip off`) is in FAVORITES and in its
   own folder.
4. A game with `also = FAVORITES` in an ini AND in the favorites list appears once in FAVORITES.
5. No favorites (`favs = 0`, and an empty list): no FAVORITES folder.
6. `mt_layer_sort` drops `FAVORITES.INI` and keeps `aae.ini`.
7. 101 distinct `file =` lines: `n == MT_FAV_MAX`.

Commit title: `Favorites: a list read from favorites.ini, shown as a FAVORITES folder`

### Task 2: Adding and removing a favorite in the file's text

```c
/* text (favorites.ini as it is, may be empty) with path added as a favorite, or removed if it is one already.
 * Writes the new text to out and returns its length; *added is 1 if it was added, 0 if removed.
 * Returns -1 if the result does not fit in max, -2 if adding would make more than MT_FAV_MAX favorites. */
int mt_fav_toggle(const char *text, const char *title, const char *path, char *out, int max, int *added);
```

- Adding appends `[TITLE]\nfile = PATH\nalso = FAVORITES\n`, after a blank line if the text does not end with one.
  An empty text first gets the header comment shown under **The file** above. In the title, `[` and `]` become `(`
  and `)` (a `]` would end the section name early).
- Removing deletes every section whose `file =` value has the same `nmPathKey` as `path`: from its `[` line up to
  the next `[` line or the end, and one blank line before it if there is one. Everything else stays byte for byte
  (other sections, comments, a hand-written `also = FAVORITES, TWO PLAYERS`).
- `out` must not overlap `text`.

Tests (`test_fav_toggle()`), written first and seen failing:
1. Empty text, add `dkong.um2` titled `VECTOR KONG`: the exact expected text (header, blank line, section), `added == 1`.
2. Add a second (`@fw/Rip Off`), then remove the first by `DKONG.UM2`: the result is exactly the header plus the
   second section; `added == 0`.
3. Removing the middle one of three keeps the other two and the comments between them unchanged (compare strings).
4. Round trip: after any add, `mt_favs_read` of the result has the path; after its remove, it does not.
5. Title `A [B] C` is written as `[A (B) C]`, and `nmParseMenuText` of the result still reads the `file =` line
   (check with `mt_favs_read`).
6. `max` one byte too small returns -1; adding to a text that already has `MT_FAV_MAX` favorites returns -2, while
   removing one of them still works.

Commit title: `Favorites: add or remove one in the text of favorites.ini`

### Task 3: Keeping the place when the menu is rebuilt

Toggling rebuilds the whole tree (Task 4), which moves every node. The cursor must stay on the same game.

```c
#define MT_PLACE_DEPTH 6
typedef struct { int depth; char dir[MT_PLACE_DEPTH][MT_TITLE]; char image[160]; int cursor; } MtPlace;
void mt_place_save(MtPlace *p, const MtNav *n, const MtTree *t);    /* the folder titles from the top, the game */
void mt_place_restore(MtNav *n, const MtTree *t, const MtPlace *p); /* the same place in a rebuilt tree */
```

- Restore walks down from the root by folder title (case-insensitive). If every folder is found, the cursor goes to
  the entry with the saved image; if that is gone, to the saved cursor index, or the last entry if the folder got
  shorter. If a folder is gone (FAVORITES after its last game was removed), it stops in the deepest folder found,
  with the cursor on the position the missing folder had (clamped). `top` is set so the cursor is visible, as
  `mt_move` does.

Tests (`test_place()`), written first and seen failing, using `load()` and the favorites from Task 1:
1. Cursor on `ZETA GAME` inside ALL GAMES, rebuild with a favorite added: still on `ZETA GAME`, `dir` is ALL GAMES.
2. Cursor on the only game in FAVORITES, rebuild without favorites: at the top level, cursor index valid.
3. Cursor on entry 9 of a 12-entry folder, rebuild: `top` keeps it on screen (`cursor - top < MT_ROWS`).
4. Cursor in FAVORITES on the second of three; remove it and rebuild: in FAVORITES on what is now the second.

Commit title: `Keep the cursor on the same game when the menu is rebuilt`

### Task 4: Button 3 on the console

In `src/main.c`:
- A static `MtFavs s_favs`. `read_menu()` ends by reading `menu/favorites.ini` into `s_text` and calling
  `mt_favs_read` (no file: an empty list). The `MtCard` in `setup()` passes `&s_favs`.
- One function builds the menu (records cleared with `memset(&s_recs, 0, sizeof s_recs)`, `read_menu()`,
  `mt_build`, the MENU TOO LONG / SOME BUILT-IN GAMES NOT LISTED banner); `setup()` and the toggle both use it.
  The firmware scan stays in `setup()` only.
- In `update()`, button 3 (when no notes or info page is open) on a game — an entry whose image is a card path or
  `@fw:<n>`; nothing happens on a folder or another `@` action — toggles it:
  path = the image, or `@fw/` + `fw_game_name(&s_fw, n)` for a built-in; title = `mt_title`. Read
  `menu/favorites.ini` into the first 16 KB of `s_text`, `mt_fav_toggle` into the second 16 KB, `uvm2_sd_write`
  it. Then `mt_place_save`, rebuild, `mt_place_restore`. Message in `s_error`: `ADDED TO FAVORITES`,
  `REMOVED FROM FAVORITES`, `FAVORITES FULL` (-2), `FAVORITES FILE TOO BIG` (-1), or `COULD NOT SAVE FAVORITES`
  (write failed; the menu is not rebuilt then).
- No new static buffer except `s_favs` (9.6 KB). Put `arm-none-eabi-size build_uvm2/pico/uvmc2menu.elf` output
  from before and after in the commit body.

In `tools/preview.c`: an argument whose file name is `favorites.ini` (any case) is read with `mt_favs_read` and
passed as the card's `favs`, not parsed as a layer.

In `README.md`: a short **Favorites** section after the menu files table (button 3 adds or removes; the FAVORITES
folder; `menu/favorites.ini`, its format, that it may be edited by hand and that updates never touch it), and add
`menu/favorites.ini` to the table of files on the card.

Checks: the gate; then `make preview CARD=<a folder you make under build_host/ with a menu/ and two game files>
INIS="<that folder>/menu/favorites.ini"` lists FAVORITES with the favorite in it — paste that output in the commit
body. There is no console here: hardware is tested by the orchestrator.

Commit title: `Button 3 marks a game as a favorite`
