# The SDK's USB debug monitor: notes for later

Not used yet. Looked at on 2026-11-15 while chasing a stray folder start at power-on (the menu opened a folder nobody
chose; a longer wait before the stick and buttons are armed seems to have fixed it, so the monitor was not needed).

**What it is.** Part of the UVMC2 SDK since v1.4.0 (our submodule is v1.4.7), not of the firmware. A build with
`UVM2_USB_MONITOR=1` loads `MONITOR.BIN` from the card's root into PSRAM at boot and answers on USB (id 2E8A:10DB):
the program's `vpy_log("x %d\n", x)` text (`uvmc2.py log`), files onto the card, and gdb over a second serial port.
Docs: `uvmc2-starter-kit/docs/13-usb-upload-and-debug.md`. `MONITOR.BIN` is prebuilt in
`third_party/uvmc2-sdk/uvm2-sdk/prebuilt/`. The firmware (1.1.13a here) needs nothing; no newer firmware was seen.

**Checked against this project** (`make uvm2 UVM2_USB_MONITOR=1`):
- Links with no warnings from `src/`; about +1.5 KB SRAM (text +1.3 KB, data +72 B, bss +164 B); about 21 KB stay free.
- PSRAM: the monitor uses 256 KB from `0x117C0000`. We load `.um2` games from `0x15000000` and use no other PSRAM, so
  they meet only for a game over about 7.75 MB.
- It runs once at the end of each SDK frame, on the menu's core. So it works while the menu runs, and stops once a game is
  served by `cart_serve` (no more frames): it cannot watch scores during a game.
- The build turns on `UVM2_REPLAY_LAST` (redraw the last picture while no new one comes). Check the loading and Transfer
  screens still look right, and our longest frame (31 ms) against the extra per-frame work: neither measured.
- `transfer.um2` is a separate image with its own TinyUSB code: no conflict. The README's remark that the SD bridge needs the
  stock menu stays true for a normal build.

**To use it:** install `picotool` (not installed here; needs sudo), copy `MONITOR.BIN` to the card's root, build with the
flag, put the build on the card, run `third_party/uvmc2-sdk/tools/uvmc2.py log`. Keep it a debug build, out of releases.
Add `vpy_log` lines for what is wanted (stick and button values, the cursor).
