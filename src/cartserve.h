#ifndef CARTSERVE_H
#define CARTSERVE_H
#include <stdint.h>
/* The score the next start writes to the BIOS's high-score slot, $CBEB-$CBF1, before the 6809 runs: 7 bytes kept until
 * the next call. 0 clears it (no score written). */
void cart_set_score(const uint8_t score[7]);
/* While the next cart_serve runs, core 0 watches the console's writes to $CBEB-$CBF1 and calls save with the 7 bytes
 * once a new score has settled (and once more before the reset reboot). The callback may write the flash: core 1
 * serves from RAM. 0: no watching (the default). */
void cart_watch_score(void (*save)(const uint8_t score[7]));
/* While the next cart_serve_banked of a CART_FLASH image runs, core 0 keeps a CRC of each 4 KB page of the image and, about
 * every 100 ms, calls save(offset, page) for each page the game has changed since (the firmware's own loop, 0x10000b28).
 * save returns 1 if it kept the page; one that did not is offered again. The callback may use the SD card: core 1 serves
 * from RAM. 0: no watching (the default). */
void cart_watch_flash(int (*save)(uint32_t image_offset, const uint8_t *page));
/* Answers the 6809's reads from rom, in place of this program: stops drawing, gives the bus back and restarts the console
 * through the BIOS. Does not return; holding the console's reset for 2 seconds reboots the cartridge. size is 32 KB or less. */
void cart_serve(uint8_t *rom, uint32_t size, int writable);
/* The same for an image laid out by cart.c: scheme is CART_PB6 or CART_FLASH. */
void cart_serve_banked(uint8_t *image, int scheme);
/* Streaming: starts the server on the two banks (cart.h) and returns, with interrupts off. Core 0 then keeps the banks
 * filled, watching how many times the player has switched, and calls cart_check_reset often: it reboots the cartridge
 * once the console's reset has been held for 2 seconds. */
void cart_stream_start(uint8_t *banks);
uint32_t cart_stream_flipped(void);
void cart_check_reset(void);
#endif
