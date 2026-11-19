/* Host tests for browse.c: one card folder's listing, its display titles and paths, and the cursor in it. */
#include "browse.h"
#include <stdio.h>
#include <string.h>

static int passes, fails;
#define CHECK(x) do { if (x) passes++; else { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static char pool[32768];             /* as large as s_text, the pool on the console */

/* 1. folders and the three file types are kept, hidden and other files are skipped, folders sort first */
static void test_filter_sort(void)
{
  BrList b;
  br_start(&b, pool, sizeof pool, "");
  CHECK(br_add(&b, "zeta.BIN", 0) == 1);
  CHECK(br_add(&b, ".hidden", 1) == 0);
  CHECK(br_add(&b, "roms", 1) == 1);
  CHECK(br_add(&b, "readme.txt", 0) == 0);
  CHECK(br_add(&b, "Alpha.um2", 0) == 1);
  CHECK(br_add(&b, "PORTS", 1) == 1);
  CHECK(br_add(&b, "x.VEC", 0) == 1);
  CHECK(b.n == 5 && b.dropped == 0);
  br_sort(&b);
  const char *want[] = { "PORTS", "roms", "Alpha.um2", "x.VEC", "zeta.BIN" };
  const int want_dir[] = { 1, 1, 0, 0, 0 };
  for (int i = 0; i < 5; i++)
  {
    CHECK(!strcmp(br_name(&b, i), want[i]));
    CHECK(b.dir[i] == want_dir[i]);
  }
}

/* 2. display form: capitals, '_' as a space, bytes the font cannot draw as '?'; the extension stays */
static void test_title(void)
{
  BrList b;
  char name[] = { 'a', (char) 0x82, 'b', '.', 'b', 'i', 'n', 0 };
  char out[32];
  br_start(&b, pool, sizeof pool, "");
  CHECK(br_add(&b, "star_wars v2.bin", 0) == 1);
  CHECK(br_add(&b, name, 0) == 1);
  br_title(&b, 0, out, sizeof out);
  CHECK(!strcmp(out, "STAR WARS V2.BIN"));
  br_title(&b, 1, out, sizeof out);
  CHECK(!strcmp(out, "A?B.BIN"));
}

/* 3. the full path of an entry, with no leading slash in the root */
static void test_child_path(void)
{
  BrList b;
  char out[64];
  br_start(&b, pool, sizeof pool, "");
  CHECK(br_add(&b, "a.um2", 0) == 1);
  CHECK(br_child_path(&b, 0, out, sizeof out) == 1);
  CHECK(!strcmp(out, "a.um2"));

  br_start(&b, pool, sizeof pool, "PORTS/UM2");
  CHECK(br_add(&b, "b.um2", 0) == 1);
  CHECK(br_child_path(&b, 0, out, sizeof out) == 1);
  CHECK(!strcmp(out, "PORTS/UM2/b.um2"));
  CHECK(br_child_path(&b, 0, out, 8) == 0);
}

/* 4. the parent of a folder path and the part we leave; the root has no parent */
static void test_parent(void)
{
  char out[32], last[32];
  CHECK(br_parent("A/B/C", out, sizeof out, last, sizeof last) == 1);
  CHECK(!strcmp(out, "A/B"));
  CHECK(!strcmp(last, "C"));
  CHECK(br_parent("A", out, sizeof out, last, sizeof last) == 1);
  CHECK(!strcmp(out, ""));
  CHECK(!strcmp(last, "A"));
  CHECK(br_parent("", out, sizeof out, last, sizeof last) == 0);
  CHECK(!strcmp(out, ""));
  CHECK(!strcmp(last, "A"));
}

/* 5. the entry and pool limits are counted in dropped, and a short pool is never written past */
static void test_capacity(void)
{
  BrList b;
  char name[16];
  br_start(&b, pool, sizeof pool, "");
  for (int i = 0; i < BR_MAX + 88; i++)
  {
    snprintf(name, sizeof name, "f%04d.um2", i);
    br_add(&b, name, 0);
  }
  CHECK(b.n == BR_MAX);
  CHECK(b.dropped == 88);

  char guard_pool[7];
  memset(guard_pool, 0, sizeof guard_pool);
  guard_pool[6] = (char) 0x5a;                 /* one guard byte just past the 6-byte pool */
  BrList c;
  br_start(&c, guard_pool, 6, "");
  CHECK(br_add(&c, "a.um2", 0) == 1);          /* 5 letters + NUL exactly fills it */
  CHECK(c.n == 1 && c.dropped == 0);
  CHECK(guard_pool[6] == (char) 0x5a);
  CHECK(br_add(&c, "b.um2", 0) == -1);         /* another 6 bytes do not fit */
  CHECK(c.n == 1 && c.dropped == 1);
  CHECK(guard_pool[6] == (char) 0x5a);
}

/* 6. movement wraps, and the cursor stays inside an eight-row window */
static void test_move(void)
{
  BrList b;
  char name[16];
  br_start(&b, pool, sizeof pool, "");
  for (int i = 0; i < 10; i++)
  {
    snprintf(name, sizeof name, "g%d.um2", i);
    br_add(&b, name, 0);
  }
  CHECK(b.n == 10 && b.cursor == 0 && b.top == 0);
  br_move(&b, -1);
  CHECK(b.cursor == b.n - 1);
  CHECK(b.cursor - b.top >= 0 && b.cursor - b.top <= 7);
  br_move(&b, 1);
  CHECK(b.cursor == 0 && b.top == 0);
}

/* 7. lookup ignores case */
static void test_find(void)
{
  BrList b;
  br_start(&b, pool, sizeof pool, "");
  CHECK(br_add(&b, "PORTS", 1) == 1);
  CHECK(br_add(&b, "roms", 1) == 1);
  CHECK(br_add(&b, "a.um2", 0) == 1);
  br_sort(&b);
  CHECK(br_find(&b, "ROMS") == 1);
  CHECK(br_find(&b, "roms") == 1);
  CHECK(br_find(&b, "nope") == -1);
}

/* 8. br_filter drops the files the callback refuses, keeps every folder, and puts the cursor at the top */
static int keep_c_and_d(const BrList *b, int i, void *u)
{
  (*(int *) u)++;
  return br_name(b, i)[0] == 'c' || br_name(b, i)[0] == 'd';
}
static void test_filter(void)
{
  BrList b;
  br_start(&b, pool, sizeof pool, "sub");
  br_add(&b, "a.um2", 0); br_add(&b, "b.bin", 0); br_add(&b, "x", 1); br_add(&b, "c.um2", 0); br_add(&b, "d.vec", 0); br_add(&b, "y", 1);
  br_sort(&b);                                       /* x y a.um2 b.bin c.um2 d.vec */
  b.cursor = 4; b.top = 2;
  int calls = 0;
  CHECK(br_filter(&b, keep_c_and_d, &calls) == 2);   /* the number of files it dropped */
  CHECK(calls == 4);                                 /* asked once per file, never for a folder */
  CHECK(b.n == 4);
  CHECK(strcmp(br_name(&b, 0), "x") == 0 && b.dir[0] == 1);
  CHECK(strcmp(br_name(&b, 1), "y") == 0 && b.dir[1] == 1);
  CHECK(strcmp(br_name(&b, 2), "c.um2") == 0 && b.dir[2] == 0);
  CHECK(strcmp(br_name(&b, 3), "d.vec") == 0 && b.dir[3] == 0);
  CHECK(b.cursor == 0 && b.top == 0);
  char path[BR_PATH];
  CHECK(br_child_path(&b, 2, path, sizeof path) && strcmp(path, "sub/c.um2") == 0);
  /* nothing refused: unchanged; everything refused: only the folders */
  int none = 0;
  CHECK(br_filter(&b, keep_c_and_d, &none) == 0 && b.n == 4);
  br_start(&b, pool, sizeof pool, "");
  br_add(&b, "a.um2", 0); br_add(&b, "b.bin", 0);
  CHECK(br_filter(&b, keep_c_and_d, &none) == 2 && b.n == 0 && b.cursor == 0);
}

/* 9. what the list does not show is counted, and br_note says so in one line of at most 38 characters */
static int keep_none(const BrList *b, int i, void *u) { (void) b; (void) i; (void) u; return 0; }
static void test_notes(void)
{
  BrList b;
  char out[64];
  br_start(&b, pool, sizeof pool, "cfg");
  CHECK(b.skipped == 0 && b.filtered == 0);
  CHECK(br_add(&b, "settings.ini", 0) == 0 && br_add(&b, "notes.txt", 0) == 0 && br_add(&b, "noext", 0) == 0);
  CHECK(br_add(&b, ".hidden", 0) == 0);                 /* the SDK never lists these: not counted */
  CHECK(br_add(&b, "sub", 1) == 1 && br_add(&b, "g.um2", 0) == 1);
  CHECK(b.skipped == 3);
  CHECK(br_note(&b, 0, out, sizeof out) == 1 && strcmp(out, "3 FILES NOT SHOWN: ONLY UM2 BIN VEC") == 0);
  br_start(&b, pool, sizeof pool, "cfg");
  br_add(&b, "a.ini", 0);
  CHECK(br_note(&b, 0, out, sizeof out) == 1 && strcmp(out, "1 FILE NOT SHOWN: ONLY UM2 BIN VEC") == 0);
  br_start(&b, pool, sizeof pool, "cfg");               /* start clears the counts */
  CHECK(b.skipped == 0 && br_note(&b, 0, out, sizeof out) == 0);
  /* the unmapped view: files the filter dropped, and any other kind */
  br_add(&b, "x.um2", 0); br_add(&b, "y.bin", 0); br_add(&b, "z.txt", 0);
  CHECK(br_filter(&b, keep_none, 0) == 2 && b.filtered == 2);
  CHECK(br_note(&b, 1, out, sizeof out) == 1 && strcmp(out, "2 MAPPED, 1 OTHER FILES NOT SHOWN") == 0);
  br_start(&b, pool, sizeof pool, "cfg");
  br_add(&b, "x.um2", 0);
  br_filter(&b, keep_none, 0);
  CHECK(br_note(&b, 1, out, sizeof out) == 1 && strcmp(out, "1 MAPPED FILE NOT SHOWN") == 0);
  br_start(&b, pool, sizeof pool, "cfg");
  br_add(&b, "x.um2", 0); br_add(&b, "q.log", 0); br_add(&b, "r.log", 0);
  CHECK(br_note(&b, 1, out, sizeof out) == 1 && strcmp(out, "2 OTHER FILES NOT SHOWN") == 0);   /* nothing dropped yet */
  br_start(&b, pool, sizeof pool, "cfg");
  br_add(&b, "x.um2", 0);
  CHECK(br_note(&b, 1, out, sizeof out) == 0);
  /* never longer than the 38 characters the line holds, even with 4-digit counts */
  b.skipped = 1023; b.filtered = 1023;
  CHECK(br_note(&b, 1, out, sizeof out) == 1 && strlen(out) <= 38);
  CHECK(br_note(&b, 0, out, sizeof out) == 1 && strlen(out) <= 38);
  /* the empty list's first line says why it is empty */
  b.n = 0; b.skipped = 0; b.filtered = 0;
  CHECK(strcmp(br_empty(&b, 0), "NOTHING FOUND") == 0 && strcmp(br_empty(&b, 1), "NO UNMAPPED FILES") == 0);
  b.skipped = 4;
  CHECK(strcmp(br_empty(&b, 0), "NO UM2/BIN/VEC FILES HERE") == 0 && strcmp(br_empty(&b, 1), "NO UNMAPPED FILES") == 0);
}

/* 10. the heading: the whole path in display form, its beginning cut to "..." when it does not fit */
static void test_head(void)
{
  char out[40];
  br_head("", out, 18);                 CHECK(strcmp(out, "SD CARD") == 0);
  br_head("games", out, 18);            CHECK(strcmp(out, "GAMES") == 0);
  br_head("games/extra", out, 18);      CHECK(strcmp(out, "GAMES/EXTRA") == 0);
  br_head("My_Games/sub", out, 18);     CHECK(strcmp(out, "MY GAMES/SUB") == 0);        /* '_' is a space, case folds up */
  br_head("a\xC3\xA9/b", out, 18);      CHECK(strcmp(out, "A?" "?/B") == 0);                 /* bytes the font lacks are '?' */
  br_head("123456789/123456789", out, 19);  CHECK(strcmp(out, "123456789/123456789") == 0);   /* exactly fits: no cut */
  br_head("AAAAAAAAAA/BBBBBBBBBB", out, 18); CHECK(strcmp(out, "...AAAA/BBBBBBBBBB") == 0);    /* the end stays */
  br_head("one/two/three/four/five", out, 18); CHECK(strcmp(out, "...THREE/FOUR/FIVE") == 0);   /* the last 15 characters */
  CHECK(strlen(out) == 18);
  br_head("abcdefghijklmnopqrstuvwxyz", out, 10); CHECK(strcmp(out, "...TUVWXYZ") == 0);  /* one long name: its end */
  br_head("x/y", out, 3);               CHECK(strcmp(out, "X/Y") == 0);                   /* fits in exactly 3 */
  br_head("xyz/abc", out, 3);           CHECK(strcmp(out, "...") == 0);                   /* no room for more than the dots */
  br_head("games/extra", out, 4);       CHECK(strcmp(out, "...A") == 0);
}

int main(void)
{
  test_filter_sort();
  test_title();
  test_child_path();
  test_parent();
  test_capacity();
  test_move();
  test_find();
  test_filter();
  test_notes();
  test_head();
  printf("%d passed, %d failed\n", passes, fails);
  return fails != 0;
}
