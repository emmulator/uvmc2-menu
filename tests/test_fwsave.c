/* Host test of fw_saves_ok (fwcat.c): a flash with the signs of the layout, and each sign taken away in turn. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fwcat.h"
#include "bsave.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void put(uint8_t *fl, uint32_t at, const uint8_t *b, uint32_t n) { memcpy(fl + at, b, n); }

static void good(uint8_t *fl)
{
  static const uint8_t dir[8] = { 0x00, 0xFF, 0xFF, 0x14, 0x00, 0xFF, 0xFF, 0x00 };
  static const uint8_t base[6] = { 0xC0, 0xF5, 0x7F, 0x63, 0x06, 0x33 };
  static const uint8_t hs[4] = { 0x00, 0x70, 0xFF, 0x10 };
  memset(fl, 0xFF, 0x1000000u);
  memset(fl, 0x11, 0x20000u);
  put(fl, 0x2fc, dir, 8);
  put(fl, 0x322, base, 6);
  put(fl, 0x5c80, hs, 4);
}

int main(int argc, char **argv)
{
  uint8_t *fl = malloc(0x1000000u);
  FwSaveCheck r;
  good(fl);
  CHECK(fw_saves_ok(fl, &r) && r.dir && r.pagebase && r.scores && r.blank);
  CHECK(fw_saves_ok(fl, 0));

  good(fl); fl[0x2fd] = 0;                          /* the directory address changed */
  CHECK(!fw_saves_ok(fl, &r) && !r.dir && r.pagebase && r.scores && r.blank);
  good(fl); fl[0x322] = 0;                          /* the page arithmetic changed */
  CHECK(!fw_saves_ok(fl, &r) && r.dir && !r.pagebase);
  good(fl); fl[0x5c81] = 0x71;                      /* the score table moved */
  CHECK(!fw_saves_ok(fl, &r) && r.dir && r.pagebase && !r.scores);
  good(fl); fl[0x5c83] = 0x14;                      /* the other alias of the same address is also fine */
  CHECK(fw_saves_ok(fl, &r) && r.scores);
  good(fl); fl[0x2fc + 8000] = 0x00;                /* a sign beyond 16 KB is not looked for... */
  CHECK(fw_saves_ok(fl, &r));
  good(fl); fl[bs_page_offset(5) + 9] = 0x42;       /* an unclaimed slot's page holds data: not ours */
  CHECK(!fw_saves_ok(fl, &r) && !r.blank && r.dir && r.pagebase && r.scores);
  good(fl); fl[BS_DIR_OFF + 5 * 8] = 0x01; fl[bs_page_offset(5) + 9] = 0x42;   /* the same slot, claimed: it is the game's */
  CHECK(fw_saves_ok(fl, &r) && r.blank);
  good(fl); memset(fl + 0x5c80, 0x11, 4);           /* and a flash with no signs at all */
  memset(fl + 0x2fc, 0x11, 8); memset(fl + 0x322, 0x11, 6);
  CHECK(!fw_saves_ok(fl, &r) && !r.dir && !r.pagebase && !r.scores);

  /* real firmware images, if given: every one the menu was written against must pass */
  for (int i = 1; i < argc; i++)
  {
    FILE *f = fopen(argv[i], "rb");
    if (!f || fread(fl, 1, 0x1000000u, f) != 0x1000000u) { printf("cannot read %s\n", argv[i]); fails++; if (f) fclose(f); continue; }
    fclose(f);
    int ok = fw_saves_ok(fl, &r);
    printf("%s: %s (dir %d, page %d, scores %d, blank %d)\n", argv[i], ok ? "ok" : "NOT OK", r.dir, r.pagebase, r.scores, r.blank);
    CHECK(ok);
  }
  free(fl);
  printf(fails ? "fwsave: %d FAILED\n" : "fwsave: all passed\n", fails);
  return fails != 0;
}
