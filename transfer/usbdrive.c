/* usbdrive.c: the SD card as a USB drive (TinyUSB, a CDC port and MSC), as the firmware's Transfer Mode does (seen on
 * 1.1.13a: it drops its menu's USB device and comes back as cafe:4003 with a serial port and a drive). Device code; the
 * card's size is sdsize.c's (host-tested). Sectors go straight through FatFs's disk layer (the SDK's SPI driver), and
 * nothing else in this program touches the card. */
#include "usbdrive.h"
#include "sdsize.h"
#include "sdfast.h"
#include "tusb.h"
#include "ff.h"
#include "diskio.h"
#include "pico/time.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/pll.h"
#include "hardware/resets.h"
#include <string.h>

static volatile int s_state = USBD_WAITING;
static uint32_t s_blocks, s_read, s_written, s_slow;
static uint32_t s_rd_us, s_wr_us, s_rd_n, s_wr_n;     /* time in the card, and how many requests: for the screen */
static int s_fast;                       /* sdfast_init worked: runs of sectors go through sdfast.c */

/* ---- USB identity: the firmware's ids, our name ---- */
static const tusb_desc_device_t s_device = {
  .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
  .bDeviceClass = TUSB_CLASS_MISC, .bDeviceSubClass = MISC_SUBCLASS_COMMON, .bDeviceProtocol = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE, .idVendor = 0xCAFE, .idProduct = 0x4003, .bcdDevice = 0x0100,
  .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1,
};
uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *) &s_device; }

enum { ITF_CDC = 0, ITF_CDC_DATA, ITF_MSC, ITF_TOTAL };
#define CONFIG_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_MSC_DESC_LEN)
static const uint8_t s_config[] = {
  TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CONFIG_LEN, 0, 100),
  TUD_CDC_DESCRIPTOR(ITF_CDC, 4, 0x81, 8, 0x02, 0x82, 64),
  TUD_MSC_DESCRIPTOR(ITF_MSC, 5, 0x03, 0x83, 64),
};
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) { (void) index; return s_config; }

static const char *const s_strings[] = { "", "UVMC2 MENU", "UVMC2", "UVMC2MENU", "UVMC2 MENU", "UVMC2 SD CARD" };
static uint16_t s_str[32];
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void) langid;
  uint8_t n;
  if (index == 0) { s_str[1] = 0x0409; n = 1; }
  else
  {
    if (index >= sizeof s_strings / sizeof s_strings[0]) return 0;
    const char *s = s_strings[index];
    for (n = 0; s[n] && n < 31; n++) s_str[1 + n] = (uint16_t) s[n];
  }
  s_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * n + 2));
  return s_str;
}

/* ---- the drive ---- */
static void pad(uint8_t *d, const char *s, int n)
{
  int i = 0;
  for (; s[i] && i < n; i++) d[i] = (uint8_t) s[i];
  for (; i < n; i++) d[i] = ' ';
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
  (void) lun;
  pad(vendor_id, "UVMC2", 8);
  pad(product_id, "SD Card", 16);
  pad(product_rev, "1.0", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
  (void) lun;
  if (s_state == USBD_EJECTED) { tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00); return false; }
  return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
  (void) lun;
  *block_count = s_blocks;
  *block_size = 512;
}

/* An eject on the computer (START STOP UNIT with LoEj and not Start): the drive goes, and the cartridge restarts. */
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
  (void) lun; (void) power_condition;
  if (load_eject && !start) s_state = USBD_EJECTED;
  return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
  (void) lun;
  if (offset || (bufsize & 511u) || lba + bufsize / 512u > s_blocks) return -1;
  /* runs of sectors at once; if that fails, the SDK's one-at-a-time path */
  uint32_t t0 = time_us_32();
  if (!(s_fast && sdfast_read(lba, buffer, bufsize / 512u)))
  {
    s_slow++;
    if (disk_read(0, buffer, lba, bufsize / 512u) != RES_OK) return -1;
  }
  s_rd_us += time_us_32() - t0; s_rd_n++;
  s_read += bufsize;
  if (s_state == USBD_WAITING) s_state = USBD_CONNECTED;
  return (int32_t) bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
  (void) lun;
  if (offset || (bufsize & 511u) || lba + bufsize / 512u > s_blocks) return -1;
  uint32_t t0 = time_us_32();
  if (!(s_fast && sdfast_write(lba, buffer, bufsize / 512u)))
  {
    s_slow++;
    if (disk_write(0, buffer, lba, bufsize / 512u) != RES_OK) return -1;
  }
  s_wr_us += time_us_32() - t0; s_wr_n++;
  s_written += bufsize;
  return (int32_t) bufsize;
}

/* Anything else (SYNCHRONIZE CACHE among them: every block is already on the card when write10 returns). */
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
  (void) buffer; (void) bufsize;
  if (scsi_cmd[0] == 0x35) return 0;                       /* SYNCHRONIZE CACHE (10) */
  if (scsi_cmd[0] == SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL) return 0;
  tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
  return -1;
}

int usbdrive_start(void)
{
  static uint8_t sector[512];
  if (disk_initialize(0) & STA_NOINIT) { s_state = USBD_NO_CARD; return 0; }
  if (disk_read(0, sector, 0, 1) != RES_OK || (s_blocks = sd_blocks(sector)) == 0) { s_state = USBD_NO_CARD; return 0; }
  /* the multi-block path, checked against the SDK's reading of sector 0 */
  static uint8_t check[512];
  s_fast = sdfast_init() && sdfast_read(0, check, 1) && memcmp(check, sector, 512) == 0;
  if (!s_fast) disk_initialize(0);       /* back to the SDK's own speed for the slow path */
  /* The USB clock: the firmware leaves it at 48 MHz from PLL_USB (the SDK measured it; uvm2_monitor.c). Without it
   * the controller never leaves reset and tud_init would wait for ever. */
  if (!(pll_usb_hw->cs & PLL_CS_LOCK_BITS) || !(clocks_hw->clk[clk_usb].ctrl & CLOCKS_CLK_USB_CTRL_ENABLE_BITS))
  { s_state = USBD_NO_USB; return 0; }
  reset_block(RESETS_RESET_USBCTRL_BITS);
  unreset_block(RESETS_RESET_USBCTRL_BITS);
  uint32_t t0 = time_us_32();
  while (!(resets_hw->reset_done & RESETS_RESET_USBCTRL_BITS))
    if (time_us_32() - t0 > 10000u) { s_state = USBD_NO_USB; return 0; }
  s_state = USBD_WAITING;
  tud_init(0);
  return 1;
}

void usbdrive_serve(uint32_t us)
{
  uint32_t t0 = time_us_32();
  do tud_task(); while (time_us_32() - t0 < us);
  if (s_state == USBD_WAITING && tud_mounted() && s_read) s_state = USBD_CONNECTED;
}

UsbDriveStatus usbdrive_status(void)
{
  UsbDriveStatus st = { s_state, s_read / 1024u, s_written / 1024u, s_fast, s_slow,
                        s_rd_us / 1000u, s_wr_us / 1000u, s_rd_n, s_wr_n };
  return st;
}
