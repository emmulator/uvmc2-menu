/* The firmware's high-score table in cartridge flash: records of a key and the Vectrex BIOS score slot. The table
 * is read through a pointer and written only through the two callbacks, so host tests can simulate the flash. */
#ifndef HISCORE_H
#define HISCORE_H
#include <stdint.h>

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

#endif
