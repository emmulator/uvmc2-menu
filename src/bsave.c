/* See bsave.h. */
#include "bsave.h"
#include <string.h>

static uint32_t rd32(const uint8_t *p) { return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24; }

int bs_find(const uint8_t *flash, uint32_t key)
{
  if (key == 0xFFFFFFFFu) return -1;                    /* that is what a free entry holds */
  for (uint32_t i = 0; i < BS_DIR_N; i++)
    if (rd32(flash + BS_DIR_OFF + i * 8u) == key) return (int) i;
  return -1;
}

uint32_t bs_page_offset(int n) { return BS_PAGE0 - (uint32_t) n * BS_PAGE; }

int bs_load(const uint8_t *flash, uint32_t key, uint8_t *page)
{
  int n = bs_find(flash, key);
  if (n < 0) return 0;
  memcpy(page, flash + bs_page_offset(n), BS_PAGE);
  return 1;
}

static int bs_claim(const uint8_t *flash, uint32_t key, const BsFlash *f)
{
  for (uint32_t i = 0; i < BS_DIR_N; i++)
  {
    if (rd32(flash + BS_DIR_OFF + i * 8u) != 0xFFFFFFFFu) continue;
    const uint8_t *p = flash + bs_page_offset((int) i);
    uint32_t used = 0;
    for (uint32_t b = 0; b < BS_PAGE; b++) used |= (uint32_t) (p[b] != 0xFF);
    if (used) continue;                                   /* a free entry whose page is not blank is not ours to use */
    uint8_t dir[BS_SUBPAGE];
    memcpy(dir, flash + BS_DIR_OFF, BS_SUBPAGE);
    for (int b = 0; b < 4; b++) dir[i * 8u + (uint32_t) b] = (uint8_t) (key >> (8 * b));
    f->program(BS_DIR_OFF, dir, f->u);
    return (int) i;
  }
  return -1;
}

int bs_save(const uint8_t *flash, uint32_t key, const uint8_t *page, const BsFlash *f)
{
  int n = bs_find(flash, key);
  if (n >= 0)
  {
    /* what the flash holds now: nothing to do if it is the same */
    if (memcmp(flash + bs_page_offset(n), page, BS_PAGE) == 0) return 0;
  }
  else
  {
    n = bs_claim(flash, key, f);
    if (n < 0) return -1;
  }
  const uint8_t *old = flash + bs_page_offset(n);
  uint32_t off = bs_page_offset(n), dirty = 0;
  int erase = 0;
  for (uint32_t i = 0; i < BS_PAGE; i++)
    if (page[i] != old[i])
    {
      dirty |= 1u << (i / BS_SUBPAGE);
      if (page[i] & (uint8_t) ~old[i]) erase = 1;       /* a bit has to go 0 -> 1: only an erase does that */
    }
  if (erase) { f->erase(off, f->u); dirty = 0xFFFFu; }
  for (uint32_t p = 0; p < BS_PAGE / BS_SUBPAGE; p++)
    if (dirty & (1u << p)) f->program(off + p * BS_SUBPAGE, page + p * BS_SUBPAGE, f->u);
  return 1;
}
