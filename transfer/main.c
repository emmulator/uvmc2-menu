/* transfer.um2: TRANSFER MODE, started from the menu's SYSTEM folder. The SD card becomes a USB drive (usbdrive.c), as
 * in the firmware's System / Transfer Mode, in a program of its own so that the menu keeps its RAM.
 *
 * Going back, as the firmware does: an eject on the computer, or the console's RESET (the SDK's reset watch, counting
 * only: UVM2_RESET_WATCH_ONLY), reboots the cartridge, and the firmware starts the menu again, which reads the card
 * afresh. Built like the SDK's SD bridge: USB is served all the time, the screen is redrawn now and then and core 1
 * repeats the last picture meanwhile (UVM2_REPLAY_LAST); nothing is drawn while USB is being set up, which wants its
 * answers at once. */
#include <stdint.h>
#include <stdio.h>
#include "pico/time.h"
#include "pico/bootrom.h"
#include "boot/picoboot_constants.h"
#include "tusb.h"
#include "uvm2_draw.h"
#include "fonttext.h"
#include <vpy.h>
#include "uvm2_bus.h"
#include "usbdrive.h"

#define REDRAW_US  1000000u    /* the figures, once a second: each redraw stops USB for about 9 ms */
#define EJECT_US   300000u     /* after an eject: time for the answer to reach the computer */

static void back_to_menu(void)
{
  rom_reboot(BOOT_TYPE_NORMAL | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 10, 0, 0);
  for (;;) { }
}

static uint32_t s_screen_us, s_screens;   /* time spent drawing, for the screen itself */

/* The menu's layout (main.c, FIRMWARE INFO): heading at size 8, lines at size 6, the diagnostics at size 5 (the notes pages' size). */
#define LEFT    (-100)
#define HEAD_Y  108
#define LIST_Y  84
#define ROW_H   16

static void screen(const UsbDriveStatus *st)
{
  char b[40];
  uint32_t t0 = time_us_32();
  int y = LIST_Y;
  uvm2_frame_begin();
  font_text(LEFT, HEAD_Y, "TRANSFER MODE", 8);
  const char *what = st->state == USBD_NO_CARD ? "NO SD CARD" :
                     st->state == USBD_NO_USB ? "USB DID NOT START" :
                     st->state == USBD_EJECTED ? "EJECTED: RESTARTING" :
                     st->state == USBD_CONNECTED ? "CONNECTED" : "WAITING FOR COMPUTER";
  font_text(LEFT, y, what, 6); y -= 2 * ROW_H;
  if (st->state != USBD_NO_CARD && st->state != USBD_NO_USB)
  {
    snprintf(b, sizeof b, "READ %lu KB", (unsigned long) st->read_kb); font_text(LEFT, y, b, 6); y -= ROW_H;
    snprintf(b, sizeof b, "WRITTEN %lu KB", (unsigned long) st->written_kb); font_text(LEFT, y, b, 6); y -= 2 * ROW_H;
    font_text(LEFT, y, "EJECT ON THE COMPUTER", 6); y -= ROW_H;
    font_text(LEFT, y, "OR PRESS RESET", 6); y -= ROW_H;
  }
  else { font_text(LEFT, y, "PRESS RESET", 6); y -= ROW_H; }
  font_text(LEFT, y, "TO GO BACK TO THE MENU", 6);
  /* for reports (README): which card path and its fallbacks, time in the card and requests, this screen's own time */
  y = -76;
  snprintf(b, sizeof b, st->fast ? "FAST, FALLBACKS %lu" : "SLOW PATH", (unsigned long) st->slow);
  font_text_small(LEFT, y, b, 5); y -= 12;
  snprintf(b, sizeof b, "R %lu MS %lu  W %lu MS %lu", (unsigned long) st->rd_ms, (unsigned long) st->rd_n,
           (unsigned long) st->wr_ms, (unsigned long) st->wr_n);
  font_text_small(LEFT, y, b, 5); y -= 12;
  snprintf(b, sizeof b, "SCREEN %lu MS %lu", (unsigned long) (s_screen_us / 1000u), (unsigned long) s_screens);
  font_text_small(LEFT, y, b, 5);
  vpy_flush();                          /* the strokes, into this frame */
  uvm2_frame_end();
  s_screen_us += time_us_32() - t0; s_screens++;
}

int main(void)
{
  vpy_init();
  UsbDriveStatus st = { USBD_WAITING, 0, 0, 0, 0, 0, 0, 0, 0 };
  screen(&st);                          /* first: once USB is up it wants its answers at once */
  int ok = usbdrive_start();
  uint32_t seen0 = uvm2_reset_seen, shown = time_us_32(), ejected_at = 0;
  for (;;)
  {
    if (ok) usbdrive_serve(2000u);
    if (uvm2_reset_seen != seen0) back_to_menu();
    UsbDriveStatus now = usbdrive_status();
    /* redrawn when something changed, and not while USB is still being set up */
    if ((!ok || tud_mounted()) && time_us_32() - shown > REDRAW_US &&
        (now.state != st.state || now.read_kb != st.read_kb || now.written_kb != st.written_kb || now.slow != st.slow || now.rd_n != st.rd_n || now.wr_n != st.wr_n))
    {
      st = now;
      screen(&st);
      shown = time_us_32();
    }
    if (now.state == USBD_EJECTED)
    {
      if (!ejected_at) ejected_at = time_us_32();
      else if (time_us_32() - ejected_at > EJECT_US) back_to_menu();
    }
  }
}
