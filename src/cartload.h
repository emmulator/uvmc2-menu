#ifndef CARTLOAD_H
#define CARTLOAD_H
#include <stdint.h>
/* Starts a banked game (64 KB or 256 KB), or a streamed one (over 256 KB, cartstream.c: player is the firmware's video
 * player, which only that needs). The image is built in `arena`, which the caller no longer needs: once this has
 * started reading into it, nothing in it is kept, so a failure from then on restarts the menu instead of returning to it.
 * Both return only when they refused before touching the arena, with the text for the menu's error line. */
const char *cartload_file(const char *path, const uint8_t *player, uint8_t *arena, uint32_t arena_bytes);
/* Called with a game's score key before it is served: the CRC-32 the firmware keys its high-score table on, for a 64 KB
 * file or a 64 KB / 192 KB built-in. 0: not called. The 256 KB and streamed files have no score in the firmware. */
void cartload_on_key(void (*f)(uint32_t key));
/* A built-in game of 64 KB or 192 KB: its zlib stream, the unpacked size and CRC-32 from the firmware's list. */
const char *cartload_builtin(const uint8_t *packed, uint32_t packed_bytes, uint32_t size, uint32_t crc,
                             uint8_t *arena, uint32_t arena_bytes);
/* The least arena either needs: a 192 KB image and one spare 16 KB to re-order it. */
#define CARTLOAD_ARENA_MIN (0x30000u + 0x4000u)
#endif
