/* A built-in flash game's save, as the firmware keeps it (docs/bankswitch.md, section 8). Vectorblade and Telengard are
 * 192 KB flash-scheme games that program a 4 KB page of their own image: their options, calibration and scores. The
 * firmware copies that page in from its own flash when the game starts, and writes it back once the game has left it
 * alone for a moment. Where, as read from firmware 1.1.13a (0x10000250, 0x10000304):
 *
 *   directory   the last 256 bytes of the 16 MB flash (offset 0xFFFF00): 32 entries of 8 bytes, the game's CRC-32 from the
 *               firmware's list (little-endian) and four bytes of 0xFF. A free entry is 0xFFFFFFFF; one is claimed by
 *               programming its key into the (already erased) page.
 *   save page   for the game in entry n, the 4 KB at offset 0xFF6000 - n * 0x1000 (the pages below the high-score table).
 *   page in the image   the byte that comes before the game's zlib stream, in 4 KB units.
 *
 * The flash is read through a pointer to flash offset 0 (uncached) and written only through the callbacks, so host
 * tests can simulate it, as with hiscore.c. */
#ifndef BSAVE_H
#define BSAVE_H
#include <stdint.h>

#define BS_PAGE     0x1000u
#define BS_SUBPAGE  0x100u
#define BS_DIR_OFF  0xFFFF00u
#define BS_DIR_N    32u
#define BS_PAGE0    0xFF6000u

typedef struct
{
  void (*program)(uint32_t offset, const uint8_t *data, void *u);   /* 256 bytes at a flash offset, a multiple of 256 */
  void (*erase)(uint32_t offset, void *u);                           /* the 4 KB sector at a flash offset */
  void *u;
} BsFlash;

/* The entry holding key, or -1. */
int bs_find(const uint8_t *flash, uint32_t key);
/* The flash offset of the save page of entry n. */
uint32_t bs_page_offset(int n);
/* The saved page for key: 1 and the 4 KB copied to page, or 0 if the game has none. */
int bs_load(const uint8_t *flash, uint32_t key, uint8_t *page);
/* Keeps page as key's save. Claims a free entry whose page is blank if the game has none. As the firmware: nothing if the flash already holds
 * these bytes; the sector is erased only if a bit has to go from 0 to 1; only the 256-byte pieces that differ are
 * programmed (all of them after an erase). Returns 1 if written, 0 if unchanged, -1 if no entry can be claimed (all 32 belong to other
 * games, or the free ones have data in their pages (the firmware would write over the high-score table then; this does nothing). */
int bs_save(const uint8_t *flash, uint32_t key, const uint8_t *page, const BsFlash *f);
#endif
