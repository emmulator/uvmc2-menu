#ifndef SDSIZE_H
#define SDSIZE_H
#include <stdint.h>
/* How many 512-byte blocks the USB drive offers, from the card's first sector: the end of its last partition (an MBR),
 * or the volume's own size (FAT or exFAT with no partition table). The SD driver cannot ask the card itself (no CMD9),
 * and the computer needs no more than this to reach every file. 0 if the sector is neither. */
uint32_t sd_blocks(const uint8_t *sector0);
#endif
