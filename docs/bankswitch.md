# How the firmware serves images over 32 KB

Read from a static disassembly of firmware 1.1.13a (flash dump, offset 0 = 0x10000000). Later checked on a console: the
plain, PB6 and 256 KB flash schemes serve games, and Telengard's 256 KB save (section 8) works;
the stream scheme (section 6) has not been run. Every statement is marked **confirmed** (the code says so, with the address) or **inferred** (a reading of
what the code is for, or of the user's files). The interpolator constants `0x141c8`, `0x145c8`, `0x139ca` are in the
same order next to each other in 1.1.9a, 1.1.11a and 1.1.12a (searched in each image), so the set of schemes has not
changed across those versions; only 1.1.13a was disassembled.

Addresses `0x1000xxxx` are flash. The server loops run from SRAM: the firmware copies flash `0x10f5645c..` to SRAM
`0x20000198..0x20004cfc` at start-up (copy table at `0x1000019c`), so SRAM `0x2000xxxx` is at flash
`0x10f5645c + (a - 0x20000198)`.

## Summary

| scheme | file | selector | what the 6809 sees | loop (SRAM) |
|---|---|---|---|---|
| plain | 1 byte to 32 KB | none | `$0000-$7FFF` | `0x20000198` (`0x20000218` if the game has RAM) |
| PB6 | exactly 64 KB (`0x10000`) | GPIO22 = PB6 | two 32 KB banks at `$0000-$7FFF` | `0x20000198`, other interpolator setting |
| flash | exactly 256 KB (`0x40000`) | GPIO22 and GPIO23 | four banks of 48 KB at `$0000-$BFFF`, flash chip commands | `0x20000404` |
| stream | over 256 KB | a write of `$02` to `$xxFF` | a 507-byte stub in two 32 KB banks, fed from the card | `0x20000544` |

The .bin launcher refuses everything else: 128 KB (`0x20000`), 192 KB, and every size between 32 KB and 256 KB except
64 KB. Built-in games use the same schemes: 64 KB (`Chimney Hunt 64`) is PB6, 192 KB unpacked (`Telengard`,
`VectorbladeV1.13`) is flash, and six (`Arcade Jason`, `BadApple`, `BadAppleDE`, `BlaBla`, `LaLinea`, `Tank`; sizes
`0x85801` and up) are stream.

## 1. How the firmware chooses the scheme (confirmed)

By file size only. There is no header or signature check anywhere on the path.

`0x10000c28` is the .bin launcher (called from the browse code at `0x10000df8` and for `autostart.bin` at
`0x10000d5e`). It mallocs `0x30000`, calls the loader `0x10000944(path, buf)`, which returns a size, and then:

| loader returns | goes to |
|---|---|
| `<= 0` | give up (`0x10000c46`) |
| `> 0x40000` | `0x10000a70` (stream) |
| `== 0x30000` | `0x10000b28` (flash) |
| anything else | CRC-32 of the buffer (`0x10019850` is zlib's crc32), per-game record lookup by that CRC (`0x10005c64`), `0x10005a60(ctx, buf, size, 0)`, then `0x10000584` (the poll loop) |

The loader `0x10000944` opens the file and looks at its length (`[sp+0x20]`, `0x1000095e`):

| file length | action | returns |
|---|---|---|
| 0 | fail (`0x10000960`) | -1 |
| `<= 0x8000` | plain read into the buffer (`0x1000098a`) | the length |
| `0x10000` | `0x10000a1c`: PB6 re-ordering, section 3 | `0x10000` |
| `0x40000` | `0x100009c2`: flash re-ordering, section 4 | `0x30000` |
| `> 0x40000` | `0x100009a6`: copies a 507-byte stub (`0x1fb` bytes at `0x10f2fdf0`) to buffer+0 and buffer+0x8000 | the length |
| anything else (`0x8001-0xffff`, `0x10001-0x3ffff`) | fail (`0x10000976`) | -1 |

So a 128 KB file such as `Telengard_128kFlash.bin` is refused by this firmware: it falls through every comparison
(`cmp 0x40000; bhi; beq` at `0x10000962-0x10000968`, then `cmp 0x10000`, then `cmp 0x8000; bls`).

Built-in games are launched by `0x100006f0` (called at `0x10000e04`); the record is `{ptr, size, packed | flags << 24,
crc, name, index}`. It chooses on the record's size (`0x1000073e-0x10000754`): `<= 0x8000` plain, `0x10000` PB6,
`0x30000` flash, otherwise stream (`0x10000498`). Bit 0 of the flags byte (passed as the fourth argument of
`0x10005a60`, `0x1000079a`) means "the game has RAM".

`0x10005a60(ctx, buf, size, flags)` (confirmed): stores `ctx+0x10 = buf`, `ctx+0x14 = size`, `ctx+0x08 = 0`, and if
`flags & 1` forces `size = 0x8000` and sets `ctx+0x18 = 1`. `ctx+0x08` is the reset-hold counter (section 7), `ctx+0x0c`
the 1 KB console-RAM mirror (high scores), `ctx+0x1c` the toggle counter of the stream scheme.

Core 1's entry `0x10005b74` configures the interpolators (`0x10005adc`, section 5) and then picks the loop from
`ctx+0x14` and `ctx+0x18` (`0x10005b92-0x10005bac`):

    size == 0x30000                 -> thunk 0x100504b8 -> SRAM 0x20000404   (flash)
    size  > 0x30000                 -> thunk 0x10050408 -> SRAM 0x20000544   (stream)
    size  < 0x30000, ctx+0x18 set   -> thunk 0x10050468 -> SRAM 0x20000218   (plain with RAM)
    size  < 0x30000, ctx+0x18 clear -> thunk 0x10050498 -> SRAM 0x20000198   (plain, also PB6)

Each thunk is `ldr pc, [pc]` followed by the SRAM address (`0x20000405` etc., Thumb bit set).

## 2. Plain (confirmed)

Loop `0x20000198`: wait until GPIO31 (the clock line) is low; if `(g & 0x6000000) == 0x4000000` (R/W high, A15 low) put
`byte[peek_full]` on D0-D7 (`0x200001ba-0x200001dc`), where `g` is the whole GPIO input word. Otherwise: if
`(0x73ffe00 & ~g) == 0`, add one to the reset-hold counter `ctx+8`; else clear it, and if
`(g & 0x7380000) == 0x3080000` (a write with A15 = A14 = 1, A13 = A12 = 0, A11 = 1: `$C800-$CFFF`, the console's RAM)
store the byte into the RAM mirror through interpolator 1 (`0x200001ea-0x200001fe`). The ROM image is read-only.

The "game has RAM" loop `0x20000218` is the same with one more case: a write with A15 = 0 (`cbnz r2` at `0x2000023a`
falls through to `0x2000023c-0x20000244`) stores `g`'s low byte at the interpolated address, so `$0000-$7FFF` is
RAM-backed. The size is forced to 32 KB. The repo's `cart_serve(..., writable)` already does this.

Interpolator 0 (`0x10005adc`), per size: below 16 KB lane 0 masks A0-A13 to the size (`8 | msb << 10`); 16 KB:
`0x3408`; 32 KB: lane 1 `0x139ca` = GPIO24 (A14) into bit 14. The repo's `core1_server` has exactly these.

## 3. PB6, 64 KB (confirmed layout; the role of GPIO23 inferred)

Interpolator 0 for size `0x10000` is lane 0 `0x3408` (A0-A13 from GPIO8-21) and lane 1 `0x141c8`
(`0x10005b52`; shift 8, mask bits 14-16, cross input). The buffer offset is therefore

    offset = A0..A13 | GPIO22 << 14 | GPIO23 << 15 | GPIO24 << 16        (GPIO24 = A14)

so the buffer is eight 16 KB chunks, chunk index = `PB6 + 2 * GPIO23 + 4 * A14`. GPIO22 is PB6 (SDK `uvm2_bus.h`:
`UVM2_PB6_MASK 0x00400000`; the pad has a pull-up, `UVM2_PULLUP_PINS 0xA8C00000`). GPIO23 is `/IRQ` in the SDK's map.

The loader fills this layout by re-ordering the file (`0x10000a1c-0x10000a62`): source chunk `n = 0..3` (read in file
order, 16 KB each) goes to destination chunk `d = 4 * (n & 1) + (n >> 1)`, that is 0, 4, 1, 5, and is then copied again
32 KB higher (`memcpy` at `0x10000a56`, destination `+0x8000`), that is to chunks 2, 6, 3, 7. With `bank = n >> 1`
(file offset `0x8000 * bank`) and `a14 = n & 1`: chunk `bank + 4 * a14`, and its copy at `bank + 2 + 4 * a14`.
**The copy makes GPIO23 irrelevant**: the 6809 sees file byte `0x8000 * PB6 + A0..A14` whatever GPIO23 does. That is
the usual Vectrex 64 KB scheme (PB6 selects the 32 KB half). The built-in `Chimney Hunt 64` is unpacked in the same
order (`0x10000824-0x10000888`).

Bank at power-on: **inferred**. PB6 and GPIO23 have pull-ups on the pads (`uvm2_bus.c`), so before the game drives
PB6 the bank is the upper half. The user's 64 KB files (11 of them) all have the `g GCE` header at offsets `0x0000` and
`0x8000`, which fits a design where either half can be the first one read.

## 4. Flash, 256 KB file / 192 KB image (confirmed layout and commands; GPIO23's role inferred)

The 256 KB file is four 64 KB blocks, each with its first 48 KB used and the last 16 KB unused (all `0xFF` in the
user's two 256 KB files). The loader reads the first three 16 KB chunks of each block (`0x100009c2-0x100009fc`: an
`lseek` to `block * 0x10000`, then three reads of `0x4000`) into destination chunk `d = block + 4 * j` for
`j = 0..2` (`orr r1, r4, r6` with r4 = 0, 4, 8 and r6 = the block). The image is `0x30000` bytes and the loader
returns `0x30000`, which is what makes `0x10000c70` take the flash path. A built-in (`0x1000074a-0x100007ae`, unpack at
`0x100007bc-0x1000080c`) uses the same destination order and reads the same 12 chunks as one stream, but its data
starts one byte after the record's pointer: that byte is the number of the 4 KB page used for saving (section 8).
Checked against the user's files: the unpacked `VectorbladeV1.13` equals the 12 chunks of `VectorbladeV1.13.bin`
except for 237 bytes (a version difference).

Interpolator 0: lane 0 `0x3408`, lane 1 `0x145c8` (`0x10005b64`; shift 8, mask bits 14-17):

    offset = A0..A13 | GPIO22 << 14 | GPIO23 << 15 | A14 << 16 | A15 << 17
    chunk  = (GPIO22 + 2 * GPIO23) + 4 * (A14 + 2 * A15)        (A15 A14 = 3 never reaches here)

That is: the **bank** is `GPIO22 + 2 * GPIO23` (0 to 3, the file block) and the 6809 sees 48 KB of it at
`$0000-$BFFF`, three 16 KB windows chosen by A14/A15. **A15 is not ignored: the loop answers reads of
`$8000-$BFFF`.**

Loop `0x20000404` (SRAM), per bus cycle:

1. wait for GPIO31 low; `r3 = (g >> 24) & 3` is A14 and A15 (`0x20000418`).
2. A15 A14 = 3 (`$C000-$FFFF`): the reset-hold counter and the RAM mirror as in the plain loop (`0x200004d8-0x20000510`).
   Nothing is served here.
3. R/W high (`0x20000424-0x20000426`): serve `byte[peek_full]` as in the plain loop. **Any** read with A15 A14 not
   both 1 is served, with no other address check.
4. a write: a state machine on the low 22 bits of `g` (`0x2000045c`, jump table at `0x20000466`). With
   `key = g & ~(0xfe000000 | 0x00c00000)` (so the bank bits and A15 do not count):

| state | event | next |
|---|---|---|
| 0 | `key == 0x11555aa` (`$AA` written to `$5555`) | 1 (`0x2000051c`) |
| 1 | `key == 0x2aaa55` (`$55` written to `$2AAA`) | 2 (`0x200004ae`) |
| 2 | `(g & 0x13fff00) == 0x1155500` (address `$5555`) and data `$90` | autoselect, then 0 (`0x200004c2-0x200004ce`, `0x20000514`) |
| 2 | same address and data `$A0` | 3 (`0x200004d4`) |
| 3 | any write | store the data byte at the current interpolated address (`0x2000049e`), then 0 |
| any other | anything else | 0 (`0x20000472`) |

Autoselect (`0x200002b0`): while active, every read (A15 A14 not 3) returns `0xBF` if A0 = 0 and `0xB6` if A0 = 1
(`0x200002ec-0x20000302`), the SST39SF020 manufacturer and device codes; a write of `$F0` (`0x200002dc-0x200002e0`)
ends it. Erase: states 4, 5, 6 (`$AA` at `$5555`, `$55` at `$2AAA`, `$30` = erase the 4 KB sector, handler
`0x2000034c`, which writes `0xFF` over 4096 bytes) exist in the jump table, but nothing in the function sets state 4
(the `$5555` handler at `0x200004c2` knows only `$90` and `$A0`, so `$80` resets to 0): **inferred unreachable in
1.1.13a**, so a sector erase does nothing there. Programming overwrites the byte (no AND with the old value).

Which bank is selected at which moment is the live state of GPIO22/GPIO23; the loop has no bank register and no write
that changes the bank. **Inferred**: GPIO22 is PB6; GPIO23 is `/IRQ` in the SDK's pin map, and the firmware uses it as
the second bank line, so a game selecting banks 2 and 3 must make that pad go low. What drives it on the real
cartridge is not known from the firmware. The user's `Telengard_256kFlash.bin` has block 2 = block 0 and block 3 =
block 1 (byte for byte), so it works whatever GPIO23 does; `VectorbladeV1.13.bin` has four different blocks.

Bank at power-on: **inferred** as for PB6 (both pads pull up, so bank 3). Every block of both 256 KB files starts with
a `g GCE` header.

Writes survive only in RAM here. The firmware copies changed 4 KB pages back (section 8).

## 5. Interpolator settings, decoded (confirmed)

`0x10005adc` writes lane 0 control at `SIO+0xac` and lane 1 at `SIO+0xb0`, and `BASE2 = ctx+0x10` at `SIO+0x90`.

| value | shift | mask bits | cross input |
|---|---|---|---|
| `0x3408` | 8 | 0-13 | no |
| `0x139ca` | 10 | 14 | yes |
| `0x141c8` | 8 | 14-16 | yes |
| `0x145c8` | 8 | 14-17 | yes |

A "cross input" lane 1 reads ACCUM0, the word the loop stored; `PEEK_FULL` (`SIO+0xa8`) is `BASE2 + lane 0 + lane 1`.
Interpolator 1 is `0x2408` (A0-A9) with base `ctx+0xc`: the 1 KB console-RAM mirror (`0x10005b76-0x10005b8a`), used by
the firmware to keep the high score (`0x10000584`).

## 6. Stream, over 256 KB (confirmed by disassembly; implemented in cartstream.c, not yet run on a console)

Loop `0x20000544`: reads as the plain loop with the 32 KB setting (`0x139ca`); a write with A15 = 0, R/W = 0, A0-A7 =
`0xFF` and data `$02` (`(g & 0x600ffff) == 0xff02`, `0x2000055e-0x20000578`) flips a bit, sets `BASE2 = buf + (bit << 15)`
and increments `ctx+0x1c` (`0x200005ce-0x200005e8`). The ARM side (`0x10000a70` for a file, `0x10000498` for a built-in)
puts the 507-byte stub from `0x10f2fdf0` in both 32 KB banks, then keeps refilling `buf + (n & 1) * 0x8000 + 0x4000`
with 1536 bytes (`0x600`) of the file every time the counter moves (`0x10000ad8-0x10000b0e`). So the game is delivered
through the 6809 by a stub that is not ours.

In detail: before the server starts, pieces 0 and 1 of the file go to `$4000` of banks 0 and 1. Then, with `n` the
pieces refilled so far, each time the counter differs from `n` the next piece goes to bank `n & 1` (the one the player
just left) and `n` goes up. At the end of the file it seeks back to 0, so a part-filled last piece is left as it is.
Built-ins (`0x10000498`) do the same with zlib (`inflate` with `avail_out = 0x600`; at the end of the stream
`inflateReset` and start again).

The stub is VIDE's `projects/ExtremePlayer/ExtremePlayer_0.bin` byte for byte (507 bytes, CRC-32 `0x48e234b7`, header
"VIDEOPLAYER"; at `0xf2f978`, `0xf2fa08`, `0xf2fa50` and `0xf2fdf0` in 1.1.9a to 1.1.13a). The protocol is Malban's
"Extreme Multicart" one (the PiTrex's `vecx.videoplayer` emulates it, `writeExtreme` in `vectrexBase/vecx.c`), and the
built-in videos are VIDE's `BadApple30`, `BadAppleDE`, `BlaBla30` and `LaLinea30` (same CRC-32).

## 7. Reset (confirmed)

All loops count consecutive bus cycles with `(0x73ffe00 & ~g) == 0` (R/W high, A15 A14 = 3, A1-A13 = 1: the `$FFFE`
vector fetch) in `ctx+8`; the poll loop `0x10000584` (every 100 ms, `0x1000061e`) leaves when it is above `0x2dc6bf`
(2,999,999). The repo already does this. Reads of `$C000-$FFFF` are never served. The contents of the buffer beyond a
short file are whatever `malloc` returned (not cleared).

## 8. Saving (confirmed; the file save is implemented, the built-in one is not)

- A 256 KB .bin (`0x10000b28`) re-opens the file for writing (`f_open` flag 2), computes a CRC per 4 KB page of the
  192 KB image (48 pages), and every 100 ms writes any changed page back to the file at
  `block * 64 KB + chunk * 16 KB + page * 4 KB` (`0x10000be0-0x10000bea`, `0x10012954` is `f_write`).
- A built-in flash game (`0x10000304`) keeps its save page (the byte before its packed data) in a 4 KB page of the
  firmware's own flash (`0x14000000 + ...`) and copies the page in and out.

## What the repo implements

Plain (already), PB6, flash with its program and autoselect commands (the 64 KB and 256 KB file layouts and the two
built-in forms above), and stream (files and built-ins; the player is found in flash by `fw_stream_player`, built-ins
are unpacked as they play by `zinflate_stream` with a 32 KB window). The 256 KB file save (cartserve.c `watch_flash`, cartload.c `save_page`; the image order is the firmware's, so `cart_flash_file_offset` is its arithmetic). Not implemented: a built-in flash game's save, sector erase (unreachable in the firmware), and 128 KB
(refused by the firmware; the user's `Telengard_128kFlash.bin` is byte for byte the first two blocks of
`Telengard_256kFlash.bin`, but nothing in the firmware says how a 128 KB cartridge is wired).
