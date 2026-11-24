/* cartload.c: builds the image of a banked game in memory, then serves it. Device code (SD card, inflate); the layout
 * rules are in cart.c, which the host tests cover. */
#include "cartload.h"
#include <stdio.h>
#include "cart.h"
#include "cartserve.h"
#include "cartstream.h"
#include "bsave.h"
#include "inflate.h"
#include "launch.h"
#include "uvm2_sd.h"
#include "ff.h"
#include "hardware/flash.h"

/* The arena is gone: there is no menu to return to, so start the cartridge again. It comes back to this menu. */
static void restart(void)
{
  launch_cartridge_reboot();
  for (;;) { }
}

static void (*s_key_hook)(uint32_t key);
void cartload_on_key(void (*f)(uint32_t key)) { s_key_hook = f; }

/* A 256 KB game's saves: the page the game changed goes back to its place in the file, as the firmware does. The file is
 * opened on the first save and stays open; each page is flushed at once, since the console may be switched off at any
 * moment. The SDK mounts the card afresh on each of its own calls, so mounting here is safe, as in autostart_write_head. */
static char s_save_path[160];
static int save_page(uint32_t image_offset, const uint8_t *page)
{
  static FATFS fs;
  static FIL f;
  static int open;
  UINT put = 0;
  if (!open)
  {
    if (f_mount(&fs, "", 1) != FR_OK) return 0;
    if (f_open(&f, s_save_path, FA_WRITE | FA_OPEN_EXISTING) != FR_OK) { f_mount(0, "", 0); return 0; }
    open = 1;
  }
  if (f_lseek(&f, cart_flash_file_offset(image_offset)) != FR_OK) return 0;
  if (f_write(&f, page, CART_FLASH_PAGE, &put) != FR_OK || put != CART_FLASH_PAGE) return 0;
  return f_sync(&f) == FR_OK;
}

/* A built-in flash game's save (Vectorblade, Telengard): the one 4 KB page of its image the game programs, kept in the
 * firmware's own flash where the firmware keeps it (bsave.h), so the game finds it again from either menu. The page is
 * copied in before the game starts. While it runs, a changed page is written once it has held still for a round of the
 * watch (about 120 ms), as the firmware waits for it to settle: a game programs its page a byte at a time. */
#define FLASH_NOCACHE ((const uint8_t *) 0x14000000u)       /* flash offset 0, past the cache, as the firmware reads it */
static int s_saves_allowed;
void cartload_allow_saves(int on) { s_saves_allowed = on; }
static uint32_t s_bs_key, s_bs_off, s_bs_seen;
static int s_bs_waiting;

static void bs_program(uint32_t off, const uint8_t *data, void *u) { (void) u; flash_range_program(off, data, BS_SUBPAGE); }
static void bs_erase(uint32_t off, void *u) { (void) u; flash_range_erase(off, BS_PAGE); }

static int bs_watch(uint32_t image_offset, const uint8_t *page)
{
  static const BsFlash flash = { bs_program, bs_erase, 0 };
  if (image_offset != s_bs_off) return 1;                   /* the firmware keeps this page and no other */
  uint32_t c = zcrc32(page, BS_PAGE);
  if (!s_bs_waiting || c != s_bs_seen) { s_bs_seen = c; s_bs_waiting = 1; return 0; }
  s_bs_waiting = 0;
  (void) bs_save(FLASH_NOCACHE, s_bs_key, page, &flash);   /* -1, no free entry in the directory: nothing is kept */
  return 1;
}

const char *cartload_file(const char *path, const uint8_t *player, uint8_t *arena, uint32_t arena_bytes)
{
  uvm2_sd_file f;
  if (!uvm2_sd_open(path, &f)) return "CANNOT OPEN FILE";
  const uint32_t len = f.len;
  const int scheme = cart_file_scheme(len);
  if (scheme == CART_NONE) { uvm2_sd_close(&f); return cart_refusal(len); }
  if (scheme == CART_PLAIN) { uvm2_sd_close(&f); return "READ ERROR"; }
  if (scheme == CART_STREAM) { uvm2_sd_close(&f); return cartstream_file(path, player, arena, arena_bytes); }
  if (cart_image_bytes(scheme, len) > arena_bytes) { uvm2_sd_close(&f); return "NOT ENOUGH MEMORY"; }

  for (int n = 0; n < cart_pieces(scheme); n++)
  {
    uint32_t off, slot;
    cart_piece(scheme, n, &off, &slot);
    if (!uvm2_sd_seek(&f, off)) restart();
    uint8_t *dst = arena + slot * CART_CHUNK;
    for (uint32_t got = 0; got < CART_CHUNK;)
    {
      uint32_t r = uvm2_sd_next(&f, dst + got, CART_CHUNK - got);
      if (r == 0) restart();
      got += r;
    }
  }
  uvm2_sd_close(&f);
  cart_finish(scheme, arena);
  if (scheme == CART_PB6 && s_key_hook) s_key_hook(zcrc32(arena, 0x10000u));
  if (scheme == CART_FLASH)
  {
    snprintf(s_save_path, sizeof s_save_path, "%s", path);
    cart_watch_flash(save_page);
  }
  cart_serve_banked(arena, scheme);
  return "COULD NOT START";
}

const char *cartload_builtin(const uint8_t *packed, uint32_t packed_bytes, uint32_t size, uint32_t crc,
                             uint8_t *arena, uint32_t arena_bytes)
{
  const int scheme = cart_builtin_scheme(size);
  if (scheme != CART_PB6 && scheme != CART_FLASH) return "NOT SUPPORTED";
  if (arena_bytes < (scheme == CART_FLASH ? CARTLOAD_ARENA_MIN : cart_image_bytes(scheme, size))) return "NOT ENOUGH MEMORY";
  if (zinflate(arena, size, packed, packed_bytes) != (int) size || zcrc32(arena, size) != crc) restart();
  cart_from_stream(scheme, arena, arena + 0x30000u);
  if (scheme == CART_FLASH)
  {
    /* the byte before the zlib stream is the number of the save page, in 4 KB units of the image (cart.h, cart_builtin_prefix) */
    const uint32_t page = packed[-1];
    if (s_saves_allowed && page < CART_FLASH_PAGES)
    {
      s_bs_key = crc; s_bs_off = page * CART_FLASH_PAGE; s_bs_waiting = 0;
      (void) bs_load(FLASH_NOCACHE, crc, arena + s_bs_off);
      cart_watch_flash(bs_watch);
    }
  }
  else if (s_key_hook) s_key_hook(crc);                    /* the firmware keeps no score for a 192 KB game: its page is its save */
  cart_serve_banked(arena, scheme);
  return "COULD NOT START";
}
