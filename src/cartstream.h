#ifndef CARTSTREAM_H
#define CARTSTREAM_H
#include <stdint.h>
/* Streamed cartridges (over 256 KB: the firmware's videos). player is the firmware's 507-byte video player
 * (fw_stream_player). The banks and the inflate window are built in `arena`, which the caller no longer needs. Both
 * return only when they refused before starting, with the text for the menu's error line; once playing, holding the
 * console's reset for 2 seconds comes back to the menu. */
const char *cartstream_builtin(const uint8_t *packed, uint32_t packed_bytes, uint32_t size, const uint8_t *player,
                               uint8_t *arena, uint32_t arena_bytes);
const char *cartstream_file(const char *path, const uint8_t *player, uint8_t *arena, uint32_t arena_bytes);
/* The two 32 KB banks and the 32 KB inflate window. */
#define CARTSTREAM_ARENA_MIN 0x18000u
#endif
