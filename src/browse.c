#include "browse.h"
#include "menu_tree.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* A file is listed only if it is one the loader can start: .um2, .bin or .vec, any case. */
static int br_file_ok(const char *name)
{
  const char *dot = strrchr(name, '.');
  if (!dot) return 0;
  return strcasecmp(dot, ".um2") == 0 || strcasecmp(dot, ".bin") == 0 || strcasecmp(dot, ".vec") == 0;
}

void br_start(BrList *b, char *pool, int pool_size, const char *path)
{
  b->pool = pool;
  b->pool_size = pool_size;
  b->used = 0;
  b->n = 0;
  b->dropped = 0;
  b->skipped = 0;
  b->filtered = 0;
  b->cursor = 0;
  b->top = 0;
  if (!path) path = "";
  int len = (int) strlen(path);
  if (len >= BR_PATH) len = BR_PATH - 1;
  memcpy(b->path, path, (size_t) len);
  b->path[len] = 0;
}

int br_add(BrList *b, const char *name, int is_dir)
{
  if (name[0] == '.') return 0;
  if (!is_dir && !br_file_ok(name)) { b->skipped++; return 0; }
  if (b->n >= BR_MAX) { b->dropped++; return -1; }
  int len = (int) strlen(name) + 1;
  if (b->used + len > b->pool_size) { b->dropped++; return -1; }
  b->off[b->n] = (uint16_t) b->used;
  memcpy(b->pool + b->used, name, (size_t) len);
  b->used += len;
  b->dir[b->n] = (uint8_t) (is_dir ? 1 : 0);
  b->n++;
  return 1;
}

/* Does an entry at ao/ad come before one at bo/bd: folders first, then A-Z ignoring case. */
static int br_before(const BrList *b, uint16_t ao, uint8_t ad, uint16_t bo, uint8_t bd)
{
  if (ad != bd) return ad > bd;
  return strcasecmp(b->pool + ao, b->pool + bo) < 0;
}

void br_sort(BrList *b)
{
  for (int i = 1; i < b->n; i++)
  {
    uint16_t off = b->off[i];
    uint8_t dir = b->dir[i];
    int j = i;
    while (j > 0 && br_before(b, off, dir, b->off[j - 1], b->dir[j - 1]))
    {
      b->off[j] = b->off[j - 1];
      b->dir[j] = b->dir[j - 1];
      j--;
    }
    b->off[j] = off;
    b->dir[j] = dir;
  }
}

void br_move(BrList *b, int d)
{
  if (b->n == 0) return;
  int k = b->n;
  b->cursor = ((b->cursor + d) % k + k) % k;
  if (b->cursor < b->top) b->top = b->cursor;
  if (b->cursor >= b->top + MT_ROWS) b->top = b->cursor - MT_ROWS + 1;
}

const char *br_name(const BrList *b, int i)
{
  if (i < 0 || i >= b->n) return "";
  return b->pool + b->off[i];
}

static void title_of(const char *s, char *out, int max);

void br_title(const BrList *b, int i, char *out, int max)
{
  title_of(br_name(b, i), out, max);
}

void br_title_base(const BrList *b, int i, char *out, int max)
{
  const char *s = br_name(b, i), *slash = strrchr(s, '/');
  title_of(slash ? slash + 1 : s, out, max);
}

static void title_of(const char *s, char *out, int max)
{
  if (max <= 0) return;
  int k = 0;
  for (; s[k] && k < max - 1; k++)
  {
    unsigned char c = (unsigned char) s[k];
    if (c == '_') c = ' ';
    else
    {
      if (c >= 'a' && c <= 'z') c = (unsigned char) (c - 'a' + 'A');
      if (c < 32 || c > 95) c = '?';
    }
    out[k] = (char) c;
  }
  out[k] = 0;
}

int br_child_path(const BrList *b, int i, char *out, int max)
{
  if (i < 0 || i >= b->n || max <= 0) return 0;
  const char *name = br_name(b, i);
  int plen = (int) strlen(b->path);
  int nlen = (int) strlen(name);
  int len = plen + (plen > 0 ? 1 : 0) + nlen;
  if (len + 1 > max) return 0;
  int p = 0;
  if (plen > 0)
  {
    memcpy(out, b->path, (size_t) plen);
    p = plen;
    out[p++] = '/';
  }
  memcpy(out + p, name, (size_t) nlen + 1);
  return 1;
}

int br_parent(const char *path, char *out, int max, char *last, int lmax)
{
  if (!path || !*path) return 0;
  const char *slash = strrchr(path, '/');
  const char *base = slash ? slash + 1 : path;
  int plen = slash ? (int) (slash - path) : 0;
  int llen = (int) strlen(base);
  if (plen + 1 > max || llen + 1 > lmax) return 0;
  memcpy(out, path, (size_t) plen);
  out[plen] = 0;
  memcpy(last, base, (size_t) llen + 1);
  return 1;
}

int br_find(const BrList *b, const char *name)
{
  for (int i = 0; i < b->n; i++)
    if (strcasecmp(br_name(b, i), name) == 0) return i;
  return -1;
}

int br_filter(BrList *b, int (*keep)(const BrList *b, int i, void *u), void *u)
{
  int j = 0, dropped = 0;
  for (int i = 0; i < b->n; i++)
  {
    if (!b->dir[i] && !keep(b, i, u)) { dropped++; continue; }
    b->off[j] = b->off[i];
    b->dir[j] = b->dir[i];
    j++;
  }
  b->n = j;
  b->cursor = b->top = 0;
  b->filtered = dropped;
  return dropped;
}

int br_note(const BrList *b, int unmapped, char *out, int max)
{
  int o = b->skipped > 999 ? 999 : b->skipped, m = b->filtered > 999 ? 999 : b->filtered;
  if (unmapped && m > 0 && o > 0) snprintf(out, (size_t) max, "%d MAPPED, %d OTHER FILES NOT SHOWN", m, o);
  else if (unmapped && m > 0) snprintf(out, (size_t) max, "%d MAPPED %s NOT SHOWN", m, m == 1 ? "FILE" : "FILES");
  else if (unmapped && o > 0) snprintf(out, (size_t) max, "%d OTHER %s NOT SHOWN", o, o == 1 ? "FILE" : "FILES");
  else if (!unmapped && o > 0) snprintf(out, (size_t) max, "%d %s NOT SHOWN: ONLY UM2 BIN VEC", o, o == 1 ? "FILE" : "FILES");
  else return 0;
  return 1;
}

const char *br_empty(const BrList *b, int unmapped)
{
  if (unmapped) return "NO UNMAPPED FILES";
  return b->skipped > 0 ? "NO UM2/BIN/VEC FILES HERE" : "NOTHING FOUND";
}

void br_head(const char *path, char *out, int max)
{
  char full[BR_PATH + 1];
  int n = 0;
  if (!path[0]) { snprintf(out, (size_t) max + 1, "SD CARD"); return; }
  for (const char *p = path; *p && n < BR_PATH; p++, n++)
  {
    unsigned char c = (unsigned char) *p;
    if (c == '_') c = ' ';
    else
    {
      if (c >= 'a' && c <= 'z') c = (unsigned char) (c - 'a' + 'A');
      if (c < 32 || c > 95) c = '?';
    }
    full[n] = (char) c;
  }
  full[n] = 0;
  if (n <= max) { memcpy(out, full, (size_t) n + 1); return; }
  int dots = max < 3 ? max : 3;
  memset(out, '.', (size_t) dots);
  memcpy(out + dots, full + n - (max - dots), (size_t) (max - dots) + 1);
}
