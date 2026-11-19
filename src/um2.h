/* The .um2 header, as firmware 1.1.13a's loader reads it (0x10f56110). No hardware here. */
#ifndef UM2_H
#define UM2_H
#include <stdint.h>
#define UM2_MAX_BLOCKS   20u          /* the firmware refuses more than this */
#define UM2_SRAM_BASE    0x20000000u
#define UM2_SRAM_LIMIT   0x20080000u  /* above is the launcher's own scratch */
#define UM2_OFF_BIT      0x80000000u  /* set in the count: firmware sees > 20, skips the file */

typedef struct { uint32_t addr, words; } um2_block;
typedef struct { uint32_t count; um2_block block[UM2_MAX_BLOCKS]; uint32_t payload_off, bytes; } um2_info;

enum { UM2_OK = 0, UM2_E_SHORT, UM2_E_MAGIC, UM2_E_COUNT, UM2_E_RANGE, UM2_E_TRUNC };

/* head: the first bytes of the file. file_size: the whole file. */
int um2_parse(const uint8_t *head, uint32_t n, uint32_t file_size, um2_info *out);
const char *um2_error(int e);

/* The on/off switch: changes the count in a buffer holding the file's first sector. */
int  um2_is_off(const uint8_t *head);
void um2_set_off(uint8_t *head, int off);
#endif
