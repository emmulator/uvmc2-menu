#ifndef SDFAST_H
#define SDFAST_H
#include <stdint.h>
/* Runs of sectors with the card's multi-block commands (CMD18, CMD25), for transfer.um2: the SDK's driver moves one
 * sector per command and waits for each write to be programmed, which held USB to about 0.15 MB/s writing.
 * sdfast_init after the SDK has brought the card up (disk_initialize). Each returns 1 on success. */
int sdfast_init(void);
int sdfast_read(uint32_t lba, uint8_t *buf, uint32_t count);
int sdfast_write(uint32_t lba, const uint8_t *buf, uint32_t count);
#endif
