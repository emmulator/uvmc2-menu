#include "newlist.h"
#include <string.h>
#include <strings.h>

static int ext_ok(const char *name)
{
  const char *dot = strrchr(name, '.');
  if (!dot) return 0;
  return strcasecmp(dot, ".um2") == 0 || strcasecmp(dot, ".bin") == 0 || strcasecmp(dot, ".vec") == 0;
}

int nw_skip(const char *path)
{
  const char *slash = strchr(path, '/');
  if (!slash)
    return !ext_ok(path) || strcasecmp(path, "autostart.um2") == 0 || strcasecmp(path, "MENU.um2") == 0;
  int n = (int) (slash - path);
  if ((n == 4 && strncasecmp(path, "menu", 4) == 0) || (n == 6 && strncasecmp(path, "config", 6) == 0)) return 1;
  return !ext_ok(strrchr(path, '/') + 1);
}

void nw_start(NwList *l, int k)
{
  l->k = k < 1 ? 1 : k > NW_MAX ? NW_MAX : k;
  l->n = 0;
}

/* Does a file with stamp a and path pa come before one with stamp b and path pb: newer first, then by path. */
static int before(uint32_t a, const char *pa, uint32_t b, const char *pb)
{
  return a > b || (a == b && strcasecmp(pa, pb) < 0);
}

int nw_offer(NwList *l, const char *path, uint16_t fdate, uint16_t ftime)
{
  if (nw_skip(path) || (int) strlen(path) >= NW_PATH) return 0;
  uint32_t s = (uint32_t) fdate << 16 | ftime;
  int at = l->n;
  while (at > 0 && before(s, path, l->stamp[at - 1], l->path[at - 1])) at--;
  if (at >= l->k) return 0;                                  /* not newer than the k we already hold */
  int last = l->n < l->k ? l->n : l->k - 1;                  /* the slot the shift ends in; a full list drops its last */
  for (int i = last; i > at; i--)
  {
    l->stamp[i] = l->stamp[i - 1];
    memcpy(l->path[i], l->path[i - 1], NW_PATH);
  }
  l->stamp[at] = s;
  strcpy(l->path[at], path);
  if (l->n < l->k) l->n++;
  return 1;
}

void nw_to_browse(NwList *l, BrList *b)
{
  b->pool = (char *) l->path;
  b->pool_size = (int) sizeof l->path;
  b->used = l->n * NW_PATH;
  for (int i = 0; i < l->n; i++)
  {
    b->off[i] = (uint16_t) (i * NW_PATH);
    b->dir[i] = 0;
  }
  b->n = l->n;
  b->dropped = b->skipped = b->filtered = 0;
  b->cursor = b->top = 0;
  b->path[0] = 0;
}

int nw_walk(NwList *l, const NwFs *fs, void *u, int max_visit)
{
  char path[NW_PATH];
  int parent_len[NW_DEPTH];                  /* the path's length before each open folder's name was added to it */
  int depth = 0, plen = 0, visited = 0, early = 0;
  path[0] = 0;
  parent_len[0] = 0;
  if (!fs->open(0, "", u)) return -1;
  NwEntry e;
  while (depth >= 0)
  {
    if (!fs->read(depth, &e, u))
    {
      fs->close(depth, u);
      plen = parent_len[depth];
      path[plen] = 0;
      depth--;
      continue;
    }
    if (e.name[0] == '.' || e.hidden) continue;
    if (++visited > max_visit) { early = 1; break; }
    int nlen = (int) strlen(e.name);
    if (plen + (plen ? 1 : 0) + nlen >= NW_PATH) continue;
    if (e.is_dir)
    {
      if (depth + 1 >= NW_DEPTH) continue;
      int before = plen;
      if (plen) path[plen++] = '/';
      memcpy(path + plen, e.name, (size_t) nlen + 1);
      plen += nlen;
      if (!fs->open(depth + 1, path, u)) { plen = before; path[plen] = 0; continue; }
      parent_len[++depth] = before;
      continue;
    }
    char full[NW_PATH];
    memcpy(full, path, (size_t) plen);
    if (plen) full[plen] = '/';
    memcpy(full + plen + (plen ? 1 : 0), e.name, (size_t) nlen + 1);
    if (nw_skip(full)) continue;
    /* the card's own test (a menu file hides it, or it is mapped) can be slow: ask it only about a file that would make the list */
    uint32_t stamp = (uint32_t) e.fdate << 16 | e.ftime;
    int at = l->n;
    while (at > 0 && before(stamp, full, l->stamp[at - 1], l->path[at - 1])) at--;
    if (at >= l->k) continue;
    if (fs->skip && fs->skip(full, u)) continue;
    nw_offer(l, full, e.fdate, e.ftime);
  }
  while (depth >= 0) fs->close(depth--, u);
  return early;
}
