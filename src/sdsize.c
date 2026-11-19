/* sdsize.c: see sdsize.h. */
#include "sdsize.h"

static uint32_t le32(const uint8_t *p) { return p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24; }
static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t) p[1] << 8; }

/* A FAT boot sector: the jump, 512 bytes a sector, and a total. */
static uint32_t fat_total(const uint8_t *s)
{
  /* and, so that an MBR whose boot code starts with a jump is not taken for one: sectors per cluster a power of two,
   * one or two FATs */
  if (!(s[0] == 0xEB || s[0] == 0xE9) || le16(s + 11) != 512) return 0;
  if (s[13] == 0 || (s[13] & (s[13] - 1)) || (s[16] != 1 && s[16] != 2)) return 0;
  uint32_t t = le16(s + 19);
  return t ? t : le32(s + 32);
}

uint32_t sd_blocks(const uint8_t *s)
{
  if (s[510] != 0x55 || s[511] != 0xAA) return 0;
  /* exFAT: its own boot sector, a 64-bit volume length at 72 */
  if (s[3] == 'E' && s[4] == 'X' && s[5] == 'F' && s[6] == 'A' && s[7] == 'T')
    return le32(s + 76) ? 0xffffffffu : le32(s + 72);
  /* A volume with no partition table */
  uint32_t fat = fat_total(s);
  if (fat) return fat;
  /* The MBR: the end of the last partition */
  uint32_t end = 0;
  for (int i = 0; i < 4; i++)
  {
    const uint8_t *e = s + 446 + 16 * i;
    if (e[0] != 0 && e[0] != 0x80) return 0;
    uint32_t start = le32(e + 8), n = le32(e + 12);
    if (e[4] == 0 || n == 0) continue;
    if (start + n < start) return 0xffffffffu;
    if (start + n > end) end = start + n;
  }
  return end;
}
