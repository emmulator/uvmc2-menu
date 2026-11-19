/* cartstream.c: plays a streamed cartridge (the firmware's videos): the player in two banks, refilled while it runs.
 * Device code; the bank rules are cart.c's (host-tested), the bus side is cartserve.c. See docs/bankswitch.md, section 6. */
#include "cartstream.h"
#include "cart.h"
#include "cartserve.h"
#include "inflate.h"
#include "launch.h"
#include "uvm2_sd.h"
#include <string.h>

/* The arena is laid out as the banks (64 KB), then the inflate window (32 KB). */
#define WINDOW_AT CART_STREAM_BANKS

static int s_started;

/* The player is started the first time a piece has to wait, that is once pieces 0 and 1 are in place. After that this
 * waits until the player has left the bank the next piece goes to, rebooting if the console's reset is held. */
static void wait_flips(uint32_t flips, void *user)
{
  uint8_t *banks = user;
  if (!s_started) { s_started = 1; cart_stream_start(banks); }
  while (cart_stream_flipped() < flips) cart_check_reset();
  cart_check_reset();
}

static void restart(void)
{
  launch_cartridge_reboot();
  for (;;) { }
}

static const char *prepare(const uint8_t *player, uint8_t *arena, uint32_t arena_bytes, CartFeed *f)
{
  if (!player) return "NO VIDEO PLAYER IN FIRMWARE";
  if (arena_bytes < CARTSTREAM_ARENA_MIN) return "NOT ENOUGH MEMORY";
  memset(arena, 0, CART_STREAM_BANKS);
  memcpy(arena, player, CART_STREAM_PLAYER);
  memcpy(arena + 0x8000u, player, CART_STREAM_PLAYER);
  s_started = 0;
  f->banks = arena; f->piece = 0; f->k = 0; f->wait = wait_flips; f->user = arena;
  return 0;
}

const char *cartstream_builtin(const uint8_t *packed, uint32_t packed_bytes, uint32_t size, const uint8_t *player,
                               uint8_t *arena, uint32_t arena_bytes)
{
  CartFeed f;
  const char *why = prepare(player, arena, arena_bytes, &f);
  if (why) return why;
  for (;;)
  {
    /* from here the arena is the player's: a bad stream cannot go back to the menu */
    if (zinflate_stream(packed, packed_bytes, arena + WINDOW_AT, cart_feed_byte, &f) != (int) size) restart();
    cart_feed_end(&f);
  }
}

const char *cartstream_file(const char *path, const uint8_t *player, uint8_t *arena, uint32_t arena_bytes)
{
  uvm2_sd_file sd;
  CartFeed f;
  if (!uvm2_sd_open(path, &sd)) return "CANNOT OPEN FILE";
  const char *why = prepare(player, arena, arena_bytes, &f);
  if (why) { uvm2_sd_close(&sd); return why; }
  const uint32_t len = sd.len;
  uint32_t pos = 0;
  for (;;)
  {
    if (f.piece >= 2u) wait_flips(cart_stream_flips(f.piece), arena);
    uint8_t *dst = arena + cart_stream_offset(f.piece);
    uint32_t got = 0;
    while (got < CART_STREAM_PIECE && pos < len)
    {
      uint32_t r = uvm2_sd_next(&sd, dst + got, CART_STREAM_PIECE - got);
      if (r == 0) restart();
      got += r; pos += r;
    }
    f.piece++;
    if (pos >= len)
    {
      if (!uvm2_sd_seek(&sd, 0)) restart();
      pos = 0;
    }
  }
}
