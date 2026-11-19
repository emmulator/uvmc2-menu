# uvmc2-menu: notes for coding agents

A replacement game menu for the Ultimate Vectrex Multicart 2 (UVMC2). It is an RP2350 program (`.um2`) built with
the UVMC2 SDK and drawn on the Vectrex. README.md describes the user side; this file is what you need to change code.

## Layout

| path | what |
|---|---|
| `src/main.c` | the device program: SD card, input (`update()`), drawing (`draw()`), launching. Not host-testable. |
| `src/menu_tree.c/.h` | the menu as a tree, built from the ini records; navigation, notes, hints. Pure C, host-tested. |
| `src/pitrex/menuFile.h` | the ini parser: sections become `NmRec` records with keys (`nmPathKey`, `nmSlugKey`). |
| `src/fwcat.c/.h` | finds the firmware's built-in game list in flash. |
| `src/browse.c/.h` | BROWSE SD CARD's list of one card folder: filter, sort, paths. Pure C, host-tested (`tests/test_browse.c`). |
| `transfer/`, `transfer.mk` | `transfer.um2`, SYSTEM / TRANSFER MODE (the card as a USB drive, TinyUSB): a program of its own, built into `build_transfer/` by `make transfer` (part of `make install`). |
| `tests/test_menu.c` | host tests for menu_tree and the parser (`CHECK(x)` macro, one `test_*` function per area, called from `main`). |
| `tools/preview.c` | host dry run: prints the menu a card would get (`make preview CARD=...`). |
| `src/cart.c`, `cartserve.c`, `cartload.c`, `cartstream.c` | serving a `.bin`: size scheme and bank layout, the core 1 bus loop (scores, flash save), loading from the card, streaming. `docs/bankswitch.md` says what each copies from the firmware. |
| `src/hiscore.c/.h` | the firmware's high-score table in flash (find, save, count); host-tested with a simulated flash. |
| `tools/` | `hs_dump.py`, `scan_firmware.py`, `gen_card_ini.py`, `gen_catalog.py`, font tools. |
| `card/menu/menu.default.ini` | the shipped menu file. |
| `examples/` | `menu.ini` (parsed by `test_example_ini`: keep its section count in step) and `e-srg.ini`. |
| `LICENSE`, `THIRD_PARTY_NOTICES.md`, `CHANGELOG.md` | GPL-3.0; the dependencies and their licences; release notes. Update the notices when a dependency or a pinned version changes, and, for each release, `VERSION` (the only place the number is set: the Makefile passes it to the C build) and the changelog heading (`make dist` checks it). |
| `third_party/` | the UVMC2 SDK and pico-sdk (git submodules). Never edit. |

## Build and test

    make test      # host tests (test_menu, test_font, test_cart, test_browse, test_hiscore): each prints "N passed, 0 failed", no warnings
    make uvm2      # the cartridge build, build_uvm2/uvmc2menu.um2 (a few minutes the first time)
    make transfer  # TRANSFER MODE's program, build_transfer/transfer.um2 (its own build; fetches TinyUSB)

`make uvm2` prints warnings from the SDK's Rust crates and one linker note (`missing .note.GNU-stack`); those are
not ours. Code under `src/`, `tests/` and `tools/` must compile with no warnings at all.

Do not run `make clean`: it deletes `build_uvm2/`, and the picotool rebuild takes minutes.
There is no Vectrex here: nothing can be tested on hardware. Keep device code in `main.c` thin and put logic that
can be tested in `menu_tree.c`, with tests.

## Conventions

- C (gnu11), two-space indent, braces on their own line for functions and blocks as in the surrounding code.
- Comments say what and why, in plain words, like the existing ones. No commented-out code.
- No dynamic allocation on the device: static buffers, sized with a `#define`, with the limit checked.
- RAM is tight: the menu has about 30 KB of RAM left. Any new static buffer over 4 KB must be justified in the commit.
- Menu text is shown in capitals in the menu's own font (ASCII 33-95); keep new on-screen strings short (the list is
  21 characters wide, note lines 38).
- Never write to an SD card or a mounted card folder; never touch `card/` unless the task says so.
- Commits: a title line, then a body with the proof (tests added, what they check, the commands run and their
  results). No Co-Authored-By or other attribution trailers.
