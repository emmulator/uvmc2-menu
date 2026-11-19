/* Host tests for hiscore.c: the firmware's high-score table, read through a pointer and changed through two
 * callbacks. The simulated flash is NOR: program ANDs bytes in, erase sets a sector to 0xFF, and both are counted. */
#include "hiscore.h"
#include <stdio.h>
#include <string.h>

static int passes, fails;
#define CHECK(x) do { if (x) passes++; else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static uint8_t s_flash[HS_BYTES];
static int s_programs, s_erases;

static void sim_program(uint32_t offset, const uint8_t *page, void *u)
{
  uint8_t *flash = u;
  for (uint32_t i = 0; i < HS_PAGE; i++) flash[offset + i] &= page[i];
  s_programs++;
}

static void sim_erase(uint32_t offset, void *u)
{
  uint8_t *flash = u;
  memset(flash + offset, 0xFF, HS_SECTOR);
  s_erases++;
}

static const HsFlash s_f = { sim_program, sim_erase, s_flash };

static void flash_reset(void)
{
  memset(s_flash, 0xFF, sizeof s_flash);
  s_programs = 0;
  s_erases = 0;
}

static uint32_t rec_key(const uint8_t *table, int i)
{
  const uint8_t *p = table + (uint32_t) i * HS_REC;
  return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

/* Writes one record byte by byte, as the firmware leaves it: key, the seven score bytes, the rest 0xFF. */
static void put_rec(uint8_t *table, int i, uint32_t key, const uint8_t score[7])
{
  uint8_t *p = table + (uint32_t) i * HS_REC;
  p[0] = (uint8_t) key;
  p[1] = (uint8_t) (key >> 8);
  p[2] = (uint8_t) (key >> 16);
  p[3] = (uint8_t) (key >> 24);
  for (int k = 0; k < 7; k++) p[4 + k] = score[k];
  for (int k = 11; k < 16; k++) p[k] = 0xFF;
}

/* A valid, distinct score for i: "  dddd " then 0x80, four decimal digits with leading zeros. */
static void score_for(int i, uint8_t score[7])
{
  score[0] = ' ';
  score[1] = (uint8_t) ('0' + (i / 1000) % 10);
  score[2] = (uint8_t) ('0' + (i / 100) % 10);
  score[3] = (uint8_t) ('0' + (i / 10) % 10);
  score[4] = (uint8_t) ('0' + i % 10);
  score[5] = ' ';
  score[6] = 0x80;
}

/* 1. an empty table finds and counts nothing; the first save programs one page and leaves the rest of the record 0xFF */
static void test_empty_save(void)
{
  const uint32_t key = 0x12345678u;
  const uint8_t score[7] = { ' ', ' ', '1', '2', '3', '0', 0x80 };
  uint8_t out[7];

  flash_reset();
  CHECK(hs_find(s_flash, key, out) == 0);
  CHECK(hs_count(s_flash) == 0);

  CHECK(hs_save(s_flash, key, score, &s_f) == 1);
  CHECK(s_programs == 1 && s_erases == 0);
  CHECK(hs_find(s_flash, key, out) == 1);
  CHECK(memcmp(out, score, 7) == 0);
  CHECK(rec_key(s_flash, 0) == key);
  CHECK(memcmp(s_flash + 4, score, 7) == 0);
  for (int i = 11; i < 16; i++) CHECK(s_flash[i] == 0xFF);
}

/* 2. saving the score already there changes nothing and touches no flash */
static void test_same_score(void)
{
  const uint32_t key = 0x12345678u;
  const uint8_t score[7] = { ' ', ' ', '1', '2', '3', '0', 0x80 };
  uint8_t before[HS_BYTES];

  flash_reset();
  CHECK(hs_save(s_flash, key, score, &s_f) == 1);
  memcpy(before, s_flash, sizeof before);
  s_programs = s_erases = 0;
  CHECK(hs_save(s_flash, key, score, &s_f) == 0);
  CHECK(s_programs == 0 && s_erases == 0);
  CHECK(memcmp(before, s_flash, sizeof before) == 0);
}

/* 3. a new score for the same key: the old key is zeroed, the new record is appended, lookup finds the new score */
static void test_new_score(void)
{
  const uint32_t key = 0x12345678u;
  const uint8_t oldscore[7] = { ' ', ' ', '1', '2', '3', '0', 0x80 };
  const uint8_t newscore[7] = { ' ', ' ', '9', '9', '9', '9', 0x80 };
  uint8_t out[7];

  flash_reset();
  CHECK(hs_save(s_flash, key, oldscore, &s_f) == 1);
  s_programs = s_erases = 0;
  CHECK(hs_save(s_flash, key, newscore, &s_f) == 1);
  CHECK(s_programs == 2 && s_erases == 0);
  CHECK(rec_key(s_flash, 0) == 0);
  CHECK(rec_key(s_flash, 1) == key);
  CHECK(memcmp(s_flash + HS_REC + 4, newscore, 7) == 0);
  CHECK(hs_find(s_flash, key, out) == 1);
  CHECK(memcmp(out, newscore, 7) == 0);
  CHECK(hs_count(s_flash) == 1);
}

/* 4. a table built byte by byte as the firmware leaves it: a deleted record, then two live keys */
static void test_built_table(void)
{
  const uint8_t oldscore[7] = { '0', '0', '0', '1', '0', '0', 0x80 };
  const uint8_t score_a[7] = { ' ', ' ', '1', '1', '1', '1', 0x80 };
  const uint8_t score_b[7] = { ' ', ' ', '2', '2', '2', '2', 0x80 };
  uint8_t out[7];

  flash_reset();
  put_rec(s_flash, 0, 0, oldscore);
  put_rec(s_flash, 1, 0xAAAABBBBu, score_a);
  put_rec(s_flash, 2, 0xCCCCDDDDu, score_b);
  CHECK(hs_count(s_flash) == 2);
  CHECK(hs_find(s_flash, 0xAAAABBBBu, out) == 1 && memcmp(out, score_a, 7) == 0);
  CHECK(hs_find(s_flash, 0xCCCCDDDDu, out) == 1 && memcmp(out, score_b, 7) == 0);
  CHECK(hs_find(s_flash, 0, out) == 0);
  CHECK(hs_find(s_flash, 0x11111111u, out) == 0);
}

/* 5. a full table with half its records deleted: compacting sectors keeps every live score and the new one fits */
static void test_compact(void)
{
  const uint32_t key = 0xDEADBEEFu;
  const uint8_t score[7] = { ' ', ' ', '7', '7', '7', '7', 0x80 };
  uint8_t out[7], want[7];

  flash_reset();
  for (int i = 0; i < (int) (HS_BYTES / HS_REC); i++)
  {
    if (i % 2 == 0) put_rec(s_flash, i, 0, score);            /* deleted */
    else
    {
      score_for(i, want);
      put_rec(s_flash, i, 0x40000000u + (uint32_t) i, want);
    }
  }
  CHECK(hs_count(s_flash) == (int) (HS_BYTES / HS_REC / 2));
  s_programs = s_erases = 0;
  CHECK(hs_save(s_flash, key, score, &s_f) == 1);
  CHECK(s_erases > 0 && s_erases <= 8);

  for (int i = 1; i < (int) (HS_BYTES / HS_REC); i += 2)
  {
    score_for(i, want);
    CHECK(hs_find(s_flash, 0x40000000u + (uint32_t) i, out) == 1);
    CHECK(memcmp(out, want, 7) == 0);
  }
  CHECK(hs_find(s_flash, key, out) == 1 && memcmp(out, score, 7) == 0);
  CHECK(hs_count(s_flash) == (int) (HS_BYTES / HS_REC / 2) + 1);
}

/* 6. every record live: there is nothing to reclaim, so the save fails and the table stays as it was */
static void test_full(void)
{
  const uint8_t score[7] = { ' ', ' ', '7', '7', '7', '7', 0x80 };
  uint8_t before[HS_BYTES], want[7];

  flash_reset();
  for (int i = 0; i < (int) (HS_BYTES / HS_REC); i++)
  {
    score_for(i, want);
    put_rec(s_flash, i, 0x40000000u + (uint32_t) i, want);
  }
  memcpy(before, s_flash, sizeof before);
  CHECK(hs_save(s_flash, 0xDEADBEEFu, score, &s_f) == -1);
  CHECK(memcmp(before, s_flash, sizeof before) == 0);
}

/* 7. what a score may be, and an invalid one is refused before any flash call */
static void test_valid(void)
{
  const uint8_t good1[7] = { ' ', ' ', '1', '2', '3', '0', 0x80 };
  const uint8_t good2[7] = { ' ', ' ', ' ', ' ', ' ', '0', 0x80 };
  const uint8_t no_end[7] = { ' ', ' ', '1', '2', '3', '0', 0 };
  const uint8_t letter[7] = { ' ', ' ', '1', 'A', '3', '0', 0x80 };
  const uint8_t zero[7] = { ' ', ' ', '1', '2', '3', 0, 0x80 };

  CHECK(hs_valid(good1) == 1);
  CHECK(hs_valid(good2) == 1);
  CHECK(hs_valid(no_end) == 0);
  CHECK(hs_valid(letter) == 0);
  CHECK(hs_valid(zero) == 0);

  flash_reset();
  CHECK(hs_save(s_flash, 0x12345678u, letter, &s_f) == -2);
  CHECK(s_programs == 0 && s_erases == 0);
  for (int i = 0; i < (int) HS_BYTES; i++) CHECK(s_flash[i] == 0xFF);
}

int main(void)
{
  test_empty_save();
  test_same_score();
  test_new_score();
  test_built_table();
  test_compact();
  test_full();
  test_valid();
  printf("%d passed, %d failed\n", passes, fails);
  return fails != 0;
}
