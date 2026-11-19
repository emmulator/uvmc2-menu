/* The firmware's high-score table. The layout and the update rules are the ones the firmware at 0x10005c64 and
 * 0x100006bc use: 16-byte records of a little-endian key, seven score bytes and five 0xFF bytes; a free record's key
 * is 0xFFFFFFFF, a deleted one's is 0, and lookup returns the first record with the wanted key. The table is read
 * through the caller's pointer and written only through the flash callbacks, so this file stays free of SDK calls. */
#include "hiscore.h"
#include <string.h>

/* One page and one sector, the sizes both callbacks write. 4.25 KB of static RAM: the save needs the whole sector
 * while it compacts it, and the page while it reprograms one. Only hs_save uses them. */
static uint8_t s_page[HS_PAGE];
static uint8_t s_sector[HS_SECTOR];

static uint32_t hs_key(const uint8_t *rec)
{
  return (uint32_t) rec[0] | (uint32_t) rec[1] << 8 | (uint32_t) rec[2] << 16 | (uint32_t) rec[3] << 24;
}

/* Live as the firmware's compaction counts it: 0 is deleted, 0xFFFFFFFE and 0xFFFFFFFF are not a game's key. */
static int hs_live(uint32_t key)
{
  return key != 0 && key != 0xFFFFFFFEu && key != 0xFFFFFFFFu;
}

int hs_valid(const uint8_t score[7])
{
  for (int i = 0; i < 6; i++)
    if (score[i] != ' ' && (score[i] < '0' || score[i] > '9')) return 0;
  return score[6] == 0x80;
}

/* The first record with key, or -1. As the firmware: a deleted or free record cannot match a game's key. */
static int hs_index(const uint8_t *table, uint32_t key)
{
  if (!hs_live(key)) return -1;
  for (uint32_t i = 0; i < HS_BYTES / HS_REC; i++)
    if (hs_key(table + i * HS_REC) == key) return (int) i;
  return -1;
}

int hs_find(const uint8_t *table, uint32_t key, uint8_t out[7])
{
  int i = hs_index(table, key);
  if (i < 0) return 0;
  memcpy(out, table + (uint32_t) i * HS_REC + 4, 7);
  return 1;
}

int hs_count(const uint8_t *table)
{
  int n = 0;
  for (uint32_t i = 0; i < HS_BYTES / HS_REC; i++)
    if (hs_live(hs_key(table + i * HS_REC))) n++;
  return n;
}

static int hs_free(const uint8_t *table)
{
  for (uint32_t i = 0; i < HS_BYTES / HS_REC; i++)
    if (hs_key(table + i * HS_REC) == 0xFFFFFFFFu) return (int) i;
  return -1;
}

/* Zeroes the 4-byte key at the start of the page holding record i. Programming only clears bits, so the rest of
 * the page can be written again unchanged. */
static void hs_delete(const uint8_t *table, int i, const HsFlash *f)
{
  uint32_t offset = ((uint32_t) i * HS_REC) & ~(HS_PAGE - 1u);
  memcpy(s_page, table + offset, HS_PAGE);
  memset(s_page, 0, 4);
  f->program(offset, s_page, f->u);
}

/* Each sector in turn: keep its live records at the start of a 4 KB buffer, erase the sector, and program back
 * only the pages that hold records. A sector with no deleted or orphan record is left alone: compacting it would
 * write the same bytes back. */
static void hs_compact(const uint8_t *table, const HsFlash *f)
{
  for (uint32_t s = 0; s < HS_BYTES / HS_SECTOR; s++)
  {
    uint32_t base = s * HS_SECTOR;
    int n = 0, reclaim = 0;
    memset(s_sector, 0xFF, HS_SECTOR);
    for (uint32_t i = 0; i < HS_SECTOR / HS_REC; i++)
    {
      uint32_t key = hs_key(table + base + i * HS_REC);
      if (hs_live(key))
      {
        memcpy(s_sector + (uint32_t) n * HS_REC, table + base + i * HS_REC, HS_REC);
        n++;
      }
      else if (key != 0xFFFFFFFFu) reclaim = 1;
    }
    if (!reclaim) continue;
    f->erase(base, f->u);
    uint32_t pages = ((uint32_t) n * HS_REC + HS_PAGE - 1) / HS_PAGE;
    for (uint32_t p = 0; p < pages; p++)
      f->program(base + p * HS_PAGE, s_sector + p * HS_PAGE, f->u);
  }
}

int hs_save(const uint8_t *table, uint32_t key, const uint8_t score[7], const HsFlash *f)
{
  if (!hs_valid(score)) return -2;

  int old = hs_index(table, key);
  if (old >= 0 && memcmp(table + (uint32_t) old * HS_REC + 4, score, 7) == 0) return 0;
  if (old >= 0) hs_delete(table, old, f);

  int slot = hs_free(table);
  if (slot < 0)
  {
    /* Every record live: compaction would only rewrite them, so there is no slot to make. */
    if (hs_count(table) == (int) (HS_BYTES / HS_REC)) return -1;
    hs_compact(table, f);
    slot = hs_free(table);
    if (slot < 0) return -1;
  }

  uint32_t offset = ((uint32_t) slot * HS_REC) & ~(HS_PAGE - 1u);
  memcpy(s_page, table + offset, HS_PAGE);
  uint8_t *rec = s_page + (uint32_t) slot * HS_REC - offset;
  rec[0] = (uint8_t) key;
  rec[1] = (uint8_t) (key >> 8);
  rec[2] = (uint8_t) (key >> 16);
  rec[3] = (uint8_t) (key >> 24);
  memcpy(rec + 4, score, 7);
  f->program(offset, s_page, f->u);
  return 1;
}
