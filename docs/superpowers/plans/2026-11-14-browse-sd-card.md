# BROWSE SD CARD, and no ALL GAMES — implementation plan

**Goal.** The default menu drops the ALL GAMES folder. Every game it held gets its own place instead, and a new
BROWSE SD CARD entry at the top level walks the card's folders. It lists the subfolders and the .um2, .bin and .vec
files in each, and starts the file you pick. Today a game in a subfolder that no ini file names cannot be reached
from the menu at all.

**Confirmed facts (checked by the orchestrator, build on them):**
- ALL GAMES is not only a duplicate list. Today it is the ONLY home of:
  1. the 124 of 226 built-ins that the firmware puts in no category (`data/firmware-catalog-1.1.13a.json`: 124 have
     no category). `mt_add_builtin()` in `src/menu_tree.c` puts these in the `@fw` folder rule's category, or in
     `"ALL GAMES"` when there is no rule. The default ini has no `@fw` rule;
  2. the files that the `[CARD FILES]` rule (root, `um2 bin vec`) and the `[PORTS]` rule (`PORTS/UM2`, `um2`) find.
     Both rules say `category = ALL GAMES` in `card/menu/menu.default.ini`.
  The categorised built-ins also go there, because `mt_add_builtin()` hard-codes `also = "ALL GAMES"` for them.
- The card scan (`card_list()` in `src/main.c`) lists the files of one folder, not its subfolders, so nothing below
  the rule folders is ever listed.
- An entry's category is always a folder: `add_everywhere()` uses `"OTHER"` when the category is empty. Nothing can
  sit at the top level next to the folders today.
- `card/menu/menu.default.ini` has two `[TRANSFER MODE]` sections. The first one, `image = @transfer`, is stale:
  `src/main.c` knows no `@transfer` action, and the second one (`menu/transfer.um2`) is the real entry.
- `load_start(image)` in `src/main.c` reads a .bin or .vec into `s_text` (32 KB). So a path that lives in `s_text`
  is overwritten while it is being used, and anything kept in `s_text` is gone after any load, including one that
  fails.
- The SD directory API is `uvm2_sd_dir_open(path, &d)` (`""` is the root), `uvm2_sd_dir_next(&d, name, max, &size)`
  (returns `UVM2_SD_DIR_FILE` 1, `UVM2_SD_DIR_FOLDER` 2 or `UVM2_SD_DIR_END` 0) and `uvm2_sd_dir_close(&d)`, in
  `third_party/uvmc2-sdk/uvm2-sdk/uvm2_sd.h`. It already skips hidden and system entries and every name starting
  with '.', and gives names unsorted in the card's order. `br_add` skips dot names anyway, for the host tests.
- RAM: `__bss_end__` is 0x2007c29c and `__HeapLimit` is 0x20080000, so 15.7 KB is free (text 117984, data 852,
  bss 395868 from `arm-none-eabi-size build_uvm2/pico/uvmc2menu.elf`). The size tool is at
  `~/.local/opt/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-size`. This whole plan may add at
  most 4 KB of static RAM.

**Review focus.** A user's own `menu.ini` must still be able to bring ALL GAMES back with ini lines alone (Task 1
says how). The browser keeps nothing in `s_text` that it needs after a load. It never writes to the card.

**Files.** Task 1: `src/menu_tree.c`, `src/menu_tree.h`, `tests/test_menu.c`, `card/menu/menu.default.ini`,
`README.md`. Task 2: `src/menu_tree.c`, `src/menu_tree.h`, `tests/test_menu.c`. Task 3: `src/browse.c`,
`src/browse.h` (new), `tests/test_browse.c` (new), `Makefile` (the `test` target and `UVM2_SRCS`). Task 4:
`src/main.c`, `card/menu/menu.default.ini`, `README.md`.

**Gate (all tasks):** `make test` prints `0 failed` for every test program with no warnings, and `make uvm2`
succeeds with no `warning:` or `error:` line for a file under `src/`, `tests/` or `tools/`.

### Task 1: Every game in a place of its own, without ALL GAMES

In `src/menu_tree.c` / `.h`:
- `#define MT_BUILTIN_OTHER "HOMEBREW"` in `menu_tree.h`: where an uncategorised built-in goes when no ini says.
- `mt_add_builtin()`: a built-in with a firmware category goes in that category, plus the `@fw` folder rule's `also`
  if there is a rule (no hard-coded `"ALL GAMES"`). A built-in without one goes in the rule's category if it has
  one, else `MT_BUILTIN_OTHER`, plus the rule's `also`. As before, a `file = @fw/<name>` section's own `category` or
  `also` replaces these. Update the comment above `mt_add_builtin` in `menu_tree.h` and the one above
  `add_builtins()`.

  The result: a user who wants the old ALL GAMES back writes `[BUILT-IN]` / `folder = @fw` / `also = ALL GAMES`
  plus `category = ALL GAMES` in `[CARD FILES]` and `[PORTS]`. README says this (below).

In `card/menu/menu.default.ini`:
- `[CARD FILES]` gets `category = MY GAMES` and `[PORTS]` gets `category = PORTS`.
- Delete the first `[TRANSFER MODE]` section (`image = @transfer`) and its text lines. Keep the second.
- In the comment block at the top, change the `[ALL GAMES]` sentence to name `[MY GAMES]` instead.
- After `[PORTS]`, a block headed `; built-in games the firmware puts in no folder (the rest go to HOMEBREW)` with
  one section per game below: `[<NAME IN CAPITALS>]`, `file = @fw/<firmware name, exactly as below>`,
  `category = <folder>`. Use the firmware's own spelling of each folder name (as in
  `data/firmware-catalog-1.1.13a.json` `categories`), so these join the folders the categorised built-ins are in.
  The orchestrator chose these 19; do not add or move any:
  - `Original`: `Rip Off`, `Solar Quest (Buttons)`
  - `Tools`: `Doodle`, `Engine Analyzer`, `morse`, `Pythagorian Theorem`, `Spectrum Color Test`, `VecTalk`,
    `VectorClock`
  - `Music`: `BLUESKYRACER_MagicalSoundShower`, `BLUESKYRACER_Splashwave`, `FunkyDrummer_v100`, `SmashTV Music`,
    `Vextrument_v100`
  - `Xmas`: `presentdrop`, `Snowball_standard`
  - `Demos`: `vecribbon-demo`
  - `Spinner`: `Space Frenzy (Spinner)`
  - `Lightpen`: `Doodle LP`

  Ini records are scarce (`NM_MAX_RECS` 192 for all the ini files together, and a user's card already uses about
  156), so add nothing else to the default file.

In `README.md`: change every sentence that says games are listed "in ALL GAMES" (lines near 116 and 167 today) to
describe the new places (MY GAMES, PORTS, HOMEBREW and the firmware's folders). Add a short paragraph, **Bringing
back ALL GAMES**, with the three ini sections above.

Tests in `tests/test_menu.c`. Update `test_builtins` and any other test that expects ALL GAMES from the default
rules, and add `test_no_all_games()`, called from `main`. Write the tests first and watch them fail:
1. With no `@fw` rule, an uncategorised built-in is only in `HOMEBREW`; a categorised one is only in its
   category. There is no ALL GAMES folder anywhere.
2. With `[BUILT-IN] folder = @fw category = MISC also = ALL GAMES`, an uncategorised built-in is in MISC and
   ALL GAMES, and a categorised one is in its category and ALL GAMES.
3. A `file = @fw/<name>` section with `category = TOYS` still wins over both (the DRUM MACHINE checks, kept).
4. Parsing `card/menu/menu.default.ini` itself (read the file in the test) with a card that has `a.um2` in the root
   and `PORTS/UM2/b.um2`: `a.um2` is in MY GAMES, `b.um2` is in PORTS, and SYSTEM has exactly one TRANSFER MODE.
5. Parsing `card/menu/menu.default.ini` with built-ins `Rip Off` (no firmware category), `Doodle LP` (none),
   `Zantis` (none) and one with firmware category `Original`: Rip Off is in the same `Original` folder node as the
   categorised one (one folder, not two differing in case), Doodle LP is in `Lightpen`, Zantis is in `HOMEBREW`.
6. The default file's records: `nmParseMenuText` of `menu.default.ini` alone uses at most 32 records (print the
   number in the test output).

Check: `make preview CARD=build_host/task4-card` shows `MY GAMES/` with DKONG and RIP and no `ALL GAMES/`. Paste
the output in the commit body.

Commit title: `Default menu without ALL GAMES: built-ins in HOMEBREW or a firmware folder, card files in MY GAMES`

### Task 2: Entries at the top level (category = /)

- An entry whose category is exactly `/` (spaces around it ignored) is added straight under the root, not in a
  folder. This applies to an ini `NM_REC_ENTRY` with `category = /` and to `also = /`. A folder rule with
  `category = /` puts its files at the top level too.
- Top-level order: FAVORITES first (as now), then the folders by title, then the top-level entries by title, then
  SYSTEM last. A listed order (`order = listed`) still wins where it applies today.
- `mt_hint` and the navigation need no change: an entry at the top level is a game or action like any other. Check
  that `mt_back` at the top level stays a no-op.

Tests (`test_top_level()`), written first and seen failing:
1. `[BROWSE SD CARD] category = / image = @browse`, with the usual test card: it is a child of the root, it is not a
   category, and it comes after the last folder and before SYSTEM.
2. Two top-level entries `B` and `A` come out as `A`, `B`.
3. ` category =  /  ` (with spaces) works the same as `/`.
4. `category = A / B` still makes a folder B in folder A (the existing behaviour, kept).
5. With no SYSTEM folder, the top-level entries are last.

Commit title: `category = / puts an entry at the top level of the menu`

### Task 3: The browser's list, host-tested

A new module that holds one card folder's listing and the place in it. It is pure C with no SD calls, and
`src/main.c` fills it (Task 4). Names live in a pool the caller passes in, so the module can use `s_text` while it
browses and needs no static buffer of its own beyond its index.

```c
/* browse.h */
#define BR_MAX   512            /* entries in one folder; more are counted in dropped */
#define BR_PATH  160            /* the folder's path from the root, "" for the root */
typedef struct
{
  char *pool; int pool_size, used;           /* names, NUL-ended, given by the caller */
  uint16_t off[BR_MAX];                      /* each entry's name in pool */
  uint8_t dir[BR_MAX];                       /* 1 if a folder */
  int n, dropped, cursor, top;
  char path[BR_PATH];
} BrList;
void br_start(BrList *b, char *pool, int pool_size, const char *path);   /* empty list for path, cursor 0 */
/* One name from the card. Adds a folder unless its name starts with '.'; adds a file only if it ends in .um2, .bin
 * or .vec (any case). Anything else is skipped. Returns 1 if added, 0 if skipped, -1 if full (counted in dropped). */
int  br_add(BrList *b, const char *name, int is_dir);
void br_sort(BrList *b);                     /* folders first, then files, each A-Z ignoring case */
void br_move(BrList *b, int d);              /* as mt_move: wraps, keeps cursor in the MT_ROWS window */
const char *br_name(const BrList *b, int i);
/* The display form of entry i: capitals, '_' as a space, any byte outside 32-95 after upper-casing as '?'.
 * Files keep their extension ("ZAPPYBIRD V150.BIN"). */
void br_title(const BrList *b, int i, char *out, int max);
/* The full path of entry i ("PORTS/UM2/x.um2", or "x.bin" in the root) into out. 0 if it does not fit. */
int  br_child_path(const BrList *b, int i, char *out, int max);
/* path's parent into out ("A/B" -> "A", "A" -> ""), and the last part (the folder we leave) into last. 0 at root. */
int  br_parent(const char *path, char *out, int max, char *last, int lmax);
int  br_find(const BrList *b, const char *name);  /* index of name, ignoring case, or -1 */
```

The row count on screen is `MT_ROWS` from `menu_tree.h`. Include it, or define `BR_ROWS 8` with a
`_Static_assert(BR_ROWS == MT_ROWS, ...)` in `browse.c`.

Tests in `tests/test_browse.c` (new program, built and run by `make test` after `test_cart`, `-Wall -Wextra`).
Write them first and watch them fail:
1. Adding `zeta.BIN`, `.hidden` (dir), `roms` (dir), `readme.txt`, `Alpha.um2`, `PORTS` (dir), `x.VEC` then
   sorting gives, in order: `PORTS`, `roms`, `Alpha.um2`, `x.VEC`, `zeta.BIN`. `.hidden` and `readme.txt` are
   skipped.
2. `br_title` of `star_wars v2.bin` is `STAR WARS V2.BIN`; a name with a byte over 127 shows `?` there.
3. `br_child_path`: root + `a.um2` is `a.um2`; path `PORTS/UM2` + `b.um2` is `PORTS/UM2/b.um2`; a result longer than
   `max` returns 0.
4. `br_parent`: `A/B/C` gives `A/B` and `C`; `A` gives `` and `A`; `` returns 0.
5. 600 files added to a 512-entry list: `n == BR_MAX`, `dropped == 88`. A pool too small for the next name also
   counts as dropped, with no write past `pool_size` (put a guard byte after the pool and check it).
6. `br_move` from 0 by -1 wraps to the last entry, and `top` keeps `cursor - top` in `0..7`.
7. `br_find(b, "ROMS")` finds `roms`.

`Makefile`: add `src/browse.c` to `UVM2_SRCS` and the `test_browse` build and run to `test`.

Commit title: `A host-tested list for browsing a card folder`

### Task 4: BROWSE SD CARD on the console

In `card/menu/menu.default.ini`, after the `[PORTS]` section:

    [BROWSE SD CARD]
    category = /
    image    = @browse
    text     = EVERY FOLDER ON THE CARD, AND THE
    text     = UM2, BIN AND VEC FILES IN IT.

In `src/main.c`:
- `static BrList s_br;` and `static int s_browsing;`. The names pool is `s_text`, which is free while browsing (see
  the facts: refill it after any load).
- A function `browse_open(const char *path, const char *select)`: `br_start` with `s_text`, then list `path` with
  `uvm2_sd_dir_open` / `_next` / `_close` into `br_add`, then `br_sort`. If `select` is not 0, put the cursor on
  `br_find(select)` and fix `top` (use `br_move`, or set it the way `br_move` does). If the folder cannot be opened,
  show `CANNOT OPEN FOLDER` in `s_error` and stay where you were.
- `@browse` in `update()`'s action list: `browse_open("", 0)` and `s_browsing = 1`.
- While `s_browsing`, `update()` handles the input before the tree code does, then returns:
  - stick up/down: `br_move`.
  - button 4 or stick right on a folder: open it (`br_child_path`, then `browse_open(that, 0)`). `PATH TOO LONG` if
    it does not fit.
  - button 4 or stick right on a file: copy `br_child_path` into a static `char s_br_file[BR_PATH]` (NOT in
    `s_text`), then `load_start(s_br_file)`. If the load fails (the menu is still running and `s_error` is set),
    call `browse_open(s_br.path...)` again, copying the path out first. The load has overwritten `s_text`. Keep the
    cursor on the same file (`select` = its name, copied before the load).
  - button 1 or stick left: at the root, `s_browsing = 0`, which goes back to the tree where it was. Elsewhere,
    `br_parent`, then `browse_open(parent, last)` so the cursor lands on the folder we came out of.
  - buttons 2 and 3 do nothing while browsing.
- `draw()` while `s_browsing`: the same layout as the tree list (`HEAD_Y`, `LIST_Y`, `ROW_H`, `>` cursor, `>` mark in
  `MARK_X` for folders, the chevrons, the `n/k` position at the top right). The heading is the folder's last part
  (`SD CARD` at the root), cut to `HEAD_CH`. Show `NOTHING FOUND` for an empty folder. Show `FOLDER TOO LONG` on the
  message row if `dropped > 0`, then any `s_error`. The hint row (size 4) reads `4 OPEN  1 BACK`. Share code with the
  tree's list drawing where it is easy. Do not change how the tree list looks.
- RAM: put the `arm-none-eabi-size` output from before and after in the commit body (path in the facts). bss may grow
  by at most 4096 bytes. `BrList` is about 1.7 KB.

In `README.md`: one short **Browse SD card** section. Say what it lists, the buttons, and that it never changes the
card. Add BROWSE SD CARD where the README lists the top-level menu.

Checks: the gate, plus `make preview CARD=build_host/task4-card` shows `BROWSE SD CARD <- @browse` at the top level,
after the folders and before `SYSTEM/`. Paste it in the commit body. There is no console here; the orchestrator
tests on hardware.

Commit title: `BROWSE SD CARD: walk the card's folders and start any .um2, .bin or .vec`
