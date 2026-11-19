# Compact ini records: 1024 sections instead of 192 — implementation plan

**Goal.** Every `[SECTION]` in the menu's ini files becomes one record (`NmRec`, `src/pitrex/menuFile.h`). The menu
holds at most `NM_MAX_RECS` = 192 records, and the user's card already uses about 153. Make the records compact so
that 1024 fit, in no more RAM than today.

**Confirmed facts (checked by the orchestrator, build on them):**
- `NmRec` is 708 bytes, almost all of it fixed `char` arrays: `key[NM_KEY_LEN=96]`, `path[96]`, `files[24]`,
  `title[32]`, `image[96]`, `category[56]`, `p1[48]`, `p2[48]`, `needs[56]`, `also[96]`, `name[40]`, then flags and
  ints. `NmRecs` is 168720 bytes (192 records plus `pool[NM_POOL=32768]`, where note and control lines go).
  The defines come from the Makefile: `MENU_DEFS = -DNM_MAX_RECS=192 -DNM_POOL=32768 -DNM_KEY_LEN=96`.
- On the user's card (`menu.default.ini`, `aae.ini`, `catalog.ini`, `menu.ini`) the 153 records hold 11657 bytes
  of field text (76 per record, NUL bytes included) and 4784 bytes of note lines.
- The menu never uses `p1`, `p2`, `emulator`, `skip` or `hasRows`, and never calls `nmRecToSidecar`; they are left
  over from the PiTrex menu. Delete them (`hasRows` is only set by the parser; check with grep before deleting).
- A section is first parsed into `NmSection` / `SidecarText` (`menuSidecar.h`), whose fixed sizes cap each value's
  length. Those caps stay: this plan changes only how a finished section is stored (`nmEndSection`).
- Code that reads record fields: `src/menu_tree.c`, `src/main.c`, `tools/preview.c`, `tests/test_menu.c`, and
  `src/pitrex/menuFile.h` itself.
- RAM: `arm-none-eabi-size build_uvm2/pico/uvmc2menu.elf` gives text 121208, data 852, bss 397756 now; about
  10.4 KB is free. The tool is at
  `~/.local/opt/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin/arm-none-eabi-size`.

**Review focus.** The menu built from the same ini files must be the same as before, entry for entry, titles, notes,
order and all. Overriding a field in a later file must still replace it. A full pool or record table must drop
sections and count them (the MENU TOO LONG banner), never write past the end.

**Files.** Task 1: `src/pitrex/menuFile.h`, `src/pitrex/menuSidecar.h` (only to delete what Task 1 removes),
`src/menu_tree.c`, `src/menu_tree.h`, `src/main.c`, `tools/preview.c`, `tests/test_menu.c`, `Makefile` (MENU_DEFS).
Task 2: `src/main.c`, `tools/gen_stress_ini.py` (new), `README.md`.

**Gate (all tasks):** `make test` prints `0 failed` for every test program with no warnings, and `make uvm2`
succeeds with no `warning:` or `error:` line for a file under `src/`, `tests/` or `tools/`.

### Task 1: Records that keep their text in the pool

Change `NmRec` so that each text field is a 16-bit position in `NmRecs.pool` rather than an array:

- Fields kept: `kind`, `hide`, `order`, `nrows`, `nctl` (chars); `key`, `path`, `files`, `title`, `image`,
  `category`, `needs`, `also`, `name` (text, as pool positions); `seq` (a `uint16_t` is enough: it counts sections read); `rows`, `ctl` (where the note and control
  lines start). Remove `p1`, `p2`, `emulator`, `skip`, `hasRows` and `nmRecToSidecar`.
- Pool positions are `uint16_t` counted in 2-byte units (strings start on even bytes), so a pool of up to 128 KB is
  addressable. Position 0 is a shared empty string: an unset field reads as `""`, never as a null pointer. `rows`
  and `ctl` may use the same units.
- One accessor, used everywhere a field is read:
  `static inline const char *nmStr(const NmRecs *r, uint16_t at)` (in `menuFile.h`), e.g. `nmStr(r, e->category)`.
  Code that checked `e->image[0]` now checks `nmStr(r, e->image)[0]`. `menu_tree.c` and `main.c` get the
  `NmRecs *` they need, which they already have in every place that reads a record (`s_recs`, `MtTree` builders).
- Setting a field when a later section overrides it adds the new text to the pool and moves the position; the old
  text stays unused. A text that would not fit in the pool leaves the field unchanged and counts in `dropped`.
- `nmFindRec` and `nmFindRecByName` compare through `nmStr`.
- Makefile `MENU_DEFS`: `-DNM_MAX_RECS=1024 -DNM_POOL=114688 -DNM_KEY_LEN=96`. Add
  `_Static_assert(sizeof(NmRec) <= 32, ...)` in `menuFile.h`.

Tests in `tests/test_menu.c`. Every existing test must pass with its expectations unchanged; adapt only how a test
reads a field (`nmStr`). New `test_compact_records()`, called from `main`, written first and seen failing:
1. `sizeof(NmRec) <= 32` and `sizeof(NmRecs) <= 150 * 1024`.
2. 1000 distinct sections (`[GAME n]` / `image = gn.um2` / `category = C<n % 20>` / `text = NOTE n`) parse with
   `n == 1000`, `dropped == 0`, and record 999's title, image, category and note line read back exactly.
3. 1025 sections give `n == 1024` and `dropped == 1`.
4. Override: `[A] image = a.um2 category = X` in one text, then `[A] category = Y` in a second: one record, image
   `a.um2`, category `Y`; then a third text with `[A] also = Z`: category still `Y`, also `Z`.
5. An unset field reads as `""` (not null) for every text field of a section that sets only `image`.
6. Pool exhaustion: with a small pool (a separate `NmRecs` built with a local `NM_POOL`, or a test-only define)
   long values stop being stored, `dropped` counts them, and a guard byte after the pool is unchanged.
7. Same menu as before: build the tree from `card/menu/menu.default.ini` plus the existing test layers and compare
   `make preview`-style output (titles, images, notes, order) with the output from before this change. Save the
   expected text in the test (generate it once from the current code before you change it).

Commit body: `arm-none-eabi-size` before and after (bss must go down), the `sizeof(NmRec)` and `sizeof(NmRecs)`
printed by the test, and `make test` / `make uvm2` results.

Commit title: `ini records keep their text in the pool: 1024 sections in less RAM than 192`

### Task 2: Measure the menu's build time on the console

There is no console here, so this task adds the measurement; the orchestrator reads it on hardware.

- `src/main.c`: time `build_menu()` (reading the ini files and `mt_build`) with `time_us_32()` into a static, and
  show it on FIRMWARE INFO as one more line, `MENU BUILD n MS`, after `SCAN n MS`. Also show
  `SECTIONS n / NM_MAX_RECS` (the record count and the limit) on the line after it.
- `tools/gen_stress_ini.py N OUT`: writes an ini file with N sections in the same shape as Task 1's test 2 (plus one
  `file = @fw/Rip Off` / `category = ORIGINAL` section every 50, so built-in lookups are exercised). Run with
  `python3 -I`. Default N 900.
- `README.md`: under the troubleshooting / FIRMWARE INFO description, one sentence each for the two new lines.

Checks: the gate; `python3 -I tools/gen_stress_ini.py 900 build_host/stress.ini` then
`make preview CARD=build_host/task4-card INIS=build_host/stress.ini` prints a last line with `dropped 0/0` and at
least 900 more rows than without it. Paste both last lines in the commit body.

Commit title: `FIRMWARE INFO shows the menu's build time and its section count`
