/* cart.h: which scheme serves a cartridge image, and how the image is laid out in memory. Pure C, host-tested. The rules
 * are the firmware's (docs/bankswitch.md): the size alone decides, and the image is re-ordered so that the server can find
 * a byte by adding the bus bits, through the interpolator, to the base of the buffer. */
#ifndef CART_H
#define CART_H
#include <stdint.h>

enum { CART_NONE = 0, CART_PLAIN, CART_PB6, CART_FLASH, CART_STREAM };

/* Interpolator 0, lane 1 (lane 0 is 0x3408 for all: A0-A13 from GPIO8-21). Shift 8, cross input, mask bits 14-16 (GPIO22
 * PB6, GPIO23, GPIO24 = A14) or 14-17 (and GPIO25 = A15). */
#define CART_LANE0_A13   0x3408u
#define CART_LANE1_A14   0x139cau
#define CART_LANE1_PB6   0x141c8u
#define CART_LANE1_FLASH 0x145c8u

#define CART_CHUNK 0x4000u
#define CART_PLAIN_MAX 0x8000u

/* The scheme for a .bin / .vec of this many bytes: the firmware serves 1 to 32 KB, 64 KB and 256 KB, and streams anything
 * over 256 KB. CART_NONE for anything else, including 128 KB (the firmware refuses it). */
int cart_file_scheme(uint32_t size);
/* The same for a built-in game's unpacked size: 32 KB or less, 64 KB, 192 KB (a 256 KB file without the unused
 * quarter of each 64 KB block), and streamed over 256 KB. */
int cart_builtin_scheme(uint32_t size);
/* The bytes of data a built-in starts with before its zlib stream: 1 for the flash scheme (the number of its save page), 0 otherwise. */
uint32_t cart_builtin_prefix(uint32_t size);
/* Why a file of this size is not served, short enough for the menu's error line. */
const char *cart_refusal(uint32_t size);

/* The memory the server reads: 32 KB or less as they are, PB6 128 KB, flash 192 KB, stream two 32 KB banks. */
uint32_t cart_image_bytes(int scheme, uint32_t size);

/* Reading a file: how many 16 KB pieces of it, and for piece n where it is in the file and which 16 KB slot of the
 * image it goes to. Only for CART_PB6 and CART_FLASH. */
int cart_pieces(int scheme);
void cart_piece(int scheme, int n, uint32_t *file_offset, uint32_t *slot);
/* Saving a 256 KB game: where in the file the byte at this offset of the flash image belongs (the inverse of cart_piece
 * for CART_FLASH), so a 4 KB page of the image that the game changed can be written back to the file. */
uint32_t cart_flash_file_offset(uint32_t image_offset);
#define CART_FLASH_PAGE   0x1000u
#define CART_FLASH_PAGES  48u
/* After all the pieces are in place. PB6: copies every slot to the slot two higher (the second bank line is not ours to
 * rely on, so both of its states show the same bytes). */
void cart_finish(int scheme, uint8_t *image);

/* Built-in games unpack to the pieces in file order, one after the other, from the start of the image. This moves them
 * to their slots, in place (FLASH needs one spare 16 KB at tmp), and finishes. */
void cart_from_stream(int scheme, uint8_t *image, uint8_t *tmp);

/* Where the server reads for a bus word g (the GPIO inputs): the offset into the image, as the interpolator adds it up.
 * The model of what the hardware does with CART_LANE0_A13 and the CART_LANE1_* constants, for the tests. */
uint32_t cart_offset(int scheme, uint32_t g);
/* ---- streaming (docs/bankswitch.md, section 6) ----
 * The firmware's video player (VIDE's ExtremePlayer, 507 bytes) sits at $0000 of two 32 KB banks. Each time it writes $02
 * to an address ending in $FF the cartridge switches to the other bank, and refills the bank just left at $4000 with the
 * next 1536 bytes of the file: piece n goes to bank n & 1, and may be written once the player has switched n - 1 times
 * (pieces 0 and 1 are there before it starts). At the end of the file it goes on from the start. */
#define CART_STREAM_BANKS   0x10000u
#define CART_STREAM_DATA    0x4000u
#define CART_STREAM_PIECE   0x600u
#define CART_STREAM_PLAYER  0x1fbu
#define CART_STREAM_PLAYER_CRC 0x48e234b7u
uint32_t cart_stream_offset(uint32_t piece);   /* where piece n starts in the banks */
uint32_t cart_stream_flips(uint32_t piece);    /* switches the player must have made before piece n may be written */
/* Puts the bytes of a file, as they come, into the banks. wait(flips) is called before a piece that needs the player to
 * have switched that many times, and returns once it has (the first such call is the moment to start the player). */
typedef struct
{
  uint8_t *banks;
  uint32_t piece, k;
  void (*wait)(uint32_t flips, void *user);
  void *user;
} CartFeed;
void cart_feed_byte(uint8_t b, void *feed);    /* a CartFeed *: the shape zinflate_stream's sink has */
void cart_feed_end(CartFeed *f);               /* the end of the file: a part-filled piece is left as it is */
#endif
