/* Host tests for cart.c: the firmware's size rules (docs/bankswitch.md), the layout of the images, and that the
 * interpolator constants find, for every address and bank line, the byte the file says. */
#include "cart.h"
#include "inflate.h"
#include "sdsize.h"
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int passed, failed;
#define CHECK(x) do { if (x) passed++; else { failed++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

/* sizes of the .bin and .vec files on the user's card (and ~/Games/Vectrex), as the firmware's loader 0x10000944 sees them */
static const struct { const char *name; uint32_t size; int scheme; } files[] = {
  { "settings.bin", 54, CART_PLAIN },
  { "Wormhole_with_autofire_by_NigelH.vec", 32768, CART_PLAIN },
  { "Star Seige by John Dondzila (1999).vec", 32768, CART_PLAIN },
  { "minestorm1.bin", 8192, CART_PLAIN },
  { "NeonArena_v058_16k.bin", 16384, CART_PLAIN },
  { "AlienStorm.bin", 32062, CART_PLAIN },
  { "contrex.bin", 65536, CART_PB6 },
  { "crazy_comets.bin", 65536, CART_PB6 },
  { "GalaxyWarsSL_v102.bin", 65536, CART_PB6 },
  { "LFO-LFO_v100.bin", 65536, CART_PB6 },
  { "llamatron.bin", 65536, CART_PB6 },
  { "MStormArcade.bin", 65536, CART_PB6 },
  { "Neon_Hawk_demo_v1_0.bin", 65536, CART_PB6 },
  { "Robot_Arena.bin", 65536, CART_PB6 },
  { "starace.bin", 65536, CART_PB6 },
  { "Telengard_64kPB6.bin", 65536, CART_PB6 },
  { "wizard_of_wor.bin", 65536, CART_PB6 },
  { "Telengard_128kFlash.bin", 131072, CART_NONE },        /* the firmware refuses it */
  { "Telengard_256kFlash.bin", 262144, CART_FLASH },
  { "VectorbladeV1.13.bin", 262144, CART_FLASH },
  { "TankPotrace310.bin", 6936577, CART_STREAM },          /* the firmware streams it */
};

static void test_rules(void)
{
  for (size_t i = 0; i < sizeof files / sizeof files[0]; i++)
  {
    if (cart_file_scheme(files[i].size) != files[i].scheme) printf("  %s\n", files[i].name);
    CHECK(cart_file_scheme(files[i].size) == files[i].scheme);
  }
  /* the loader 0x10000944: 0 fails, up to 0x8000 plain, 0x10000 and 0x40000 special, nothing else below 0x40001 */
  CHECK(cart_file_scheme(0) == CART_NONE);
  CHECK(cart_file_scheme(1) == CART_PLAIN);
  CHECK(cart_file_scheme(0x8000) == CART_PLAIN);
  CHECK(cart_file_scheme(0x8001) == CART_NONE);
  CHECK(cart_file_scheme(0xffff) == CART_NONE);
  CHECK(cart_file_scheme(0x10000) == CART_PB6);
  CHECK(cart_file_scheme(0x10001) == CART_NONE);
  CHECK(cart_file_scheme(0x20000) == CART_NONE);
  CHECK(cart_file_scheme(0x30000) == CART_NONE);         /* the unpacked built-in form is not a file size */
  CHECK(cart_file_scheme(0x3ffff) == CART_NONE);
  CHECK(cart_file_scheme(0x40000) == CART_FLASH);
  CHECK(cart_file_scheme(0x40001) == CART_STREAM);       /* over 256 KB: streamed (0x10000a70) */
  /* built-ins: the record's unpacked size (the launcher 0x100006f0) */
  CHECK(cart_builtin_scheme(0x1000) == CART_PLAIN);
  CHECK(cart_builtin_scheme(0x8000) == CART_PLAIN);
  CHECK(cart_builtin_scheme(0x10000) == CART_PB6);
  CHECK(cart_builtin_scheme(0x30000) == CART_FLASH);
  CHECK(cart_builtin_scheme(0x40000) == CART_NONE);
  CHECK(cart_builtin_scheme(0x85801) == CART_STREAM);
  CHECK(cart_builtin_prefix(0x30000) == 1 && cart_builtin_prefix(0x10000) == 0 && cart_builtin_prefix(0x2000) == 0);
  /* every refusal has a line that fits the menu's error line */
  const uint32_t refused[] = { 0, 0x8001, 0x20000, 0x30000, 0x3ffff };
  for (size_t i = 0; i < sizeof refused / sizeof refused[0]; i++) CHECK(strlen(cart_refusal(refused[i])) <= 30);
  CHECK(strcmp(cart_refusal(0x20000), "128K NOT SUPPORTED") == 0);
  CHECK(cart_image_bytes(CART_PB6, 0x10000) == 0x20000 && cart_image_bytes(CART_FLASH, 0x40000) == 0x30000);
  CHECK(cart_image_bytes(CART_PLAIN, 1234) == 1234);
  CHECK(cart_image_bytes(CART_STREAM, 6936577) == 0x10000);
}

/* A file of the size the scheme wants, every 16 KB piece of it a different byte value, so a wrong slot shows. A byte is
 * file[offset] = offset / 256 xor offset / 65536 * 7, which differs for every 256-byte row of a 256 KB file. */
static uint8_t pattern(uint32_t off) { return (uint8_t) ((off >> 8) * 5u + (off >> 16) * 13u + off); }
static uint8_t *make_file(uint32_t size)
{
  uint8_t *f = malloc(size);
  for (uint32_t i = 0; i < size; i++) f[i] = pattern(i);
  return f;
}

/* The bus word for address a (0 to 0xffff) with the two bank lines, as the 6809 reading would give it. */
static uint32_t bus(uint32_t a, uint32_t pb6, uint32_t g23)
{
  return ((a & 0x3fffu) << 8) | (pb6 << 22) | (g23 << 23) | (((a >> 14) & 1u) << 24) | (((a >> 15) & 1u) << 25) | (1u << 26);
}

static uint8_t *file_image(int scheme, const uint8_t *file)
{
  uint8_t *img = calloc(1, cart_image_bytes(scheme, 0));
  for (int n = 0; n < cart_pieces(scheme); n++)
  {
    uint32_t off, slot;
    cart_piece(scheme, n, &off, &slot);
    memcpy(img + slot * CART_CHUNK, file + off, CART_CHUNK);
  }
  cart_finish(scheme, img);
  return img;
}

static void test_pb6(void)
{
  uint8_t *file = make_file(0x10000), *img = file_image(CART_PB6, file);
  CHECK(cart_pieces(CART_PB6) == 4);
  /* the 6809 reads A0-A14 below $8000; PB6 picks the 32 KB bank; the second line (GPIO23) changes nothing */
  int bad = 0;
  for (uint32_t pb6 = 0; pb6 < 2; pb6++)
    for (uint32_t g23 = 0; g23 < 2; g23++)
      for (uint32_t a = 0; a < 0x8000; a++)
        if (img[cart_offset(CART_PB6, bus(a, pb6, g23))] != file[pb6 * 0x8000 + a]) bad++;
  CHECK(bad == 0);
  /* every one of the 8 slots was filled from the file */
  int slots = 0;
  for (uint32_t s = 0; s < 8; s++)
  {
    int found = 0;
    for (uint32_t p = 0; p < 4; p++) if (memcmp(img + s * CART_CHUNK, file + p * CART_CHUNK, CART_CHUNK) == 0) found = 1;
    slots += found;
  }
  CHECK(slots == 8);
  free(file); free(img);
}

static void test_flash(void)
{
  uint8_t *file = make_file(0x40000), *img = file_image(CART_FLASH, file);
  CHECK(cart_pieces(CART_FLASH) == 12);
  /* four banks (GPIO22 + 2 * GPIO23), each seen at $0000-$BFFF: file byte 64 KB * bank + address */
  int bad = 0;
  for (uint32_t bank = 0; bank < 4; bank++)
    for (uint32_t a = 0; a < 0xc000; a++)
      if (img[cart_offset(CART_FLASH, bus(a, bank & 1, bank >> 1))] != file[bank * 0x10000 + a]) bad++;
  CHECK(bad == 0);
  /* no read reaches past the image */
  uint32_t max = 0;
  for (uint32_t bank = 0; bank < 4; bank++)
    for (uint32_t a = 0; a < 0xc000; a += 0x3fff)
    {
      uint32_t o = cart_offset(CART_FLASH, bus(a, bank & 1, bank >> 1));
      if (o > max) max = o;
    }
  CHECK(max < 0x30000);
  /* the pieces read are 3 per bank, in the first 48 KB of each 64 KB, and each slot is used once */
  uint32_t seen = 0;
  for (int n = 0; n < 12; n++)
  {
    uint32_t off, slot;
    cart_piece(CART_FLASH, n, &off, &slot);
    CHECK((off & 0xffff) < 0xc000 && slot < 12 && !(seen & (1u << slot)));
    seen |= 1u << slot;
  }
  CHECK(seen == 0xfff);
  free(file); free(img);
}

/* A built-in unpacks to the file's pieces one after the other; cart_from_stream must give the image the file gives. */
static void test_stream(void)
{
  for (int scheme = CART_PB6; scheme <= CART_FLASH; scheme++)
  {
    uint32_t size = scheme == CART_PB6 ? 0x10000 : 0x40000;
    uint8_t *file = make_file(size), *want = file_image(scheme, file);
    uint32_t bytes = cart_image_bytes(scheme, 0);
    uint8_t *img = malloc(bytes + CART_CHUNK), *tmp = img + bytes;
    memset(img, 0xee, bytes + CART_CHUNK);
    for (int n = 0; n < cart_pieces(scheme); n++)
    {
      uint32_t off, slot;
      cart_piece(scheme, n, &off, &slot);
      memcpy(img + (uint32_t) n * CART_CHUNK, file + off, CART_CHUNK);
    }
    cart_from_stream(scheme, img, tmp);
    CHECK(memcmp(img, want, bytes) == 0);
    free(file); free(want); free(img);
  }
}

/* The hand-worked values of docs/bankswitch.md, section 3 and 4. */
static void test_layout(void)
{
  uint32_t off, slot;
  const uint32_t pb6_slot[4] = { 0, 4, 1, 5 };
  for (int n = 0; n < 4; n++)
  {
    cart_piece(CART_PB6, n, &off, &slot);
    CHECK(off == (uint32_t) n * 0x4000 && slot == pb6_slot[n]);
  }
  cart_piece(CART_FLASH, 0, &off, &slot);  CHECK(off == 0 && slot == 0);
  cart_piece(CART_FLASH, 2, &off, &slot);  CHECK(off == 0x8000 && slot == 8);
  cart_piece(CART_FLASH, 3, &off, &slot);  CHECK(off == 0x10000 && slot == 1);
  cart_piece(CART_FLASH, 11, &off, &slot); CHECK(off == 0x30000 + 0x8000 && slot == 11);
  /* the interpolator values: lane 1 shift 8, mask 14-16 (PB6) or 14-17 (flash) */
  CHECK(cart_offset(CART_PB6, 1u << 22) == 0x4000 && cart_offset(CART_PB6, 1u << 23) == 0x8000);
  CHECK(cart_offset(CART_PB6, 1u << 24) == 0x10000 && cart_offset(CART_PB6, 1u << 25) == 0);
  CHECK(cart_offset(CART_FLASH, 1u << 25) == 0x20000 && cart_offset(CART_FLASH, 1u << 24) == 0x10000);
  CHECK(cart_offset(CART_FLASH, 0x3fffu << 8) == 0x3fff);
  CHECK(cart_offset(CART_PLAIN, 1u << 24) == 0x4000 && cart_offset(CART_PLAIN, 1u << 22) == 0);   /* the 32 KB setting */
}

/* ---- streaming ---- */

/* A fake player: the banks as the cartridge holds them, checked each time a piece is waited for. */
static struct { uint8_t *banks; const uint8_t *file; uint32_t len, flips, waits, bad; uint32_t want_flips[8]; } fp;

/* Before piece n (n >= 2) may be written, the player has switched n - 1 times; it is then reading piece n - 1 from bank
 * (n - 1) & 1, and that bank must hold exactly that piece of the file (from the start again after the end). */
static void fake_wait(uint32_t flips, void *user)
{
  (void) user;
  if (fp.waits < 8) fp.want_flips[fp.waits] = flips;
  fp.waits++;
  fp.flips = flips;
  uint32_t n = flips, at = 0, per = (fp.len + CART_STREAM_PIECE - 1) / CART_STREAM_PIECE;
  at = (n % per) * CART_STREAM_PIECE;
  uint32_t k = fp.len - at < CART_STREAM_PIECE ? fp.len - at : CART_STREAM_PIECE;
  if (memcmp(fp.banks + (n & 1u) * 0x8000u + 0x4000u, fp.file + at, k)) fp.bad++;   /* not cart_stream_offset: its own check */
}

static void test_feed(void)
{
  CHECK(cart_stream_offset(0) == 0x4000 && cart_stream_offset(1) == 0xC000 && cart_stream_offset(2) == 0x4000);
  CHECK(cart_stream_flips(0) == 0 && cart_stream_flips(1) == 0 && cart_stream_flips(2) == 1 && cart_stream_flips(9) == 8);
  /* a file of 10 whole pieces and one byte, like the firmware's videos, fed twice round */
  const uint32_t len = 10 * CART_STREAM_PIECE + 1;
  uint8_t *file = make_file(len), *banks = calloc(1, CART_STREAM_BANKS);
  memset(&fp, 0, sizeof fp);
  fp.banks = banks; fp.file = file; fp.len = len;
  CartFeed f = { banks, 0, 0, fake_wait, 0 };
  for (int round = 0; round < 2; round++)
  {
    for (uint32_t i = 0; i < len; i++) cart_feed_byte(file[i], &f);
    cart_feed_end(&f);
  }
  CHECK(f.piece == 22 && f.k == 0);           /* 11 pieces a round, the last holding one byte */
  CHECK(fp.waits == 20);                      /* pieces 2 to 21 */
  CHECK(fp.want_flips[0] == 1 && fp.want_flips[1] == 2 && fp.want_flips[7] == 8);
  CHECK(fp.bad == 0);
  /* the player at $0000 is never written: only $4000-$45FF of each bank */
  int outside = 0;
  for (uint32_t a = 0; a < CART_STREAM_BANKS; a++)
    if (((a & 0x7fff) < CART_STREAM_DATA || (a & 0x7fff) >= CART_STREAM_DATA + CART_STREAM_PIECE) && banks[a]) outside++;
  CHECK(outside == 0);
  free(file); free(banks);
}

/* zinflate_stream against zlib, on data with matches reaching back the full 32 KB window. */
static uint32_t s_crc, s_n;
static uint8_t *s_out;
static void sink(uint8_t b, void *u) { (void) u; s_out[s_n++] = b; }

static void test_inflate_stream(void)
{
  const uint32_t len = 300000;
  uint8_t *src = malloc(len);
  uint32_t x = 1;
  for (uint32_t i = 0; i < len; i++)
  {
    x = x * 1103515245u + 12345u;
    /* runs, repeats from far back (up to 32 KB), and noise */
    if (i > 32768 && (x >> 28) < 6) src[i] = src[i - 32768 + ((x >> 8) & 7)];
    else if (i > 300 && (x >> 28) < 10) src[i] = src[i - 300];
    else src[i] = (uint8_t) (x >> 16);
  }
  for (int level = 0; level <= 9; level += 9)
  {
    uLongf zn = compressBound(len);
    uint8_t *z = malloc(zn), *window = malloc(ZWINDOW);
    CHECK(compress2(z, &zn, src, len, level) == Z_OK);
    s_out = malloc(len); s_n = 0;
    CHECK(zinflate_stream(z, (uint32_t) zn, window, sink, 0) == (int) len);
    CHECK(s_n == len && memcmp(s_out, src, len) == 0);
    uint8_t *one = malloc(len);
    CHECK(zinflate(one, len, z, (uint32_t) zn) == (int) len && memcmp(one, src, len) == 0);
    free(z); free(window); free(s_out); free(one);
  }
  (void) s_crc;
  free(src);
}

/* ---- the USB drive's size (sdsize.c) ---- */
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); p[2] = (uint8_t) (v >> 16); p[3] = (uint8_t) (v >> 24); }

static void test_sdsize(void)
{
  uint8_t s[512];
  /* the TEST card (lsblk): one FAT32 partition from 2048, 122816512 sectors; the card is 122818560 */
  memset(s, 0, sizeof s);
  s[0] = 0xFA; s[510] = 0x55; s[511] = 0xAA;
  s[446 + 4] = 0x0C; put32(s + 446 + 8, 2048); put32(s + 446 + 12, 122816512u);
  CHECK(sd_blocks(s) == 122818560u);
  /* a second partition further on: the end of the last one */
  s[462 + 4] = 0x83; put32(s + 462 + 8, 200000000u); put32(s + 462 + 12, 1000u);
  CHECK(sd_blocks(s) == 200001000u);
  /* an MBR whose boot code starts with a jump (GRUB's: EB 63) is still read as an MBR */
  memset(s + 462, 0, 16);
  s[0] = 0xEB; s[1] = 0x63; s[2] = 0x90; s[11] = 0x00; s[12] = 0x02; s[13] = 0x00; s[16] = 0x00;
  CHECK(sd_blocks(s) == 122818560u);
  /* no signature, or a bad boot flag: not a disk we know */
  s[511] = 0; CHECK(sd_blocks(s) == 0); s[511] = 0xAA;
  s[446] = 0x12; CHECK(sd_blocks(s) == 0); s[446] = 0;
  /* a FAT32 volume with no partition table: its own total */
  memset(s, 0, sizeof s);
  s[0] = 0xEB; s[1] = 0x58; s[2] = 0x90; s[11] = 0x00; s[12] = 0x02; s[13] = 64; s[16] = 2; s[510] = 0x55; s[511] = 0xAA;
  put32(s + 32, 62333952u);
  CHECK(sd_blocks(s) == 62333952u);
  s[19] = 0x40; s[20] = 0x1F;                  /* FAT16's 16-bit total wins when set */
  CHECK(sd_blocks(s) == 8000u);
  /* exFAT with no partition table: the volume length */
  memset(s, 0, sizeof s);
  s[0] = 0xEB; s[1] = 0x76; s[2] = 0x90; memcpy(s + 3, "EXFAT   ", 8); s[510] = 0x55; s[511] = 0xAA;
  put32(s + 72, 122816512u);
  CHECK(sd_blocks(s) == 122816512u);
  /* an empty partition table */
  memset(s, 0, sizeof s); s[510] = 0x55; s[511] = 0xAA;
  CHECK(sd_blocks(s) == 0);
}

/* Saving a 256 KB game: an image offset maps back to the file offset the firmware writes it to
 * (block * 64 KB + chunk * 16 KB + page * 4 KB, docs/bankswitch.md section 8). */
static void test_flash_file_offset(void)
{
  /* the inverse of cart_piece, byte by byte at the corners of every 4 KB page */
  int bad = 0;
  for (int n = 0; n < cart_pieces(CART_FLASH); n++)
  {
    uint32_t off, slot;
    cart_piece(CART_FLASH, n, &off, &slot);
    for (uint32_t k = 0; k < CART_CHUNK; k += 0x1000u)
    {
      if (cart_flash_file_offset(slot * CART_CHUNK + k) != off + k) bad++;
      if (cart_flash_file_offset(slot * CART_CHUNK + k + 0xfffu) != off + k + 0xfffu) bad++;
    }
  }
  CHECK(bad == 0);
  /* the firmware's own arithmetic for page i of the 48 (0x10000be0): ((i>>2)&12 | i&3 | (i<<2)&48) << 12 */
  bad = 0;
  for (uint32_t i = 0; i < 48; i++)
    if (cart_flash_file_offset(i * 0x1000u) != ((((i >> 2) & 12u) | (i & 3u) | ((i << 2) & 48u)) << 12)) bad++;
  CHECK(bad == 0);
  CHECK(cart_flash_file_offset(0) == 0);
  CHECK(cart_flash_file_offset(0x4000u) == 0x10000u);           /* slot 1 is bank 1, window 0 */
  CHECK(cart_flash_file_offset(0x10000u) == 0x4000u);           /* slot 4 is bank 0, window 1 */
}

int main(void)
{
  test_rules();
  test_layout();
  test_pb6();
  test_flash();
  test_flash_file_offset();
  test_stream();
  test_feed();
  test_inflate_stream();
  test_sdsize();
  printf("%d passed, %d failed\n", passed, failed);
  return failed != 0;
}
