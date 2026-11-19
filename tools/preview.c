/* Host dry run: prints the menu the cartridge would build from a card (read only). preview <card root> */
#include "menu_tree.h"
#include "fwcat.h"
#include <stdlib.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
static NmRecs r; static MtTree t; static const char *root; static char txt[32768]; static int rows; static MtFavs favs;
static int ex(const char *p, void *u) { (void) u; char f[1024]; struct stat s; snprintf(f, sizeof f, "%s/%s", root, p); return stat(f, &s) == 0; }
static void ls(const char *d, void (*add)(const char *, void *), void *c, void *u)
{
  (void) u; char f[1024]; snprintf(f, sizeof f, "%s/%s", root, d);
  DIR *x = opendir(f); if (!x) return; struct dirent *e; struct stat s; char q[1400];
  while ((e = readdir(x))) { snprintf(q, sizeof q, "%s/%s", f, e->d_name); if (e->d_name[0] != '.' && !stat(q, &s) && S_ISREG(s.st_mode)) add(e->d_name, c); }
  closedir(x);
}
static void layer(const char *p, const char *stem)
{
  char f[1024]; snprintf(f, sizeof f, "%s", p); FILE *h = fopen(f, "r"); if (!h) return;
  size_t n = fread(txt, 1, sizeof txt - 1, h); fclose(h); txt[n] = 0; nmParseMenuText(&r, txt, stem);
}
/* A favorites.ini is not a layer: only its file = lines matter, and it is the card's favorites. */
static int is_favorites(const char *p)
{
  const char *b = strrchr(p, '/');
  return strcasecmp(b ? b + 1 : p, MT_FAVORITES) == 0;
}
static void favorites(const char *p)
{
  char f[1024]; snprintf(f, sizeof f, "%s", p); FILE *h = fopen(f, "r"); if (!h) return;
  size_t n = fread(txt, 1, sizeof txt - 1, h); fclose(h); txt[n] = 0; mt_favs_read(&favs, txt);
}
static FwCat fw; static uint8_t flash[FW_SIZE];
static void builtins(MtTree *tt, void *u)
{
  (void) u; const char *f = getenv("FLASH"); if (!f) return;
  FILE *h = fopen(f, "rb"); if (!h) return; size_t g = fread(flash, 1, sizeof flash, h); (void) g; fclose(h);
  fw_scan(&fw, flash);
  for (int i = 0; i < fw.ngames; i++)
  {
    if (!fw_can_serve(&fw, i)) continue;
    char n[MT_TITLE], im[16]; fw_game_name(&fw, i, n, sizeof n); snprintf(im, sizeof im, "@fw:%d", i);
    mt_add_builtin(tt, &r, fw.game[i].cat != FW_NONE ? fw.cat[fw.game[i].cat] : 0, n, im);
  }
}
static void show(int d, int depth)
{
  for (int c = t.node[d].first; c >= 0; c = t.node[c].next)
  {
    printf("%*s%s%s%s%s\n", depth * 2, "", mt_title(&t, c), mt_is_category(&t, c) ? "/" : "  <- ", mt_image(&t, c), "");
    if (t.node[c].rec >= 0) { const NmRec *q = &r.rec[t.node[c].rec]; const char *p = nmStr(&r, q->rows); for (int i = 0; i < q->nrows; i++) { printf("%*s   : %s\n", depth * 2, "", p); p += strlen(p) + 1; } }
    rows++;
    if (mt_is_category(&t, c)) show(c, depth + 1);
  }
}
int main(int a, char **v)
{
  root = v[1];
  mt_favs_read(&favs, "");
  layer("card/menu/menu.default.ini", "menu.default");
  for (int i = 2; i < a; i++)
    if (is_favorites(v[i])) favorites(v[i]);
    else layer(v[i], "extra");
  MtCard c = { ex, ls, builtins, 0, &favs };
  mt_build(&t, &r, &c);
  show(0, 0);
  printf("%d rows, %d of %d nodes, %d of %d name bytes, dropped %d/%d\n", rows, t.n, MT_MAX_NODES, t.used, MT_POOL, r.dropped, t.dropped);
  return 0;
}
