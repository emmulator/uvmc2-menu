/* uvmc2-menu: a menu from files on the card, started by the firmware as autostart.um2. See README.md. */
#include <vpy.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu_tree.h"
#include "browse.h"
#include "newlist.h"
#include "um2.h"
#include "launch.h"
#include "font.h"
#include "fonttext.h"
#include "fwcat.h"
#include "inflate.h"
#include "hiscore.h"
#include "cartserve.h"
#include "cartload.h"
#include "cartstream.h"
#include "cart.h"
#include "pico/time.h"
#include "uvm2_sd.h"
#include "uvm2_bus.h"
#include "uvm2_draw.h"
#include "uvm2_config.h"
#include <strings.h>
#include "hardware/structs/watchdog.h"
#include "ff.h"
#include "pico/multicore.h"
#include "hardware/flash.h"
#include "bsave.h"

extern int uvm2_psram_ready;

#define SELF "autostart.um2"
#define CHUNK 8192u

/* One block, so that a banked game's image can be built over both: nothing of the menu is needed once a game starts.
 * The union keeps it at least as large as the launch arena, even though the compact records are smaller. */
static union
{
  struct { NmRecs recs; MtTree tree; } menu;
  uint8_t arena[CARTLOAD_ARENA_MIN];
} s_menu;
_Static_assert(sizeof s_menu >= CARTLOAD_ARENA_MIN, "a 256 KB cartridge needs 192 KB and one spare 16 KB here");
_Static_assert(sizeof s_menu >= CARTSTREAM_ARENA_MIN, "a streamed cartridge needs two banks and an inflate window here");
#define s_recs (s_menu.menu.recs)
#define s_tree (s_menu.menu.tree)
static MtNav s_nav;
static MtFavs s_favs;
static char s_text[32768];
static FATFS s_fatfs;             /* the one work area for mounting the card through FatFs (autostart header, NEW's walk) */
static BrList s_br;
static int s_browsing;
static char s_br_file[BR_PATH];
static char s_launched_as[64];
static char s_banner[40];
static int s_saves_ok = 1;          /* fw_saves_ok: the flash places for scores and saves are the firmware's; off = no flash writes */
static char s_error[40];
static FwCat s_fw;
static uint32_t s_scan_ms, s_build_ms;
static int s_fw_shown, s_fw_hidden, s_fw_unsupported;   /* built-ins listed, hidden by a menu file, not startable */
#define FLASH_NOCACHE ((const uint8_t *) 0x14000000u)   /* flash, past the cache */
#define HS_TABLE (FLASH_NOCACHE + 0xFF7000u)             /* the firmware's high-score table, uncached like its game list */

/* Restores the score this key's game had, from the firmware's table, for the next cart_serve to write to the BIOS's slot.
 * The table can hold a record the game's own high-score code would not: hs_valid keeps only what the BIOS accepts. */
/* Writing the table, as the firmware does: flash offset 0xFF7000 on. Called by core 0 while a game runs, with
 * interrupts already off; core 1 serves the game from RAM, so the flash may stop answering meanwhile. */
#define HS_FLASH_OFF 0xFF7000u
static void hs_program(uint32_t offset, const uint8_t *page, void *u) { (void) u; flash_range_program(HS_FLASH_OFF + offset, page, HS_PAGE); }
static void hs_erase(uint32_t offset, void *u) { (void) u; flash_range_erase(HS_FLASH_OFF + offset, HS_SECTOR); }
static uint32_t s_score_key;

static void save_score(const uint8_t score[7])
{
  static const HsFlash flash = { hs_program, hs_erase, 0 };
  hs_save(HS_TABLE, s_score_key, score, &flash);
}

/* The score this key's game had, for the next cart_serve to write to the BIOS's slot, and the key a new one is kept
 * under while the game runs (cart_watch_score). */
static void restore_score(uint32_t key)
{
  uint8_t score[7];
  s_score_key = key;
  cart_set_score(0);
  if (!s_saves_ok) return;                 /* the table may not be where this menu reads it: neither restore nor keep scores */
  int found = hs_find(HS_TABLE, key, score);
  /* The firmware's record for a game that writes only the six digits (E-SRG) has blank flash, 0xFF, where the BIOS keeps
   * its $80 terminator: restore it as $80. */
  if (found && score[6] == 0xFF) score[6] = 0x80;
  if (found && hs_valid(score)) cart_set_score(score);
  cart_watch_score(save_score);
}

/* ---- the card ---- */
static int card_exists(const char *path, void *u)
{
  (void) u;
  uvm2_sd_file f;
  int n = (int) strlen(path);
  if (n > 0 && path[n - 1] == '/')
  {
    char d[96]; uvm2_sd_dir dd;
    snprintf(d, sizeof d, "%.*s", n - 1, path);
    if (!uvm2_sd_dir_open(d, &dd)) return 0;
    uvm2_sd_dir_close(&dd);
    return 1;
  }
  if (!uvm2_sd_open(path, &f)) return 0;
  uvm2_sd_close(&f);
  return 1;
}

static void card_list(const char *dir, void (*add)(const char *, void *), void *ctx, void *u)
{
  (void) u;
  uvm2_sd_dir d; char name[96]; uint32_t size;
  if (!uvm2_sd_dir_open(strcmp(dir, ".") == 0 ? "" : dir, &d)) return;
  int k;
  while ((k = uvm2_sd_dir_next(&d, name, sizeof name, &size)) != UVM2_SD_DIR_END)
    if (k == UVM2_SD_DIR_FILE) add(name, ctx);
  uvm2_sd_dir_close(&d);
}

static void read_layer(const char *path, const char *stem)
{
  uint32_t n = uvm2_sd_read(path, (unsigned char *) s_text, sizeof s_text - 1);
  if (n == 0) return;
  s_text[n] = 0;
  nmParseMenuText(&s_recs, s_text, stem);
}

/* menu.default.ini (replaced by an update), the others alphabetically, menu.ini last: later wins */
static void read_menu(void)
{
  static char names[32][MT_LAYER_NAME];
  int k = 0, t; uvm2_sd_dir d; uint32_t size;
  read_layer("menu/menu.default.ini", "menu.default");
  if (uvm2_sd_dir_open("menu", &d))
  {
    char name[96];
    while ((t = uvm2_sd_dir_next(&d, name, sizeof name, &size)) != UVM2_SD_DIR_END)
      if (t == UVM2_SD_DIR_FILE && k < 32 && strlen(name) < MT_LAYER_NAME) strcpy(names[k++], name);
    uvm2_sd_dir_close(&d);
  }
  k = mt_layer_sort(names, k);
  for (int i = 0; i < k; i++)
  {
    char p[64];
    snprintf(p, sizeof p, "menu/%s", names[i]);
    names[i][strlen(names[i]) - 4] = 0;
    read_layer(p, names[i]);
  }
  read_layer("menu/menu.ini", "menu");
  /* favorites.ini is not a menu layer: it has its own reader and only its file = lines matter. No file: no favorites. */
  uint32_t n = uvm2_sd_read("menu/" MT_FAVORITES, (unsigned char *) s_text, sizeof s_text - 1);
  s_text[n] = 0;
  mt_favs_read(&s_favs, s_text);
}

/* Optional: menu = off in menu/settings.ini switches the menu off. No file or no line means on. */
static int new_menu_setting(void)
{
  static char b[1024];
  uint32_t n = uvm2_sd_read("menu/" MT_SETTINGS, (unsigned char *) b, sizeof b - 1);
  if (n == 0) return 1;
  b[n] = 0;
  return mt_setting_on(b, "menu", 1);
}

/* Started from Browse: whoever does that wants this menu, so menu = off in settings.ini becomes on. */
static int settings_turn_on(void)
{
  static char b[1024], o[1100];
  uint32_t n = uvm2_sd_read("menu/" MT_SETTINGS, (unsigned char *) b, sizeof b - 1);
  b[n] = 0;
  if (n == 0 || mt_setting_on(b, "menu", 1)) return 1;
  int k = mt_setting_set(b, "menu", "on", o, sizeof o);
  return k > 0 && uvm2_sd_write("menu/" MT_SETTINGS, (const unsigned char *) o, (uint32_t) k) == 1;
}

/* ---- the switch: the count in autostart.um2's header ---- */
/* The first n bytes of autostart.um2, rewritten in place through FatFs. Not uvm2_sd_overwrite: that writes a whole
 * 512-byte sector and makes its last byte a newline, which here is a byte of the program. FatFs writes only these n
 * bytes and keeps the rest of the sector. The SDK mounts the card afresh on each call, so mounting here is safe. */
static int autostart_write_head(const unsigned char *h, unsigned n)
{
  FIL f;
  UINT put = 0;
  if (f_mount(&s_fatfs, "", 1) != FR_OK) return 0;
  int ok = f_open(&f, SELF, FA_WRITE | FA_OPEN_EXISTING) == FR_OK;
  if (ok)
  {
    ok = f_write(&f, h, n, &put) == FR_OK && put == n;
    if (f_close(&f) != FR_OK) ok = 0;
  }
  f_mount(0, "", 0);
  return ok;
}

static int autostart_set_off(int off)
{
  static unsigned char s[512], back[512];
  if (uvm2_sd_read_from(SELF, s, 512, 0) != 512) return 0;
  if (memcmp(s, "2CMU", 4) != 0) return 0;
  if (um2_is_off(s) == !!off) return 1;
  um2_set_off(s, off);
  if (!autostart_write_head(s, 12)) return 0;           /* magic, version, block count */
  if (uvm2_sd_read_from(SELF, back, 512, 0) != 512) return 0;
  return memcmp(s, back, 512) == 0;
}

static int autostart_is_off(void)
{
  static unsigned char s[12];
  if (uvm2_sd_read_from(SELF, s, 12, 0) != 12) return 0;
  return memcmp(s, "2CMU", 4) == 0 && um2_is_off(s);
}

/* The console's cold-start flag, Vec_Cold_Flag at $CBFE, cleared so that its BIOS starts cold: the VECTREX intro, then
 * the cartridge's title. After a game it holds $7321 (warm), and a warm start reads the cartridge header at once,
 * before the firmware rebooting under it answers, finds none and runs the built-in Mine Storm. The 6809 is halted
 * while this menu draws, so the bus is ours once core 1 stops, as cart_serve does. */
static void console_cold_start(void)
{
  multicore_reset_core1();
  uvm2_mem_write(0xCBFEu, 0x00u);
  uvm2_mem_write(0xCBFFu, 0x00u);
}

static void go_to_cartridge_menu(void)
{
  /* Started from Browse with no autostart.um2: nothing to switch off, the firmware's menu comes up by itself. */
  if (!card_exists(SELF, 0) || autostart_set_off(1)) { console_cold_start(); launch_cartridge_reboot(); }
  strcpy(s_error, "COULD NOT SWITCH OFF");     /* never reboot unchanged: that would loop */
}

/* ---- loading a game, one chunk per frame ---- */
static struct { int on; uvm2_sd_file f; um2_info u; uint32_t done; char path[160]; } s_load;
static unsigned char s_chunk[CHUNK];

static void load_fail(const char *why)
{
  if (s_load.on) uvm2_sd_close(&s_load.f);
  s_load.on = 0;
  snprintf(s_error, sizeof s_error, "%s", why);
}

static void load_start(const char *image)
{
  uint32_t size = 0, n;
  static unsigned char head[12 + UM2_MAX_BLOCKS * 8];
  int len = (int) strlen(image);
  s_error[0] = 0;
  if (len < 4) { load_fail("BAD FILE NAME"); return; }
  if (strcasecmp(image + len - 4, ".bin") == 0 || strcasecmp(image + len - 4, ".vec") == 0)
  {
    /* s_text held the menu files while reading them; nothing needs it now, and it is the 32 KB we want */
    uint32_t got = uvm2_sd_read(image, (unsigned char *) s_text, 0x8000u);
    if (got == 0)
    {
      /* over 32 KB: a 64 KB or 256 KB cartridge is built over the menu's own memory, which holds the name we were given */
      char path[160];
      snprintf(path, sizeof path, "%s", image);
      load_fail(cartload_file(path, fw_stream_player(s_fw.flash), (uint8_t *) &s_menu, sizeof s_menu));
      return;
    }
    restore_score(zcrc32((const uint8_t *) s_text, got));
    cart_serve((uint8_t *) s_text, got, 0);
  }
  if (strcasecmp(image + len - 4, ".um2") != 0) { load_fail("NOT A GAME FILE"); return; }
  if (uvm2_psram_ready != 1) { load_fail("NO PSRAM"); return; }
  /* the size, from the directory */
  { char dir[96], base[96], nm[96]; const char *sl = strrchr(image, '/'); uvm2_sd_dir d; int t, found = 0;
    if (sl) { snprintf(dir, sizeof dir, "%.*s", (int) (sl - image), image); snprintf(base, sizeof base, "%s", sl + 1); }
    else { dir[0] = 0; snprintf(base, sizeof base, "%s", image); }
    if (uvm2_sd_dir_open(dir, &d))
    {
      while ((t = uvm2_sd_dir_next(&d, nm, sizeof nm, &size)) != UVM2_SD_DIR_END)
        if (t == UVM2_SD_DIR_FILE && strcasecmp(nm, base) == 0) { found = 1; break; }
      uvm2_sd_dir_close(&d);
    }
    if (!found) { load_fail("FILE NOT FOUND"); return; }
  }
  if (!uvm2_sd_open(image, &s_load.f)) { load_fail("CANNOT OPEN FILE"); return; }
  s_load.on = 1;
  n = uvm2_sd_next(&s_load.f, head, sizeof head);
  int e = um2_parse(head, n, size, &s_load.u);
  if (e != UM2_OK) { load_fail(um2_error(e)); return; }
  if (s_load.u.bytes > 0x400000u) { load_fail("TOO BIG"); return; }
  if (!uvm2_sd_seek(&s_load.f, s_load.u.payload_off)) { load_fail("SEEK FAILED"); return; }
  s_load.done = 0;
  snprintf(s_load.path, sizeof s_load.path, "%s", image);
}

static void load_step(void)
{
  uint32_t want = s_load.u.bytes - s_load.done;
  if (want > CHUNK) want = CHUNK;
  uint32_t n = uvm2_sd_next(&s_load.f, s_chunk, want);
  if (n != want) { load_fail("READ ERROR"); return; }
  volatile unsigned char *dst = (volatile unsigned char *) (LAUNCH_PSRAM + s_load.done);
  for (uint32_t i = 0; i < n; i++) dst[i] = s_chunk[i];
  for (uint32_t i = 0; i < n; i++) if (dst[i] != s_chunk[i]) { load_fail("PSRAM ERROR"); return; }
  s_load.done += n;
  if (s_load.done == s_load.u.bytes)
  {
    uvm2_sd_close(&s_load.f);
    launch_go(s_load.path, &s_load.u);
  }
}

/* Fill the browser from a folder. The names go in s_text, which is free while browsing, but a load overwrites it, so
 * this is called again after one. On failure the list stays as it was. */
static NwList s_nw;               /* NEW: the newest files on the card; s_br shows it while s_new */
static int s_new, s_new_partial;   /* in NEW (inside s_browsing); the walk stopped before the whole card was read */
static int s_new_unmapped;         /* NEW shows only the files no menu entry, hiding section or folder rule covers (button 2) */
static int s_br_unmapped;          /* BROWSE SD CARD shows only the files no menu folder lists (button 2) */
static int br_unmapped(const BrList *b, int i, void *u)
{
  char path[BR_PATH];
  (void) u;
  return !br_child_path(b, i, path, sizeof path) || !mt_file_mapped(&s_recs, path);
}

/* NEW: the newest game files on the card by FAT date (newlist.c does the walking; this is the card side of it: FatFs, one open
 * directory per depth). NEW_VISIT stops the walk on a card with a huge number of files. */
#define NEW_VISIT 6000
static DIR s_wdir[NW_DEPTH];
static FILINFO s_wfi;
static int new_open_dir(int depth, const char *path, void *u) { (void) u; return f_opendir(&s_wdir[depth], path) == FR_OK; }
static int new_read_dir(int depth, NwEntry *e, void *u)
{
  (void) u;
  if (f_readdir(&s_wdir[depth], &s_wfi) != FR_OK || s_wfi.fname[0] == 0) return 0;
  e->name = s_wfi.fname;
  e->is_dir = (s_wfi.fattrib & AM_DIR) != 0;
  e->hidden = (s_wfi.fattrib & (AM_HID | AM_SYS)) != 0;
  e->fdate = s_wfi.fdate;
  e->ftime = s_wfi.ftime;
  return 1;
}
static void new_close_dir(int depth, void *u) { (void) u; f_closedir(&s_wdir[depth]); }
static int new_skip(const char *path, void *u)
{
  (void) u;
  return s_new_unmapped ? mt_file_mapped(&s_recs, path) : mt_file_hidden(&s_recs, path);     /* mapped includes hidden */
}

static void new_scan(void)
{
  static const NwFs fs = { new_open_dir, new_read_dir, new_close_dir, new_skip };
  /* s_text is free here: BROWSE and NEW are not showing, and the list NEW keeps is s_nw's own */
  uint32_t got = uvm2_sd_read("menu/" MT_SETTINGS, (unsigned char *) s_text, 1023);
  s_text[got] = 0;
  nw_start(&s_nw, mt_setting_int(s_text, "new", NW_DEFAULT));
  s_new_partial = 0;
  if (f_mount(&s_fatfs, "", 1) != FR_OK) { strcpy(s_error, "CANNOT READ CARD"); return; }
  int r = nw_walk(&s_nw, &fs, 0, NEW_VISIT);
  f_mount(0, "", 0);
  if (r < 0) strcpy(s_error, "CANNOT READ CARD");
  s_new_partial = r > 0;
}

static void new_open(void)
{
  s_error[0] = 0;
  s_new_unmapped = 0;
  new_scan();
  nw_to_browse(&s_nw, &s_br);
  s_new = 1;
  s_browsing = 1;
}

static void browse_open(const char *path, const char *select)
{
  uvm2_sd_dir d;
  if (!uvm2_sd_dir_open(path ? path : "", &d))
  {
    strcpy(s_error, "CANNOT OPEN FOLDER");
    return;
  }
  br_start(&s_br, s_text, (int) sizeof s_text, path ? path : "");
  char name[96]; uint32_t size; int k;
  while ((k = uvm2_sd_dir_next(&d, name, sizeof name, &size)) != UVM2_SD_DIR_END)
    if (k == UVM2_SD_DIR_FILE || k == UVM2_SD_DIR_FOLDER)
      br_add(&s_br, name, k == UVM2_SD_DIR_FOLDER);
  uvm2_sd_dir_close(&d);
  br_sort(&s_br);
  if (s_br_unmapped) br_filter(&s_br, br_unmapped, 0);
  if (select)
  {
    int i = br_find(&s_br, select);
    if (i >= 0)
    {
      s_br.cursor = i;
      if (s_br.cursor >= s_br.top + MT_ROWS) s_br.top = s_br.cursor - MT_ROWS + 1;
    }
  }
}

/* A built-in game: unpack it from flash, check it against the firmware's own CRC, serve it. */
static void start_builtin(int i)
{
  const FwGame *g = &s_fw.game[i];
  if (!fw_can_serve(&s_fw, i)) { strcpy(s_error, "NOT SUPPORTED YET"); return; }
  uint32_t size = g->size;
  if (cart_builtin_scheme(size) == CART_STREAM)
  {
    const char *why = cartstream_builtin(fw_game_data(&s_fw, i), fw_game_packed(&s_fw, i), size,
                                         fw_stream_player(s_fw.flash), (uint8_t *) &s_menu, sizeof s_menu);
    snprintf(s_error, sizeof s_error, "%s", why);
    return;
  }
  if (size > 0x8000u)
  {
    const char *why = cartload_builtin(fw_game_data(&s_fw, i), fw_game_packed(&s_fw, i), size, g->crc,
                                       (uint8_t *) &s_menu, sizeof s_menu);
    snprintf(s_error, sizeof s_error, "%s", why);
    return;
  }
  memset(s_text, 0, 0x8000u);
  int n = zinflate((uint8_t *) s_text, 0x8000u, fw_game_data(&s_fw, i), fw_game_packed(&s_fw, i));
  if (n != (int) size) { strcpy(s_error, "COULD NOT UNPACK"); return; }
  if (zcrc32((const uint8_t *) s_text, size) != g->crc) { strcpy(s_error, "BAD CHECKSUM"); return; }
  if (g->writable) size = 0x8000u;
  restore_score(g->crc);
  cart_serve((uint8_t *) s_text, size, g->writable);
}

/* ---- the title screen ----
 * Started by the firmware at power-on, this menu takes the place of the firmware's own, and with it of the console's
 * title screen, which shows the firmware's cartridge header and credits its maker. So it shows the same lines first.
 * Only once per power-on: watchdog scratch 1 survives the reboots of a game starting and ending, but not power-off.
 * (The firmware uses scratch 0 for the image path and the boot ROM 4 to 7.) */
/* This menu's version, shown on the title screen under "Custom Menu". It comes from the VERSION file: the Makefile passes it
 * as -DMENU_VERSION=x.y.z (a bare number, so # can make it a string). Nothing in the C source needs changing for a release. */
#ifndef MENU_VERSION
#error "MENU_VERSION is not set: build with make (it reads the VERSION file)"
#endif
#define MENU_STR_(x) #x
#define MENU_STR(x) MENU_STR_(x)
#define TITLE_MS    4000u
#define TITLE_SEEN  0x31544954u          /* "TIT1" */
static FwTitle s_title;
static int s_title_on;
static uint32_t s_title_t0;

static void title_start(void)
{
  if (watchdog_hw->scratch[1] == TITLE_SEEN) return;
  watchdog_hw->scratch[1] = TITLE_SEEN;
  fw_title(FLASH_NOCACHE, &s_title);
  s_title_on = 1;
  s_title_t0 = time_us_32();
}

/* ---- input ---- */
#define ARM_FRAMES 25
static int s_armed;                /* everything released for ARM_FRAMES frames since start */
static int s_still;
static int s_calibrate;            /* CALIBRATE SCREEN chosen: the wizard opens once everything is released */
static int s_b_prev[5], s_stick_prev, s_repeat;
static int s_notes, s_info, s_has_more;
static const char *s_hs_msg;        /* the result of saving the score table from DIAGNOSTICS, once tried */
static int s_hs_count;              /* live records in the firmware's table, read once when DIAGNOSTICS opens */

static int pressed(int b)
{
  int now = vpy_j1_button(b), r = now && !s_b_prev[b];
  s_b_prev[b] = now;
  return r;
}

/* -1 up, 1 down, with repeat; left/right as single steps through *h */
static int stick(int *h)
{
  int y = vpy_j1_y(), x = vpy_j1_x();
  int v = y > 60 ? -1 : y < -60 ? 1 : 0;
  int hz = x > 60 ? 1 : x < -60 ? -1 : 0;
  static int hprev;
  *h = hz != hprev ? hz : 0;
  hprev = hz;
  if (v == 0) { s_stick_prev = 0; s_repeat = 0; return 0; }
  if (v != s_stick_prev) { s_stick_prev = v; s_repeat = 18; return v; }
  if (--s_repeat <= 0) { s_repeat = 5; return v; }
  return 0;
}

static void favorite_toggle(const char *path, const char *title);
static void toggle_favorite(void);

static void update(void)
{
  if (s_load.on) { load_step(); return; }
  if (s_title_on)
  {
    int any = 0;
    for (int b = 1; b <= 4; b++) any |= pressed(b);
    if (any || time_us_32() - s_title_t0 > TITLE_MS * 1000u) s_title_on = 0;
    return;
  }
  if (!s_armed)
  {
    int any = 0;
    for (int b = 1; b <= 4; b++) { s_b_prev[b] = vpy_j1_button(b); any |= s_b_prev[b]; }
    /* Released for ARM_FRAMES frames running (about half a second): the stick's readings can be off for a moment after
     * power-on, and one stray frame must not open a folder. */
    if (!any && vpy_j1_x() > -60 && vpy_j1_x() < 60 && vpy_j1_y() > -60 && vpy_j1_y() < 60) { if (++s_still >= ARM_FRAMES) s_armed = 1; }
    else s_still = 0;
    return;
  }
  int h, v = stick(&h);
  int b1 = pressed(1), b2 = pressed(2), b3 = pressed(3);
  int b4 = pressed(4);
  if (s_browsing)
  {
    if (v) { br_move(&s_br, v); s_error[0] = 0; }
    if (b1 || h < 0)
    {
      s_error[0] = 0;
      if (s_br.path[0] == 0) s_browsing = s_new = 0;
      else
      {
        char parent[BR_PATH], last[BR_PATH];
        if (br_parent(s_br.path, parent, sizeof parent, last, sizeof last)) browse_open(parent, last);
        else strcpy(s_error, "PATH TOO LONG");
      }
    }
    else if (b4 || h > 0)
    {
      int c = s_br.cursor;
      if (c >= 0 && c < s_br.n)
      {
        if (s_br.dir[c])
        {
          char path[BR_PATH];
          s_error[0] = 0;
          if (br_child_path(&s_br, c, path, sizeof path)) browse_open(path, 0);
          else strcpy(s_error, "PATH TOO LONG");
        }
        else
        {
          char path[BR_PATH], sel[96], err[sizeof s_error];
          snprintf(path, sizeof path, "%s", s_br.path);
          snprintf(sel, sizeof sel, "%s", br_name(&s_br, c));
          if (!br_child_path(&s_br, c, s_br_file, sizeof s_br_file)) strcpy(s_error, "PATH TOO LONG");
          else
          {
            load_start(s_br_file);
            if (s_error[0] && !s_new)
            {
              /* The load used s_text for the file: list the folder again, keeping its reason on screen. */
              snprintf(err, sizeof err, "%s", s_error);
              browse_open(path, sel);
              snprintf(s_error, sizeof s_error, "%s", err);
            }
          }
        }
      }
    }
    else if (b2 && s_new)
    {
      s_new_unmapped = !s_new_unmapped;       /* the list is the newest of what is shown, so it is made again */
      s_error[0] = 0;
      new_scan();
      nw_to_browse(&s_nw, &s_br);
    }
    else if (b2)
    {
      char folder[BR_PATH], sel[96];
      int c = s_br.cursor;
      snprintf(folder, sizeof folder, "%s", s_br.path);
      snprintf(sel, sizeof sel, "%s", c >= 0 && c < s_br.n ? br_name(&s_br, c) : "");
      s_br_unmapped = !s_br_unmapped;
      s_error[0] = 0;
      browse_open(folder, sel[0] ? sel : 0);
    }
    else if (b3)
    {
      int c = s_br.cursor;
      if (c >= 0 && c < s_br.n && !s_br.dir[c])
      {
        char folder[BR_PATH], sel[96], path[BR_PATH], title[MT_TITLE];
        /* s_text is about to be the favorites file, then the tree's names: keep the browser's place first. */
        snprintf(folder, sizeof folder, "%s", s_br.path);
        snprintf(sel, sizeof sel, "%s", br_name(&s_br, c));
        if (!br_child_path(&s_br, c, path, sizeof path)) strcpy(s_error, "PATH TOO LONG");
        else
        {
          if (s_new) br_title_base(&s_br, c, title, sizeof title);
          else br_title(&s_br, c, title, sizeof title);
          favorite_toggle(path, title);
          if (!s_new) browse_open(folder, sel);          /* s_error is the toggle's message: opening leaves it alone */
        }
      }
    }
    return;
  }
  if (s_info && b3)
  {
    /* The firmware's whole score table, for tools/hs_dump.py to match its keys to games on a PC. */
    int ok = uvm2_sd_write("menu/hiscores.bin", (const unsigned char *) HS_TABLE, HS_BYTES) == 1;
    /* and the built-in games' saves (bsave.h): the 256-byte directory, then the 32 save pages, the last one first */
    ok = ok && uvm2_sd_write2("menu/bsaves.bin", FLASH_NOCACHE + BS_DIR_OFF, BS_SUBPAGE,
                              FLASH_NOCACHE + BS_PAGE0 - (BS_DIR_N - 1u) * BS_PAGE, BS_DIR_N * BS_PAGE) == 1;
    s_hs_msg = ok ? "SAVED HISCORES.BIN, BSAVES.BIN" : "COULD NOT SAVE";
    return;
  }
  if (s_notes || s_info) { if (b1 || b2 || b4 || h) s_notes = s_info = 0; return; }
  if (v) { mt_move(&s_nav, &s_tree, v); s_error[0] = 0; }
  if (b1 || h < 0) { mt_back(&s_nav, &s_tree); s_error[0] = 0; }
  if (b2 && s_has_more) s_notes = 1;
  if (b3) toggle_favorite();
  if (b4 || h > 0)
  {
    int c = mt_nth(&s_tree, s_nav.dir, s_nav.cursor);
    if (c < 0) return;
    const char *image = mt_image(&s_tree, c);
    if (image[0] == 0) mt_enter(&s_nav, &s_tree);
    else
    {
      if (strcmp(image, "@stock") == 0) go_to_cartridge_menu();
      else if (strcmp(image, "@fwinfo") == 0) { s_info = 1; s_hs_msg = 0; s_hs_count = hs_count(HS_TABLE); }
      else if (strcmp(image, "@calibrate") == 0) s_calibrate = 1;
      else if (strcmp(image, "@browse") == 0) { s_error[0] = 0; s_br_unmapped = 0; s_new = 0; browse_open("", 0); s_browsing = 1; }
      else if (strcmp(image, "@new") == 0) new_open();
      else if (strncmp(image, "@fw:", 4) == 0) start_builtin(atoi(image + 4));
      else if (image[0] == '@') strcpy(s_error, "UNKNOWN ACTION");
      else load_start(image);
    }
  }
}

/* ---- drawing ----
 * A character is about 1.04 units wide per point of text size (hello_uvmc2: 11 at size 12 = 136). The list uses
 * size 8 (8.3 units) and notes size 5 (5.2). Everything stays inside +-100: the first build
 * drew at -120 and +112 and both sides were cut off on a real console. */
#define LEFT    (-100)
#define LIST_X  (LEFT + 16)
#define LIST_CH 21
#define HEAD_CH 15
#define PATH_CH 18                  /* the browser's heading: the position count at the right starts about 53 units in */
#define NOTE_CH MT_NOTE_WIDTH
#define MARK_X  92
#define ROW_H   16
#define INFO_ROW 14                 /* DIAGNOSTICS has fourteen lines: 14 puts the last at -98, above -100 */
#define HEAD_Y  108
#define LIST_Y  84
#define LIST_BOTTOM (LIST_Y - (MT_ROWS - 1) * ROW_H - 8)
#define NOTE_Y  (LIST_BOTTOM - 18)         /* below the down arrow, which is drawn at LIST_BOTTOM - 9 */
#define NOTE_H  12
#define MSG_Y   (-92)
#define HINT_Y  (-100)

static int s_size = 8;
static void text(int x, int y, const char *s) { font_text(x, y, s, s_size); }
static void text_small(int x, int y, const char *s) { font_text_small(x, y, s, s_size); }

static void text_cut(int x, int y, const char *s, int max)
{
  char b[48];
  snprintf(b, sizeof b, "%.*s", max, s);
  text(x, y, b);
}

/* The highlighted entry's text, with a built-in game's developer. */
static void add_notes(MtNotes *m, int c, const NmRec *r)
{
  if (r)
  {
    const char *p = nmStr(&s_recs, r->rows);
    for (int i = 0; i < r->nrows; i++) { mt_notes_add(m, p); p += strlen(p) + 1; }
  }
  if (c >= 0 && strncmp(mt_image(&s_tree, c), "@fw:", 4) == 0)
  {
    const FwGame *g = &s_fw.game[atoi(mt_image(&s_tree, c) + 4)];
    char b[80];
    if (g->dev[0] != FW_NONE)
    {
      if (g->dev[1] != FW_NONE) snprintf(b, sizeof b, "BY %s, %s", s_fw.dev[g->dev[0]], s_fw.dev[g->dev[1]]);
      else snprintf(b, sizeof b, "BY %s", s_fw.dev[g->dev[0]]);
      mt_notes_add(m, b);
    }
  }
}

/* A chevron in VPy units: tip at (x, y), arms back by dy, so dy > 0 points up and dy < 0 down. */
static void chevron(int x, int y, int dy)
{
  const int32_t k = 127;
  vpy_draw_line_dev((x - 8) * k, (y - dy) * k, x * k, y * k, 90);
  vpy_draw_line_dev(x * k, y * k, (x + 8) * k, (y - dy) * k, 90);
}

/* The browser: the tree's list layout over the folder's own entries, with folders marked like the tree's. */
static void browse_draw(void)
{
  char title[LIST_CH + 1], head[PATH_CH + 1];
  s_size = 8;
  if (s_new) strcpy(head, s_new_unmapped ? "NEW: UNMAPPED" : "NEW");
  else br_head(s_br.path, head, PATH_CH);
  text(LEFT, HEAD_Y, head);
  if (s_br.n == 0) text(LEFT, LIST_Y, s_new ? (s_new_unmapped ? "NOTHING UNMAPPED" : "NO GAME FILES FOUND") : br_empty(&s_br, s_br_unmapped));
  for (int i = 0; i < MT_ROWS && s_br.top + i < s_br.n; i++)
  {
    int id = s_br.top + i;
    int y = LIST_Y - i * ROW_H;
    if (id == s_br.cursor) text(LEFT, y, ">");
    int folder = s_br.dir[id], fav = 0;
    if (!folder)
    {
      char path[BR_PATH];
      fav = br_child_path(&s_br, id, path, sizeof path) && mt_favs_has(&s_favs, path);
    }
    if (s_new) br_title_base(&s_br, id, title, LIST_CH + 1);        /* the file's name; the whole path shows under the list */
    else br_title(&s_br, id, title, folder ? LIST_CH : LIST_CH + 1);
    text_cut(LIST_X, y, title, folder || fav ? LIST_CH - 1 : LIST_CH);
    if (folder) text(MARK_X, y, ">");
    else if (fav) text(MARK_X, y, "'");            /* a favorite file: the mark the tree uses */
  }
  if (s_br.top > 0) chevron(0, LIST_Y + 11, 5);
  if (s_br.top + MT_ROWS < s_br.n) chevron(0, LIST_BOTTOM - 9, -5);
  s_size = 4;
  int c = s_br.cursor;
  const int on_file = c >= 0 && c < s_br.n && !s_br.dir[c];
  if (s_new) text_small(LEFT, HINT_Y, s_new_unmapped ? "4 OPEN  1 BACK  3 FAVORITE  2 SHOW ALL" : "4 OPEN  1 BACK  3 FAVORITE  2 UNMAPPED");
  else if (s_br_unmapped) text_small(LEFT, HINT_Y, on_file ? "4 OPEN  1 BACK  3 FAVORITE  2 SHOW ALL" : "4 OPEN  1 BACK  2 SHOW ALL");
  else text_small(LEFT, HINT_Y, on_file ? "4 OPEN  1 BACK  3 FAVORITE  2 UNMAPPED" : "4 OPEN  1 BACK  2 UNMAPPED");
  s_size = 5;
  /* the highlighted entry's whole name, where the tree shows an entry's notes: the list cuts long names */
  if (s_br.cursor < s_br.n)
  {
    char full[100], line[NOTE_CH + 1];
    br_title(&s_br, s_br.cursor, full, sizeof full);
    const char *p = full;
    for (int i = 0; i < 2 && mt_wrap_next(&p, line, NOTE_CH); i++) text_small(LEFT, NOTE_Y - i * NOTE_H, line);
  }
  if (s_br.dropped > 0)
  {
    char b[48];                          /* says what to do about it: the list stops at BR_MAX */
    snprintf(b, sizeof b, "SHOWING %d OF %d: USE SUBFOLDERS", s_br.n, s_br.n + s_br.dropped);
    text_cut(LEFT, MSG_Y, b, NOTE_CH);
  }
  else if (s_error[0]) text_cut(LEFT, MSG_Y, s_error, NOTE_CH);
  else if (s_new) text_cut(LEFT, MSG_Y, s_new_partial ? "ONLY PART OF THE CARD WAS READ" : s_new_unmapped ? "THE NEWEST UNMAPPED FILES ON THE CARD" : "THE NEWEST FILES BY DATE ON THE CARD", NOTE_CH);
  else
  {
    char b[48];                          /* what the list leaves out, so a folder that looks short or empty explains itself */
    if (br_note(&s_br, s_br_unmapped, b, sizeof b)) text_cut(LEFT, MSG_Y, b, NOTE_CH);
  }
  if (s_br.n > MT_ROWS)
  {
    char pos[16];
    int n = snprintf(pos, sizeof pos, "%d/%d", s_br.cursor + 1, s_br.n);
    text_small(100 - n * FONT_ADV * s_size * FONT_STEP / 127, HEAD_Y, pos);
  }
}

/* What the frames really cost, for the info screen. 0 dropped means nothing was lost; over 20 ms is under 50 Hz. */
static uint32_t s_peak_dropped, s_peak_cycles;
static void note_frame_stats(void)
{
  if (uvm2_stats.dropped > s_peak_dropped) s_peak_dropped = uvm2_stats.dropped;
  if (uvm2_stats.exec_cycles > s_peak_cycles) s_peak_cycles = uvm2_stats.exec_cycles;
}

/* A centred line at size sz: x from the character count, as the font advances. */
static int title_width(const char *l, int sz) { int n = (int) strlen(l); return n > 0 ? (n * FONT_ADV - 2) * sz * FONT_STEP / 127 : 0; }

/* Centred, at the header's heights spread over the screen: the lines sit between y = 64 and -32 there. They are moved up by
 * TITLE_LIFT to leave room under them for the credits: the firmware's maker, then this menu's. */
#define TITLE_LIFT  12
#define CREDIT_SIZE 4
#define CREDIT_ROW  7                       /* a credit line's pitch; a glyph is 6 grid steps = about 4.2 units at size 4 */
#define CREDIT_GAP  4                       /* extra between the two credits */
static void draw_title(void)
{
  int low = 0;                              /* the lowest edge drawn so far */
  for (int i = 0; i < s_title.n; i++)
  {
    const char *l = s_title.line[i];
    s_size = strncmp(l, "V ", 2) == 0 ? 6 : 9;            /* the version, smaller */
    int y = (s_title.y[i] - 16) * 3 / 2 + 6 + TITLE_LIFT;
    int bottom = y - 6 * s_size * FONT_STEP / 127;
    if (i == 0 || bottom < low) low = bottom;
    text(-title_width(l, s_size) / 2, y, l);
  }
  char menu[32];
  snprintf(menu, sizeof menu, "CUSTOM MENU V%s", MENU_STR(MENU_VERSION));
  const char *const credit[] = { "FIRMWARE", "RALF KORNMANN", 0, menu, "JOHN EMMER" };
  int y = low - 6;
  s_size = CREDIT_SIZE;
  for (int i = 0; i < 5; i++)
  {
    if (!credit[i]) { y -= CREDIT_GAP; continue; }
    text_small(-title_width(credit[i], CREDIT_SIZE) / 2, y, credit[i]);
    y -= CREDIT_ROW;
  }
}

static void draw(void)
{
  s_size = 8;
  if (s_title_on) { draw_title(); return; }
  if (s_load.on)
  {
    char b[24];
    text(-50, 20, "LOADING");
    snprintf(b, sizeof b, "%lu KB", (unsigned long) (s_load.done / 1024));
    text(-50, -5, b);
    return;
  }
  if (s_info)
  {
    char b[48];
    int y = LIST_Y;
    text(LEFT, HEAD_Y, "DIAGNOSTICS");
    s_size = 6;
    if (s_fw.ngames == 0) { text(LEFT, y, "GAME LIST NOT FOUND"); y -= INFO_ROW; }
    else
    {
      snprintf(b, sizeof b, "GAME LIST %08lX", (unsigned long) s_fw.table); text(LEFT, y, b); y -= INFO_ROW;
      snprintf(b, sizeof b, "GAMES %d%s", s_fw.ngames, s_fw.cut_off ? " + MORE NOT READ" : ""); text(LEFT, y, b); y -= INFO_ROW;
      snprintf(b, sizeof b, "LISTED %d   HIDDEN %d", s_fw_shown, s_fw_hidden); text(LEFT, y, b); y -= INFO_ROW;
      snprintf(b, sizeof b, "NOT SUPPORTED %d", s_fw_unsupported); text(LEFT, y, b); y -= INFO_ROW;
      snprintf(b, sizeof b, "DEVELOPERS %d", s_fw.ndevs); text(LEFT, y, b); y -= INFO_ROW;
      snprintf(b, sizeof b, "CATEGORIES %d", s_fw.ncats); text(LEFT, y, b); y -= INFO_ROW;
    }
    snprintf(b, sizeof b, "SCAN %lu MS", (unsigned long) s_scan_ms); text(LEFT, y, b);
    y -= INFO_ROW;
    snprintf(b, sizeof b, "MENU BUILD %lu MS", (unsigned long) s_build_ms); text(LEFT, y, b);
    y -= INFO_ROW;
    snprintf(b, sizeof b, "SECTIONS %d / %d", s_recs.n, NM_MAX_RECS); text(LEFT, y, b);
    y -= INFO_ROW;
    if (s_hs_msg) snprintf(b, sizeof b, "%s", s_hs_msg);
    else if (!s_saves_ok) snprintf(b, sizeof b, "SAVES OFF: UNKNOWN FIRMWARE");
    else snprintf(b, sizeof b, "HIGH SCORES %d/%d 3=SAVE", s_hs_count, (int) (HS_BYTES / HS_REC));
    text(LEFT, y, b);
    y -= INFO_ROW;
    snprintf(b, sizeof b, "ENTRIES %d / %d", s_tree.n, MT_MAX_NODES); text(LEFT, y, b); y -= INFO_ROW;
    snprintf(b, sizeof b, "NAMES %d / %d KB", s_tree.used / 1024, MT_POOL / 1024); text(LEFT, y, b); y -= INFO_ROW;
    snprintf(b, sizeof b, "MOST DROPPED %lu", (unsigned long) s_peak_dropped); text(LEFT, y, b); y -= INFO_ROW;
    snprintf(b, sizeof b, "LONGEST FRAME %lu MS", (unsigned long) (s_peak_cycles * 2u / 3000u)); text(LEFT, y, b);
    return;
  }
  if (s_browsing) { browse_draw(); return; }
  /* What matters most goes first: if a frame ever overflows the command list, it is the end that is lost. */
  int c = mt_nth(&s_tree, s_nav.dir, s_nav.cursor);
  const NmRec *r = (c >= 0 && s_tree.node[c].rec >= 0) ? &s_recs.rec[s_tree.node[c].rec] : 0;
  static MtNotes notes;
  if (s_notes)                                  /* Button 2: its title, all the notes, then the controls */
  {
    int y = LIST_Y;
    if (c >= 0) text_cut(LEFT, HEAD_Y, mt_title(&s_tree, c), HEAD_CH + 9);
    s_size = 4;
    text_small(LEFT, HINT_Y, "BACK: ANY BUTTON / LEFT");
    s_size = 5;
    mt_notes_start(&notes, (y - MSG_Y) / NOTE_H + 1);
    add_notes(&notes, c, r);
    if (r)
    {
      const char *p = nmStr(&s_recs, r->ctl);
      for (int i = 0; i < r->nctl; i++) { mt_notes_add(&notes, p); p += strlen(p) + 1; }
    }
    mt_notes_end(&notes);
    for (int i = 0; i < notes.n; i++, y -= NOTE_H) text_small(LEFT, y, notes.line[i]);
    return;
  }
  /* wrapped to the screen; a message takes the last row */
  const int nend = (s_error[0] || s_banner[0]) ? MSG_Y + NOTE_H : MSG_Y;
  mt_notes_start(&notes, (NOTE_Y - nend) / NOTE_H + 1);
  add_notes(&notes, c, r);
  s_has_more = notes.more || (r && r->nctl > 0);
  mt_notes_end(&notes);

  if (c >= 0)
  {
    char hint[64];
    const char *im = mt_image(&s_tree, c);
    int game = im[0] != 0 && (im[0] != '@' || strncmp(im, "@fw:", 4) == 0);   /* what button 3 works on */
    mt_hint(hint, sizeof hint, mt_is_category(&s_tree, c) || !strcmp(im, "@browse") || !strcmp(im, "@new"), s_tree.node[s_nav.dir].parent >= 0, game, s_has_more);
    s_size = 4;
    text_small(LEFT, HINT_Y, hint);
  }
  s_size = 8;
  text_cut(LEFT, HEAD_Y, mt_title(&s_tree, s_nav.dir), HEAD_CH);
  int k = mt_count(&s_tree, s_nav.dir);
  if (k == 0) text(LEFT, LIST_Y, "NOTHING FOUND");
  for (int i = 0; i < MT_ROWS && s_nav.top + i < k; i++)
  {
    int id = mt_nth(&s_tree, s_nav.dir, s_nav.top + i);
    int y = LIST_Y - i * ROW_H;
    if (s_nav.top + i == s_nav.cursor) text(LEFT, y, ">");
    /* > marks what opens onto more: a folder, BROWSE SD CARD and NEW */
    int folder = mt_is_category(&s_tree, id) || !strcmp(mt_image(&s_tree, id), "@browse") || !strcmp(mt_image(&s_tree, id), "@new"), fav = s_tree.node[id].fav;
    text_cut(LIST_X, y, mt_title(&s_tree, id), folder || fav ? LIST_CH - 1 : LIST_CH);
    if (folder) text(MARK_X, y, ">");
    else if (fav) text(MARK_X, y, "'");            /* a favorite: one stroke, in the column folders use for > */
  }
  /* more to scroll to: chevrons in the gaps above and below the list, and where we are in it */
  if (mt_more_above(&s_nav)) chevron(0, LIST_Y + 11, 5);
  if (mt_more_below(&s_nav, &s_tree)) chevron(0, LIST_BOTTOM - 9, -5);

  /* the highlighted entry's text, under the list, in the smaller size */
  s_size = 5;
  if (s_error[0]) text_cut(LEFT, MSG_Y, s_error, NOTE_CH);
  else if (s_banner[0]) text_cut(LEFT, MSG_Y, s_banner, NOTE_CH);
  for (int i = 0; i < notes.n; i++) text_small(LEFT, NOTE_Y - i * NOTE_H, notes.line[i]);
  if (k > MT_ROWS)
  {
    char pos[16];
    int n = snprintf(pos, sizeof pos, "%d/%d", s_nav.cursor + 1, k);
    text_small(100 - n * FONT_ADV * s_size * FONT_STEP / 127, HEAD_Y, pos);
  }
}

/* The built-in games that can be started, in their firmware category, in HOMEBREW when the firmware files them
 * nowhere, and in the @fw rule's also. */
static void add_builtins(MtTree *t, void *u)
{
  (void) u;
  for (int i = 0; i < s_fw.ngames; i++)
  {
    char name[MT_TITLE], image[16];
    if (!fw_can_serve(&s_fw, i) || !fw_game_name(&s_fw, i, name, sizeof name)) { s_fw_unsupported++; continue; }
    snprintf(image, sizeof image, "@fw:%d", i);
    const FwGame *g = &s_fw.game[i];
    if (mt_add_builtin(t, &s_recs, g->cat != FW_NONE ? s_fw.cat[g->cat] : 0, name, image)) s_fw_shown++;
    else s_fw_hidden++;
  }
}

/* The whole menu from the card: records, files and favorites, then the tree. setup() and the button 3 toggle both
 * use it, so a favorite shows at once. The firmware scan stays in setup(): it does not change on a rebuild. */
static void build_menu(void)
{
  uint32_t t0 = time_us_32();
  memset(&s_recs, 0, sizeof s_recs);
  read_menu();
  s_fw_shown = s_fw_hidden = s_fw_unsupported = 0;
  static const MtCard card = { card_exists, card_list, add_builtins, 0, &s_favs };
  mt_build(&s_tree, &s_recs, &card);
  if (!s_saves_ok) strcpy(s_banner, "UNKNOWN FIRMWARE: SCORES+SAVES OFF");
  else if (s_recs.dropped || s_tree.dropped) strcpy(s_banner, "MENU TOO LONG");
  else if (s_fw.cut_off) strcpy(s_banner, "SOME BUILT-IN GAMES NOT LISTED");
  s_build_ms = (time_us_32() - t0) / 1000u;
}

/* Put path (a card path, or @fw/<name>) in the favorites, or take it out; title is what the file shows for it. The
 * file is read into the first half of s_text and the new text written into the second, so no other buffer is needed.
 * The tree is rebuilt and kept where it was: its caller draws the tree or the browser afterwards. */
#define FAV_HALF 16384u
static void favorite_toggle(const char *path, const char *title)
{
  uint32_t n = uvm2_sd_read("menu/" MT_FAVORITES, (unsigned char *) s_text, FAV_HALF - 1);
  if (n == 0)
  {
    /* uvm2_sd_read also returns 0 when the file does not fit, so one byte tells an empty or missing file apart. */
    unsigned char one;
    if (uvm2_sd_read_from("menu/" MT_FAVORITES, &one, 1, 0) != 0)
    {
      strcpy(s_error, "FAVORITES FILE TOO BIG");
      return;
    }
    s_text[0] = 0;
  }
  else s_text[n] = 0;

  int added = 0;
  char *out = s_text + FAV_HALF;
  int k = mt_fav_toggle(s_text, title, path, out, FAV_HALF, &added);
  if (k == -1) { strcpy(s_error, "FAVORITES FILE TOO BIG"); return; }
  if (k == -2) { strcpy(s_error, "FAVORITES FULL"); return; }
  if (uvm2_sd_write("menu/" MT_FAVORITES, (const unsigned char *) out, (uint32_t) k) != 1)
  {
    strcpy(s_error, "COULD NOT SAVE FAVORITES");   /* the card is as it was, so the menu is not rebuilt */
    return;
  }
  MtPlace place;
  mt_place_save(&place, &s_nav, &s_tree);
  build_menu();
  mt_place_restore(&s_nav, &s_tree, &place);
  strcpy(s_error, added ? "ADDED TO FAVORITES" : "REMOVED FROM FAVORITES");
}

/* Button 3 in the menu: a game's path from the tree, then the shared toggle. A folder or another @ action does nothing. */
static void toggle_favorite(void)
{
  int c = mt_nth(&s_tree, s_nav.dir, s_nav.cursor);
  if (c < 0) return;
  const char *image = mt_image(&s_tree, c);
  char path[96];
  if (strncmp(image, "@fw:", 4) == 0)
  {
    int i = atoi(image + 4);
    char name[MT_TITLE];
    if (i < 0 || i >= s_fw.ngames || !fw_game_name(&s_fw, i, name, sizeof name)) return;
    snprintf(path, sizeof path, "@fw/%s", name);
  }
  else if (image[0] == 0 || image[0] == '@') return;   /* a folder, or an action that is not a game */
  else snprintf(path, sizeof path, "%s", image);
  favorite_toggle(path, mt_title(&s_tree, c));
}

static void setup(void)
{
  snprintf(s_launched_as, sizeof s_launched_as, "%.*s", 63, (const char *) 0x20080000u);
  mt_nav_init(&s_nav);
  cartload_on_key(restore_score);
  if (!uvm2_sd_init()) strcpy(s_error, "NO SD CARD");
  else
  {
    int as_autostart = strcasecmp(s_launched_as, SELF) == 0;
    int want = new_menu_setting();
    if (as_autostart && !want) go_to_cartridge_menu();
    if (as_autostart) title_start();
    if (!as_autostart && (!want || autostart_is_off()))
      strcpy(s_banner, settings_turn_on() && (!card_exists(SELF, 0) || autostart_set_off(0)) ? "MENU IS ON AGAIN" : "COULD NOT SWITCH ON");
    uint32_t t0 = time_us_32();
    fw_scan(&s_fw, FLASH_NOCACHE);
    s_saves_ok = fw_saves_ok(FLASH_NOCACHE, 0);
    cartload_allow_saves(s_saves_ok);
    s_scan_ms = (time_us_32() - t0) / 1000u;
    build_menu();
  }
  if (s_tree.n == 0) mt_clear(&s_tree, "UVMC2 MENU");
}

/* ---- CALIBRATE SCREEN ----
 * The SDK's calibration screen (uvm2_config_wizard), the one that opens when buttons 2 and 3 are held while a .um2
 * starts. It saves config/uvm2.cfg, the file every SDK program reads (the UVMC2 ports, jukebox3, uvmc2_calibration and
 * this menu), and its changes apply at once. It measures the stick's centre over its first frames, and the menu opens
 * entries with the stick too, so it waits until the stick and the buttons are let go. */
static int released(void)
{
  for (int b = 1; b <= 4; b++) if (vpy_j1_button(b)) return 0;
  return vpy_j1_x() > -60 && vpy_j1_x() < 60 && vpy_j1_y() > -60 && vpy_j1_y() < 60;
}

static void calibrate(void)
{
  s_calibrate = 0;
  /* The menu's frame is open and still empty here: the wizard opens and closes frames of its own and returns with
   * the last one closed, so one is opened again for the menu to draw into. */
  int saved = uvm2_config_wizard();
  uvm2_frame_begin();
  strcpy(s_error, saved ? "CALIBRATION SAVED" : "CALIBRATION NOT SAVED");
  s_armed = 0;                     /* button 4 closed the wizard: ignore it until let go */
}

static void loop(void)
{
  if (s_calibrate && released()) calibrate();
  note_frame_stats();
  update();
  draw();
}

int main(void)
{
  vpy_run(setup, loop);
  return 0;
}
