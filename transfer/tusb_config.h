/* TinyUSB for transfer.um2 (usbdrive.c): a serial port and a USB drive, the layout of the firmware's Transfer Mode.
 * The serial port is offered, as the firmware does, and nothing is done with it. */
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H
#define CFG_TUSB_RHPORT0_MODE   (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUD_ENABLED         1
#define CFG_TUD_ENDPOINT0_SIZE  64
#define CFG_TUD_CDC             1
#define CFG_TUD_CDC_RX_BUFSIZE  64
#define CFG_TUD_CDC_TX_BUFSIZE  64
#define CFG_TUD_CDC_EP_BUFSIZE  64
#define CFG_TUD_MSC             1
/* 16 sectors per callback: fewer round trips through TinyUSB (the SD driver still moves one sector at a time). */
#define CFG_TUD_MSC_EP_BUFSIZE  8192
#endif
