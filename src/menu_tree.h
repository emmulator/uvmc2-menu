/* The menu as a tree, from the PiTrex menu records (menuFile.h). No hardware here. */
#ifndef MENU_TREE_H
#define MENU_TREE_H
#include "menuFile.h"
#include <stdint.h>
#define MT_MAX_NODES 1500
#define MT_POOL 40960            /* the titles and paths of all entries; each costs its real length */
#define MT_TITLE 32
#define MT_BUILTIN_OTHER "HOMEBREW"     /* where an uncategorised built-in goes when no ini says */

typedef struct MtNode
{
  uint16_t title, image;         /* offsets into the pool; use mt_title and mt_image */
  int16_t rec;                   /* the record with its notes, or -1 */
  int16_t parent, first, next;   /* indexes, -1 = none */
  int16_t seq;
  char listed;                   /* a category with order = listed */
  char fav;                      /* a game in favorites.ini, outside the FAVORITES folder: drawn with a mark */
} MtNode;
_Static_assert(sizeof (MtNode) == 16, "MtNode stays 16 bytes: 1500 of them");

#define MT_FAVORITES "favorites.ini"     /* in menu/; read on its own, never as a layer */
#define MT_FAV_FOLDER "FAVORITES"
#define MT_FAV_MAX 300          /* favorites; their paths share MT_FAV_POOL bytes, so fewer if the paths are long (about 25 each: ~320) */
#define MT_FAV_POOL 8192
/* The favorites' paths as written, packed one after another in pool, each with the hash of its nmPathKey so a lookup does
 * not have to make the key of every one. full: some did not fit. */
typedef struct { int n, used, full; uint16_t off[MT_FAV_MAX]; uint32_t hash[MT_FAV_MAX]; char pool[MT_FAV_POOL]; } MtFavs;
static inline const char *mt_fav_path(const MtFavs *f, int i) { return f->pool + f->off[i]; }
/* The favorites in the text of favorites.ini: every "file =" value (trimmed, kept as written), in file order. Lines that
 * are not "file = ..." (sections, also, comments, blank) are skipped; a path whose nmPathKey is already listed is not
 * added twice; past MT_FAV_MAX, or when the pool is full, the rest are ignored (and full is set). */
void mt_favs_read(MtFavs *f, const char *text);
int  mt_favs_has(const MtFavs *f, const char *path);   /* 1 if path (a card path or @fw/<name>) is a favorite */
/* text (favorites.ini as it is, may be empty) with path added as a favorite, or removed if it is one already.
 * Writes the new text to out and returns its length; *added is 1 if it was added, 0 if removed.
 * Returns -1 if the result does not fit in max, -2 if the table has no room for another favorite. */
int mt_fav_toggle(const char *text, const char *title, const char *path, char *out, int max, int *added);

typedef struct
{
  MtNode node[MT_MAX_NODES];
  int n, dropped;
  char pool[MT_POOL];
  int used;
  const MtFavs *favs;            /* the favorites of this build, for mt_add_builtin; 0 outside mt_build */
} MtTree;

static inline const char *mt_title(const MtTree *t, int n) { return t->pool + t->node[n].title; }
static inline const char *mt_image(const MtTree *t, int n) { return t->pool + t->node[n].image; }   /* empty: a category */
static inline int mt_is_category(const MtTree *t, int n) { return t->pool[t->node[n].image] == 0; }
void mt_clear(MtTree *t, const char *title);               /* empty but for a root called title */
int  mt_new_entry(MtTree *t, const char *title, const char *image, int parent);   /* for tests; -1 if full */

/* What the tree needs from the card. */
typedef struct
{
  int (*exists)(const char *path, void *u);
  /* calls add() for each file in a folder ("." is the root) */
  void (*list)(const char *dir, void (*add)(const char *name, void *ctx), void *ctx, void *u);
  void (*extra)(MtTree *t, void *u);     /* adds entries of its own, with mt_add */
  void *u;
  const MtFavs *favs;                    /* favorites, or 0 */
} MtCard;

/* menu/settings.ini: "key = value" lines, ; starts a comment, case does not matter. Returns the value of key as on (1)
 * or off (0): on, yes, 1 / off, no, 0. dflt if the key is missing or the value is none of these. */
#define MT_SETTINGS "settings.ini"
int mt_setting_on(const char *text, const char *key, int dflt);
/* text with key's value changed to value, the rest (comments, other lines) kept; a missing key is added at the end.
 * Returns the length written to out, or -1 if it does not fit in max. */
int mt_setting_set(const char *text, const char *key, const char *value, char *out, int max);

/* The middle menu layers: drops menu.default.ini, menu.ini, settings.ini and anything not .ini, sorts the rest. Returns how many.
 * The load order is menu.default.ini, these, menu.ini; what is read later wins. */
#define MT_LAYER_NAME 48
int mt_layer_sort(char (*names)[MT_LAYER_NAME], int n);

/* cat and also are category paths ("ARCADE / ATARI"); "/" (spaces around it ignored) means the top level itself,
 * not a folder. At the top level folders (FAVORITES too) and entries go together by title, SYSTEM last. */
void mt_add(MtTree *t, const char *cat, const char *also, const char *title, const char *image);
/* A built-in game, for MtCard.extra: in its firmware category (fwcat, 0 for none), plus the @fw folder rule's also
 * if there is a rule. A game the firmware puts in no category goes in the rule's category, or in MT_BUILTIN_OTHER
 * when there is no rule. [NAME] with file = @fw/<its name> renames, moves (category, also) or hides one; a folder rule
 * with folder = @fw (and image = @fw) gives those. Returns 0 if it is hidden. */
int  mt_add_builtin(MtTree *t, NmRecs *r, const char *fwcat, const char *name, const char *image);
void mt_build(MtTree *t, NmRecs *r, const MtCard *c);   /* node 0 is the root */
/* 1 if the menu's ini files map the card file at path ("sub/x.bin", "x.um2"): an entry with this image (hidden or not), a
 * file = section that hides it (hiding is a decision about it), or a folder rule for its folder that takes its extension.
 * A file = section that only moves or renames it lists nothing without a rule behind it, so it does not map. A favorite is a
 * mark on something, not a mapping. The menu's own files count, as there is nothing to map them to. For BROWSE SD CARD's
 * view of the files only BROWSE reaches. */
int mt_file_mapped(NmRecs *r, const char *path);

typedef struct { int dir, cursor, top; } MtNav;
#define MT_ROWS 8

/* The place across a rebuild: the folder titles from the top, the game's image and its cursor index. */
#define MT_PLACE_DEPTH 6
typedef struct { int depth; char dir[MT_PLACE_DEPTH][MT_TITLE]; char image[160]; int cursor; } MtPlace;
void mt_place_save(MtPlace *p, const MtNav *n, const MtTree *t);
void mt_place_restore(MtNav *n, const MtTree *t, const MtPlace *p);
/* Word wrap: copies the next line of at most width characters into out (out holds width + 1) and moves *s past it.
 * Breaks at a space when it can. Returns 0 when there is nothing left. */
int  mt_wrap_next(const char **s, char *out, int width);
/* The lines under the list. What does not fit ends the last line with "...". */
#define MT_NOTE_LINES 16
#define MT_NOTE_WIDTH 38
typedef struct { char line[MT_NOTE_LINES][MT_NOTE_WIDTH + 1]; int n, more, max; } MtNotes;
void mt_notes_start(MtNotes *m, int max);
void mt_notes_add(MtNotes *m, const char *s);
void mt_notes_end(MtNotes *m);
/* The instructions line: OPEN or START, BACK unless at the top of the menu, FAVORITE on a game, MORE only when there
 * is more (last, as it comes and goes). The stick's left and right are not named: the line has room for 48 characters. */
void mt_hint(char *out, int max, int is_folder, int can_back, int can_fav, int has_more);
void mt_nav_init(MtNav *n);
int  mt_count(const MtTree *t, int dir);
int  mt_nth(const MtTree *t, int dir, int i);
int  mt_more_above(const MtNav *n);
int  mt_more_below(const MtNav *n, const MtTree *t);
void mt_move(MtNav *n, const MtTree *t, int delta);     /* wraps */
int  mt_enter(MtNav *n, const MtTree *t);               /* 1 if a category was opened */
int  mt_back(MtNav *n, const MtTree *t);                /* 1 if it went up */
#endif
