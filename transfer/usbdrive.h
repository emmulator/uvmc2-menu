#ifndef USBDRIVE_H
#define USBDRIVE_H
#include <stdint.h>
/* The SD card as a USB drive, as the firmware's System / Transfer Mode does (the same layout: a serial port and a drive,
 * id cafe:4003; named UVMC2 MENU). Nothing else may use the card meanwhile. */
enum { USBD_WAITING, USBD_CONNECTED, USBD_EJECTED, USBD_NO_CARD, USBD_NO_USB };
/* fast: runs of sectors go through sdfast.c; slow: how many of them fell back to the SDK's one-sector path */
/* rd_ms, wr_ms: time spent in the card; rd_n, wr_n: the requests that took it */
typedef struct { int state; uint32_t read_kb, written_kb; int fast; uint32_t slow, rd_ms, wr_ms, rd_n, wr_n; } UsbDriveStatus;
/* Starts USB. 0 if the card cannot be read or USB cannot start (the state says which); nothing is offered then. */
int usbdrive_start(void);
/* Serves USB for about `us` microseconds. */
void usbdrive_serve(uint32_t us);
UsbDriveStatus usbdrive_status(void);
#endif
