/* The NEW list: the newest few game files on the card, by the date the card holds for each. No SD calls here. The cartridge
 * has no clock, so this never asks how long ago a file was added; it keeps whichever files have the latest dates. */
#ifndef NEWLIST_H
#define NEWLIST_H
#include <stdint.h>
#include "browse.h"

#define NW_MAX   20             /* most entries the list can hold; the setting is clamped to 1..NW_MAX */
#define NW_DEFAULT 10
#define NW_PATH  96             /* longest path kept, with its NUL; longer ones are left out */
typedef struct
{
  int k, n;                                  /* wanted, and held (n <= k) */
  uint32_t stamp[NW_MAX];                    /* FAT date << 16 | FAT time, newest first */
  char path[NW_MAX][NW_PATH];
} NwList;

void nw_start(NwList *l, int k);             /* empty list that keeps the k newest (k clamped to 1..NW_MAX) */
/* Is path a file NEW never lists: not .um2, .bin or .vec; the menu's own files (autostart.um2 and MENU.um2 in the root); or
 * anything in the menu/ or config/ folders of the root. */
int  nw_skip(const char *path);
/* Offers a file with the FAT date and time the card holds for it. Returns 1 if it is now in the list. A file dated as
 * newly as one already held goes after it, unless its path sorts first. */
int  nw_offer(NwList *l, const char *path, uint16_t fdate, uint16_t ftime);
/* The card, as the walk needs it: directories opened by depth number (the caller keeps one open directory per depth, so the
 * walk itself needs no recursion), and read one entry at a time. read returns 1 with e filled in, 0 at the end or on an error. */
typedef struct
{
  const char *name;                          /* valid until the next read of that depth */
  int is_dir, hidden;                        /* hidden: the card marks it hidden or system */
  uint16_t fdate, ftime;
} NwEntry;
typedef struct
{
  int  (*open)(int depth, const char *path, void *u);          /* path "" is the root; 1 if it opened */
  int  (*read)(int depth, NwEntry *e, void *u);
  void (*close)(int depth, void *u);
  int  (*skip)(const char *path, void *u);                     /* 1 to leave a file out; may be 0. Asked only about a file that
                                                                * would make the list, so it may be slow */
} NwFs;
#define NW_DEPTH 6                           /* folders deep the walk goes: the root is depth 0 */
/* Walks the whole card, offering every game file. Entries whose names start with '.' or that the card marks hidden are
 * left out, as are folders deeper than NW_DEPTH - 1 and paths too long for NW_PATH. After max_visit entries it stops. Returns
 * 1 if it stopped early, 0 if it read everything, -1 if the root would not open. */
int  nw_walk(NwList *l, const NwFs *fs, void *u, int max_visit);

/* Points b at the list's paths (as names in the root), newest first, so the browser's drawing and keys can show it. b's
 * pool is the list itself: keep l alive while b is used. */
void nw_to_browse(NwList *l, BrList *b);

#endif
