#ifndef LAUNCH_H
#define LAUNCH_H
#include "um2.h"
/* Starts the image already copied to PSRAM, the way firmware 1.1.13a does (0x10f56040). Does not return. */
#define LAUNCH_PSRAM 0x15000000u      /* uncached alias: writing through the cached one corrupts */
void launch_go(const char *path, const um2_info *u);
void launch_cartridge_reboot(void);
#endif
