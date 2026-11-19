/* cart.c: see cart.h. */
#include "cart.h"
#include <string.h>

int cart_file_scheme(uint32_t size)
{
  if (size == 0) return CART_NONE;
  if (size <= CART_PLAIN_MAX) return CART_PLAIN;
  if (size == 0x10000u) return CART_PB6;
  if (size == 0x40000u) return CART_FLASH;
  if (size > 0x40000u) return CART_STREAM;
  return CART_NONE;
}

int cart_builtin_scheme(uint32_t size)
{
  if (size == 0) return CART_NONE;
  if (size <= CART_PLAIN_MAX) return CART_PLAIN;
  if (size == 0x10000u) return CART_PB6;
  if (size == 0x30000u) return CART_FLASH;
  if (size > 0x40000u) return CART_STREAM;
  return CART_NONE;
}

uint32_t cart_builtin_prefix(uint32_t size) { return size == 0x30000u ? 1u : 0u; }

const char *cart_refusal(uint32_t size)
{
  if (size == 0) return "EMPTY FILE";
  if (size == 0x20000u) return "128K NOT SUPPORTED";
  return "SIZE MUST BE 32K, 64K, 256K";
}

uint32_t cart_image_bytes(int scheme, uint32_t size)
{
  if (scheme == CART_PB6) return 0x20000u;
  if (scheme == CART_FLASH) return 0x30000u;
  if (scheme == CART_STREAM) return CART_STREAM_BANKS;
  return size;
}

int cart_pieces(int scheme) { return scheme == CART_PB6 ? 4 : scheme == CART_FLASH ? 12 : 0; }

void cart_piece(int scheme, int n, uint32_t *file_offset, uint32_t *slot)
{
  uint32_t u = (uint32_t) n;
  if (scheme == CART_PB6)
  {
    /* The file is two 32 KB banks. A piece is a bank's 16 KB window: slot = bank + 4 * window. */
    *file_offset = u * CART_CHUNK;
    *slot = (u >> 1) + 4u * (u & 1u);
  }
  else
  {
    /* The file is four 64 KB banks of which the first 48 KB is used, in three windows: slot = bank + 4 * window. */
    *file_offset = (u / 3u) * 0x10000u + (u % 3u) * CART_CHUNK;
    *slot = u / 3u + 4u * (u % 3u);
  }
}

uint32_t cart_flash_file_offset(uint32_t image_offset)
{
  uint32_t slot = image_offset / CART_CHUNK, in = image_offset % CART_CHUNK;
  /* slot = bank + 4 * window (cart_piece): the file has the bank's 64 KB, and the window's 16 KB in it */
  return (slot & 3u) * 0x10000u + (slot >> 2) * CART_CHUNK + in;
}

void cart_finish(int scheme, uint8_t *image)
{
  if (scheme != CART_PB6) return;
  for (uint32_t s = 0; s < 8; s += 4)
    for (uint32_t b = 0; b < 2; b++)
      memcpy(image + (s + b + 2u) * CART_CHUNK, image + (s + b) * CART_CHUNK, CART_CHUNK);
}

void cart_from_stream(int scheme, uint8_t *image, uint8_t *tmp)
{
  if (scheme == CART_PB6)
  {
    /* pieces 0..3 sit in slots 0..3 and go to 0, 4, 1, 5: in this order each target is free when its turn comes */
    for (int n = 1; n < 4; n++)
    {
      uint32_t off, slot;
      cart_piece(scheme, n, &off, &slot);
      memcpy(image + slot * CART_CHUNK, image + (uint32_t) n * CART_CHUNK, CART_CHUNK);
    }
  }
  else if (scheme == CART_FLASH)
  {
    /* A permutation of 12 slots in cycles: the slot d takes what is at src(d); a cycle needs one piece set aside. */
    uint8_t done[12];
    memset(done, 0, sizeof done);
    for (uint32_t start = 0; start < 12; start++)
    {
      uint32_t src = (start & 3u) * 3u + (start >> 2);
      if (done[start] || src == start) { done[start] = 1; continue; }
      memcpy(tmp, image + start * CART_CHUNK, CART_CHUNK);
      uint32_t d = start;
      for (;;)
      {
        done[d] = 1;
        src = (d & 3u) * 3u + (d >> 2);
        if (src == start) { memcpy(image + d * CART_CHUNK, tmp, CART_CHUNK); break; }
        memcpy(image + d * CART_CHUNK, image + src * CART_CHUNK, CART_CHUNK);
        d = src;
      }
    }
  }
  cart_finish(scheme, image);
}

uint32_t cart_stream_offset(uint32_t piece) { return ((piece & 1u) << 15) + CART_STREAM_DATA; }
uint32_t cart_stream_flips(uint32_t piece) { return piece < 2u ? 0u : piece - 1u; }

void cart_feed_byte(uint8_t b, void *feed)
{
  CartFeed *f = feed;
  if (f->k == 0 && f->piece >= 2u) f->wait(cart_stream_flips(f->piece), f->user);
  f->banks[cart_stream_offset(f->piece) + f->k] = b;
  if (++f->k == CART_STREAM_PIECE) { f->k = 0; f->piece++; }
}

void cart_feed_end(CartFeed *f)
{
  if (f->k) { f->k = 0; f->piece++; }
}

/* What the interpolator does to a lane: rotate the word right by SHIFT, keep bits MASK_LSB to MASK_MSB. */
static uint32_t lane(uint32_t ctrl, uint32_t accum)
{
  uint32_t shift = ctrl & 31u, lsb = (ctrl >> 5) & 31u, msb = (ctrl >> 10) & 31u;
  uint32_t v = shift ? (accum >> shift) | (accum << (32u - shift)) : accum;
  uint32_t mask = (msb == 31u ? 0xffffffffu : (1u << (msb + 1u)) - 1u) & ~((1u << lsb) - 1u);
  return v & mask;
}

uint32_t cart_offset(int scheme, uint32_t g)
{
  uint32_t l1 = scheme == CART_PB6 ? CART_LANE1_PB6 : scheme == CART_FLASH ? CART_LANE1_FLASH : CART_LANE1_A14;
  return lane(CART_LANE0_A13, g) + lane(l1, g);
}
