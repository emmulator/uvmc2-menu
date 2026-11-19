/* One card folder's listing: entries in a caller's name pool, folders first, and the cursor in it. No SD calls. */
#ifndef BROWSE_H
#define BROWSE_H
#include <stdint.h>

#define BR_MAX   1024           /* entries in one folder; more are counted in dropped */
#define BR_PATH  160            /* the folder's path from the root, "" for the root */
typedef struct
{
  char *pool; int pool_size, used;           /* names, NUL-ended, given by the caller */
  uint16_t off[BR_MAX];                      /* each entry's name in pool */
  uint8_t dir[BR_MAX];                       /* 1 if a folder */
  int n, dropped, cursor, top;
  int skipped;                               /* files br_add left out: not .um2, .bin or .vec */
  int filtered;                              /* files br_filter dropped */
  char path[BR_PATH];
} BrList;

void br_start(BrList *b, char *pool, int pool_size, const char *path);   /* empty list for path, cursor 0 */
/* One name from the card. Adds a folder unless its name starts with '.'; adds a file only if it ends in .um2, .bin
 * or .vec (any case). Anything else is skipped. Returns 1 if added, 0 if skipped, -1 if full (counted in dropped). */
int  br_add(BrList *b, const char *name, int is_dir);
void br_sort(BrList *b);                     /* folders first, then files, each A-Z ignoring case */
void br_move(BrList *b, int d);              /* as mt_move: wraps, keeps cursor in the MT_ROWS window */
const char *br_name(const BrList *b, int i);
/* The display form of entry i: capitals, '_' as a space, any byte outside 32-95 after upper-casing as '?'.
 * Files keep their extension ("ZAPPYBIRD V150.BIN"). */
void br_title(const BrList *b, int i, char *out, int max);
/* The full path of entry i ("PORTS/UM2/x.um2", or "x.bin" in the root) into out. 0 if it does not fit. */
int  br_child_path(const BrList *b, int i, char *out, int max);
/* path's parent into out ("A/B" -> "A", "A" -> ""), and the last part (the folder we leave) into last. 0 at root. */
int  br_parent(const char *path, char *out, int max, char *last, int lmax);
int  br_find(const BrList *b, const char *name);  /* index of name, ignoring case, or -1 */
/* The heading for a folder: its whole path in display form (capitals, '_' as a space, a byte the font lacks as '?'), or SD CARD
 * at the root. If it is longer than max characters its beginning is replaced by "..." so the end, the most local folders,
 * stays. out holds max + 1. */
void br_head(const char *path, char *out, int max);
/* One line (at most 38 characters) saying what the list leaves out: the files of other kinds, and in the unmapped view the
 * mapped files br_filter dropped. 1 if there is something to say, 0 if not. */
int  br_note(const BrList *b, int unmapped, char *out, int max);
/* The list's first line when it has no entries, saying why it is empty. */
const char *br_empty(const BrList *b, int unmapped);
/* Drops the files for which keep(b, i, u) returns 0 (i is the entry's index before any drop); folders stay. The cursor goes
 * back to the top. Returns how many files it dropped, also kept in b->filtered. */
int  br_filter(BrList *b, int (*keep)(const BrList *b, int i, void *u), void *u);

#endif
