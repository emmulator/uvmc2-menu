# High scores, shared with the firmware — implementation plan

**Goal.** A game started from this menu gets the high score it had, and a new high score is kept, in the same flash
table the firmware uses, so a score set from either menu shows in both. This plan does the table and the restore.
Saving while a game runs (watching the console's writes) is the orchestrator's own task, after these two.

**Confirmed facts (read from the 1.1.13a disassembly by the orchestrator, build on them):**
- The table is the cartridge flash from `0x10FF7000` to `0x10FFF000` (32 KB, 8 sectors of 4 KB; flash offset
  `0xFF7000`). Records are 16 bytes: a 4-byte little-endian key, then 7 score bytes (the BIOS high-score slot,
  console RAM `$CBEB`-`$CBF1`: 6 characters, then `$80`), then 5 bytes left `0xFF`.
- A free record has key `0xFFFFFFFF`. A deleted record has key `0`. Lookup (firmware `0x10005c64`) returns the FIRST
  record, scanning from the start, whose key equals the wanted key; deleted ones never match a real key.
- Update (firmware `0x100006bc`..`0x100006c4`): zero the old record's key (re-program its 256-byte page with the key
  bytes 0: programming can only clear bits, so no erase is needed), then append the new record at the first free slot
  (`0x10005cd0`: read the page, put key + 7 bytes into it, program the page).
- When no slot is free (`0x10005bb4`): for each 4 KB sector in turn, copy its live records (key not `0`, `0xFFFFFFFE`
  or `0xFFFFFFFF`) to the start of a 4 KB buffer filled with `0xFF`, erase the sector, program the buffer back
  (only the pages that hold records). Then append.
- The key is zlib CRC-32 (`zcrc32` in `src/inflate.c` gives the same value) of the whole cartridge image the
  firmware serves: for a card `.bin`/`.vec`, the file's bytes; for a built-in game, the CRC in the firmware's game list
  (`FwGame.crc`, `src/fwcat.h`), the CRC of the unpacked image.
- The console keeps `$CBEB` over the warm start that launching a game does (`JMP $F000` with `Vec_Cold_Flag`
  already set), so writing the 7 bytes there before the 6809 runs restores the score (see `vectrex-dev` skill,
  hardware.md, "Persistent high score").
- `src/cartserve.c` `start()` already writes console RAM while the 6809 is halted: `uvm2_mem_write(0xCBFBu, ...)`.

**Files.** Task 1: `src/hiscore.c`, `src/hiscore.h` (new), `tests/test_hiscore.c` (new), `Makefile` (test target and
`UVM2_SRCS`). Task 2: `src/cartserve.c`, `src/cartserve.h`, `src/main.c`, `README.md`.

**Gate (all tasks):** `make test` prints `0 failed` for every test program with no warnings, and `make uvm2`
succeeds with no `warning:` or `error:` line for a file under `src/`, `tests/` or `tools/`.

### Task 1: The score table, host-tested

Pure C, no SDK calls. The table is read through a pointer (on the console, the flash at `0x10FF7000`) and changed
only through two callbacks, so tests can use a simulated flash.

```c
/* hiscore.h */
#define HS_BYTES   0x8000u                  /* the table */
#define HS_SECTOR  0x1000u
#define HS_PAGE    0x100u
#define HS_REC     16u
typedef struct
{
  void (*program)(uint32_t offset, const uint8_t *page, void *u);   /* one 256-byte page at offset in the table */
  void (*erase)(uint32_t offset, void *u);                           /* one 4 KB sector at offset in the table */
  void *u;
} HsFlash;
int  hs_valid(const uint8_t score[7]);      /* 6 bytes ' ' or '0'-'9', then 0x80 */
/* The score saved for key: 1 and its 7 bytes in out, or 0. As the firmware: the first record with this key. */
int  hs_find(const uint8_t *table, uint32_t key, uint8_t out[7]);
int  hs_count(const uint8_t *table);        /* live records */
/* Keep score for key, as the firmware does: nothing if the first record for key already holds it; else zero that
 * record's key, append, and compact sector by sector first if no slot is free. Returns 1 if written, 0 if
 * unchanged, -1 if the table is full even after compacting, -2 if the score is not valid (nothing written). */
int  hs_save(const uint8_t *table, uint32_t key, const uint8_t score[7], const HsFlash *f);
```

`hs_save` reads the table only through `table` and writes only through `f`; a 256-byte page buffer and a 4 KB sector
buffer may be static (8 KB at most, justify in the commit).

Tests in `tests/test_hiscore.c` (new program in `make test`, `-Wall -Wextra`), with a simulated NOR flash (program
ANDs bytes in, erase sets 0xFF; count the calls). Write them first and watch them fail:
1. Empty table: `hs_find` is 0, `hs_count` is 0. Save key 0x12345678 `"  1230\x80"`: one program call, then
   `hs_find` returns those bytes, and the record bytes 11..15 are 0xFF.
2. Saving the same score again: returns 0, no flash call.
3. A new score for the same key: the old record's key reads 0, the new record is the next slot, `hs_find` returns the
   new score, `hs_count` is 1.
4. A table built byte-by-byte as the firmware leaves it (a deleted record, then two live keys) reads back right.
5. Full table (2048 records, half of them deleted): one more save erases sectors, keeps every live score (check all of
   them with `hs_find`), and the new score is found. Erase calls count is at most 8.
6. Full table with every record live: returns -1 and the table is unchanged.
7. `hs_valid`: `"  1230\x80"` and `"     0\x80"` are valid; a missing `$80`, a letter, or a 0 byte are not; `hs_save`
   with an invalid score returns -2 and makes no flash call.

Commit title: `The firmware's high-score table: find, count and save, host-tested`

### Task 2: Restore the score when a game starts; count them on DIAGNOSTICS

- `src/cartserve.c` / `.h`: `void cart_set_score(const uint8_t score[7]);` keeps the 7 bytes (or `cart_set_score(0)`
  clears them). `start()` writes them to `$CBEB`-`$CBF1` with `uvm2_mem_write`, right after the NMI vector, when set.
- `src/main.c`: before serving, look up the key in the table at `0x10FF7000` (read it like the firmware list, through
  the flash's uncached alias if `fwcat` uses one; check `FLASH_NOCACHE` in main.c) and, if found and `hs_valid`, pass
  it to `cart_set_score`:
  - a built-in game (`start_builtin`): key `s_fw.game[i].crc`;
  - a card `.bin`/`.vec` of up to 32 KB (`load_start`, the `cart_serve((uint8_t *) s_text, got, 0)` path): key
    `zcrc32(s_text, got)`.
  Bigger files (banked, flash, streamed) are left for later: no score, as today. Say so in the README.
- DIAGNOSTICS: one more line, `HIGH SCORES n` (`hs_count`), after `SECTIONS`. Check that the page still fits: the
  last line must stay above y = -100 (`INFO_ROW` may go down to 13).
- `README.md`: in "Limits of this build", replace the high-score line with: scores are read from the firmware's table
  and restored when a game of up to 32 KB or a built-in starts; saving new scores is not done yet.

Checks: the gate; `arm-none-eabi-size` before and after in the commit body (tool path in AGENTS.md / earlier plans:
`~/.local/opt/arm-gnu-toolchain-15.2.rel1-x86_64-arm-none-eabi/bin/`). No console here; the orchestrator tests on
hardware.

Commit title: `Restore a game's high score from the firmware's table when it starts`
