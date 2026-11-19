/* cartserve.c: being a cartridge. See README.md and docs/bankswitch.md.
 *
 * Core 1 watches the bus. Bus bits (GPIO): D0-7 = 0-7, A0-13 = 8-21, PB6 = 22, A14 = 24, A15 = 25, R/W = 26, /HALT = 27,
 * /NMI = 29, ~E = 31. While ~E is low the address is stable. A read with A15 low is ours: put the byte on D0-7 until ~E
 * goes high. The address becomes a pointer through the interpolator, which is per core, so core 1 sets it up itself. */
#include "cartserve.h"
#include "cart.h"
#include "inflate.h"
#include "launch.h"
#include "uvm2_bus.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "hardware/structs/interp.h"
#include "hardware/resets.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"

#define B_CLK    (1u << 31)
#define M_RW_A15 ((1u << 26) | (1u << 25))
#define V_READ   (1u << 26)
#define PIN_HALT 27
#define PIN_NMI  29
#define CART_PINS 0xAFFFFFFFu
/* Reset held: the CPU sits on the $FFFE vector fetch. 1.5 MHz x 2 s. */
#define M_VECTOR 0x073FFE00u
#define HOLD_CYCLES 3000000u

static uint8_t *volatile s_rom;
static volatile uint32_t s_size, s_writable, s_held, s_scheme, s_flips;
static uint8_t s_score[7];
static int s_score_set;

void cart_set_score(const uint8_t score[7])
{
  if (score)
  {
    for (int i = 0; i < 7; i++) s_score[i] = score[i];
    s_score_set = 1;
  }
  else s_score_set = 0;
}

static inline uint32_t in_lo(void) { uint32_t v; __asm__ volatile ("mrc p0, #0, %0, c0, c8, #0" : "=r" (v)); return v; }
static inline uint32_t out_lo(void) { uint32_t v; __asm__ volatile ("mrc p0, #0, %0, c0, c0, #0" : "=r" (v)); return v; }
static inline void out_xor(uint32_t v) { __asm__ volatile ("mcr p0, #1, %0, c0, c0, #0" :: "r" (v)); }
static inline void oe_set(uint32_t v) { __asm__ volatile ("mcr p0, #2, %0, c0, c4, #0" :: "r" (v)); }
static inline void oe_clr(uint32_t v) { __asm__ volatile ("mcr p0, #3, %0, c0, c4, #0" :: "r" (v)); }

/* Answers a read below $C000 with the byte the interpolator points at: the address becomes a pointer, the byte is put on
 * D0-D7 until the clock goes high again. */
#define SERVE_BYTE(g)                                                  \
  do {                                                                 \
    interp0_hw->accum[0] = (g);                                        \
    const uint8_t *p_ = (const uint8_t *) interp0_hw->peek[2];         \
    out_xor((*p_ ^ out_lo()) & 0xffu);                                 \
    oe_set(0xffu);                                                     \
    while (!(in_lo() & B_CLK)) { }                                     \
    oe_clr(0xffu);                                                     \
  } while (0)

#define M_A1415  (3u << 24)
#define V_RW     (1u << 26)
/* The flash chip's command bytes, as the firmware matches them (docs/bankswitch.md, section 4): the data, A0-A14, and not
 * the bank lines (GPIO22, 23) or A15. $5555 is A14 with A0-A13 = $1555. */
#define FL_KEY      0x013fffffu
#define FL_AA_5555  0x011555aau
#define FL_55_2AAA  0x002aaa55u
#define FL_ADDR     0x013fff00u
#define FL_AT_5555  0x01155500u

/* The chip's identity, while it is in autoselect mode: every read gives the manufacturer (A0 low) or the device code
 * (A0 high) of an SST39SF020. A write of $F0 ends it. */
static void flash_autoselect(uint32_t *held)
{
  for (;;)
  {
    uint32_t g;
    do g = in_lo(); while (g & B_CLK);
    if ((g & M_A1415) == M_A1415)
    {
      if ((M_VECTOR & ~g) == 0) s_held = ++*held; else *held = 0;
    }
    else if (g & V_RW)
    {
      out_xor((((g & 0x100u) ? 0xB6u : 0xBFu) ^ out_lo()) & 0xffu);
      oe_set(0xffu);
      while (!(in_lo() & B_CLK)) { }
      oe_clr(0xffu);
      *held = 0;
      continue;
    }
    else if ((g & 0xffu) == 0xF0u) return;
    while (!(in_lo() & B_CLK)) { }
  }
}

/* The 192 KB image of a 256 KB cartridge: four banks, chosen by GPIO22 and GPIO23, each seen at $0000-$BFFF; a flash
 * chip that takes the program command and the identity command. Sector erase is left out, as it is in the firmware. */
static void flash_server(void)
{
  uint32_t held = 0, st = 0;
  for (;;)
  {
    uint32_t g;
    do g = in_lo(); while (g & B_CLK);
    if ((g & M_A1415) != M_A1415)
    {
      if (g & V_RW)
      {
        SERVE_BYTE(g);
        held = 0;
        continue;
      }
      held = 0;
      const uint32_t key = g & FL_KEY;
      if (st == 0) st = key == FL_AA_5555;
      else if (st == 1) st = key == FL_55_2AAA ? 2u : 0u;
      else if (st == 2)
      {
        st = 0;
        if ((g & FL_ADDR) == FL_AT_5555)
        {
          if ((g & 0xffu) == 0x90u) flash_autoselect(&held);
          else if ((g & 0xffu) == 0xA0u) st = 3;
        }
      }
      else
      {
        interp0_hw->accum[0] = g;
        *(uint8_t *) interp0_hw->peek[2] = (uint8_t) g;
        st = 0;
      }
    }
    else if ((M_VECTOR & ~g) == 0) s_held = ++held;
    else held = 0;
    while (!(in_lo() & B_CLK)) { }
  }
}

/* One bank of up to 32 KB, or two banks by PB6 (the interpolator does that): reads below $8000 only. */
static void plain_server(void)
{
  const uint32_t writable = s_writable;
  uint32_t held = 0;
  for (;;)
  {
    uint32_t g;
    do g = in_lo(); while (g & B_CLK);
    if ((g & M_RW_A15) == V_READ)
    {
      SERVE_BYTE(g);
      held = 0;
      continue;
    }
    if ((M_VECTOR & ~g) == 0) s_held = ++held;
    else
    {
      held = 0;
      if (writable && (g & M_RW_A15) == 0)
      {
        interp0_hw->accum[0] = g;
        *(uint8_t *) interp0_hw->peek[2] = (uint8_t) g;
      }
    }
    while (!(in_lo() & B_CLK)) { }
  }
}

/* The video player's two 32 KB banks: a write of $02 to $xxFF (A15 low) switches to the other one and counts, for core 0
 * to refill the one left (cart.h). Reads below $8000 as in plain_server. */
#define M_FLIP 0x0600ffffu
#define V_FLIP 0x0000ff02u
static void stream_server(void)
{
  uint8_t *const rom = s_rom;
  uint32_t held = 0, bank = 0;
  for (;;)
  {
    uint32_t g;
    do g = in_lo(); while (g & B_CLK);
    if ((g & M_RW_A15) == V_READ)
    {
      SERVE_BYTE(g);
      held = 0;
      continue;
    }
    if ((M_VECTOR & ~g) == 0) s_held = ++held;
    else
    {
      held = 0;
      if ((g & M_FLIP) == V_FLIP)
      {
        bank ^= 1u;
        interp0_hw->base[2] = (uint32_t) (rom + (bank << 15));
        s_flips++;
      }
    }
    while (!(in_lo() & B_CLK)) { }
  }
}

static void core1_server(void)
{
  /* the GPIO coprocessor is off until a core enables it */
  *(volatile uint32_t *) 0xE000ED88u |= 3u;
  __asm__ volatile ("dsb\n isb");

  uint32_t size = s_size, msb = 0;
  for (uint32_t v = size - 1; v > 1; v >>= 1) msb++;
  /* Lane 0: A0-13 from GPIO 8-21, masked to the image. Lane 1 (from ACCUM0): A14 from GPIO 24, for images over 16 KB;
   * for the banked images PB6 (GPIO22) and the lines above it. */
  interp0_hw->accum[0] = 0;
  interp0_hw->accum[1] = 0;
  interp0_hw->base[0] = 0;
  interp0_hw->base[1] = 0;
  interp0_hw->base[2] = (uint32_t) s_rom;
  if (s_scheme == CART_PB6 || s_scheme == CART_FLASH)
  {
    interp0_hw->ctrl[0] = CART_LANE0_A13;
    interp0_hw->ctrl[1] = s_scheme == CART_PB6 ? CART_LANE1_PB6 : CART_LANE1_FLASH;
  }
  else if (size < 0x4000u)
  {
    interp0_hw->ctrl[0] = 8u | (msb << 10);
    interp0_hw->ctrl[1] = 0x7c00u;
  }
  else if (size == 0x4000u)
  {
    interp0_hw->ctrl[0] = CART_LANE0_A13;
    interp0_hw->ctrl[1] = 0x7c00u;
  }
  else
  {
    interp0_hw->ctrl[0] = CART_LANE0_A13;
    interp0_hw->ctrl[1] = CART_LANE1_A14;
  }

  if (s_scheme == CART_FLASH) flash_server();
  if (s_scheme == CART_STREAM) stream_server();
  plain_server();
}

static void start(uint8_t *rom, uint32_t size, int scheme, int writable)
{
  s_rom = rom; s_size = size; s_writable = (uint32_t) writable; s_held = 0; s_scheme = (uint32_t) scheme; s_flips = 0;

  /* 1. Stop drawing. The 6809 is still halted and the bus is ours. */
  multicore_reset_core1();

  /* 2. The way back into the BIOS: its NMI vector jumps through RAM at $CBFB. JMP $F000 is what reset does. */
  uvm2_mem_write(0xCBFBu, 0x7Eu);
  uvm2_mem_write(0xCBFCu, 0xF0u);
  uvm2_mem_write(0xCBFDu, 0x00u);

  /* 2b. The BIOS's high-score slot, $CBEB-$CBF1, survives the warm start the NMI makes, so writing it here restores
   * the score the game had. The 6809 is still halted and the bus is ours. */
  if (s_score_set)
    for (uint32_t i = 0; i < 7; i++) uvm2_mem_write(0xCBEBu + i, s_score[i]);

  /* 3. The pins back to plain inputs, except /HALT, still held, and /NMI, pulled low: its edge waits for the 6809. */
  reset_block_mask(RESETS_RESET_DMA_BITS | RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS | RESETS_RESET_PIO2_BITS);
  uvm2_bus_pads();
  sio_hw->gpio_oe_clr = CART_PINS & ~(1u << PIN_HALT);
  sio_hw->gpio_clr = (1u << PIN_HALT) | (1u << PIN_NMI);
  sio_hw->gpio_oe_set = 1u << PIN_NMI;

  /* 4. Answer the bus first, then let the 6809 go. */
  multicore_launch_core1(core1_server);
  busy_wait_us_32(1000);
  sio_hw->gpio_oe_clr = 1u << PIN_HALT;
  busy_wait_us_32(5000);
  sio_hw->gpio_oe_clr = 1u << PIN_NMI;
  (void) save_and_disable_interrupts();
}

void cart_check_reset(void)
{
  if (s_held >= HOLD_CYCLES) launch_cartridge_reboot();
}

/* ---- the high score, as the firmware keeps it: the console's writes to the BIOS's slot, seen on the bus ---- */
static void (*s_score_save)(const uint8_t score[7]);
void cart_watch_score(void (*save)(const uint8_t score[7])) { s_score_save = save; }

#define A_SCORE   0xCBEBu
#define SETTLE_US 300000u                   /* a score is written a byte at a time: save once it has been still this long */

/* Worth saving: digits or spaces then $80, changed since the last save, and not all blanks and zeros (a game clearing
 * the slot is not a score). */
static int score_new(const uint8_t *ram, const uint8_t *kept)
{
  int digit = 0, same = 1;
  for (int i = 0; i < 7; i++) if (ram[i] != kept[i]) same = 0;
  for (int i = 0; i < 6; i++)
  {
    if (ram[i] != ' ' && (ram[i] < '0' || ram[i] > '9')) return 0;
    if (ram[i] > '0' && ram[i] <= '9') digit = 1;
  }
  return ram[6] == 0x80 && digit && !same;
}

/* Core 0 while the game runs. A write is ~E low (address and R/W stable) with R/W low; the 6809's data is valid until
 * ~E goes high, so the byte is the last one read before that edge. Every 4096 loops: the reset hold (saving a settled
 * score first) and the settle timer. */
static void watch_score(void)
{
  uint8_t ram[7], kept[7];
  for (int i = 0; i < 7; i++) ram[i] = kept[i] = s_score_set ? s_score[i] : 0;
  int dirty = 0;
  uint32_t t_write = 0, loops = 0;
  for (;;)
  {
    uint32_t v = in_lo();
    if (!(v & (B_CLK | V_RW)))
    {
      uint32_t k = (((v >> 8) & 0x3fffu) | ((v >> 10) & 0xc000u)) - A_SCORE;
      if (k < 7u)
      {
        uint32_t last = v;
        while (!(v & B_CLK)) { last = v; v = in_lo(); }
        ram[k] = (uint8_t) last;
        dirty = 1;
        t_write = time_us_32();
        continue;
      }
    }
    if (++loops & 4095u) continue;
    int held = s_held >= HOLD_CYCLES;
    if (dirty && (held || time_us_32() - t_write > SETTLE_US))
    {
      if (score_new(ram, kept)) { s_score_save(ram); for (int i = 0; i < 7; i++) kept[i] = ram[i]; }
      dirty = 0;
    }
    if (held) launch_cartridge_reboot();
  }
}

/* ---- a flash game's saves: the changed 4 KB pages of its image, written back by the menu ---- */
static int (*s_flash_save)(uint32_t image_offset, const uint8_t *page);
void cart_watch_flash(int (*save)(uint32_t image_offset, const uint8_t *page)) { s_flash_save = save; }

#define FLASH_PAGES_PER_TURN 4u            /* 48 pages in 12 turns of 10 ms: a change is seen within about 120 ms */
static void watch_flash(const uint8_t *image)
{
  uint32_t crc[CART_FLASH_PAGES];
  for (uint32_t i = 0; i < CART_FLASH_PAGES; i++) crc[i] = zcrc32(image + i * CART_FLASH_PAGE, CART_FLASH_PAGE);
  for (uint32_t next = 0;;)
  {
    for (uint32_t n = 0; n < FLASH_PAGES_PER_TURN; n++, next = (next + 1u) % CART_FLASH_PAGES)
    {
      const uint8_t *page = image + next * CART_FLASH_PAGE;
      uint32_t c = zcrc32(page, CART_FLASH_PAGE);
      if (c != crc[next] && s_flash_save(next * CART_FLASH_PAGE, page)) crc[next] = c;
    }
    cart_check_reset();
    busy_wait_us_32(10000);
  }
}

static void serve(uint8_t *rom, uint32_t size, int scheme, int writable)
{
  start(rom, size, scheme, writable);
  if (scheme == CART_FLASH && s_flash_save) watch_flash(rom);
  if (s_score_save) watch_score();
  /* Core 0 only waits for reset to be held. */
  for (;;) { cart_check_reset(); busy_wait_us_32(10000); }
}

void cart_stream_start(uint8_t *banks) { start(banks, 0x8000u, CART_STREAM, 0); }
uint32_t cart_stream_flipped(void) { return s_flips; }

void cart_serve(uint8_t *rom, uint32_t size, int writable) { serve(rom, size, CART_PLAIN, writable); }
void cart_serve_banked(uint8_t *image, int scheme) { serve(image, cart_image_bytes(scheme, 0), scheme, 0); }
