// The menu files, read in layers: menu/menu.default.ini (shipped), then every other menu/*.ini in alphabetical order,
// then menu/menu.ini (the user's changes, last so it has the final word). Read into records in memory; the menu
// asks for a record by key.
// No hardware dependencies, so it can be unit tested on a PC (new_menu/tools/test_new_menu.c).
//
// One section is one record. The section name is the title shown in the menu, unless "title =" says otherwise.
// What the section is follows from one key:
//   image  = ...   an entry that starts a program                     key "e.<section>"
//   file   = ...   the description of a file the menu found           key from the path (nmPathKey)
//   folder = ...   with image: every file in the folder is an entry   key "f.<folder>"
//                  without image: settings of a ROM folder group      key "dir.<folder under vectrex/>"
//   none           settings of a category ("ARCADE / ATARI")          key "cat.<path>"
// A section called [entry] is the 0.1.0 format (one file per entry): its key is the file name, as before.
// A later section with the same section name, file or folder changes the earlier one: what it sets replaces what
// was there, the rest stays. A later section with only a name and changes (no image, file or folder) changes the
// earlier section of that name, whatever it was. "also = A, B" lists a copy in further categories.
// Format and examples: NEW_MENU_README.md, new_menu/menu/11-menu-ini-plan.md
#ifndef MENU_FILE_H
#define MENU_FILE_H

#include <stdint.h>
#include <string.h>
#include "menuSidecar.h"

#ifndef NM_KEY_LEN
#define NM_KEY_LEN 64
#endif
#ifndef NM_MAX_RECS
#define NM_MAX_RECS 400
#endif
#ifndef NM_POOL
#define NM_POOL 65536
#endif
#define NM_REC_PATH 96

#define NM_REC_ENTRY 1
#define NM_REC_FILE 2
#define NM_REC_FOLDER 3
#define NM_REC_CATEGORY 4
#define NM_REC_NOTES 5                 // 0.1.0 notes file (no image): matched by its file name

typedef struct
{
  uint16_t key, path, files, title, image, category, needs, also, name;   /* positions in the pool */
  uint16_t seq;                        // the last time a section mentioned it (order = listed sorts by this)
  uint16_t rows;                       // where the first note line starts in the pool (lines follow, 0-separated)
  uint16_t ctl;                        // where the first control line starts in the pool
  char kind, hide, order, nrows, nctl;
} NmRec;
_Static_assert(sizeof(NmRec) <= 32, "NmRec stays 32 bytes or less: 1024 of them");

typedef struct
{
  NmRec rec[NM_MAX_RECS];
  int n;
  char pool[NM_POOL];
  int used;
  int dropped;                         // records and text that did not fit
  int seqNext;
} NmRecs;

/* A text field: a position in the pool in 2-byte units. Position 0 is a shared empty string. */
static inline const char *nmStr(const NmRecs *r, uint16_t at)
{
  return r->pool + (int) at * 2;
}

// ---- keys --------------------------------------------------------------------------------------------------

// "rom." + slug: lower case, every run of other characters one '_' (the 0.1.0 notes-file names)
static void nmSlugKey(const char *pre, const char *s, char *out, int max)
{
  int n = 0;
  for (int i = 0; pre[i] != 0 && n < max - 1; i++) out[n++] = pre[i];
  int start = n;
  for (; *s != 0 && n < max - 1; s++)
  {
    char c = *s;
    if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[n++] = c;
    else if (n > start && out[n - 1] != '_') out[n++] = '_';
  }
  if (n > start && out[n - 1] == '_') n--;
  out[n] = 0;
}

static int nmStartsNoCase(const char *s, const char *pre)
{
  for (; *pre != 0; s++, pre++)
  {
    char a = *s, b = *pre;
    if (a >= 'A' && a <= 'Z') a = (char) (a + 32);
    if (b >= 'A' && b <= 'Z') b = (char) (b + 32);
    if (a != b) return 0;
  }
  return 1;
}

// The key of a file on the card, the same one the 0.1.0 notes files used:
//   vectrex/original/BERZERK.BIN -> rom.original_berzerk_bin     piZero1/voom.img -> voom     other -> file.<slug>
static void nmPathKey(const char *path, char *out, int max)
{
  if (nmStartsNoCase(path, "vectrex/")) { nmSlugKey("rom.", path + 8, out, max); return; }
  if ((nmStartsNoCase(path, "piZero1/") || nmStartsNoCase(path, "piZero2/")))
  {
    const char *b = path + 8;
    int len = (int) strlen(b);
    if (len > 4 && nmStartsNoCase(b + len - 4, ".img") && len - 4 < max)
    {
      for (int i = 0; i < len - 4; i++) out[i] = (b[i] >= 'A' && b[i] <= 'Z') ? (char) (b[i] + 32) : b[i];
      out[len - 4] = 0;
      return;
    }
  }
  nmSlugKey("file.", path, out, max);
}

// ---- records -----------------------------------------------------------------------------------------------

static NmRec *nmFindRec(NmRecs *r, const char *key)
{
  for (int i = 0; i < r->n; i++)
    if (strcmp(nmStr(r, r->rec[i].key), key) == 0) return &r->rec[i];
  return 0;
}

static void nmFolderKey(const NmRecs *r, const char *path, const char *files, char *out, int max);

// The first record that came from a section with this name (not counting categories), or 0.
static const NmRec *nmFindRecByName(NmRecs *r, const char *name)
{
  for (int i = 0; i < r->n; i++)
  {
    const NmRec *c = &r->rec[i];
    if (c->kind == NM_REC_CATEGORY || nmStr(r, c->name)[0] == 0) continue;
    const char *a = nmStr(r, c->name), *b = name;
    while (*a != 0 && *b != 0)
    {
      char x = *a++, y = *b++;
      if (x >= 'a' && x <= 'z') x = (char) (x - 32);
      if (y >= 'a' && y <= 'z') y = (char) (y - 32);
      if (x != y) break;
      if (*a == 0 && *b == 0) return c;
    }
  }
  return 0;
}

// Appends a copy of s to the pool as is (lines follow each other); 0 means full.
static int nmPoolAppend(NmRecs *r, const char *s)
{
  int len = (int) strlen(s);
  if (r->used + len + 1 > NM_POOL) return 0;
  memcpy(r->pool + r->used, s, (size_t) len + 1);
  r->used += len + 1;
  return 1;
}

// Adds a field's copy of s to the pool; the position is in 2-byte units, so fields start on even bytes. 0 means full.
static uint16_t nmPoolAdd(NmRecs *r, const char *s)
{
  if (r->used < 2) r->used = 2;                 // byte 0 is the shared empty string
  if (r->used & 1) r->used++;                   // and every field starts on an even byte
  uint16_t at = (uint16_t) (r->used / 2);
  if (!nmPoolAppend(r, s)) return 0;
  return at;
}

// A text the section sets replaces the old one; a text that does not fit leaves the field alone and is counted.
static void nmSetStr(NmRecs *r, uint16_t *field, const char *s)
{
  uint16_t at = nmPoolAdd(r, s);
  if (at == 0) { r->dropped++; return; }
  *field = at;
}

// Where a list of note or control lines starts (even byte); the lines are added after it.
static uint16_t nmPoolMark(NmRecs *r)
{
  if (r->used < 2) r->used = 2;
  if (r->used & 1) r->used++;
  return (uint16_t) (r->used / 2);
}

// One section, collected while its lines are read.
typedef struct
{
  char name[64];
  char path[NM_REC_PATH];
  char isFile, isFolder, orderSet;
  char files[24];
  SidecarText st;
} NmSection;

static void nmTrimEnd(char *s)
{
  int n = (int) strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

// Stores the section that has been read. stem: the file name without ".ini" (for [entry] sections).
static void nmEndSection(NmRecs *r, NmSection *sec, const char *stem)
{
  if (sec->name[0] == 0) return;
  SidecarText *st = &sec->st;
  // A folder rule that names the extensions it lists (files =) and gives no image starts each file by its own path.
  if (sec->isFolder && sec->files[0] != 0 && st->image[0] == 0) sidecarCopy(st->image, (int) sizeof st->image, "{dir}/{file}");
  int legacy = nmStartsNoCase(sec->name, "entry") && sec->name[5] == 0;
  char key[NM_KEY_LEN];
  char kind;
  if (legacy)
  {
    kind = st->image[0] != 0 ? NM_REC_ENTRY : NM_REC_NOTES;
    sidecarCopy(key, NM_KEY_LEN, stem);
  }
  else if (st->image[0] != 0 && !sec->isFolder)
  {
    kind = NM_REC_ENTRY;
    nmSlugKey("e.", sec->name, key, NM_KEY_LEN);
  }
  else if (sec->isFile)
  {
    kind = NM_REC_FILE;
    nmPathKey(sec->path, key, NM_KEY_LEN);
  }
  else if (sec->isFolder && st->image[0] != 0)
  {
    kind = NM_REC_FOLDER;
    nmFolderKey(r, sec->path, sec->files, key, NM_KEY_LEN);
  }
  else if (sec->isFolder)
  {
    kind = NM_REC_NOTES;                    // a ROM folder group: renamed or hidden like a 0.1.0 dir.<folder>.ini
    nmSlugKey("dir.", nmStartsNoCase(sec->path, "vectrex/") ? sec->path + 8 : sec->path, key, NM_KEY_LEN);
  }
  else
  {
    const NmRec *same = nmFindRecByName(r, sec->name);   // no image, file or folder: changes what an earlier section
    if (same != 0) { kind = same->kind; sidecarCopy(key, NM_KEY_LEN, nmStr(r, same->key)); }   // of that name described ...
    else { kind = NM_REC_CATEGORY; nmSlugKey("cat.", sec->name, key, NM_KEY_LEN); }  // ... else it is a category
  }
  if (!legacy && st->title[0] == 0 && kind != NM_REC_CATEGORY && nmFindRec(r, key) == 0)
    sidecarCopy(st->title, (int) sizeof st->title, sec->name);

  NmRec *d = nmFindRec(r, key);
  if (d == 0)
  {
    if (r->n >= NM_MAX_RECS) { r->dropped++; return; }
    d = &r->rec[r->n++];
    memset(d, 0, sizeof *d);
    d->kind = kind;
    nmSetStr(r, &d->key, key);
    if (!legacy) nmSetStr(r, &d->name, sec->name);
  }
  else if (d->kind == NM_REC_NOTES && kind != NM_REC_NOTES) d->kind = kind;
  // what this section sets replaces what an earlier one with the same key said
  d->seq = (uint16_t) ++r->seqNext;
  if (sec->path[0]) nmSetStr(r, &d->path, sec->path);
  if (sec->files[0]) nmSetStr(r, &d->files, sec->files);
  if (st->title[0]) nmSetStr(r, &d->title, st->title);
  if (st->image[0]) nmSetStr(r, &d->image, st->image);
  if (st->category[0]) nmSetStr(r, &d->category, st->category);
  if (st->needs[0]) nmSetStr(r, &d->needs, st->needs);
  if (st->hideSet) d->hide = (char) st->hide;
  if (st->also[0]) nmSetStr(r, &d->also, st->also);
  if (sec->orderSet) d->order = (char) st->order;
  if (st->found)
  {
    d->nrows = 0;
    d->rows = nmPoolMark(r);
    for (int i = 0; i < st->rows; i++)
    {
      if (nmPoolAppend(r, st->row[i])) d->nrows++;
      else r->dropped++;
    }
  }
  if (st->nctl > 0)
  {
    d->nctl = 0;
    d->ctl = nmPoolMark(r);
    for (int i = 0; i < st->nctl; i++)
    {
      if (nmPoolAppend(r, st->ctl[i])) d->nctl++;
      else r->dropped++;
    }
  }
}

// Reads one menu file (its whole text, changed in place). stem: its name without ".ini".
static void nmParseMenuText(NmRecs *r, char *text, const char *stem)
{
  static NmSection sec;                       // static: large, and the loader's stack is small
  memset(&sec, 0, sizeof sec);
  char *p = text;
  while (*p != 0)
  {
    char *line = p;
    while (*p != 0 && *p != '\n' && *p != '\r') p++;
    if (*p != 0) *p++ = 0;
    while (*line == ' ' || *line == '\t') line++;
    if (*line == 0 || *line == ';' || *line == '#') continue;
    if (*line == '[')
    {
      char *end = strchr(line, ']');
      if (end == 0) continue;                 // a broken line is skipped
      *end = 0;
      nmEndSection(r, &sec, stem);
      memset(&sec, 0, sizeof sec);
      char *name = line + 1;
      while (*name == ' ') name++;
      nmTrimEnd(name);
      sidecarCopy(sec.name, (int) sizeof sec.name, name);
      continue;
    }
    if (sec.name[0] == 0) continue;           // lines before the first section
    char *eq = strchr(line, '=');
    if (eq == 0) continue;
    *eq = 0;
    char *name = line, *value = eq + 1;
    nmTrimEnd(name);
    while (*value == ' ' || *value == '\t') value++;
    for (char *c = value; *c != 0; c++)        // " ; comment" ends the value (as in the other PiTrex ini files)
      if (*c == ';' && c > value && (c[-1] == ' ' || c[-1] == '\t')) { *c = 0; break; }
    nmTrimEnd(value);
    if (strcmp(name, "file") == 0) { sidecarCopy(sec.path, NM_REC_PATH, value); sec.isFile = 1; }
    else if (strcmp(name, "folder") == 0)
    {
      sidecarCopy(sec.path, NM_REC_PATH, value);
      int n = (int) strlen(sec.path);
      while (n > 0 && sec.path[n - 1] == '/') sec.path[--n] = 0;
      sec.isFolder = 1;
    }
    else if (strcmp(name, "files") == 0) sidecarCopy(sec.files, (int) sizeof sec.files, value);
    else
    {
      if (strcmp(name, "order") == 0) sec.orderSet = 1;
      sidecarLine(&sec.st, name, value);
    }
  }
  nmEndSection(r, &sec, stem);
}

// "{dir}" and "{file}" in a folder rule's parameter
static void nmSubst(char *out, int max, const char *pat, const char *dir, const char *file)
{
  int n = 0;
  for (const char *p = pat; *p != 0 && n < max - 1;)
  {
    const char *v = 0;
    if (strncmp(p, "{dir}", 5) == 0) { v = dir; p += 5; }
    else if (strncmp(p, "{file}", 6) == 0) { v = file; p += 6; }
    if (v != 0) { while (*v != 0 && n < max - 1) out[n++] = *v++; }
    else out[n++] = *p++;
  }
  out[n] = 0;
}

// Does the file name end in one of the extensions of the list ("bin vec", empty = any)?
static int nmExtOk(const char *name, const char *list)
{
  if (list[0] == 0) return 1;
  const char *dot = strrchr(name, '.');
  if (dot == 0) return 0;
  dot++;
  int dl = (int) strlen(dot);
  for (const char *p = list; *p != 0;)
  {
    while (*p == ' ' || *p == ',' || *p == '.') p++;
    const char *w = p;
    while (*p != 0 && *p != ' ' && *p != ',') p++;
    int wl = (int) (p - w);
    if (wl == dl && wl > 0)
    {
      int same = 1;
      for (int i = 0; i < wl; i++)
      {
        char a = w[i], b = dot[i];
        if (a >= 'A' && a <= 'Z') a = (char) (a + 32);
        if (b >= 'A' && b <= 'Z') b = (char) (b + 32);
        if (a != b) same = 0;
      }
      if (same) return 1;
    }
  }
  return 0;
}

// Do two "files" lists share an extension ("um2 bin vec" and "bin vec" do; "um2" and "bin vec" do not)? Empty = any.
static int nmFilesOverlap(const char *a, const char *b)
{
  if (a[0] == 0 || b[0] == 0) return 1;
  for (const char *p = a; *p != 0;)
  {
    while (*p == ' ' || *p == ',' || *p == '.') p++;
    char probe[40] = "x.";
    int n = 2;
    while (*p != 0 && *p != ' ' && *p != ',') { if (n < 38) probe[n++] = *p; p++; }
    probe[n] = 0;
    if (n > 2 && nmExtOk(probe, b)) return 1;
  }
  return 0;
}

// The key of a folder rule: "f." + folder. A later rule for the same folder replaces an earlier one when their file
// types overlap (a card's rule overrides the catalogue's); a rule for other types of that folder is a rule of its own.
static void nmFolderKey(const NmRecs *r, const char *path, const char *files, char *out, int max)
{
  char base[NM_KEY_LEN];
  nmSlugKey("f.", path, base, NM_KEY_LEN);
  int bl = (int) strlen(base);
  if (bl > max - 5) bl = max - 5;
  for (int n = 0; n < 20; n++)
  {
    memcpy(out, base, (size_t) bl);
    int k = bl;
    if (n > 0) { out[k++] = '.'; if (n > 9) out[k++] = (char) ('0' + n / 10); out[k++] = (char) ('0' + n % 10); }
    out[k] = 0;
    const NmRec *e = nmFindRec((NmRecs *) r, out);
    if (e == 0 || e->kind != NM_REC_FOLDER) return;
    if (nmFilesOverlap(nmStr(r, e->files), files)) return;
  }
}


// A Vectrex cartridge starts with "g GCE" (what the Vectrex BIOS checks at power-on).
static int nmIsCartridge(const unsigned char *head, int n)
{
  return n >= 5 && head[0] == 'g' && head[1] == ' ' && head[2] == 'G' && head[3] == 'C' && head[4] == 'E';
}

#endif
