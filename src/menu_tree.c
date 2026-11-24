#include "menu_tree.h"
#include <stdio.h>
#include <strings.h>

/* Offset of a copy of s in the pool, or -1 if it is full. Offset 0 is the empty string. */
static int pool_add(MtTree *t, const char *s)
{
  if (s[0] == 0) return 0;
  int len = (int) strlen(s);
  if (t->used + len + 1 > MT_POOL) return -1;
  int at = t->used;
  memcpy(t->pool + at, s, (size_t) len + 1);
  t->used += len + 1;
  return at;
}

static int new_node(MtTree *t, int title, int image, int rec, int parent)
{
  if (t->n >= MT_MAX_NODES) { t->dropped++; return -1; }
  MtNode *n = &t->node[t->n];
  memset(n, 0, sizeof *n);
  n->title = (uint16_t) title; n->image = (uint16_t) image;
  n->rec = (int16_t) rec; n->parent = (int16_t) parent; n->first = -1; n->next = -1;
  if (parent >= 0) { n->next = t->node[parent].first; t->node[parent].first = (int16_t) t->n; }
  return t->n++;
}

/* Titles stop at MT_TITLE; the screen cannot show more. */
static int title_add(MtTree *t, const char *title)
{
  char b[MT_TITLE];
  sidecarCopy(b, MT_TITLE, title);
  return pool_add(t, b);
}

void mt_clear(MtTree *t, const char *title)
{
  t->n = 0; t->dropped = 0; t->used = 1; t->pool[0] = 0; t->favs = 0;
  new_node(t, title_add(t, title), 0, -1, -1);
}

static uint32_t key_hash(const char *k)
{
  uint32_t h = 2166136261u;
  for (; *k; k++) h = (h ^ (unsigned char) *k) * 16777619u;
  return h;
}

/* The favorites in the text: only "file =" lines count, wherever they are (sections and also lines are for people). */
void mt_favs_read(MtFavs *f, const char *text)
{
  f->n = f->used = f->full = 0;
  for (const char *l = text; l && *l; )
  {
    const char *end = strchr(l, '\n');
    const char *p = l;
    while (*p == ' ' || *p == '\t') p++;
    if (*p && *p != ';' && *p != '#' && strncasecmp(p, "file", 4) == 0)
    {
      const char *e = p + 4;
      while (*e == ' ' || *e == '\t') e++;
      if (*e == '=')
      {
        e++;
        while (*e == ' ' || *e == '\t') e++;
        const char *v = e;
        while (*e && *e != '\n' && *e != '\r')
        {
          if (*e == ';' && e > v && (e[-1] == ' ' || e[-1] == '\t')) break;   /* " ; comment" ends the value */
          e++;
        }
        int len = (int) (e - v);
        while (len > 0 && (v[len - 1] == ' ' || v[len - 1] == '\t')) len--;
        if (len > 0)
        {
          char path[NM_KEY_LEN], key[NM_KEY_LEN];
          int n = len < (int) sizeof path - 1 ? len : (int) sizeof path - 1;
          memcpy(path, v, (size_t) n); path[n] = 0;
          nmPathKey(path, key, sizeof key);
          if (mt_favs_has(f, path)) { l = end ? end + 1 : 0; continue; }
          int pl = (int) strlen(path) + 1;
          if (f->n >= MT_FAV_MAX || f->used + pl > MT_FAV_POOL) f->full = 1;
          else
          {
            memcpy(f->pool + f->used, path, (size_t) pl);
            f->hash[f->n] = key_hash(key);
            f->off[f->n++] = (uint16_t) f->used;
            f->used += pl;
          }
        }
      }
    }
    l = end ? end + 1 : 0;
  }
}

int mt_favs_has(const MtFavs *f, const char *path)
{
  if (f == 0) return 0;
  char key[NM_KEY_LEN];
  nmPathKey(path, key, sizeof key);
  uint32_t h = key_hash(key);
  for (int i = 0; i < f->n; i++)
  {
    if (f->hash[i] != h) continue;
    char k[NM_KEY_LEN];
    nmPathKey(mt_fav_path(f, i), k, sizeof k);
    if (strcmp(k, key) == 0) return 1;
  }
  return 0;
}

static const char FAV_HEADER[] =
  "; menu/favorites.ini: your favorites. Button 3 in the menu adds or removes a game; you can also edit this file.\n";

/* Copy n bytes to *p, which must leave room for the NUL at end = out + max. */
static int fav_put(char **p, char *end, const char *s, int n)
{
  if (n < 0 || (n > 0 && *p + n >= end)) return -1;
  memcpy(*p, s, (size_t) n);
  *p += n;
  return 0;
}

static int fav_puts(char **p, char *end, const char *s)
{
  return fav_put(p, end, s, (int) strlen(s));
}

/* Does s end with a blank line? "\n" and "abc\n\n" do; "abc\n" and "abc" do not. */
static int fav_ends_blank(const char *s)
{
  int n = (int) strlen(s);
  if (n == 0 || s[n - 1] != '\n') return 0;
  int k = n - 1, p = k - 1;
  while (p >= 0 && s[p] != '\n') p--;
  for (int i = p + 1; i < k; i++) if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r') return 0;
  return 1;
}

/* Remove the one blank line that ends out, if there is one; *len is the new length. */
static void fav_drop_blank(char *out, int *len)
{
  int n = *len;
  if (n < 1 || out[n - 1] != '\n') return;
  int k = n - 1, p = k - 1;
  while (p >= 0 && out[p] != '\n') p--;
  for (int i = p + 1; i < k; i++) if (out[i] != ' ' && out[i] != '\t' && out[i] != '\r') return;
  *len = p + 1;
}

/* If l is a "file = value" line, *v is its value and *len its length with trailing blanks removed. */
static int fav_file_value(const char *l, const char **v, int *len)
{
  const char *p = l;
  while (*p == ' ' || *p == '\t') p++;
  if (*p == 0 || *p == ';' || *p == '#' || strncasecmp(p, "file", 4) != 0) return 0;
  const char *e = p + 4;
  while (*e == ' ' || *e == '\t') e++;
  if (*e != '=') return 0;
  e++;
  while (*e == ' ' || *e == '\t') e++;
  const char *start = e;
  while (*e && *e != '\n' && *e != '\r')
  {
    if (*e == ';' && e > start && (e[-1] == ' ' || e[-1] == '\t')) break;
    e++;
  }
  int n = (int) (e - start);
  while (n > 0 && (start[n - 1] == ' ' || start[n - 1] == '\t')) n--;
  *v = start;
  *len = n;
  return n > 0;
}

/* The nmPathKey of a file value, cut to the same NM_KEY_LEN-byte path mt_favs_read stores, so the two agree. */
static void fav_value_key(const char *v, int len, char *key)
{
  char path[NM_KEY_LEN];
  int n = len < (int) sizeof path - 1 ? len : (int) sizeof path - 1;
  memcpy(path, v, (size_t) n);
  path[n] = 0;
  nmPathKey(path, key, NM_KEY_LEN);
}

/* Does any file = line in text have this key? Unlike mt_favs_read this does not stop at MT_FAV_MAX. */
static int fav_text_has(const char *text, const char *key)
{
  for (const char *l = text; l && *l; )
  {
    const char *v;
    int len;
    if (fav_file_value(l, &v, &len))
    {
      char k[NM_KEY_LEN];
      fav_value_key(v, len, k);
      if (strcmp(k, key) == 0) return 1;
    }
    const char *end = strchr(l, '\n');
    l = end ? end + 1 : 0;
  }
  return 0;
}

static const char *fav_line_end(const char *l)
{
  while (*l && *l != '\n') l++;
  return *l == '\n' ? l + 1 : l;
}

static int fav_is_section(const char *l)
{
  while (*l == ' ' || *l == '\t') l++;
  return *l == '[';
}

/* The start of the next section line after the one l is on, or the NUL at the end. */
static const char *fav_next_section(const char *l)
{
  const char *p = fav_line_end(l);
  while (*p)
  {
    if (fav_is_section(p)) return p;
    p = fav_line_end(p);
  }
  return p;
}

/* Does the section [s, n) have a file = line with this key? */
static int fav_section_has(const char *s, const char *n, const char *key)
{
  for (const char *l = s; l < n && *l; )
  {
    const char *v;
    int len;
    if (fav_file_value(l, &v, &len))
    {
      char k[NM_KEY_LEN];
      fav_value_key(v, len, k);
      if (strcmp(k, key) == 0) return 1;
    }
    const char *next = fav_line_end(l);
    l = next < n ? next : n;
  }
  return 0;
}

/* The end of the section's own body: trailing blank lines and comments before the next section stay (they read
 * as notes for the section that follows), so only the lines that have content are removed. */
static const char *fav_body_end(const char *s, const char *n)
{
  const char *be = s;
  for (const char *l = s; l < n && *l; )
  {
    const char *nl = strchr(l, '\n');
    const char *stop = (nl && nl < n) ? nl : n;
    const char *q = l;
    while (q < stop && (*q == ' ' || *q == '\t' || *q == '\r')) q++;
    int blank = q == stop;
    int comment = !blank && (*q == ';' || *q == '#');
    const char *next = (nl && nl < n) ? nl + 1 : n;
    if (!blank && !comment) be = next;
    l = next;
  }
  return be;
}

static int fav_add_section(char **p, char *end, const char *title, const char *path)
{
  if (fav_put(p, end, "[", 1)) return -1;
  for (const char *t = title; *t; t++)
  {
    char c = *t == '[' ? '(' : *t == ']' ? ')' : *t;
    if (fav_put(p, end, &c, 1)) return -1;
  }
  if (fav_puts(p, end, "]\nfile = ")) return -1;
  if (fav_puts(p, end, path)) return -1;
  return fav_puts(p, end, "\nalso = FAVORITES\n");
}

static int fav_add(const char *text, const char *title, const char *path, char *out, int max)
{
  if (max <= 0) return -1;
  char *p = out, *end = out + max;
  if (fav_puts(&p, end, *text ? text : FAV_HEADER)) return -1;
  *p = 0;
  if (!fav_ends_blank(out))
  {
    if (p == out || p[-1] != '\n') { if (fav_put(&p, end, "\n", 1)) return -1; }
    if (fav_put(&p, end, "\n", 1)) return -1;
  }
  if (fav_add_section(&p, end, title, path)) return -1;
  if (p >= end) return -1;
  *p = 0;
  return (int) (p - out);
}

static int fav_remove(const char *text, const char *key, char *out, int max)
{
  if (max <= 0) return -1;
  char *p = out, *end = out + max;
  int len = 0;
  for (const char *l = text; *l; )
  {
    if (fav_is_section(l))
    {
      const char *n = fav_next_section(l);
      if (fav_section_has(l, n, key))
      {
        fav_drop_blank(out, &len);
        p = out + len;
        l = fav_body_end(l, n);
        continue;
      }
    }
    const char *next = fav_line_end(l);
    if (fav_put(&p, end, l, (int) (next - l))) return -1;
    len = (int) (p - out);
    l = next;
  }
  if (p >= end) return -1;
  *p = 0;
  return (int) (p - out);
}

int mt_fav_toggle(const char *text, const char *title, const char *path, char *out, int max, int *added)
{
  char key[NM_KEY_LEN];
  nmPathKey(path, key, sizeof key);
  if (fav_text_has(text, key))
  {
    int n = fav_remove(text, key, out, max);
    if (n >= 0 && added) *added = 0;
    return n;
  }
  MtFavs f;
  mt_favs_read(&f, text);
  if (f.full || f.n >= MT_FAV_MAX || f.used + (int) strlen(path) + 1 > MT_FAV_POOL) return -2;
  int n = fav_add(text, title, path, out, max);
  if (n >= 0 && added) *added = 1;
  return n;
}

int mt_new_entry(MtTree *t, const char *title, const char *image, int parent)
{
  int tt = title_add(t, title), im = pool_add(t, image);
  if (tt < 0 || im < 0) { t->dropped++; return -1; }
  return new_node(t, tt, im, -1, parent);
}

static int eq_nc(const char *a, const char *b)
{
  for (; *a && *b; a++, b++)
  {
    char x = *a, y = *b;
    if (x >= 'a' && x <= 'z') x -= 32;
    if (y >= 'a' && y <= 'z') y -= 32;
    if (x != y) return 0;
  }
  return *a == 0 && *b == 0;
}

/* "/" (spaces around it ignored) is the top level itself, not a folder called "/". */
static int top_level(const char *cat)
{
  while (*cat == ' ') cat++;
  if (*cat != '/') return 0;
  cat++;
  while (*cat == ' ') cat++;
  return *cat == 0;
}

/* "ARCADE / ATARI": the category node, made if missing. */
static int category(MtTree *t, const char *path)
{
  int at = 0;
  const char *p = path;
  while (*p)
  {
    char seg[MT_TITLE];
    int n = 0;
    while (*p == ' ') p++;
    while (*p && *p != '/') { if (n < MT_TITLE - 1) seg[n++] = *p; p++; }
    while (n > 0 && seg[n - 1] == ' ') n--;
    seg[n] = 0;
    if (*p == '/') p++;
    if (n == 0) continue;
    int f = -1;
    for (int c = t->node[at].first; c >= 0; c = t->node[c].next)
      if (mt_is_category(t, c) && eq_nc(mt_title(t, c), seg)) { f = c; break; }
    if (f < 0)
    {
      int tt = pool_add(t, seg);
      if (tt < 0) { t->dropped++; return at; }
      f = new_node(t, tt, 0, -1, at);
    }
    if (f < 0) return at;
    at = f;
  }
  return at;
}

/* title and image are pool offsets: every list a game is in shares the one copy. */
/* The node, or -1 if the list is full or already has this game. */
static int add_to(MtTree *t, const char *cat, int title, int image, int rec, int seq)
{
  int at = top_level(cat) ? 0 : category(t, cat);
  for (int c = t->node[at].first; c >= 0; c = t->node[c].next)
    if (eq_nc(mt_image(t, c), t->pool + image)) return -1;   /* the same game twice in one list */
  int n = new_node(t, title, image, rec, at);
  if (n >= 0) t->node[n].seq = (int16_t) seq;
  return n;
}

/* A favorite is marked everywhere but in the FAVORITES folder at the top, where the mark would say nothing. */
static void mark_fav(MtTree *t, int n)
{
  if (n < 0) return;
  int p = t->node[n].parent;
  if (p >= 0 && t->node[p].parent == 0 && eq_nc(mt_title(t, p), MT_FAV_FOLDER)) return;
  t->node[n].fav = 1;
}

/* path: the card path (or @fw/<name>) the game came from, for the favorites; 0 when it cannot be a favorite. */
static void add_everywhere(MtTree *t, const char *path, const char *cat, const char *also, const char *title, const char *image, int rec, int seq)
{
  int tt = title_add(t, title), im = pool_add(t, image);
  if (tt < 0 || im < 0) { t->dropped++; return; }
  int fav = path != 0 && mt_favs_has(t->favs, path);
  int n0 = add_to(t, cat[0] ? cat : "OTHER", tt, im, rec, seq);
  if (fav) mark_fav(t, n0);
  const char *p = also;
  while (*p)
  {
    char one[56];
    int n = 0;
    while (*p == ' ' || *p == ',') p++;
    while (*p && *p != ',') { if (n < 55) one[n++] = *p; p++; }
    while (n > 0 && one[n - 1] == ' ') n--;
    one[n] = 0;
    if (n) { int c = add_to(t, one, tt, im, rec, seq); if (fav) mark_fav(t, c); }
  }
  /* The same title, image and record as its other copies; add_to refuses a second copy of one game in one list. */
  if (fav) add_to(t, MT_FAV_FOLDER, tt, im, rec, seq);
}

/* star_wars_v2.um2 -> STAR WARS V2 (the font has no _ and no lower case) */
static void file_title(const char *name, char *out, int max)
{
  int n = 0;
  const char *dot = strrchr(name, '.');
  for (const char *s = name; *s && s != dot && n < max - 1; s++)
  {
    char c = *s;
    if (c == '_') c = ' ';
    if (c >= 'a' && c <= 'z') c -= 32;
    out[n++] = c;
  }
  out[n] = 0;
}

static int is_self(const char *name) { return eq_nc(name, "autostart.um2") || eq_nc(name, "MENU.um2"); }

/* A favorite that nothing else lists: favorites.ini alone puts it in FAVORITES. A file that is not on the card is
 * skipped (it may be on another card); @fw/... favorites come through the built-in list instead. */
static void add_standalone_favs(MtTree *t, const MtCard *c)
{
  if (t->favs == 0) return;
  for (int i = 0; i < t->favs->n; i++)
  {
    const char *path = mt_fav_path(t->favs, i);
    if (strncasecmp(path, "@fw/", 4) == 0) continue;
    if (!c->exists(path, c->u)) continue;
    char key[NM_KEY_LEN];
    nmPathKey(path, key, sizeof key);
    int fav = -1, listed = 0;
    for (int f = t->node[0].first; f >= 0; f = t->node[f].next)
      if (mt_is_category(t, f) && eq_nc(mt_title(t, f), MT_FAV_FOLDER)) { fav = f; break; }
    if (fav >= 0)
      for (int e = t->node[fav].first; e >= 0; e = t->node[e].next)
      {
        char k[NM_KEY_LEN];
        nmPathKey(mt_image(t, e), k, sizeof k);
        if (strcmp(k, key) == 0) { listed = 1; break; }
      }
    if (listed) continue;
    const char *base = strrchr(path, '/');
    char title[MT_TITLE];
    file_title(base ? base + 1 : path, title, sizeof title);
    int tt = title_add(t, title), im = pool_add(t, path);
    if (tt < 0 || im < 0) { t->dropped++; continue; }
    int n = add_to(t, MT_FAV_FOLDER, tt, im, -1, 0);
    if (n >= 0) t->node[n].fav = 1;   /* it is listed nowhere else, so the mark is what says it is a favorite */
  }
}

typedef struct { MtTree *t; NmRecs *r; const NmRec *rule; int rule_i; } FolderCtx;

static void folder_add(const char *name, void *vc)
{
  FolderCtx *c = vc;
  if (!nmExtOk(name, nmStr(c->r, c->rule->files)) || is_self(name)) return;
  const char *dir = nmStr(c->r, c->rule->path);
  char path[192], key[NM_KEY_LEN], title[MT_TITLE], image[160];
  if (strcmp(dir, ".") == 0 || dir[0] == 0) snprintf(path, sizeof path, "%s", name);
  else snprintf(path, sizeof path, "%s/%s", dir, name);
  nmSubst(image, sizeof image, nmStr(c->r, c->rule->image), dir, name);
  const char *im = image;
  if (im[0] == '.' && im[1] == '/') im += 2;
  nmPathKey(path, key, sizeof key);
  const NmRec *f = nmFindRec(c->r, key);
  if (f && f->hide) return;
  file_title(name, title, sizeof title);
  const char *cat = (f && nmStr(c->r, f->category)[0]) ? nmStr(c->r, f->category) : nmStr(c->r, c->rule->category);
  const char *also = f ? nmStr(c->r, f->also) : nmStr(c->r, c->rule->also);
  const char *tt = (f && nmStr(c->r, f->title)[0]) ? nmStr(c->r, f->title) : title;
  add_everywhere(c->t, path, cat, also, tt, im, f ? (int) (f - c->r->rec) : -1, f ? f->seq : 0);
}

static void apply_categories(MtTree *t, NmRecs *r, int at, const char *path)
{
  for (int c = t->node[at].first; c >= 0; c = t->node[c].next)
    if (mt_is_category(t, c))
    {
      char sub[128], key[NM_KEY_LEN];
      if (path[0]) snprintf(sub, sizeof sub, "%s / %s", path, mt_title(t, c));
      else snprintf(sub, sizeof sub, "%s", mt_title(t, c));
      nmSlugKey("cat.", sub, key, sizeof key);
      const NmRec *rec = nmFindRec(r, key);
      if (rec)
      {
        t->node[c].rec = (int16_t) (rec - r->rec);
        t->node[c].listed = rec->order;
        if (rec->hide) t->node[c].first = -1;     /* emptied: dropped below */
      }
      apply_categories(t, r, c, sub);
    }
}

/* Takes out categories with nothing in them. Returns 1 if the node stays. */
static int prune(MtTree *t, int at)
{
  int16_t *link = &t->node[at].first;
  while (*link >= 0)
  {
    int c = *link;
    if (mt_is_category(t, c) && !prune(t, c)) *link = t->node[c].next;
    else link = &t->node[c].next;
  }
  return t->node[at].first >= 0;
}

/* At the top level folders (FAVORITES too) and entries go together by title, and SYSTEM last.
 * Inside a folder, categories first, then by title; a listed category keeps the order of its sections. */
static int before(const MtTree *t, int parent, int a, int b)
{
  const MtNode *x = &t->node[a], *y = &t->node[b];
  int xs = eq_nc(t->pool + x->title, "SYSTEM"), ys = eq_nc(t->pool + y->title, "SYSTEM");
  if (xs != ys) return ys;
  if (t->node[parent].listed) return x->seq < y->seq;
  int xc = mt_is_category(t, a), yc = mt_is_category(t, b);
  if (xc != yc && parent != 0) return xc;
  const char *p = t->pool + x->title, *q = t->pool + y->title;
  for (; *p && *q; p++, q++)
  {
    char m = *p, n = *q;
    if (m >= 'a' && m <= 'z') m -= 32;
    if (n >= 'a' && n <= 'z') n -= 32;
    if (m != n) return m < n;
  }
  return *q != 0;
}

static void sort_all(MtTree *t, int at)
{
  int16_t head = -1;
  for (int c = t->node[at].first; c >= 0;)
  {
    int nx = t->node[c].next;
    int16_t *link = &head;
    while (*link >= 0 && !before(t, at, c, *link)) link = &t->node[*link].next;
    t->node[c].next = *link;
    *link = (int16_t) c;
    c = nx;
  }
  t->node[at].first = head;
  for (int c = head; c >= 0; c = t->node[c].next)
    if (mt_is_category(t, c)) sort_all(t, c);
}

void mt_add(MtTree *t, const char *cat, const char *also, const char *title, const char *image)
{
  add_everywhere(t, 0, cat, also, title, image, -1, 0);
}

int mt_add_builtin(MtTree *t, NmRecs *r, const char *fwcat, const char *name, const char *image)
{
  char path[96], key[NM_KEY_LEN];
  snprintf(path, sizeof path, "@fw/%s", name);
  nmPathKey(path, key, sizeof key);
  const NmRec *f = nmFindRec(r, key);
  if (f && f->hide) return 0;
  nmSlugKey("f.", "@fw", key, sizeof key);
  const NmRec *rule = nmFindRec(r, key);
  if (rule && rule->kind != NM_REC_FOLDER) rule = 0;
  const char *cat, *also = rule ? nmStr(r, rule->also) : "";
  if (fwcat) cat = fwcat;
  else cat = (rule && nmStr(r, rule->category)[0]) ? nmStr(r, rule->category) : MT_BUILTIN_OTHER;
  if (f && nmStr(r, f->category)[0]) cat = nmStr(r, f->category);
  if (f && nmStr(r, f->also)[0]) also = nmStr(r, f->also);
  add_everywhere(t, path, cat, also, (f && nmStr(r, f->title)[0]) ? nmStr(r, f->title) : name, image, f ? (int) (f - r->rec) : -1, f ? f->seq : 0);
  return 1;
}

void mt_build(MtTree *t, NmRecs *r, const MtCard *card)
{
  mt_clear(t, "UVMC2 MENU");
  t->favs = card->favs;
  for (int i = 0; i < r->n; i++)
  {
    const NmRec *e = &r->rec[i];
    if (e->hide) continue;
    if (e->kind == NM_REC_ENTRY)
    {
      const char *needs = nmStr(r, e->needs);
      const char *image = nmStr(r, e->image);
      const char *title = nmStr(r, e->title);
      if (needs[0] && !card->exists(needs, card->u)) continue;
      if (image[0] != '@' && !card->exists(image, card->u)) continue;
      add_everywhere(t, image[0] == '@' ? 0 : image, nmStr(r, e->category), nmStr(r, e->also),
                     title[0] ? title : nmStr(r, e->name), image, i, e->seq);
    }
    else if (e->kind == NM_REC_FOLDER && nmStr(r, e->path)[0] != '@')   /* folder = @fw is the built-ins' rule */
    {
      FolderCtx c = { t, r, e, i };
      card->list(nmStr(r, e->path), folder_add, &c, card->u);
    }
  }
  if (card->extra) card->extra(t, card->u);
  add_standalone_favs(t, card);
  apply_categories(t, r, 0, "");
  prune(t, 0);
  sort_all(t, 0);
  t->favs = 0;                   /* only mt_build may make a favorite */
}

/* A folder as the rules and the card spell it: no case, no leading "./", no trailing "/"; "." is the root. */
static void norm_dir(const char *in, char *out, int max)
{
  while (in[0] == '.' && in[1] == '/') in += 2;
  int n = 0;
  for (; *in && n < max - 1; in++, n++) out[n] = (*in >= 'A' && *in <= 'Z') ? (char) (*in + 32) : *in;
  out[n] = 0;
  while (n > 0 && out[n - 1] == '/') out[--n] = 0;
  if (n == 1 && out[0] == '.') out[0] = 0;
}

int mt_file_hidden(NmRecs *r, const char *path)
{
  char key[NM_KEY_LEN], k2[NM_KEY_LEN];
  nmPathKey(path, key, sizeof key);
  const NmRec *f = nmFindRec(r, key);
  if (f && f->kind == NM_REC_FILE && f->hide) return 1;
  for (int i = 0; i < r->n; i++)
  {
    const NmRec *e = &r->rec[i];
    if (e->kind != NM_REC_ENTRY || !e->hide || nmStr(r, e->image)[0] == '@') continue;
    nmPathKey(nmStr(r, e->image), k2, sizeof k2);
    if (strcmp(k2, key) == 0) return 1;
  }
  return 0;
}

int mt_file_mapped(NmRecs *r, const char *path)
{
  const char *slash = strrchr(path, '/');
  const char *name = slash ? slash + 1 : path;
  if (is_self(name)) return 1;
  char key[NM_KEY_LEN], k2[NM_KEY_LEN];
  nmPathKey(path, key, sizeof key);
  const NmRec *f = nmFindRec(r, key);
  if (f && f->kind == NM_REC_FILE && f->hide) return 1;      /* hidden by a file = section: a decision about it */
  for (int i = 0; i < r->n; i++)
  {
    const NmRec *e = &r->rec[i];
    if (e->kind != NM_REC_ENTRY || nmStr(r, e->image)[0] == '@') continue;
    nmPathKey(nmStr(r, e->image), k2, sizeof k2);
    if (strcmp(k2, key) == 0) return 1;
  }
  char dir[96], rdir[96];
  snprintf(dir, sizeof dir, "%.*s", slash ? (int) (slash - path) : 0, path);
  norm_dir(dir, dir, sizeof dir);
  for (int i = 0; i < r->n; i++)
  {
    const NmRec *e = &r->rec[i];
    if (e->hide || e->kind != NM_REC_FOLDER || nmStr(r, e->path)[0] == '@') continue;   /* a hidden rule lists nothing */
    norm_dir(nmStr(r, e->path), rdir, sizeof rdir);
    if (strcmp(dir, rdir) == 0 && nmExtOk(name, nmStr(r, e->files))) return 1;
  }
  return 0;
}

int mt_layer_sort(char (*names)[MT_LAYER_NAME], int n)
{
  int k = 0;
  for (int i = 0; i < n; i++)
    if (nmExtOk(names[i], "ini") && !eq_nc(names[i], "menu.default.ini") && !eq_nc(names[i], "menu.ini") && !eq_nc(names[i], MT_SETTINGS) && !eq_nc(names[i], MT_FAVORITES))
    {
      if (k != i) memcpy(names[k], names[i], MT_LAYER_NAME);
      k++;
    }
  for (int i = 1; i < k; i++)
    for (int j = i; j > 0 && strcasecmp(names[j - 1], names[j]) > 0; j--)
    {
      char x[MT_LAYER_NAME];
      memcpy(x, names[j], MT_LAYER_NAME); memcpy(names[j], names[j - 1], MT_LAYER_NAME); memcpy(names[j - 1], x, MT_LAYER_NAME);
    }
  return k;
}

int mt_wrap_next(const char **s, char *out, int width)
{
  const char *p = *s;
  while (*p == ' ') p++;
  if (*p == 0) { *s = p; return 0; }
  int n = 0, brk = 0;
  while (p[n] && n < width) { if (p[n] == ' ') brk = n; n++; }
  if (p[n] == ' ' || p[n] == 0) brk = n;     /* it ends exactly at a word */
  if (brk == 0) brk = n;                     /* one long word: cut it */
  memcpy(out, p, (size_t) brk);
  out[brk] = 0;
  *s = p + brk;
  return 1;
}

void mt_notes_start(MtNotes *m, int max)
{
  m->n = 0; m->more = 0;
  m->max = max < 0 ? 0 : max > MT_NOTE_LINES ? MT_NOTE_LINES : max;
}

void mt_notes_add(MtNotes *m, const char *s)
{
  char l[MT_NOTE_WIDTH + 1];
  while (mt_wrap_next(&s, l, MT_NOTE_WIDTH))
  {
    if (m->n >= m->max) { m->more = 1; return; }
    memcpy(m->line[m->n++], l, sizeof l);
  }
}

void mt_notes_end(MtNotes *m)
{
  if (!m->more || m->n == 0) return;
  char *l = m->line[m->n - 1];
  int n = (int) strlen(l);
  if (n > MT_NOTE_WIDTH - 3) n = MT_NOTE_WIDTH - 3;
  while (n > 0 && l[n - 1] == ' ') n--;
  memcpy(l + n, "...", 4);
}

void mt_hint(char *out, int max, int is_folder, int can_back, int can_fav, int has_more)
{
  snprintf(out, (size_t) max, "%s: 4%s%s%s", is_folder ? "OPEN" : "START", can_back ? "   BACK: 1" : "",
           can_fav ? "   FAVORITE: 3" : "", has_more ? "   MORE: 2" : "");
}

void mt_nav_init(MtNav *n) { n->dir = 0; n->cursor = 0; n->top = 0; }

int mt_count(const MtTree *t, int dir)
{
  int k = 0;
  for (int c = t->node[dir].first; c >= 0; c = t->node[c].next) k++;
  return k;
}

int mt_nth(const MtTree *t, int dir, int i)
{
  for (int c = t->node[dir].first; c >= 0; c = t->node[c].next) if (i-- == 0) return c;
  return -1;
}

static void fix_window(MtNav *n)
{
  if (n->cursor < n->top) n->top = n->cursor;
  if (n->cursor >= n->top + MT_ROWS) n->top = n->cursor - MT_ROWS + 1;
}

int mt_more_above(const MtNav *n) { return n->top > 0; }
int mt_more_below(const MtNav *n, const MtTree *t) { return n->top + MT_ROWS < mt_count(t, n->dir); }

void mt_move(MtNav *n, const MtTree *t, int d)
{
  int k = mt_count(t, n->dir);
  if (k == 0) return;
  n->cursor = (n->cursor + d + k) % k;
  fix_window(n);
}

int mt_enter(MtNav *n, const MtTree *t)
{
  int c = mt_nth(t, n->dir, n->cursor);
  if (c < 0 || !mt_is_category(t, c)) return 0;
  n->dir = c; n->cursor = 0; n->top = 0;
  return 1;
}

int mt_back(MtNav *n, const MtTree *t)
{
  int p = t->node[n->dir].parent;
  if (p < 0) return 0;
  int from = n->dir, i = 0;
  for (int c = t->node[p].first; c >= 0; c = t->node[c].next, i++) if (c == from) break;
  n->dir = p; n->cursor = i; n->top = 0;
  fix_window(n);
  return 1;
}

/* Would a category called title come before child c in its parent's list, as before() orders it? */
static int title_before(const MtTree *t, int parent, const char *title, int c)
{
  int ts = eq_nc(title, "SYSTEM"), cs = eq_nc(mt_title(t, c), "SYSTEM");
  if (ts != cs) return !ts;                 /* SYSTEM goes last */
  if (parent != 0 && !mt_is_category(t, c)) return 1;   /* in a folder, categories come before games */
  const char *p = title, *q = mt_title(t, c);
  for (; *p && *q; p++, q++)
  {
    char m = *p, n = *q;
    if (m >= 'a' && m <= 'z') m -= 32;
    if (n >= 'a' && n <= 'z') n -= 32;
    if (m != n) return m < n;
  }
  return *p == 0 && *q != 0;                /* the shorter title comes first */
}

/* The row a folder that is gone would have had in parent's list now: the first child it sorts before.
 * A listed parent is ordered by seq, which the missing folder's record no longer supplies, so the saved cursor
 * is the best guess there. */
static int folder_position(const MtTree *t, int parent, const char *title, int saved)
{
  if (t->node[parent].listed)
  {
    int k = mt_count(t, parent);
    if (saved >= k) saved = k - 1;
    return saved < 0 ? 0 : saved;
  }
  int i = 0;
  for (int c = t->node[parent].first; c >= 0; c = t->node[c].next, i++)
    if (title_before(t, parent, title, c)) return i;
  return i;                                 /* nothing sorts after it: the end */
}

void mt_place_save(MtPlace *p, const MtNav *n, const MtTree *t)
{
  int total = 0;
  for (int at = n->dir; at > 0; at = t->node[at].parent) total++;
  p->depth = total < MT_PLACE_DEPTH ? total : MT_PLACE_DEPTH;
  int at = n->dir;
  for (int k = total - p->depth; k > 0; k--) at = t->node[at].parent;
  for (int i = p->depth - 1; i >= 0; i--)
  {
    sidecarCopy(p->dir[i], MT_TITLE, mt_title(t, at));
    at = t->node[at].parent;
  }
  int c = mt_nth(t, n->dir, n->cursor);
  if (c >= 0) sidecarCopy(p->image, sizeof p->image, mt_image(t, c));
  else p->image[0] = 0;
  p->cursor = n->cursor;
}

void mt_place_restore(MtNav *n, const MtTree *t, const MtPlace *p)
{
  int at = 0;
  for (int d = 0; d < p->depth; d++)
  {
    int found = -1;
    for (int c = t->node[at].first; c >= 0; c = t->node[c].next)
      if (mt_is_category(t, c) && eq_nc(mt_title(t, c), p->dir[d])) { found = c; break; }
    if (found < 0)
    {
      /* The folder is gone: stay where it was on the level above, on the row it sorted at. */
      int k = mt_count(t, at);
      n->dir = at;
      n->cursor = folder_position(t, at, p->dir[d], p->cursor);
      if (n->cursor >= k) n->cursor = k - 1;
      if (n->cursor < 0) n->cursor = 0;
      n->top = 0;
      fix_window(n);
      return;
    }
    at = found;
  }
  int k = mt_count(t, at), cursor = -1;
  if (p->image[0])
    for (int i = 0; i < k; i++)
    {
      int c = mt_nth(t, at, i);
      if (c >= 0 && strcmp(mt_image(t, c), p->image) == 0) { cursor = i; break; }
    }
  if (cursor < 0)
  {
    cursor = p->cursor < k ? p->cursor : k - 1;
    if (cursor < 0) cursor = 0;
  }
  n->dir = at;
  n->cursor = cursor;
  n->top = 0;
  fix_window(n);
}

int mt_setting_on(const char *text, const char *key, int dflt)
{
  int klen = (int) strlen(key);
  for (const char *l = text; l && *l; )
  {
    while (*l == ' ' || *l == '\t') l++;
    const char *end = strchr(l, '\n');
    if (strncasecmp(l, key, (size_t) klen) == 0)
    {
      const char *e = l + klen;
      while (*e == ' ' || *e == '\t') e++;
      if (*e == '=')
      {
        e++;
        while (*e == ' ' || *e == '\t') e++;
        char v[8];
        int n = 0;
        while (*e && *e != ';' && *e != '\n' && *e != '\r' && *e != ' ' && *e != '\t' && n < 7) v[n++] = *e++;
        v[n] = 0;
        if (!strcasecmp(v, "on") || !strcasecmp(v, "yes") || !strcmp(v, "1")) return 1;
        if (!strcasecmp(v, "off") || !strcasecmp(v, "no") || !strcmp(v, "0")) return 0;
        return dflt;
      }
    }
    l = end ? end + 1 : 0;
  }
  return dflt;
}

/* The value of key's line, as mt_setting_on reads it: where it starts and ends, or 0 if the key is not there. */
static const char *setting_value(const char *text, const char *key, const char **vend)
{
  int klen = (int) strlen(key);
  for (const char *l = text; l && *l; )
  {
    while (*l == ' ' || *l == '\t') l++;
    const char *end = strchr(l, '\n');
    if (strncasecmp(l, key, (size_t) klen) == 0)
    {
      const char *e = l + klen;
      while (*e == ' ' || *e == '\t') e++;
      if (*e == '=')
      {
        e++;
        while (*e == ' ' || *e == '\t') e++;
        const char *v = e;
        while (*e && *e != ';' && *e != '\n' && *e != '\r' && *e != ' ' && *e != '\t') e++;
        *vend = e;
        return v;
      }
    }
    l = end ? end + 1 : 0;
  }
  return 0;
}

int mt_setting_int(const char *text, const char *key, int dflt)
{
  const char *vend = 0, *v = setting_value(text, key, &vend);
  if (!v || v == vend) return dflt;
  int n = 0;
  for (const char *p = v; p < vend; p++)
  {
    if (*p < '0' || *p > '9' || n > 100000) return dflt;
    n = n * 10 + (*p - '0');
  }
  return n;
}

int mt_setting_set(const char *text, const char *key, const char *value, char *out, int max)
{
  const char *vend = 0, *v = setting_value(text, key, &vend);
  int n = v ? snprintf(out, (size_t) max, "%.*s%s%s", (int) (v - text), text, value, vend)
            : snprintf(out, (size_t) max, "%s%s%s = %s\n", text,
                       text[0] && text[strlen(text) - 1] != '\n' ? "\n" : "", key, value);
  return n < 0 || n >= max ? -1 : n;
}
