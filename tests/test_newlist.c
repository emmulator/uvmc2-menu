/* Host tests for newlist.c: the newest few game files by FAT date, and how they show in the browser's list. */
#include "newlist.h"
#include "menu_tree.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

static int passes, fails;
#define CHECK(x) do { if (x) passes++; else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

/* FAT date: (year - 1980) << 9 | month << 5 | day; time: hour << 11 | minute << 5 | seconds / 2 */
#define DATE(y, m, d) ((uint16_t) (((y) - 1980) << 9 | (m) << 5 | (d)))
#define TIME(h, mi) ((uint16_t) ((h) << 11 | (mi) << 5))

static void test_skip(void)
{
  CHECK(nw_skip("readme.txt") && nw_skip("roms/dkong.zip") && nw_skip("noext"));
  CHECK(!nw_skip("game.um2") && !nw_skip("GAME.BIN") && !nw_skip("sub/deep/x.Vec"));
  CHECK(nw_skip("autostart.um2") && nw_skip("MENU.um2") && nw_skip("menu.um2"));            /* the menu's own two names, any case */
  CHECK(!nw_skip("menu2.um2") && !nw_skip("autostart2.um2"));
  CHECK(!nw_skip("games/autostart.um2") && !nw_skip("games/MENU.um2"));                  /* elsewhere they are just files */
  CHECK(nw_skip("menu/transfer.um2") && nw_skip("MENU/anything.bin") && nw_skip("config/x.bin"));
  CHECK(!nw_skip("menus/x.bin") && !nw_skip("configs/x.bin") && !nw_skip("menu.bin"));   /* only those two folders */
}

static void test_order(void)
{
  NwList l;
  nw_start(&l, 3);
  CHECK(nw_offer(&l, "old.um2", DATE(2026, 1, 5), TIME(10, 0)) == 1);
  CHECK(nw_offer(&l, "mid.bin", DATE(2026, 6, 1), TIME(9, 30)) == 1);
  CHECK(nw_offer(&l, "new.vec", DATE(2026, 11, 20), TIME(8, 0)) == 1);
  CHECK(l.n == 3 && !strcmp(l.path[0], "new.vec") && !strcmp(l.path[1], "mid.bin") && !strcmp(l.path[2], "old.um2"));
  /* older than all three held: refused; newer than the oldest: it takes that place and the oldest goes */
  CHECK(nw_offer(&l, "older.um2", DATE(2025, 1, 1), 0) == 0);
  CHECK(nw_offer(&l, "later.um2", DATE(2026, 7, 1), 0) == 1);
  CHECK(l.n == 3 && !strcmp(l.path[0], "new.vec") && !strcmp(l.path[1], "later.um2") && !strcmp(l.path[2], "mid.bin"));
  /* the same day, a later time wins; the same stamp goes by path, ignoring case */
  CHECK(nw_offer(&l, "sub/b.um2", DATE(2026, 11, 20), TIME(8, 1)) == 1);
  CHECK(!strcmp(l.path[0], "sub/b.um2") && !strcmp(l.path[1], "new.vec"));
  CHECK(nw_offer(&l, "A.bin", DATE(2026, 11, 20), TIME(8, 1)) == 1);
  CHECK(!strcmp(l.path[0], "A.bin") && !strcmp(l.path[1], "sub/b.um2") && !strcmp(l.path[2], "new.vec"));
  /* not game files, not the menu's own, not too long: never offered */
  CHECK(nw_offer(&l, "notes.txt", DATE(2030, 1, 1), 0) == 0 && nw_offer(&l, "autostart.um2", DATE(2030, 1, 1), 0) == 0);
  char longp[200];
  memset(longp, 'a', sizeof longp);
  strcpy(longp + 150, ".um2");
  CHECK(nw_offer(&l, longp, DATE(2030, 1, 1), 0) == 0);
  CHECK(l.n == 3);
}

static void test_count(void)
{
  NwList l;
  nw_start(&l, 0);
  CHECK(l.k == 1);
  nw_start(&l, 1000);
  CHECK(l.k == NW_MAX);
  nw_start(&l, 1);
  CHECK(nw_offer(&l, "a.um2", DATE(2026, 1, 1), 0) == 1 && nw_offer(&l, "b.um2", DATE(2026, 2, 1), 0) == 1);
  CHECK(l.n == 1 && !strcmp(l.path[0], "b.um2"));
  /* a full list of NW_MAX takes a newer file and drops its oldest */
  nw_start(&l, NW_MAX);
  for (int i = 0; i < NW_MAX + 5; i++)
  {
    char p[16];
    snprintf(p, sizeof p, "g%02d.um2", i);
    nw_offer(&l, p, DATE(2026, 1, 1 + i), 0);
  }
  CHECK(l.n == NW_MAX && !strcmp(l.path[0], "g24.um2") && !strcmp(l.path[NW_MAX - 1], "g05.um2"));
}

static void test_browse(void)
{
  NwList l;
  static BrList b;
  nw_start(&l, 10);
  nw_offer(&l, "games/extra/zeta_game.um2", DATE(2026, 11, 20), 0);
  nw_offer(&l, "top.bin", DATE(2026, 11, 19), 0);
  nw_to_browse(&l, &b);
  CHECK(b.n == 2 && b.path[0] == 0 && !b.dir[0] && !b.dir[1] && b.dropped == 0);
  CHECK(!strcmp(br_name(&b, 0), "games/extra/zeta_game.um2") && !strcmp(br_name(&b, 1), "top.bin"));
  char out[100];
  CHECK(br_child_path(&b, 0, out, sizeof out) && !strcmp(out, "games/extra/zeta_game.um2"));   /* a name in the root: the path itself */
  br_title_base(&b, 0, out, sizeof out);
  CHECK(!strcmp(out, "ZETA GAME.UM2"));
  br_title_base(&b, 1, out, sizeof out);
  CHECK(!strcmp(out, "TOP.BIN"));
  br_title(&b, 0, out, sizeof out);
  CHECK(!strcmp(out, "GAMES/EXTRA/ZETA GAME.UM2"));
  br_move(&b, 1);
  CHECK(b.cursor == 1);
  br_move(&b, 1);
  CHECK(b.cursor == 0);                                       /* wraps */
}

static void test_settings_and_hidden(void)
{
  CHECK(mt_setting_int("new = 5\n", "new", 10) == 5);
  CHECK(mt_setting_int("; menu settings\nmenu = on\nnew=15 ; the newest fifteen\n", "new", 10) == 15);
  CHECK(mt_setting_int("menu = on\n", "new", 10) == 10);               /* no key: the default */
  CHECK(mt_setting_int("new = lots\n", "new", 10) == 10);               /* not a number */
  CHECK(mt_setting_int("new = \n", "new", 10) == 10);
  CHECK(mt_setting_int("newest = 3\nnew = 7\n", "new", 10) == 7);
  CHECK(mt_setting_int("new = 0\n", "new", 10) == 0);                  /* nw_start clamps it to 1 */
  static NmRecs r;
  memset(&r, 0, sizeof r);
  static char ini[] = "[ZSB]\nfile = ZSB.bin\nhide = 1\n[SHOWN]\nfile = shown.um2\ncategory = X\n[ONE]\nimage = sub/one.um2\nhide = 1\n[TWO]\nimage = two.um2\n";
  nmParseMenuText(&r, ini, "menu");
  CHECK(mt_file_hidden(&r, "ZSB.bin") == 1 && mt_file_hidden(&r, "zsb.BIN") == 1);
  CHECK(mt_file_hidden(&r, "shown.um2") == 0 && mt_file_hidden(&r, "two.um2") == 0 && mt_file_hidden(&r, "other.bin") == 0);
  CHECK(mt_file_hidden(&r, "sub/one.um2") == 1);
}

/* The walk over a real folder tree, through POSIX calls standing in for FatFs. mtime -> FAT date as FatFs reads it. */
static char root[64];
static DIR *dirs[NW_DEPTH];
static char dpath[NW_DEPTH][512];
static int opens, closes;
static int w_open(int depth, const char *path, void *u)
{
  (void) u;
  snprintf(dpath[depth], sizeof dpath[depth], "%s%s%s", root, path[0] ? "/" : "", path);
  dirs[depth] = opendir(dpath[depth]);
  if (dirs[depth]) opens++;
  return dirs[depth] != 0;
}
static char wname[300];
static int w_read(int depth, NwEntry *e, void *u)
{
  (void) u;
  struct dirent *d;
  while ((d = readdir(dirs[depth])) != 0)
  {
    if (!strcmp(d->d_name, ".") || !strcmp(d->d_name, "..")) continue;
    char full[900];
    struct stat st;
    snprintf(full, sizeof full, "%s/%s", dpath[depth], d->d_name);
    if (stat(full, &st) != 0) continue;
    snprintf(wname, sizeof wname, "%s", d->d_name);
    e->name = wname;
    e->is_dir = S_ISDIR(st.st_mode);
    e->hidden = 0;
    struct tm tm;
    localtime_r(&st.st_mtime, &tm);
    e->fdate = (uint16_t) ((tm.tm_year + 1900 - 1980) << 9 | (tm.tm_mon + 1) << 5 | tm.tm_mday);
    e->ftime = (uint16_t) (tm.tm_hour << 11 | tm.tm_min << 5 | tm.tm_sec / 2);
    return 1;
  }
  return 0;
}
static void w_close(int depth, void *u) { (void) u; closedir(dirs[depth]); closes++; }
static int skip_calls, skip_mode;     /* skip_mode 1: also treat the newest file as mapped, as the unmapped filter would */
static int w_skip(const char *path, void *u)
{
  (void) u;
  skip_calls++;
  return strcmp(path, "hidden_by_menu.um2") == 0 || (skip_mode && strstr(path, "fine.vec") != 0);
}

static void touch(const char *rel, long days_ago)
{
  char p[600];
  snprintf(p, sizeof p, "%s/%s", root, rel);
  FILE *f = fopen(p, "w");
  if (f) { fputs("x", f); fclose(f); }
  struct utimbuf t;
  t.actime = t.modtime = time(0) - days_ago * 86400L;
  utime(p, &t);
}

static void test_walk(void)
{
  snprintf(root, sizeof root, "/tmp/uvmc2-newtest-%d", (int) getpid());
  char cmd[1600];
  snprintf(cmd, sizeof cmd, "rm -rf %s && mkdir -p %s/games/extra/deep1/deep2/deep3/deep4 %s/menu %s/config %s/.hidden %s/roms", root, root, root, root, root, root);
  CHECK(system(cmd) == 0);
  touch("autostart.um2", 1); touch("MENU.um2", 1); touch("menu/transfer.um2", 1); touch("config/x.bin", 1);
  touch("games/extra/zeta.um2", 3);
  touch("games/alpha.bin", 5);
  touch("games/extra/deep1/deep2/deep3/deep4/toodeep.um2", 0);   /* 6th folder level: depth 5 opens, depth 6 does not */
  touch("games/extra/deep1/deep2/deep3/fine.vec", 0);
  touch("roms/dkong.zip", 0);
  touch("hidden_by_menu.um2", 0);
  touch(".hidden/secret.um2", 0);
  touch("top.BIN", 10);
  touch("old.um2", 400);
  NwFs fs = { w_open, w_read, w_close, w_skip };
  NwList l;
  nw_start(&l, 4);
  opens = closes = 0;
  CHECK(nw_walk(&l, &fs, 0, 1000) == 0);
  CHECK(opens == closes);                                           /* every directory it opened it closed */
  /* newest first: fine.vec (today, 0 days ago; toodeep is deeper than the walk goes; hidden_by_menu is skipped), then zeta
   * (3 days), alpha (5), top.BIN (10); old.um2 (400 days) does not make the four */
  CHECK(l.n == 4);
  CHECK(!strcmp(l.path[0], "games/extra/deep1/deep2/deep3/fine.vec"));
  CHECK(!strcmp(l.path[1], "games/extra/zeta.um2") && !strcmp(l.path[2], "games/alpha.bin") && !strcmp(l.path[3], "top.BIN"));
  /* a count that holds everything lists the rest after it, and never the menu's own, the hidden, or the non-game files */
  nw_start(&l, 20);
  CHECK(nw_walk(&l, &fs, 0, 1000) == 0 && l.n == 5 && !strcmp(l.path[4], "old.um2"));
  for (int i = 0; i < l.n; i++) CHECK(strstr(l.path[i], "toodeep") == 0 && strstr(l.path[i], "secret") == 0 && strstr(l.path[i], "transfer") == 0);
  /* the filter applies before the newest are picked: with the newest file filtered out, the four are the next four, not three */
  skip_mode = 1;
  nw_start(&l, 4);
  CHECK(nw_walk(&l, &fs, 0, 1000) == 0 && l.n == 4);
  CHECK(!strcmp(l.path[0], "games/extra/zeta.um2") && !strcmp(l.path[1], "games/alpha.bin") && !strcmp(l.path[2], "top.BIN") && !strcmp(l.path[3], "old.um2"));
  /* the filter is asked only about files that would make the list: at most one call per candidate (6 here), usually fewer when k is small */
  nw_start(&l, 1);
  skip_calls = 0;
  CHECK(nw_walk(&l, &fs, 0, 1000) == 0 && l.n == 1 && skip_calls <= 6);
  skip_mode = 0;
  /* a visit cap stops the walk early, and still closes what it opened */
  nw_start(&l, 20);
  opens = closes = 0;
  CHECK(nw_walk(&l, &fs, 0, 3) == 1 && opens == closes);
  /* a root that will not open */
  NwFs bad = { w_open, w_read, w_close, 0 };
  char keep[64];
  snprintf(keep, sizeof keep, "%s", root);
  snprintf(root, sizeof root, "/nonexistent/nowhere");
  CHECK(nw_walk(&l, &bad, 0, 10) == -1);
  snprintf(root, sizeof root, "%s", keep);
  snprintf(cmd, sizeof cmd, "rm -rf %s", root);
  CHECK(system(cmd) == 0);
}

int main(void)
{
  test_skip(); test_walk(); test_order(); test_count(); test_browse(); test_settings_and_hidden();
  printf("%d passed, %d failed\n", passes, fails);
  return fails != 0;
}
