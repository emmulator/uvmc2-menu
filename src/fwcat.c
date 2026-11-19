#include "fwcat.h"
#include "cart.h"
#include "inflate.h"
#include <string.h>
#define LIMIT 0x100000u          /* the first MB: the list is there in every version seen, and reads cost time */
#define MIN_RUN 16

static uint32_t rd(const FwCat *c, uint32_t off)
{
  const uint8_t *p = c->flash + off;
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}
static int in_flash(uint32_t a) { return a >= FW_FLASH && a < FW_FLASH + FW_SIZE; }

/* A short printable NUL-terminated string at a flash address? Copies it when out is given. */
static int str_at(const FwCat *c, uint32_t a, char *out, int max)
{
  if (!in_flash(a) || a - FW_FLASH > FW_SIZE - 64) return 0;
  const uint8_t *p = c->flash + (a - FW_FLASH);
  for (int i = 0; i < 64; i++)
  {
    uint8_t b = p[i];
    if (b == 0)
    {
      if (i == 0) return 0;
      if (out) { int n = i < max - 1 ? i : max - 1; memcpy(out, p, (size_t) n); out[n] = 0; }
      return 1;
    }
    if (b < 32 || b >= 127) return 0;
  }
  return 0;
}

static int is_record(const FwCat *c, uint32_t o, int idx)
{
  uint32_t id = rd(c, o + 20);
  if ((int) (id & 0xff) != (idx & 0xff) || (id >> 16) != 0) return 0;
  uint32_t p = rd(c, o);
  if (!in_flash(p) || rd(c, o + 4) == 0 || rd(c, o + 8) == 0) return 0;
  return str_at(c, rd(c, o + 16), 0, 0);
}

/* {name, list of game indexes, count}: 12 bytes each. Returns the entries in the run that starts at o. */
static int list_run(const FwCat *c, uint32_t o, uint32_t end, int ng)
{
  int n = 0;
  for (; o + 12 <= end; o += 12, n++)
  {
    uint32_t p = rd(c, o + 4), k = rd(c, o + 8);
    if (!str_at(c, rd(c, o), 0, 0) || !in_flash(p) || k == 0 || (int) k > ng || p - FW_FLASH + k > FW_SIZE) break;
    const uint8_t *l = c->flash + (p - FW_FLASH);
    for (uint32_t i = 0; i < k; i++) if (l[i] >= ng) return n;
  }
  return n;
}

int fw_scan(FwCat *c, const uint8_t *flash)
{
  memset(c, 0, sizeof *c);
  c->flash = flash;
  for (int i = 0; i < FW_MAX_GAMES; i++) { c->game[i].dev[0] = c->game[i].dev[1] = c->game[i].cat = FW_NONE; }
  uint32_t t = 0;
  int n = 0;
  for (uint32_t o = 0; o < LIMIT; o += 4)
    if (rd(c, o + 20) == 0 && is_record(c, o, 0))
    {
      n = 1;
      while (n < FW_MAX_GAMES && is_record(c, o + (uint32_t) n * 24, n)) n++;
      if (n >= MIN_RUN) { t = o; break; }
      n = 0;
    }
  if (!t) return 0;
  c->table = FW_FLASH + t;
  c->ngames = n;
  c->cut_off = n == FW_MAX_GAMES && t + (uint32_t) n * 24 + 24 <= FW_SIZE && is_record(c, t + (uint32_t) n * 24, n);
  for (int i = 0; i < n; i++)
  {
    uint32_t o = t + (uint32_t) i * 24;
    FwGame *g = &c->game[i];
    uint32_t packed = rd(c, o + 8);
    g->ptr = rd(c, o); g->size = rd(c, o + 4); g->crc = rd(c, o + 12);
    g->packed = packed & 0xffffff;
    g->writable = (uint8_t) (packed >> 24);
  }
  /* The name lists sit just before the game list: developers, then categories. */
  uint32_t lo = t > 0x800 ? t - 0x800 : 0, runs[2] = { 0, 0 };
  int len[2] = { 0, 0 };
  for (uint32_t o = lo; o < t;)
  {
    int r = list_run(c, o, t, n);
    if (r >= 3) { runs[0] = runs[1]; len[0] = len[1]; runs[1] = o; len[1] = r; o += (uint32_t) r * 12; }
    else o += 4;
  }
  for (int w = 0; w < 2; w++)
  {
    if (!len[w]) continue;
    for (int i = 0; i < len[w]; i++)
    {
      uint32_t o = runs[w] + (uint32_t) i * 12;
      uint32_t p = rd(c, o + 4), k = rd(c, o + 8);
      const uint8_t *l = c->flash + (p - FW_FLASH);
      if (w == 0 && i < FW_MAX_LISTS)
      {
        str_at(c, rd(c, o), c->dev[i], FW_NAME);
        c->ndevs = i + 1;
        for (uint32_t j = 0; j < k; j++)
        {
          FwGame *g = &c->game[l[j]];
          if (g->dev[0] == FW_NONE) g->dev[0] = (uint8_t) i;
          else if (g->dev[1] == FW_NONE) g->dev[1] = (uint8_t) i;
        }
      }
      if (w == 1 && i < 16)
      {
        str_at(c, rd(c, o), c->cat[i], FW_NAME);
        c->ncats = i + 1;
        for (uint32_t j = 0; j < k; j++) if (c->game[l[j]].cat == FW_NONE) c->game[l[j]].cat = (uint8_t) i;
      }
    }
  }
  return n;
}

int fw_game_name(const FwCat *c, int i, char *out, int max)
{
  return str_at(c, rd(c, c->table - FW_FLASH + (uint32_t) i * 24 + 16), out, max);
}
const uint8_t *fw_game_data(const FwCat *c, int i)
{
  return c->flash + (c->game[i].ptr - FW_FLASH) + cart_builtin_prefix(c->game[i].size);
}
uint32_t fw_game_packed(const FwCat *c, int i) { return c->game[i].packed - cart_builtin_prefix(c->game[i].size); }
int fw_can_serve(const FwCat *c, int i)
{
  return cart_builtin_scheme(c->game[i].size) != CART_NONE && c->game[i].packed + (c->game[i].ptr - FW_FLASH) <= FW_SIZE;
}

/* "g GCE 2026" 0x80, music (2), then per line height, width, y, x and text ending in 0x80; 0 ends the list. */
static int title_at(const uint8_t *h, const uint8_t *end, FwTitle *t)
{
  const uint8_t *p = h + 6;
  while (p < end && *p != 0x80) { if (*p < 32 || *p >= 127) return 0; p++; }
  p += 3;
  t->n = 0;
  int ours = 0;
  while (p + 4 < end && *p != 0 && t->n < FW_TITLE_LINES)
  {
    int y = (int8_t) p[2];
    p += 4;
    int k = 0;
    char *l = t->line[t->n];
    while (p < end && *p != 0x80)
    {
      if (*p < 32 || *p >= 127 || k >= (int) sizeof t->line[0] - 1) return 0;
      l[k++] = (char) *p++;
    }
    l[k] = 0;
    p++;
    if (strstr(l, "MULTICART")) ours = 1;
    t->y[t->n++] = y;
  }
  return ours && t->n > 0;
}

#define TITLE_FROM 0xf00000u      /* the menu's 6809 program has been at 0xf2fbbc to 0xf30034 in 1.1.9a to 1.1.13a */
#define TITLE_TO   0xf80000u

int fw_title(const uint8_t *flash, FwTitle *t)
{
  const uint8_t *end = flash + TITLE_TO;
  for (const uint8_t *p = flash + TITLE_FROM; p + 6 < end; p++)
  {
    p = memchr(p, 'g', (size_t) (end - p - 6));
    if (!p) break;
    if (memcmp(p, "g GCE ", 6) == 0 && title_at(p, end, t)) return 1;
  }
  static const char *const credit[] = { "ARCADE JASON", "ULTIMATE", "VECTREX", "MULTICART 2" };
  static const int ys[] = { 64, 32, 16, 0 };
  t->n = 4;
  for (int i = 0; i < 4; i++) { strcpy(t->line[i], credit[i]); t->y[i] = ys[i]; }
  return 0;
}

/* Its header, "g GCE 2016" $80, music, then the title line: the name starts 0x11 bytes in. Found by Horspool's search,
 * which skips most of the 16 MB. */
const uint8_t *fw_stream_player(const uint8_t *flash)
{
  static const uint8_t name[] = "VIDEOPLAYER\x80";
  const uint32_t m = sizeof name - 1, at = 0x11;
  uint32_t skip[256];
  for (int i = 0; i < 256; i++) skip[i] = m;
  for (uint32_t i = 0; i + 1 < m; i++) skip[name[i]] = m - 1 - i;
  for (uint32_t i = at; i + m <= FW_SIZE - CART_STREAM_PLAYER + at; i += skip[flash[i + m - 1]])
  {
    if (memcmp(flash + i, name, m)) continue;
    const uint8_t *p = flash + i - at;
    if (memcmp(p, "g GCE", 5) == 0 && zcrc32(p, CART_STREAM_PLAYER) == CART_STREAM_PLAYER_CRC) return p;
  }
  return 0;
}
