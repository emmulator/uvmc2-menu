/* Host test of bsave.c against a simulated 16 MB flash: erase sets a 4 KB sector to 0xFF, programming ANDs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsave.h"

static uint8_t *fl;
static int n_erase, n_prog, fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void prog(uint32_t off, const uint8_t *d, void *u) { (void) u; n_prog++; if (off % 256u) { printf("FAIL unaligned program\n"); fails++; } for (int i = 0; i < 256; i++) fl[off + i] &= d[i]; }
static void erase(uint32_t off, void *u) { (void) u; n_erase++; if (off % 4096u) { printf("FAIL unaligned erase\n"); fails++; } memset(fl + off, 0xFF, 4096); }
static const BsFlash F = { prog, erase, 0 };

int main(void)
{
  fl = malloc(0x1000000u);
  memset(fl, 0xFF, 0x1000000u);
  uint8_t page[4096], got[4096];

  CHECK(bs_find(fl, 0x12345678u) == -1);
  CHECK(bs_find(fl, 0xFFFFFFFFu) == -1);
  CHECK(!bs_load(fl, 0x12345678u, got));
  CHECK(bs_page_offset(0) == 0xFF6000u && bs_page_offset(1) == 0xFF5000u && bs_page_offset(31) == 0xFF6000u - 31u * 0x1000u);

  /* the first save claims entry 0 (key little-endian, the other four bytes stay 0xFF) and writes the page */
  for (int i = 0; i < 4096; i++) page[i] = (uint8_t) (i * 7 + 1);
  CHECK(bs_save(fl, 0xAABBCCDDu, page, &F) == 1);
  CHECK(fl[BS_DIR_OFF] == 0xDD && fl[BS_DIR_OFF + 1] == 0xCC && fl[BS_DIR_OFF + 2] == 0xBB && fl[BS_DIR_OFF + 3] == 0xAA);
  CHECK(fl[BS_DIR_OFF + 4] == 0xFF && fl[BS_DIR_OFF + 7] == 0xFF && fl[BS_DIR_OFF + 8] == 0xFF);
  CHECK(bs_find(fl, 0xAABBCCDDu) == 0);
  CHECK(memcmp(fl + 0xFF6000u, page, 4096) == 0);
  CHECK(bs_load(fl, 0xAABBCCDDu, got) && memcmp(got, page, 4096) == 0);
  CHECK(n_erase == 0);                         /* the page was blank: programming alone is enough */

  /* the same page again: nothing is written */
  int p0 = n_prog;
  CHECK(bs_save(fl, 0xAABBCCDDu, page, &F) == 0 && n_prog == p0 && n_erase == 0);

  /* a change that only clears bits is programmed over the top, only the piece that differs */
  page[300] &= 0x0F;
  CHECK(bs_save(fl, 0xAABBCCDDu, page, &F) == 1);
  CHECK(n_erase == 0 && n_prog == p0 + 1);
  CHECK(memcmp(fl + 0xFF6000u, page, 4096) == 0);

  /* a change that sets a bit needs the erase, then every piece */
  page[5] |= 0x80; page[5] = (uint8_t) (page[5] ^ 0x00);
  uint8_t old5 = fl[0xFF6000u + 5];
  page[5] = (uint8_t) (old5 | 0x01);
  p0 = n_prog;
  CHECK(bs_save(fl, 0xAABBCCDDu, page, &F) == 1);
  CHECK(n_erase == 1 && n_prog == p0 + 16);
  CHECK(memcmp(fl + 0xFF6000u, page, 4096) == 0);

  /* a second game gets entry 1 and the page below */
  uint8_t other[4096]; memset(other, 0x5A, sizeof other);
  CHECK(bs_save(fl, 0x01020304u, other, &F) == 1);
  CHECK(bs_find(fl, 0x01020304u) == 1 && bs_find(fl, 0xAABBCCDDu) == 0);
  CHECK(memcmp(fl + 0xFF5000u, other, 4096) == 0);
  CHECK(memcmp(fl + 0xFF6000u, page, 4096) == 0);          /* the first one is untouched */

  /* a directory the firmware filled with 30 other games: this one takes the 31st, and a full one takes nothing */
  for (uint32_t i = 2; i < 32; i++) { uint32_t k = 0x1000u + i; memcpy(fl + BS_DIR_OFF + i * 8u, &k, 4); }
  CHECK(bs_save(fl, 0x77777777u, other, &F) == -1);
  CHECK(bs_find(fl, 0x77777777u) == -1);
  CHECK(bs_find(fl, 0x1000u + 31u) == 31);
  CHECK(fl[0xFF7000u] == 0xFF);                            /* the high-score table was not touched */

  /* nothing outside the directory and the pages below the table was written */
  int stray = 0;
  for (uint32_t i = 0; i < 0x1000000u; i++)
    if (fl[i] != 0xFF && !(i >= BS_DIR_OFF && i < BS_DIR_OFF + 256u) && !(i >= 0xFF5000u && i < 0xFF7000u)) stray++;
  CHECK(stray == 0);

  /* a free entry whose page already holds data is not claimed: the next blank one is, and none at all means -1 */
  memset(fl, 0xFF, 0x1000000u);
  fl[bs_page_offset(0) + 100] = 0x12;
  CHECK(bs_save(fl, 0x0A0B0C0Du, page, &F) == 1);
  CHECK(bs_find(fl, 0x0A0B0C0Du) == 1);
  CHECK(fl[bs_page_offset(0) + 100] == 0x12);               /* what was there is untouched */
  memset(fl, 0xFF, 0x1000000u);
  for (int n = 0; n < 32; n++) fl[bs_page_offset(n) + 7] = 0;
  CHECK(bs_save(fl, 0x0A0B0C0Du, page, &F) == -1);
  CHECK(bs_find(fl, 0x0A0B0C0Du) == -1);

  free(fl);
  printf(fails ? "bsave: %d FAILED\n" : "bsave: all passed\n", fails);
  return fails != 0;
}
