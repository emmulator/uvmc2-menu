# Third-party notices

uvmc2-menu is licensed under the GNU General Public License, version 3 (`LICENSE`). This file lists what it is built
from, what it is built with, and whose work it rests on, with the terms each is under. It is the author's reading of those
terms, not legal advice.

## What is in the released files

The released `autostart.um2` / `MENU.um2` and `menu/transfer.um2` are built from this repository's `src/` and
`transfer/` together with the parts below. They are all linked into the image at build time.

| component | what for | licence | copyright |
|---|---|---|---|
| [UVMC2 SDK](https://github.com/tullulah/uvmc2-sdk) v1.4.7 (`third_party/uvmc2-sdk`: `uvm2-sdk`, `rp2350-sdk`, `vpy-c`, `vectrex-draw`, `vectrex-bus`) | drawing, input, bus, SD card access, the `.um2` header | **no licence file; the author intends MIT** (see below) | Dani Ferrer ([tullulah](https://github.com/tullulah)) |
| [FatFs](https://elm-chan.org/fsw/ff/) R0.16 (inside the SDK, `third_party/uvmc2-sdk/third_party/fatfs`) | the SD card's file system | FatFs licence, BSD-style, one clause (the SDK keeps `LICENSE.txt`) | ChaN |
| [pico-sdk](https://github.com/raspberrypi/pico-sdk) 2.2.0 (`third_party/pico-sdk`) | RP2350 start-up, hardware access, boot ROM, the CMake build | BSD-3-Clause (`third_party/pico-sdk/LICENSE.TXT`) | Raspberry Pi (Trading) Ltd |
| [TinyUSB](https://github.com/hathach/tinyusb) (fetched into the pico-sdk by the build) | `menu/transfer.um2` only: the SD card as a USB drive | MIT (`third_party/pico-sdk/lib/tinyusb/LICENSE`) | hathach (tinyusb.org) |
| Rust crates the SDK's `vectrex-bus` and `vectrex-draw` use, as locked in their `Cargo.lock` (57 crates, among them `pio`, `pio-proc`, `pio-parser`) | the PIO program that drives the bus; most of the others only run at build time | MIT, Apache-2.0, dual MIT / Apache-2.0, Unlicense, CC0-1.0 (every `license` field in the locked set; none is GPL, LGPL or MPL) | their authors |

The code in `src/` and `transfer/` that is this project's own is under the GPL (`LICENSE`). All the parts above are
compatible with it: MIT, BSD, Apache-2.0 and the FatFs licence all allow it to be combined into a GPL-3.0 work.

**Inside the SDK, not Dani Ferrer's.** Read through the pinned tag: the only third-party *code* is FatFs (above; it keeps its
own notice and `LICENSE.txt`). Two smaller things are credited to their sources, not to him:
- `pitrex-sim/include/vectrex/vectrexInterface.h` (the build includes it) declares the PiTrex SDK's drawing and input calls,
  the interface of Malban's and Kevin Koster's PiTrex `pitrex-baremetal`; it is declarations only, written for the SDK's simulator,
  and the PiTrex code behind those names is not copied. `pitrex-sim/include/baremetal/ff.h` is likewise a shim of FatFs's calls.
- `uvm2-sdk/uvm2_smp.c` takes its volume steps from the ladder MAME uses for the AY-3-8910.
The SDK's prebuilt `MONITOR.BIN` and `sdbridge.elf` (not used by this menu's build) are built with TinyUSB and the pico-sdk.

**The UVMC2 SDK has no licence file yet.** Its repository (and its `uvmc2-starter-kit`) say nothing, but the author has
said he intends it to be MIT, as his `vectrex-studio` already is. Until a licence file is added upstream, that is an
intention and not a grant. This repository uses the SDK as a git submodule at a fixed tag, and does not copy it; anyone
who copies a part of it out should keep his copyright notice with it and check upstream for the licence first. The
README's build section says which tag is pinned.

**Redistributing a built `.um2`.** The image contains the SDK, FatFs, pico-sdk and (for `transfer.um2`) TinyUSB. Their
notices are in this file, and the FatFs, pico-sdk and TinyUSB licences ask that the copyright notice and conditions travel
with source (all three) or documentation (BSD-3, MIT) when redistributed. A release archive should therefore include this
file and `LICENSE`.

## Build tools (not part of what is distributed)

| tool | licence | used for |
|---|---|---|
| Arm GNU Toolchain `arm-none-eabi` 15.2.Rel1 | GPL-3.0 with the GCC and Newlib runtime exceptions; the exceptions apply to what it links in | compiles the C |
| Rust and rustup | MIT / Apache-2.0 | compiles the SDK's two Rust crates |
| CMake | BSD-3-Clause | the pico-sdk's build |
| picotool (fetched and built by the pico-sdk's CMake) | BSD-3-Clause | makes the image |
| GNU make, git, a host C compiler, Python 3 | their own (GPL, LGPL, PSF) | the build, host tests and `tools/` |
| `unicorn` (GPL-2.0), `pyelftools` (public domain, "Unlicense") | | `make stub-check`, which emulates the launch stub; development only |
| Pillow (MIT-CMU / HPND) | | `tools/font_*.py`, which draw a sheet of the font; development only |

None of these is shipped, and what they make is not covered by their licences.

## Whose work this rests on

- **The firmware** of the Ultimate Vectrex Multicart 2 is Ralf Kornmann's ("Arcade Jason"). This menu replaces the
  firmware's own menu, but runs on top of the firmware: it starts through the firmware's `autostart.um2` hook, and uses the
  firmware's high-score table, game list, video player and game-launching rules. **None of the firmware is in this
  repository or in a release**: the menu finds what it needs in the cartridge's own flash at run time. What the repo
  holds from it is facts derived by reading firmware 1.1.13a: `data/firmware-catalog-1.1.13a.json` (the 226 built-in
  games' names, sizes, CRC-32s, developers and categories), the numbers and layouts in `docs/bankswitch.md`, and code that
  does what the firmware's loops do. The title screen credits him. The firmware's own terms were not checked.
- **The hardware, and `uvmc2-sdk`:** Dani Ferrer (tullulah), as above. The `.um2` format, the SD and bus drivers and
  the drawing layer are his.
- **The menu's file format and parser** (`src/pitrex/`) are from the PiTrex menu, which is this author's own work in a
  fork of Malban's `pitrex-baremetal` (`emmulator/pitrex-baremetal`, branch `menu-redesign`; the commit is in
  `src/pitrex/SOURCE_COMMIT`). They were written for that fork, and are the same author's. Malban's own sources in
  `pitrex-baremetal` carry no licence, and none of them is copied here. The PiTrex menu keeps its licensing notes in its
  own repository.
- **VIDE and Extreme Multicart.** The firmware's video player (VIDE's `ExtremePlayer`, by Malban) and its protocol are
  what this menu starts when it streams a file over 256 KB. The player is not in this repository: the menu finds it in
  the cartridge's flash. `docs/bankswitch.md` describes the protocol as read from the firmware.
- **The font** (`src/font_data.h`) is drawn for this project.
- **Games and ROMs.** No game, BIOS or ROM is distributed here. `examples/` name real games only to show a layout.
  Anyone who puts a game on a card needs the right to.

If you hold rights in something listed here and want it credited differently, or taken out, please open an issue.
