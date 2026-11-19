/* sdfast.c: see sdfast.h. The SDK's SPI SD driver (uvm2_sd.c) brings the card up and is then left alone; these talk to
 * the same card on the same port (SPI0, GPIO 34-36, CS on GPIO 39, raised to 25 MHz) with the multi-block
 * commands, and keep the SPI FIFO full instead of waiting for every byte. Every wait is bounded, as in the SDK: a card
 * that stops answering gives an error, never a hang. */
#include "sdfast.h"
#include "hardware/spi.h"
#include "hardware/structs/sio.h"

#define PIN_CS      39
/* 25 MHz, the card's default-speed limit: clk_peri 150 MHz / 6 exactly. The SDK leaves the port at 12.5 MHz (one
 * sector in 0.33 ms; at 1.24 MB/s measured through USB the card time was the larger half). Checked at start: sector 0
 * read this way must match the SDK's reading (usbdrive.c), or the slow path is used. */
#define FAST_BAUD   25000000u
#define SPIN_LIMIT  200000u
#define BUSY_LIMIT  2000000u            /* bytes of 0.64 us: over a second for the card to finish programming */

static int s_sdhc, s_err;

/* CS is active low and lives in the high GPIO bank (pins 32-47), driven as a GPIO by the SDK. */
static void cs(int on)
{
  if (on) sio_hw->gpio_hi_clr = 1u << (PIN_CS - 32);
  else sio_hw->gpio_hi_set = 1u << (PIN_CS - 32);
}

static int wait(uint32_t mask)
{
  for (uint32_t n = SPIN_LIMIT; n; n--) if (spi_get_hw(spi0)->sr & mask) return 1;
  s_err = 1;
  return 0;
}

static uint8_t xfer(uint8_t out)
{
  if (s_err || !wait(SPI_SSPSR_TNF_BITS)) return 0xFF;
  spi_get_hw(spi0)->dr = out;
  if (!wait(SPI_SSPSR_RNE_BITS)) return 0xFF;
  return (uint8_t) spi_get_hw(spi0)->dr;
}

/* n bytes out (0xFF if out is 0) and in (dropped if in is 0), up to 8 in flight: the FIFO's depth. */
static void burst(const uint8_t *out, uint8_t *in, uint32_t n)
{
  uint32_t sent = 0, got = 0, spins = 0;
  spi_hw_t *h = spi_get_hw(spi0);
  while (got < n && !s_err)
  {
    if (sent < n && sent - got < 8u && (h->sr & SPI_SSPSR_TNF_BITS)) { h->dr = out ? out[sent] : 0xFF; sent++; spins = 0; }
    if (h->sr & SPI_SSPSR_RNE_BITS) { uint8_t b = (uint8_t) h->dr; if (in) in[got] = b; got++; spins = 0; }
    else if (++spins > SPIN_LIMIT) s_err = 1;
  }
}

static uint8_t command(uint8_t idx, uint32_t arg)
{
  const uint8_t frame[7] = { 0xFF, (uint8_t) (0x40 | idx), (uint8_t) (arg >> 24), (uint8_t) (arg >> 16),
                             (uint8_t) (arg >> 8), (uint8_t) arg, 0xFF };
  burst(frame, 0, sizeof frame);
  for (int i = 0; i < 10; i++)
  {
    uint8_t r = xfer(0xFF);
    if (!(r & 0x80)) return r;
  }
  return 0xFF;
}

static int not_busy(void)
{
  for (uint32_t i = 0; i < BUSY_LIMIT; i++) if (xfer(0xFF) == 0xFF) return 1;
  return 0;
}

int sdfast_init(void)
{
  s_err = 0;
  spi_set_baudrate(spi0, FAST_BAUD);
  cs(1);
  /* CMD58: the OCR's CCS bit says whether the card counts in blocks (SDHC/SDXC) or bytes */
  int ok = command(58, 0) == 0x00;
  uint8_t ocr[4];
  burst(0, ocr, 4);
  cs(0);
  xfer(0xFF);
  s_sdhc = (ocr[0] & 0x40) != 0;
  return ok && !s_err;
}

int sdfast_read(uint32_t lba, uint8_t *buf, uint32_t count)
{
  s_err = 0;
  cs(1);
  if (command(18, s_sdhc ? lba : lba * 512u) != 0x00) { cs(0); return 0; }
  for (uint32_t b = 0; b < count && !s_err; b++, buf += 512)
  {
    uint8_t t = 0xFF;
    for (uint32_t i = 0; i < SPIN_LIMIT && t == 0xFF; i++) t = xfer(0xFF);
    if (t != 0xFE) { s_err = 1; break; }
    burst(0, buf, 512);
    burst(0, 0, 2);                       /* CRC, not checked (as in the SDK) */
  }
  /* CMD12 stops the stream; the byte after it is a stuff byte, then R1, then busy */
  int err = s_err;
  s_err = 0;
  const uint8_t stop[7] = { 0xFF, 0x40 | 12, 0, 0, 0, 0, 0x61 };
  burst(stop, 0, sizeof stop);
  xfer(0xFF);
  uint8_t r = 0xFF;
  for (int i = 0; i < 10 && (r & 0x80); i++) r = xfer(0xFF);
  int ok = !err && !(r & 0x80) && not_busy() && !s_err;
  cs(0);
  xfer(0xFF);
  return ok;
}

int sdfast_write(uint32_t lba, const uint8_t *buf, uint32_t count)
{
  s_err = 0;
  cs(1);
  if (command(25, s_sdhc ? lba : lba * 512u) != 0x00) { cs(0); return 0; }
  int ok = 1;
  for (uint32_t b = 0; b < count && ok; b++, buf += 512)
  {
    xfer(0xFF);
    xfer(0xFC);                           /* start of a block in a multi-block write */
    burst(buf, 0, 512);
    burst(0, 0, 2);                       /* CRC, ignored over SPI */
    uint8_t r = 0xFF;
    for (int i = 0; i < 1000 && (r & 0x11) != 0x01; i++) r = xfer(0xFF);
    ok = (r & 0x1F) == 0x05 && not_busy() && !s_err;
  }
  /* Stop Tran, then the card is busy once more while it finishes */
  s_err = 0;
  xfer(0xFD);
  xfer(0xFF);
  ok = not_busy() && ok && !s_err;
  cs(0);
  xfer(0xFF);
  return ok;
}
