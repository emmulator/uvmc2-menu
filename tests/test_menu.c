#include "menu_tree.h"
#include "um2.h"
#include <assert.h>
#include <stdio.h>
#include <strings.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); fails++; } else passes++; } while (0)
static int fails, passes;
static NmRecs recs; static MtTree tree;

static const char *files[] = { "roms/mhavoc.zip", "mhavoc.um2", "custom.um2", "sub/deep_game.bin", 0 };
/* The card is FAT: a name is found whatever its case. */
static int fx(const char *p, void *u) { (void) u; for (int i = 0; files[i]; i++) if (!strcasecmp(files[i], p)) return 1; return 0; }
static void fl(const char *d, void (*add)(const char *, void *), void *c, void *u)
{
  (void) u;
  if (!strcmp(d, ".")) { add("Zeta_game.um2", c); add("Star Wars.um2", c); add("autostart.um2", c); add("MENU.um2", c); add("AlienStorm.bin", c); add("hidden.um2", c); }
  if (!strcmp(d, "PORTS/UM2")) { add("demon.um2", c); }
}
static const MtCard card = { fx, fl, 0, 0, 0 };

static int find(int dir, const char *title)
{
  for (int c = tree.node[dir].first; c >= 0; c = tree.node[c].next) if (!strcmp(mt_title(&tree, c), title)) return c;
  return -1;
}

static void load(const char *def, const char *user)
{
  static char a[8192];
  memset(&recs, 0, sizeof recs);
  strcpy(a, def); nmParseMenuText(&recs, a, "menu.default");
  if (user) { strcpy(a, user); nmParseMenuText(&recs, a, "menu"); }
  mt_build(&tree, &recs, &card);
}

/* load() with a menu/favorites.ini text, so rebuilding can add or drop the FAVORITES folder. */
static MtFavs place_favs;
static void load_favs(const char *def, const char *user, const char *favtext)
{
  static char a[8192];
  memset(&recs, 0, sizeof recs);
  strcpy(a, def); nmParseMenuText(&recs, a, "menu.default");
  if (user) { strcpy(a, user); nmParseMenuText(&recs, a, "menu"); }
  mt_favs_read(&place_favs, favtext ? favtext : "");
  MtCard c = card;
  c.favs = &place_favs;
  mt_build(&tree, &recs, &c);
}

static const char *DEF =
"; the shipped one\n[ALL GAMES]\ncategory = ALL GAMES\nfolder = .\nfiles = um2\nimage = {dir}/{file}\n"
"[PORTS]\ncategory = ALL GAMES\nfolder = PORTS/UM2/\nfiles = um2\nimage = {dir}/{file}\n"
"[CARTRIDGE MENU]\ncategory = SYSTEM\nimage = @stock\ntext = NOTE ONE\n"
"[MHAVOC]\ncategory = ARCADE / ATARI\nimage = mhavoc.um2\nneeds = roms/mhavoc.zip\ncontrol = FIRE  BUTTON 4\n"
"[NO ROMS]\ncategory = ARCADE\nimage = custom.um2\nneeds = roms/absent.zip\n"
"[NO FILE]\ncategory = ARCADE\nimage = absent.um2\n";

/* The user layer used by test_layers and by test_compact_records: overrides, moves, hides, a listed category. */
static const char *USER =
"[MHAVOC]\ncategory = FAVORITES\nalso = TWO PLAYERS\n"
"[WARS]\nfile = Star Wars.um2\ntitle = STAR WARS 1983\ncategory = ARCADE\n"
"[HIDE ME]\nfile = hidden.um2\nhide = 1\n"
"[CARTRIDGE MENU]\ntext = CHANGED\n"
"[SYSTEM]\norder = listed\n"
"[ARCADE]\nhide = 1\n";

static void test_tree(void)
{
  load(DEF, 0);
  int all = find(0, "ALL GAMES"), arc = find(0, "ARCADE"), sys = find(0, "SYSTEM");
  CHECK(all > 0); CHECK(arc > 0); CHECK(sys > 0);
  CHECK(tree.node[0].first == all);                                 /* alphabetical ... */
  CHECK(mt_nth(&tree, 0, mt_count(&tree, 0) - 1) == sys);           /* ... SYSTEM last */
  CHECK(mt_count(&tree, arc) == 1);                                 /* NO ROMS and NO FILE hidden */
  int ata = find(arc, "ATARI");
  CHECK(ata > 0 && find(ata, "MHAVOC") > 0);
  CHECK(!strcmp(mt_image(&tree, find(ata, "MHAVOC")), "mhavoc.um2"));
  CHECK(find(all, "STAR WARS") > 0);
  CHECK(!strcmp(mt_image(&tree, find(all, "STAR WARS")), "Star Wars.um2"));     /* ./ dropped, name intact */
  CHECK(!strcmp(mt_image(&tree, find(all, "ZETA GAME")), "Zeta_game.um2"));
  CHECK(!strcmp(mt_image(&tree, find(all, "DEMON")), "PORTS/UM2/demon.um2"));
  CHECK(find(all, "AUTOSTART") < 0 && find(all, "MENU") < 0);       /* never itself */
  CHECK(find(all, "ALIENSTORM") < 0);                               /* files = um2 */
  CHECK(!strcmp(mt_image(&tree, find(sys, "CARTRIDGE MENU")), "@stock"));
  const NmRec *m = &recs.rec[tree.node[find(ata, "MHAVOC")].rec];
  CHECK(m->nctl == 1 && !strcmp(nmStr(&recs, m->ctl), "FIRE  BUTTON 4"));
}

static void test_layers(void)
{
  load(DEF, USER);
  int fav = find(0, "FAVORITES"), two = find(0, "TWO PLAYERS"), all = find(0, "ALL GAMES");
  CHECK(fav > 0 && find(fav, "MHAVOC") > 0);
  CHECK(two > 0 && find(two, "MHAVOC") > 0);
  CHECK(find(0, "ARCADE") < 0);                                     /* hide on a category */
  CHECK(find(all, "STAR WARS") < 0);                                /* moved by file = */
  CHECK(find(all, "HIDDEN") < 0);
  int sys = find(0, "SYSTEM");
  CHECK(sys > 0 && tree.node[sys].listed == 1);
  const NmRec *c = &recs.rec[tree.node[find(sys, "CARTRIDGE MENU")].rec];
  CHECK(c->nrows == 1 && !strcmp(nmStr(&recs, c->rows), "CHANGED"));  /* replaces, image stays */
  CHECK(!strcmp(nmStr(&recs, c->image), "@stock"));
}

static void test_three_layers(void)
{
  static char a[8192];
  memset(&recs, 0, sizeof recs);
  strcpy(a, DEF); nmParseMenuText(&recs, a, "menu.default");
  strcpy(a, "[MHAVOC]\ntitle = FROM MIDDLE\ntext = MIDDLE NOTE\n[WARS]\nfile = Star Wars.um2\nhide = 1\n"); nmParseMenuText(&recs, a, "a-favorites");
  strcpy(a, "[MHAVOC]\ntitle = FROM MENU INI\n[WARS]\nhide = 0\n"); nmParseMenuText(&recs, a, "menu");
  mt_build(&tree, &recs, &card);
  int ata = find(find(0, "ARCADE"), "ATARI");
  int m = find(ata, "FROM MENU INI");
  CHECK(m > 0);                                                     /* menu.ini beats the middle file */
  CHECK(!strcmp(mt_image(&tree, m), "mhavoc.um2"));                 /* default's image is still there */
  CHECK(recs.rec[tree.node[m].rec].nrows == 1);                     /* the middle file's note stays */
  CHECK(find(find(0, "ALL GAMES"), "WARS") > 0);                    /* hide = 0 undoes the middle hide; the section name is the title */
  char n[7][MT_LAYER_NAME] = { "menu.ini", "z.ini", "MENU.DEFAULT.INI", "A.INI", "notes.txt", "m.ini", "Settings.INI" };
  CHECK(mt_layer_sort(n, 7) == 3);
  CHECK(!strcmp(n[0], "A.INI") && !strcmp(n[1], "m.ini") && !strcmp(n[2], "z.ini"));
}

#define FAV_HEADER "; menu/favorites.ini: your favorites. Button 3 in the menu adds or removes a game; you can also edit this file.\n"

/* adding and removing a favorite in the text of favorites.ini, without rewriting anything else */
static void test_fav_toggle(void)
{
  static char t1[2048], t2[2048], t3[2048], many[16384], o2[16384];
  static MtFavs favs;
  int added, n, fav_len;

  /* 1. an empty file gets the header, a blank line, then the section */
  n = mt_fav_toggle("", "VECTOR KONG", "dkong.um2", t1, sizeof t1, &added);
  CHECK(n > 0 && added == 1);
  CHECK(!strcmp(t1, FAV_HEADER "\n[VECTOR KONG]\nfile = dkong.um2\nalso = FAVORITES\n"));
  CHECK(n == (int) strlen(t1));
  fav_len = n;

  /* 2. a second one, then removing the first by a differently cased path */
  n = mt_fav_toggle(t1, "RIP OFF", "@fw/Rip Off", t2, sizeof t2, &added);
  CHECK(n > 0 && added == 1);
  CHECK(!strcmp(t2, FAV_HEADER "\n[VECTOR KONG]\nfile = dkong.um2\nalso = FAVORITES\n"
                               "\n[RIP OFF]\nfile = @fw/Rip Off\nalso = FAVORITES\n"));
  n = mt_fav_toggle(t2, "VECTOR KONG", "DKONG.UM2", t3, sizeof t3, &added);
  CHECK(n > 0 && added == 0);
  CHECK(!strcmp(t3, FAV_HEADER "\n[RIP OFF]\nfile = @fw/Rip Off\nalso = FAVORITES\n"));

  /* 3. the middle of three goes, comments and the hand-written also line stay */
  n = mt_fav_toggle(
      "; menu/favorites.ini: mine\n"
      "; keep this\n"
      "[ALPHA]\nfile = alpha.um2\nalso = FAVORITES\n"
      "\n"
      "[BETA]\nfile = beta.um2\nalso = FAVORITES\n"
      "\n"
      "; keep this too\n"
      "[GAMMA]\nfile = gamma.um2\nalso = FAVORITES, TWO PLAYERS\n",
      "BETA", "beta.um2", t3, sizeof t3, &added);
  CHECK(n > 0 && added == 0);
  CHECK(!strcmp(t3,
      "; menu/favorites.ini: mine\n"
      "; keep this\n"
      "[ALPHA]\nfile = alpha.um2\nalso = FAVORITES\n"
      "\n"
      "; keep this too\n"
      "[GAMMA]\nfile = gamma.um2\nalso = FAVORITES, TWO PLAYERS\n"));

  /* 4. round trip: after the adds the list has both, after the remove only one */
  n = mt_fav_toggle("", "ENDURANCE", "PORTS/UM2/Enduro.UM2", t1, sizeof t1, &added);
  CHECK(n > 0 && added == 1);
  n = mt_fav_toggle(t1, "RIP OFF", "@fw/Rip Off", t2, sizeof t2, &added);
  CHECK(n > 0 && added == 1);
  mt_favs_read(&favs, t2);
  CHECK(mt_favs_has(&favs, "PORTS/UM2/enduro.um2"));
  CHECK(mt_favs_has(&favs, "@fw/rip off"));
  n = mt_fav_toggle(t2, "", "ports/um2/ENDuro.um2", t3, sizeof t3, &added);
  CHECK(n > 0 && added == 0);
  mt_favs_read(&favs, t3);
  CHECK(!mt_favs_has(&favs, "PORTS/UM2/Enduro.UM2"));
  CHECK(mt_favs_has(&favs, "@fw/Rip Off"));

  /* 5. brackets in the title become parentheses and the parser still reads the file line */
  n = mt_fav_toggle("", "A [B] C", "dkong.um2", t1, sizeof t1, &added);
  CHECK(n > 0 && added == 1);
  CHECK(strstr(t1, "[A (B) C]") != 0);
  mt_favs_read(&favs, t1);
  CHECK(mt_favs_has(&favs, "dkong.um2"));
  static char parsed[2048];
  static NmRecs pr;
  strcpy(parsed, t1);
  memset(&pr, 0, sizeof pr);
  nmParseMenuText(&pr, parsed, "favorites");
  int found = 0;
  for (int i = 0; i < pr.n; i++) if (!strcmp(nmStr(&pr, pr.rec[i].path), "dkong.um2")) found = 1;
  CHECK(found);

  /* 6. one byte too small; MT_FAV_MAX favorites refuse another, but remove one */
  CHECK(mt_fav_toggle("", "VECTOR KONG", "dkong.um2", t1, fav_len, &added) == -1);
  many[0] = 0;
  for (int i = 0; i < MT_FAV_MAX; i++)
  {
    char title[16], path[32];
    snprintf(title, sizeof title, "GAME %03d", i);
    snprintf(path, sizeof path, "game%03d.um2", i);
    n = mt_fav_toggle(many, title, path, o2, sizeof o2, &added);
    CHECK(n > 0 && added == 1);
    memcpy(many, o2, (size_t) n + 1);
  }
  CHECK(mt_fav_toggle(many, "ONE MORE", "extra.um2", o2, sizeof o2, &added) == -2);
  n = mt_fav_toggle(many, "GAME 000", "game000.um2", o2, sizeof o2, &added);
  CHECK(n > 0 && added == 0);
  mt_favs_read(&favs, o2);
  CHECK(favs.n == MT_FAV_MAX - 1);
}

static void test_settings(void)
{
  CHECK(mt_setting_on("", "menu", 1) == 1);                                    /* nothing: the default */
  CHECK(mt_setting_on("menu = off\n", "menu", 1) == 0);
  CHECK(mt_setting_on("; switched off for now\r\n  MENU=Off ; for now\r\n", "menu", 1) == 0);
  CHECK(mt_setting_on(";menu = off\nmenu = on\n", "menu", 0) == 1);           /* a comment line is not read */
  CHECK(mt_setting_on("menu = 0", "menu", 1) == 0 && mt_setting_on("menu = no", "menu", 1) == 0);
  CHECK(mt_setting_on("menus = off\nmenu_font = off\n", "menu", 1) == 1);     /* other keys that start the same */
  CHECK(mt_setting_on("menu = maybe\n", "menu", 1) == 1);                      /* not a value: the default */
  char o[128];
  CHECK(mt_setting_set("; mine\nmenu = off        ; on / off\nfont = b\n", "menu", "on", o, sizeof o) > 0);
  CHECK(!strcmp(o, "; mine\nmenu = on        ; on / off\nfont = b\n"));          /* only the value changes */
  CHECK(mt_setting_set("MENU=Off\r\n", "menu", "on", o, sizeof o) > 0 && !strcmp(o, "MENU=on\r\n"));
  CHECK(mt_setting_set("font = b", "menu", "on", o, sizeof o) > 0 && !strcmp(o, "font = b\nmenu = on\n"));   /* added */
  CHECK(mt_setting_set("", "menu", "on", o, sizeof o) > 0 && !strcmp(o, "menu = on\n"));
  CHECK(mt_setting_set("menu = off\n", "menu", "on", o, 8) == -1);                  /* does not fit */
}

static void test_nav(void)
{
  load(DEF, 0);
  MtNav n; mt_nav_init(&n);
  int k = mt_count(&tree, 0);
  mt_move(&n, &tree, -1); CHECK(n.cursor == k - 1);
  mt_move(&n, &tree, 1); CHECK(n.cursor == 0);
  CHECK(mt_back(&n, &tree) == 0);
                                              /* cursor 0 = ALL GAMES, whose first row is a game */
  int d = mt_nth(&tree, 0, n.cursor);
  CHECK(mt_enter(&n, &tree) == 1 && n.dir == d && n.cursor == 0);
  CHECK(mt_enter(&n, &tree) == 0);            /* a game is not opened */
  CHECK(mt_back(&n, &tree) == 1 && n.dir == 0 && n.cursor == 0);
}

/* The cursor keeps its place across a rebuild: the same game, its folder, and a visible window. */
static void test_place(void)
{
  MtPlace place;
  MtNav n;
  int all, fav, zeta;

  /* 1. a favorite is added; the cursor stays on ZETA GAME inside ALL GAMES */
  load(DEF, 0);
  all = find(0, "ALL GAMES");
  CHECK(all > 0);
  zeta = all > 0 ? find(all, "ZETA GAME") : -1;
  CHECK(zeta > 0);
  mt_nav_init(&n);
  n.dir = all;
  for (int i = 0; i < mt_count(&tree, all); i++) if (mt_nth(&tree, all, i) == zeta) n.cursor = i;
  mt_place_save(&place, &n, &tree);
  load_favs(DEF, 0, "[ZETA]\nfile = Zeta_game.um2\n");
  mt_place_restore(&n, &tree, &place);
  CHECK(n.dir > 0 && !strcmp(mt_title(&tree, n.dir), "ALL GAMES"));
  CHECK(mt_nth(&tree, n.dir, n.cursor) == find(n.dir, "ZETA GAME"));

  /* 2. the only favorite is removed: the folder is gone, the top level still has a valid row */
  load_favs(DEF, 0, "[ZETA]\nfile = Zeta_game.um2\n");
  fav = find(0, "FAVORITES");
  CHECK(fav > 0 && mt_count(&tree, fav) == 1);
  mt_nav_init(&n);
  n.dir = fav;
  n.cursor = 0;
  mt_place_save(&place, &n, &tree);
  load_favs(DEF, 0, "");
  mt_place_restore(&n, &tree, &place);
  CHECK(find(0, "FAVORITES") < 0);
  CHECK(n.dir == 0);
  CHECK(n.cursor >= 0 && n.cursor < mt_count(&tree, 0));
  CHECK(n.top == 0);

  /* 3. entry 9 of 12: after a rebuild the window still shows it */
  static char big[1024];
  big[0] = 0;
  for (int i = 0; i < 12; i++)
  {
    char sec[128];
    snprintf(sec, sizeof sec, "[GAME %02d]\ncategory = BIG\nimage = @g%02d\n", i, i);
    strcat(big, sec);
  }
  load(DEF, big);
  int cat = find(0, "BIG");
  CHECK(cat > 0 && mt_count(&tree, cat) == 12);
  mt_nav_init(&n);
  n.dir = cat;
  n.cursor = 9;
  mt_place_save(&place, &n, &tree);
  load(DEF, big);
  mt_place_restore(&n, &tree, &place);
  CHECK(n.dir == find(0, "BIG"));
  CHECK(n.cursor == 9 && mt_nth(&tree, n.dir, n.cursor) == find(n.dir, "GAME 09"));
  CHECK(n.cursor - n.top < MT_ROWS && n.top > 0);

  /* 4. the middle of three favorites goes: in FAVORITES, on what is now the second */
  load_favs(DEF, 0, "[ZETA]\nfile = Zeta_game.um2\n[WARS]\nfile = Star Wars.um2\n[DEMON]\nfile = PORTS/UM2/demon.um2\n");
  fav = find(0, "FAVORITES");
  CHECK(fav > 0 && mt_count(&tree, fav) == 3);
  CHECK(!strcmp(mt_title(&tree, mt_nth(&tree, fav, 1)), "STAR WARS"));
  mt_nav_init(&n);
  n.dir = fav;
  n.cursor = 1;
  mt_place_save(&place, &n, &tree);
  load_favs(DEF, 0, "[ZETA]\nfile = Zeta_game.um2\n[DEMON]\nfile = PORTS/UM2/demon.um2\n");
  mt_place_restore(&n, &tree, &place);
  fav = find(0, "FAVORITES");
  CHECK(fav > 0 && mt_count(&tree, fav) == 2);
  CHECK(n.dir == fav && n.cursor == 1);
  CHECK(!strcmp(mt_title(&tree, mt_nth(&tree, n.dir, n.cursor)), "ZETA GAME"));
}

static void test_scroll_marks(void)
{
  static MtTree big;
  mt_clear(&big, "ROOT");
  for (int i = 0; i < MT_ROWS + 2; i++) mt_new_entry(&big, "GAME", "x.um2", 0);
  MtNav n; mt_nav_init(&n);
  CHECK(!mt_more_above(&n) && mt_more_below(&n, &big));         /* at the top */
  for (int i = 0; i < MT_ROWS; i++) mt_move(&n, &big, 1);       /* one past the window */
  CHECK(mt_more_above(&n) && mt_more_below(&n, &big));          /* in the middle */
  mt_move(&n, &big, 1);
  CHECK(mt_more_above(&n) && !mt_more_below(&n, &big));         /* at the end */
  mt_move(&n, &big, 1);                                         /* wraps to the top */
  CHECK(n.cursor == 0 && !mt_more_above(&n) && mt_more_below(&n, &big));
  big.node[0].first = -1;
  mt_nav_init(&n);
  CHECK(!mt_more_above(&n) && !mt_more_below(&n, &big));        /* an empty list: none */
}

static void test_wrap(void)
{
  char l[39]; const char *s = "BY JEFF CORSIGLIA(GCE), CHRIS KING(GCE)";
  CHECK(mt_wrap_next(&s, l, 38) && !strcmp(l, "BY JEFF CORSIGLIA(GCE), CHRIS"));
  CHECK(mt_wrap_next(&s, l, 38) && !strcmp(l, "KING(GCE)"));
  CHECK(!mt_wrap_next(&s, l, 38));
  s = "EXACTLY TEN"; CHECK(mt_wrap_next(&s, l, 11) && !strcmp(l, "EXACTLY TEN") && !mt_wrap_next(&s, l, 11));
  s = "ABCDEFGHIJKL M"; CHECK(mt_wrap_next(&s, l, 5) && !strcmp(l, "ABCDE"));    /* a long word is cut */
  CHECK(mt_wrap_next(&s, l, 5) && !strcmp(l, "FGHIJ"));
  CHECK(mt_wrap_next(&s, l, 5) && !strcmp(l, "KL M") && !mt_wrap_next(&s, l, 5));
  s = "   "; CHECK(!mt_wrap_next(&s, l, 5));
  s = ""; CHECK(!mt_wrap_next(&s, l, 5));
}

static void test_notes(void)
{
  static MtNotes m;
  /* the e-srg example: five lines into four rows */
  mt_notes_start(&m, 4);
  mt_notes_add(&m, "THE ELITE-STYLE RADAR GAME"); mt_notes_add(&m, "PRONOUNCED E-SURGE"); mt_notes_add(&m, "GAME DESIGN JOHN EMMER");
  mt_notes_add(&m, "IMPLEMENTATION CLAUDE WITH OPUS 5.5"); mt_notes_add(&m, "VERSION 1.1.1"); mt_notes_end(&m);
  CHECK(m.n == 4 && m.more && !strcmp(m.line[3], "IMPLEMENTATION CLAUDE WITH OPUS 5.5..."));
  /* exactly full: no dots */
  mt_notes_start(&m, 2); mt_notes_add(&m, "ONE"); mt_notes_add(&m, "TWO"); mt_notes_end(&m);
  CHECK(m.n == 2 && !m.more && !strcmp(m.line[1], "TWO"));
  /* a full-width last line is shortened to fit the dots */
  mt_notes_start(&m, 1); mt_notes_add(&m, "ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKL"); mt_notes_add(&m, "X"); mt_notes_end(&m);
  CHECK(strlen(m.line[0]) == 38 && !strcmp(m.line[0] + 35, "..."));
  /* a line that wraps past the end counts as more */
  mt_notes_start(&m, 1); mt_notes_add(&m, "BY JEFF CORSIGLIA(GCE), CHRIS KING(GCE)"); mt_notes_end(&m);
  CHECK(m.more && !strcmp(m.line[0], "BY JEFF CORSIGLIA(GCE), CHRIS..."));
  mt_notes_start(&m, 4); mt_notes_end(&m); CHECK(m.n == 0 && !m.more);
  mt_notes_start(&m, -1); mt_notes_add(&m, "X"); mt_notes_end(&m); CHECK(m.n == 0);
}

static void test_hint(void)
{
  char h[64];
  mt_hint(h, sizeof h, 0, 1, 1, 1); CHECK(!strcmp(h, "START: 4   BACK: 1   FAVORITE: 3   MORE: 2"));
  CHECK(strlen(h) == 42);                                            /* the longest; the line holds 48 */
  mt_hint(h, sizeof h, 0, 1, 1, 0); CHECK(!strcmp(h, "START: 4   BACK: 1   FAVORITE: 3"));
  mt_hint(h, sizeof h, 0, 1, 0, 1); CHECK(!strcmp(h, "START: 4   BACK: 1   MORE: 2"));     /* an action, not a game */
  mt_hint(h, sizeof h, 1, 1, 0, 1); CHECK(!strcmp(h, "OPEN: 4   BACK: 1   MORE: 2"));
  mt_hint(h, sizeof h, 1, 0, 0, 0); CHECK(!strcmp(h, "OPEN: 4"));
}

static void put(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void test_um2(void)
{
  uint8_t h[512] = "2CMU"; um2_info u;
  put(h + 4, 1); put(h + 8, 1); put(h + 12, 0x20000000); put(h + 16, 100);
  CHECK(um2_parse(h, 512, 20 + 400, &u) == UM2_OK);
  CHECK(u.payload_off == 20 && u.bytes == 400 && u.count == 1);
  CHECK(um2_parse(h, 512, 419, &u) == UM2_E_TRUNC);
  CHECK(um2_parse(h, 8, 420, &u) == UM2_E_SHORT);
  um2_set_off(h, 1);
  CHECK(um2_is_off(h) && um2_parse(h, 512, 420, &u) == UM2_E_COUNT);
  uint32_t raw = h[8] | h[9] << 8 | h[10] << 16 | (uint32_t) h[11] << 24;
  CHECK(raw > UM2_MAX_BLOCKS);                 /* the firmware's own test: count > 20 fails */
  um2_set_off(h, 0);
  CHECK(!um2_is_off(h) && um2_parse(h, 512, 420, &u) == UM2_OK);
  put(h + 16, 0x20000); CHECK(um2_parse(h, 512, 1u << 24, &u) == UM2_OK);       /* exactly to the limit */
  put(h + 16, 0x20001); CHECK(um2_parse(h, 512, 1u << 24, &u) == UM2_E_RANGE);  /* would hit the launcher */
  put(h + 12, 0x10000000); put(h + 16, 1); CHECK(um2_parse(h, 512, 1u << 24, &u) == UM2_E_RANGE);
  memcpy(h, "XXXX", 4); CHECK(um2_parse(h, 512, 420, &u) == UM2_E_MAGIC);
}

static int empty_list_exists(const char *p, void *u) { (void) p; (void) u; return 0; }
static void empty_list(const char *d, void (*add)(const char *, void *), void *c, void *u) { (void) d; (void) add; (void) c; (void) u; }

static int s_extra_games;
static void many_games(MtTree *t, void *u)
{
  (void) u;
  for (int i = 0; i < s_extra_games; i++)
  {
    char title[MT_TITLE], image[16];
    snprintf(title, sizeof title, "GAME %04d", i);
    snprintf(image, sizeof image, "@fw:%d", i);
    mt_add(t, "A LIST", "ALL GAMES", title, image);
  }
}

/* built-in games: firmware category by default, moved, renamed or hidden by name, the unfiled ones by a folder = @fw rule */
static void builtins(MtTree *t, void *u)
{
  (void) u;
  mt_add_builtin(t, &recs, "ORIGINAL", "Berzerk", "@fw:0");
  mt_add_builtin(t, &recs, 0, "Rip Off", "@fw:1");
  mt_add_builtin(t, &recs, 0, "Vectrex UFO!", "@fw:2");
  mt_add_builtin(t, &recs, 0, "Engine Analyzer", "@fw:3");
  mt_add_builtin(t, &recs, "MUSIC", "Drum", "@fw:4");
}
static void test_builtins(void)
{
  static char a[2048];
  MtCard c = { fx, fl, builtins, 0, 0 };
  memset(&recs, 0, sizeof recs);
  mt_build(&tree, &recs, &c);                                       /* no rules: as the firmware files them */
  int all = find(0, "ALL GAMES");
  CHECK(all < 0);
  CHECK(find(find(0, "ORIGINAL"), "Berzerk") > 0);
  int hb = find(0, "HOMEBREW");
  CHECK(hb > 0 && find(hb, "Rip Off") > 0 && find(hb, "Vectrex UFO!") > 0 && find(hb, "Engine Analyzer") > 0);
  CHECK(mt_count(&tree, hb) == 3 && find(find(0, "ORIGINAL"), "Rip Off") < 0 && find(find(0, "MUSIC"), "Drum") > 0);
  strcpy(a, "[BUILT-IN]\nfolder = @fw\nimage = @fw\ncategory = HOMEBREW\nalso = ALL GAMES\n"
            "[RIP OFF]\nfile = @fw/Rip Off\ncategory = ORIGINAL\n"
            "[Engine]\nfile = @FW/engine analyzer\nhide = 1\n"
            "[DRUM MACHINE]\nfile = @fw/Drum\ncategory = TOYS\nalso = TOYS\n");
  memset(&recs, 0, sizeof recs);
  nmParseMenuText(&recs, a, "menu");
  mt_build(&tree, &recs, &c);
  hb = find(0, "HOMEBREW"); int org = find(0, "ORIGINAL"); all = find(0, "ALL GAMES");
  CHECK(hb > 0 && find(hb, "Vectrex UFO!") > 0 && mt_count(&tree, hb) == 1);
  CHECK(org > 0 && find(org, "RIP OFF") > 0 && find(org, "Berzerk") > 0);       /* moved; the section name is the title */
  CHECK(!strcmp(mt_image(&tree, find(org, "RIP OFF")), "@fw:1"));
  CHECK(find(hb, "Engine Analyzer") < 0 && all > 0 && find(all, "Engine") < 0);
  CHECK(all > 0 && find(all, "Berzerk") > 0 && find(all, "RIP OFF") > 0);       /* the rule's also */
  int toys = find(0, "TOYS"), music = find(0, "MUSIC");
  CHECK(toys > 0 && find(toys, "DRUM MACHINE") > 0 && mt_count(&tree, toys) == 1);
  CHECK((music < 0 || find(music, "DRUM MACHINE") < 0) && find(all, "DRUM MACHINE") < 0);
}

static int exists_default(const char *p, void *u)
{
  (void) u;
  return !strcmp(p, "menu/transfer.um2");
}
static void list_default(const char *d, void (*add)(const char *, void *), void *ctx, void *u)
{
  (void) u;
  if (!strcmp(d, ".")) add("a.um2", ctx);
  else if (!strcmp(d, "PORTS/UM2")) add("b.um2", ctx);
}

static int read_default(char *out, int max)
{
  FILE *f = fopen("card/menu/menu.default.ini", "r");
  if (!f) return -1;
  size_t n = fread(out, 1, (size_t) max - 1, f);
  fclose(f);
  out[n] = 0;
  return (int) n;
}

static int count_categories_nc(const char *title)
{
  int k = 0;
  for (int c = tree.node[0].first; c >= 0; c = tree.node[c].next)
    if (mt_is_category(&tree, c) && strcasecmp(mt_title(&tree, c), title) == 0) k++;
  return k;
}

static void builtins_default(MtTree *t, void *u)
{
  (void) u;
  mt_add_builtin(t, &recs, 0, "Rip Off", "@fw:rip");
  mt_add_builtin(t, &recs, 0, "Doodle LP", "@fw:doodle");
  mt_add_builtin(t, &recs, 0, "Zantis", "@fw:zantis");
  mt_add_builtin(t, &recs, "Original", "Berzerk", "@fw:berzerk");
}

/* The default rules put every built-in somewhere; the card's own files are left to BROWSE SD CARD; nothing is in ALL GAMES. */
static void test_no_all_games(void)
{
  static char a[8192];
  MtCard c = { fx, fl, builtins, 0, 0 };

  /* 1. no @fw rule: uncategorised games only in HOMEBREW, categorised ones only in their firmware category */
  memset(&recs, 0, sizeof recs);
  mt_build(&tree, &recs, &c);
  int hb = find(0, "HOMEBREW"), org = find(0, "ORIGINAL");
  CHECK(find(0, "ALL GAMES") < 0);
  CHECK(hb > 0 && find(hb, "Rip Off") > 0 && find(hb, "Vectrex UFO!") > 0 && find(hb, "Engine Analyzer") > 0);
  CHECK(org > 0 && find(org, "Berzerk") > 0 && find(org, "Rip Off") < 0 && find(hb, "Berzerk") < 0);

  /* 2. a folder = @fw rule puts the unfiled ones in its category, and all built-ins in its also */
  strcpy(a, "[BUILT-IN]\nfolder = @fw\nimage = @fw\ncategory = MISC\nalso = ALL GAMES\n");
  memset(&recs, 0, sizeof recs);
  nmParseMenuText(&recs, a, "menu");
  mt_build(&tree, &recs, &c);
  int misc = find(0, "MISC"), all = find(0, "ALL GAMES");
  org = find(0, "ORIGINAL");
  int music = find(0, "MUSIC");
  CHECK(misc > 0 && all > 0);
  CHECK(find(misc, "Rip Off") > 0 && find(misc, "Vectrex UFO!") > 0 && find(misc, "Engine Analyzer") > 0);
  CHECK(all > 0 && find(all, "Rip Off") > 0 && find(all, "Vectrex UFO!") > 0 && find(all, "Engine Analyzer") > 0);
  CHECK(org > 0 && find(org, "Berzerk") > 0 && find(all, "Berzerk") > 0 && find(misc, "Berzerk") < 0);
  CHECK(music > 0 && find(music, "Drum") > 0 && find(all, "Drum") > 0);

  /* 3. a file = @fw/<name> section's own category and also replace both the firmware's and the rule's */
  strcpy(a, "[BUILT-IN]\nfolder = @fw\nimage = @fw\ncategory = MISC\nalso = ALL GAMES\n"
            "[DRUM MACHINE]\nfile = @fw/Drum\ncategory = TOYS\nalso = TOYS\n");
  memset(&recs, 0, sizeof recs);
  nmParseMenuText(&recs, a, "menu");
  mt_build(&tree, &recs, &c);
  int toys = find(0, "TOYS");
  all = find(0, "ALL GAMES"); music = find(0, "MUSIC"); misc = find(0, "MISC");
  CHECK(toys > 0 && find(toys, "DRUM MACHINE") > 0 && mt_count(&tree, toys) == 1);
  CHECK((music < 0 || find(music, "DRUM MACHINE") < 0) && (misc < 0 || find(misc, "DRUM MACHINE") < 0));
  CHECK((all < 0 || find(all, "DRUM MACHINE") < 0));

  /* 4. the shipped file itself: no folder rules, so the card's own games are left to BROWSE SD CARD, at the top
   *    level; no MY GAMES or PORTS folder; one TRANSFER MODE */
  MtCard d = { exists_default, list_default, 0, 0, 0 };
  CHECK(read_default(a, sizeof a) > 0);
  memset(&recs, 0, sizeof recs);
  nmParseMenuText(&recs, a, "menu.default");
  mt_build(&tree, &recs, &d);
  int sys = find(0, "SYSTEM"), browse = find(0, "BROWSE SD CARD");
  CHECK(sys > 0 && find(0, "ALL GAMES") < 0 && find(0, "MY GAMES") < 0 && find(0, "PORTS") < 0);
  CHECK(browse > 0 && !mt_is_category(&tree, browse) && !strcmp(mt_image(&tree, browse), "@browse"));
  for (int c2 = 0; c2 < tree.n; c2++)
    CHECK(strcmp(mt_image(&tree, c2), "a.um2") != 0 && strcmp(mt_image(&tree, c2), "PORTS/UM2/b.um2") != 0);
  int transfers = 0;
  for (int c2 = sys > 0 ? tree.node[sys].first : -1; c2 >= 0; c2 = tree.node[c2].next)
    if (!strcmp(mt_title(&tree, c2), "TRANSFER MODE")) transfers++;
  CHECK(transfers == 1);

  /* 5. the shipped file's built-in sections: Rip Off joins the firmware's Original folder, Doodle LP goes to
   *    Lightpen, Zantis stays in HOMEBREW */
  MtCard e = { exists_default, list_default, builtins_default, 0, 0 };
  CHECK(read_default(a, sizeof a) > 0);
  memset(&recs, 0, sizeof recs);
  nmParseMenuText(&recs, a, "menu.default");
  mt_build(&tree, &recs, &e);
  int original = find(0, "Original"), lightpen = find(0, "Lightpen"), home = find(0, "HOMEBREW");
  CHECK(original > 0 && lightpen > 0 && home > 0);
  CHECK(count_categories_nc("Original") == 1);                      /* one folder, not two by case */
  int rip = original > 0 ? find(original, "RIP OFF") : -1;
  int ber = original > 0 ? find(original, "Berzerk") : -1;
  CHECK(rip > 0 && ber > 0);
  CHECK(lightpen > 0 && find(lightpen, "DOODLE LP") > 0);
  CHECK(home > 0 && find(home, "Zantis") > 0 && find(home, "RIP OFF") < 0);
  CHECK(find(0, "ALL GAMES") < 0);

  /* 6. the default file alone stays well under the 192 records a card has */
  static NmRecs alone;
  CHECK(read_default(a, sizeof a) > 0);
  memset(&alone, 0, sizeof alone);
  nmParseMenuText(&alone, a, "menu.default");
  printf("menu.default.ini: %d records\n", alone.n);
  CHECK(alone.n > 0 && alone.n <= 32);
}

/* a favorite that no rule lists: favorites.ini alone puts it in FAVORITES */
static void test_fav_standalone(void)
{
  /* 5. the table is about what 100 paths of 96 bytes were, and holds about three times as many of average length */
  printf("sizeof(MtFavs) = %d\n", (int) sizeof(MtFavs));
  CHECK(sizeof(MtFavs) <= 100 * NM_KEY_LEN + 1024);

  /* 1. sub/deep_game.bin is only in favorites.ini: no rule lists sub/, so it has to come from there */
  load_favs(DEF, 0, "[DEEP]\nfile = sub/deep_game.bin\n");
  int fav = find(0, "FAVORITES"), all = find(0, "ALL GAMES");
  CHECK(fav > 0 && mt_count(&tree, fav) == 1);
  int e = fav > 0 ? find(fav, "DEEP GAME") : -1;
  CHECK(e > 0);
  CHECK(e > 0 && !strcmp(mt_image(&tree, e), "sub/deep_game.bin"));
  CHECK(e > 0 && tree.node[e].fav == 1);
  CHECK(all < 0 || find(all, "DEEP GAME") < 0);   /* nothing else lists it */

  /* 2. a favorite that a rule lists is still listed once, from the rule */
  load_favs(DEF, 0, "[ZETA]\nfile = Zeta_game.um2\n");
  fav = find(0, "FAVORITES");
  CHECK(fav > 0 && mt_count(&tree, fav) == 1);
  e = fav > 0 ? find(fav, "ZETA GAME") : -1;
  CHECK(e > 0 && !strcmp(mt_image(&tree, e), "Zeta_game.um2"));

  /* 3. a favorite that is not on the card: skipped, and no FAVORITES folder when it was the only one */
  load_favs(DEF, 0, "[GONE]\nfile = sub/absent.bin\n");
  CHECK(find(0, "FAVORITES") < 0);

  /* 4. the same file in another case: the key matches, the image is kept as written in favorites.ini */
  load_favs(DEF, 0, "[DEEP]\nfile = SUB/Deep_Game.BIN\n");
  fav = find(0, "FAVORITES");
  CHECK(fav > 0 && mt_count(&tree, fav) == 1);
  e = fav > 0 ? find(fav, "DEEP GAME") : -1;
  CHECK(e > 0 && !strcmp(mt_image(&tree, e), "SUB/Deep_Game.BIN"));
}

/* favorites.ini on its own: the file = lines, the FAVORITES folder they make, and the duplicates it must not make */
static void test_favorites(void)
{
  static MtFavs favs;
  static char a[8192], many[16384];
  MtCard c;

  /* 1. only file = lines count; sections, also, comments and the commented-out file line are skipped */
  mt_favs_read(&favs,
    "; menu/favorites.ini: mine\n"
    "[VECTOR KONG]\n"
    "file = @fw/Rip Off\n"
    "also = FAVORITES\n"
    "[ENDURANCE]\n"
    "file = PORTS/UM2/Enduro.UM2\n"
    "; file = x.um2\n"
    "[RIP OFF AGAIN]\n"
    "file = @FW/rip off\n");
  CHECK(favs.n == 2);                                                 /* the repeat collapses */
  CHECK(mt_favs_has(&favs, "@fw/Rip Off"));
  CHECK(mt_favs_has(&favs, "PORTS/UM2/enduro.um2"));                  /* the same key: case does not matter */
  CHECK(!mt_favs_has(&favs, "x.um2"));                                /* the commented-out file */
  CHECK(!mt_favs_has(&favs, "PORTS/UM2/demon.um2"));

  /* 2. the card's two favorites are in ALL GAMES and in FAVORITES, sharing the title and image strings */
  mt_favs_read(&favs,
    "[ZETA]\nfile = Zeta_game.um2\nalso = FAVORITES\n"
    "[DEMON]\nfile = PORTS/UM2/DEMON.UM2\nalso = FAVORITES\n");
  c = (MtCard) { fx, fl, 0, 0, &favs };
  memset(&recs, 0, sizeof recs); strcpy(a, DEF); nmParseMenuText(&recs, a, "menu.default");
  mt_build(&tree, &recs, &c);
  int fav = find(0, "FAVORITES"), all = find(0, "ALL GAMES");
  CHECK(fav > 0 && mt_count(&tree, fav) == 2);
  int fz = fav > 0 ? find(fav, "ZETA GAME") : -1, az = all > 0 ? find(all, "ZETA GAME") : -1;
  int fd = fav > 0 ? find(fav, "DEMON") : -1, ad = all > 0 ? find(all, "DEMON") : -1;
  CHECK(fz > 0 && az > 0 && fd > 0 && ad > 0);
  CHECK(tree.node[fz].title == tree.node[az].title && tree.node[fz].image == tree.node[az].image);
  CHECK(tree.node[fd].title == tree.node[ad].title && tree.node[fd].image == tree.node[ad].image);

  /* 3. a built-in favorite: @fw/<name>, here in HOMEBREW and in FAVORITES */
  mt_favs_read(&favs, "[RIP OFF]\nfile = @fw/rip off\nalso = FAVORITES\n");
  memset(&recs, 0, sizeof recs);
  c = (MtCard) { fx, fl, builtins, 0, &favs };
  mt_build(&tree, &recs, &c);
  fav = find(0, "FAVORITES"); int hb = find(0, "HOMEBREW");
  int b = fav > 0 ? find(fav, "Rip Off") : -1, o = hb > 0 ? find(hb, "Rip Off") : -1;
  CHECK(fav > 0 && b > 0 && hb > 0 && o > 0);
  CHECK(tree.node[b].title == tree.node[o].title && tree.node[b].image == tree.node[o].image);

  /* 4. also = FAVORITES and the favorites list agree: the game is listed once */
  mt_favs_read(&favs, "[STAR WARS]\nfile = Star Wars.um2\nalso = FAVORITES\n");
  memset(&recs, 0, sizeof recs); strcpy(a, DEF); nmParseMenuText(&recs, a, "menu.default");
  strcpy(a, "[WARS]\nfile = Star Wars.um2\ntitle = STAR WARS\nalso = FAVORITES\n");
  nmParseMenuText(&recs, a, "menu");
  c = (MtCard) { fx, fl, 0, 0, &favs };
  mt_build(&tree, &recs, &c);
  fav = find(0, "FAVORITES"); all = find(0, "ALL GAMES");
  CHECK(fav > 0 && mt_count(&tree, fav) == 1);
  CHECK(fav > 0 && find(fav, "STAR WARS") > 0);
  CHECK(all > 0 && find(all, "STAR WARS") > 0);

  /* 5. no favorites at all: no FAVORITES folder (no list, and an empty list) */
  mt_favs_read(&favs, "");
  memset(&recs, 0, sizeof recs); strcpy(a, DEF); nmParseMenuText(&recs, a, "menu.default");
  c = (MtCard) { fx, fl, 0, 0, &favs };
  mt_build(&tree, &recs, &c);
  CHECK(find(0, "FAVORITES") < 0);
  c.favs = 0;
  mt_build(&tree, &recs, &c);
  CHECK(find(0, "FAVORITES") < 0);

  /* 6. favorites.ini is never read as a layer; any case */
  static char layers[3][MT_LAYER_NAME] = { "FAVORITES.INI", "favorites.ini", "aae.ini" };
  CHECK(mt_layer_sort(layers, 3) == 1);
  CHECK(!strcmp(layers[0], "aae.ini"));

  /* 7. past MT_FAV_MAX the rest are ignored */
  many[0] = 0;
  for (int i = 0; i <= MT_FAV_MAX; i++)
  {
    char line[32];
    snprintf(line, sizeof line, "file = game%03d.um2\n", i);
    strcat(many, line);
  }
  mt_favs_read(&favs, many);
  CHECK(favs.n == MT_FAV_MAX && favs.full);
  /* long paths run the pool out before the count: 3 keys of about 90 characters per 300 bytes */
  many[0] = 0;
  for (int i = 0; i < 200; i++)
  {
    char line[160];
    snprintf(line, sizeof line, "file = folder/with/a/very/long/path/that/goes/on/and/on/to/fill/the/pool/of/names/game%03d.um2\n", i);
    strcat(many, line);
  }
  mt_favs_read(&favs, many);
  CHECK(favs.n > 90 && favs.n < 200 && favs.full && favs.used <= MT_FAV_POOL);
  CHECK(mt_favs_has(&favs, "folder/with/a/very/long/path/that/goes/on/and/on/to/fill/the/pool/of/names/game000.um2"));
}

/* the mark on favorites: on every copy outside FAVORITES, never inside it, never on other games */
static void test_fav_mark(void)
{
  static MtFavs favs;
  static char a[512];
  strcpy(a, "[Z]\nfile = Zeta_game.um2\n");
  mt_favs_read(&favs, a);
  MtCard c = { fx, fl, 0, 0, &favs };
  memset(&recs, 0, sizeof recs);
  strcpy(a, DEF); nmParseMenuText(&recs, a, "menu.default");
  strcpy(a, "[ZETA]\nfile = Zeta_game.um2\nalso = TWO PLAYERS\n"); nmParseMenuText(&recs, a, "menu");
  mt_build(&tree, &recs, &c);
  int all = find(0, "ALL GAMES"), fav = find(0, "FAVORITES"), two = find(0, "TWO PLAYERS");
  CHECK(all > 0 && fav > 0 && two > 0);
  CHECK(tree.node[find(all, "ZETA")].fav == 1);
  CHECK(tree.node[find(two, "ZETA")].fav == 1);                     /* also a copy listed by also = */
  CHECK(tree.node[find(fav, "ZETA")].fav == 0);                     /* not inside FAVORITES */
  CHECK(tree.node[find(all, "STAR WARS")].fav == 0);                /* not a favorite */
  CHECK(tree.node[all].fav == 0 && tree.node[fav].fav == 0);        /* folders never */
  c.favs = 0;
  mt_build(&tree, &recs, &c);
  CHECK(tree.node[find(find(0, "ALL GAMES"), "ZETA")].fav == 0);    /* no list: no marks */
}

static void test_capacity(void)
{
  static MtTree t;
  static NmRecs none;
  MtCard card = { empty_list_exists, empty_list, many_games, 0, 0 };

  /* more than the old limit of 480 entries: nothing lost, sorted, each game in both lists */
  s_extra_games = 700;
  mt_build(&t, &none, &card);
  CHECK(t.dropped == 0);
  CHECK(t.n == 1 + 2 + 1400);
  int lists[2] = { mt_nth(&t, 0, 0), mt_nth(&t, 0, 1) };
  CHECK(!strcmp(mt_title(&t, lists[0]), "A LIST") && !strcmp(mt_title(&t, lists[1]), "ALL GAMES"));
  CHECK(mt_count(&t, lists[0]) == 700 && mt_count(&t, lists[1]) == 700);
  int a = mt_nth(&t, lists[0], 699), b = mt_nth(&t, lists[1], 699);
  CHECK(!strcmp(mt_title(&t, a), "GAME 0699") && !strcmp(mt_image(&t, a), "@fw:699"));
  CHECK(t.node[a].title == t.node[b].title && t.node[a].image == t.node[b].image);   /* one copy of the strings */
  CHECK(mt_is_category(&t, lists[0]) && !mt_is_category(&t, a));

  /* past the entry limit: reported, and what fits stays good */
  s_extra_games = MT_MAX_NODES;
  mt_build(&t, &none, &card);
  CHECK(t.dropped > 0 && t.n == MT_MAX_NODES);
  CHECK(t.used <= MT_POOL);

  /* the names run out first when they are long */
  mt_clear(&t, "ROOT");
  char longname[192];
  memset(longname, 'X', sizeof longname - 1); longname[sizeof longname - 1] = 0;
  int made = 0;
  for (int i = 0; i < MT_MAX_NODES; i++) if (mt_new_entry(&t, "GAME", longname, 0) >= 0) made++;
  CHECK(t.dropped > 0 && made > 0 && made < MT_MAX_NODES && t.used <= MT_POOL);
  CHECK(!strcmp(mt_image(&t, made), longname));                      /* a long path is kept whole */
  mt_clear(&t, "ROOT");
  CHECK(t.n == 1 && t.dropped == 0 && !strcmp(mt_title(&t, 0), "ROOT"));
}

/* category = / puts an entry at the top level: after the folders and before SYSTEM. */
/* The top level, SYSTEM aside, is in title order (case ignored), folders and entries mixed. */
static int top_sorted(void)
{
  char prev[MT_TITLE] = "";
  for (int c = tree.node[0].first; c >= 0; c = tree.node[c].next)
  {
    const char *t = mt_title(&tree, c);
    if (!strcmp(t, "SYSTEM")) continue;
    if (strcasecmp(prev, t) > 0) return 0;
    snprintf(prev, sizeof prev, "%s", t);
  }
  return 1;
}

static void test_top_level(void)
{
  int i, last_folder, at;

  /* 1. an explicit top-level entry, a game or action, sits among the folders by title, before SYSTEM */
  load(DEF, "[BROWSE SD CARD]\ncategory = /\nimage = @browse\n[ZZZ]\ncategory = ZZZ\nimage = @z\n");
  int browse = find(0, "BROWSE SD CARD");
  CHECK(browse > 0 && tree.node[browse].parent == 0 && !mt_is_category(&tree, browse));
  CHECK(top_sorted());
  i = 0; at = -1; last_folder = -1;
  int sys_at = -1;
  for (int c = tree.node[0].first; c >= 0; c = tree.node[c].next, i++)
  {
    if (!strcmp(mt_title(&tree, c), "ZZZ")) last_folder = i;
    if (c == browse) at = i;
    if (!strcmp(mt_title(&tree, c), "SYSTEM")) sys_at = i;
  }
  CHECK(at >= 0 && at < last_folder && sys_at > last_folder);   /* a folder after it by title stays after it */

  /* 2. two top-level entries come out sorted by title */
  load(DEF, "[B]\ncategory = /\nimage = @b\n[A]\ncategory = /\nimage = @a\n");
  int ia = -1, ib = -1;
  i = 0;
  for (int c = tree.node[0].first; c >= 0; c = tree.node[c].next, i++)
  {
    if (!strcmp(mt_title(&tree, c), "A")) ia = i;
    if (!strcmp(mt_title(&tree, c), "B")) ib = i;
  }
  CHECK(ia >= 0 && ib >= 0 && ia < ib);

  /* 3. spaces around the / do not matter */
  load(DEF, "[SPACED]\ncategory =  /  \nimage = @spaced\n");
  int sp = find(0, "SPACED");
  CHECK(sp > 0 && tree.node[sp].parent == 0 && !mt_is_category(&tree, sp));
  CHECK(top_sorted());
  (void) last_folder;

  /* 4. a real path still nests: A / B makes folder B in folder A */
  load(DEF, "[NEST]\ncategory = A / B\nimage = @nest\n");
  int a = find(0, "A"), ab = a > 0 ? find(a, "B") : -1;
  CHECK(a > 0 && mt_is_category(&tree, a));
  CHECK(ab > 0 && mt_is_category(&tree, ab));
  CHECK(find(ab, "NEST") > 0);

  /* 5. with no SYSTEM folder the order is by title alone: TOP after FOLDER, AAA before it */
  load("[FOLDER]\ncategory = FOLDER\nimage = @folder\n", "[TOP]\ncategory = /\nimage = @top\n[AAA]\ncategory = /\nimage = @aaa\n");
  int top = find(0, "TOP");
  CHECK(top > 0 && !mt_is_category(&tree, top));
  CHECK(mt_nth(&tree, 0, mt_count(&tree, 0) - 1) == top);
  CHECK(mt_nth(&tree, 0, 0) == find(0, "AAA"));

  /* 6. also = / adds a copy at the top level as well */
  load("[FOLDER]\ncategory = FOLDER\nimage = @folder\n",
       "[BOTH]\ncategory = FOLDER\nimage = @both\nalso = /\n");
  int both = find(0, "BOTH");
  CHECK(both > 0 && tree.node[both].parent == 0 && !mt_is_category(&tree, both));
  CHECK(find(find(0, "FOLDER"), "BOTH") > 0);

  /* 7. a folder rule with category = / puts its files at the top level too */
  load("[ROOT FILES]\ncategory = /\nfolder = .\nfiles = um2\nimage = {dir}/{file}\n", 0);
  int zeta = find(0, "ZETA GAME"), wars = find(0, "STAR WARS");
  CHECK(zeta > 0 && wars > 0);
  CHECK(tree.node[zeta].parent == 0 && tree.node[wars].parent == 0);
  CHECK(!mt_is_category(&tree, zeta) && !mt_is_category(&tree, wars));
  CHECK(!strcmp(mt_image(&tree, zeta), "Zeta_game.um2"));

  /* 8. the FAVORITES folder, when it exists, takes its place by title like any folder: after BROWSE SD CARD */
  load_favs(DEF, "[BROWSE SD CARD]\ncategory = /\nimage = @browse\n",
            "[Z]\nfile = Zeta_game.um2\n");
  int favf = find(0, "FAVORITES"), br = find(0, "BROWSE SD CARD"), ifav = -1, ibr = -1;
  CHECK(favf > 0 && mt_is_category(&tree, favf) && br > 0);
  i = 0;
  for (int c = tree.node[0].first; c >= 0; c = tree.node[c].next, i++) { if (c == favf) ifav = i; if (c == br) ibr = i; }
  CHECK(ibr >= 0 && ifav > ibr && mt_nth(&tree, 0, 0) != favf);
  CHECK(top_sorted());

  /* mt_back at the top level stays a no-op */
  MtNav n; mt_nav_init(&n);
  CHECK(mt_back(&n, &tree) == 0);
}

/* The preview-style text of a built tree: one line per entry, then its note lines, in menu order. */
static char dump_buf[65536];
static int dump_len;

static void dump_add(const char *s)
{
  int n = (int) strlen(s);
  if (dump_len + n >= (int) sizeof dump_buf) return;
  memcpy(dump_buf + dump_len, s, (size_t) n);
  dump_len += n;
  dump_buf[dump_len] = 0;
}

static void dump_node(const MtTree *t, const NmRecs *r, int dir, int depth)
{
  char line[256];
  for (int c = t->node[dir].first; c >= 0; c = t->node[c].next)
  {
    snprintf(line, sizeof line, "%*s%s%s%s\n", depth * 2, "", mt_title(t, c), mt_is_category(t, c) ? "/" : "  <- ", mt_image(t, c));
    dump_add(line);
    if (t->node[c].rec >= 0)
    {
      const NmRec *q = &r->rec[t->node[c].rec];
      const char *p = nmStr(r, q->rows);
      for (int i = 0; i < q->nrows; i++)
      {
        snprintf(line, sizeof line, "%*s   : %s\n", depth * 2, "", p);
        dump_add(line);
        p += strlen(p) + 1;
      }
    }
    if (mt_is_category(t, c)) dump_node(t, r, c, depth + 1);
  }
}

/* The menu from the shipped file plus the layers test_tree and test_layers use, as the menu shows it. */
/* Taken from the code before the compact records; BROWSE SD CARD since moved to its place by title,
 * FIRMWARE INFO since renamed DIAGNOSTICS,
 * FAVORITES since sorted by title, NEW since added. */
static const char EXPECTED_TREE[] =
"ALL GAMES/\n"
"  DEMON  <- PORTS/UM2/demon.um2\n"
"  ZETA GAME  <- Zeta_game.um2\n"
"BROWSE SD CARD  <- @browse\n"
"   : EVERY FOLDER ON THE CARD, AND THE\n"
"   : UM2, BIN AND VEC FILES IN IT.\n"
"FAVORITES/\n"
"  MHAVOC  <- mhavoc.um2\n"
"NEW  <- @new\n"
"   : THE NEWEST GAME FILES ON THE CARD, BY\n"
"   : THEIR FILE DATES: 10 UNLESS MENU/\n"
"   : SETTINGS.INI SAYS NEW = 5 (UP TO 20).\n"
"TWO PLAYERS/\n"
"  MHAVOC  <- mhavoc.um2\n"
"SYSTEM/\n"
"  DIAGNOSTICS  <- @fwinfo\n"
"     : WHAT THE MENU FOUND AND HOW IT RUNS,\n"
"     : FOR REPORTING A PROBLEM.\n"
"  CALIBRATE SCREEN  <- @calibrate\n"
"     : FIXES A DISTORTED MENU OR GAMES, LIKE\n"
"     : UVMC2_CALIBRATION. STICK UP/DOWN PICKS\n"
"     : A VALUE, LEFT/RIGHT CHANGES IT,\n"
"     : 1 SWAPS THE FIGURE, 4 SAVES IT TO\n"
"     : CONFIG/UVM2.CFG.\n"
"  TURN THIS MENU OFF  <- @stock\n"
"     : RESTARTS IN THE CARTRIDGE'S OWN MENU,\n"
"     : WHICH THEN COMES UP AT EVERY POWER-ON.\n"
"     : TO TURN THIS MENU BACK ON, START\n"
"     : MENU.UM2 FROM THAT MENU'S BROWSE.\n";

static void test_compact_records(void)
{
  static char many_text[131072];
  static NmRecs many;
  static char t[1024];
  int off;

  /* 1. the record is small, and the whole table with its pool fits in no more RAM than before */
  printf("sizeof(NmRec) = %d, sizeof(NmRecs) = %d\n", (int) sizeof(NmRec), (int) sizeof(NmRecs));
  CHECK(sizeof(NmRec) <= 32);
  CHECK(sizeof(NmRecs) <= 150 * 1024);

  /* 2. a thousand distinct sections: no drops, and the last one reads back exactly */
  off = 0;
  for (int i = 0; i < 1000; i++)
    off += snprintf(many_text + off, sizeof many_text - (size_t) off,
                    "[GAME %d]\nimage = gn.um2\ncategory = C%d\ntext = NOTE %d\n", i, i % 20, i);
  CHECK(off < (int) sizeof many_text);
  memset(&many, 0, sizeof many);
  nmParseMenuText(&many, many_text, "many");
  CHECK(many.n == 1000 && many.dropped == 0);
  const NmRec *last = &many.rec[999];
  CHECK(!strcmp(nmStr(&many, last->title), "GAME 999"));
  CHECK(!strcmp(nmStr(&many, last->image), "gn.um2"));
  CHECK(!strcmp(nmStr(&many, last->category), "C19"));
  CHECK(last->nrows == 1 && !strcmp(nmStr(&many, last->rows), "NOTE 999"));

  /* 3. one more than the table holds: it is dropped, and the rest stay good */
  off = 0;
  for (int i = 0; i < 1025; i++)
    off += snprintf(many_text + off, sizeof many_text - (size_t) off,
                    "[GAME %d]\nimage = gn.um2\ncategory = C%d\ntext = NOTE %d\n", i, i % 20, i);
  CHECK(off < (int) sizeof many_text);
  memset(&many, 0, sizeof many);
  nmParseMenuText(&many, many_text, "many");
  CHECK(many.n == 1024 && many.dropped == 1);

  /* 4. a later section of the same name replaces only what it sets */
  static NmRecs ov;
  memset(&ov, 0, sizeof ov);
  strcpy(t, "[A]\nimage = a.um2\ncategory = X\n"); nmParseMenuText(&ov, t, "one");
  strcpy(t, "[A]\ncategory = Y\n"); nmParseMenuText(&ov, t, "two");
  CHECK(ov.n == 1);
  const NmRec *ra = nmFindRec(&ov, "e.a");
  CHECK(ra != 0);
  CHECK(!strcmp(nmStr(&ov, ra->image), "a.um2"));
  CHECK(!strcmp(nmStr(&ov, ra->category), "Y"));
  strcpy(t, "[A]\nalso = Z\n"); nmParseMenuText(&ov, t, "three");
  CHECK(ov.n == 1 && !strcmp(nmStr(&ov, ra->category), "Y") && !strcmp(nmStr(&ov, ra->also), "Z"));

  /* 5. an unset text field is the shared empty string, never a null pointer */
  static NmRecs one;
  memset(&one, 0, sizeof one);
  strcpy(t, "[ONLY]\nimage = only.um2\n"); nmParseMenuText(&one, t, "only");
  const NmRec *q = &one.rec[0];
  const uint16_t fields[] = { q->key, q->path, q->files, q->title, q->image, q->category, q->needs, q->also, q->name };
  for (int i = 0; i < (int) (sizeof fields / sizeof fields[0]); i++)
    CHECK(nmStr(&one, fields[i]) != 0);
  CHECK(!strcmp(nmStr(&one, q->title), "ONLY") && !strcmp(nmStr(&one, q->image), "only.um2"));
  CHECK(nmStr(&one, q->path)[0] == 0 && nmStr(&one, q->files)[0] == 0 && nmStr(&one, q->category)[0] == 0);
  CHECK(nmStr(&one, q->needs)[0] == 0 && nmStr(&one, q->also)[0] == 0);

  /* 6. a full pool: overrides leave the fields as they were, count the drops, and stop at the end */
  typedef struct { NmRecs r; unsigned char guard; } Guarded;
  static Guarded g;
  memset(&g, 0, sizeof g);
  g.guard = 0xA5;
  strcpy(t, "[A]\nimage = a.um2\ncategory = X\n");
  nmParseMenuText(&g.r, t, "one");
  CHECK(g.r.n == 1);
  int before = g.r.dropped;
  g.r.used = NM_POOL - 2;                          /* room for two bytes: no long value fits */
  int used_before = g.r.used;
  strcpy(t, "[A]\nimage = a-very-long-image-name.um2\ncategory = ALSO-LONG\ntext = A NOTE THAT CANNOT FIT\n");
  nmParseMenuText(&g.r, t, "two");
  CHECK(g.r.dropped > before);
  CHECK(!strcmp(nmStr(&g.r, g.r.rec[0].image), "a.um2"));
  CHECK(!strcmp(nmStr(&g.r, g.r.rec[0].category), "X"));
  CHECK(g.r.rec[0].nrows == 0);
  CHECK(g.r.used == used_before && g.guard == 0xA5);

  /* 7. the same tree, entry for entry, as the code before the compact records made it */
  static char a[8192];
  memset(&recs, 0, sizeof recs);
  CHECK(read_default(a, sizeof a) > 0);
  nmParseMenuText(&recs, a, "menu.default");
  strcpy(a, DEF); nmParseMenuText(&recs, a, "extra");
  strcpy(a, USER); nmParseMenuText(&recs, a, "menu");
  mt_build(&tree, &recs, &card);
  dump_len = 0;
  dump_buf[0] = 0;
  dump_node(&tree, &recs, 0, 0);
  CHECK(!strcmp(dump_buf, EXPECTED_TREE));
  if (strcmp(dump_buf, EXPECTED_TREE)) printf("got:\n%s\nwant:\n%s", dump_buf, EXPECTED_TREE);
}

/* BROWSE SD CARD's "unmapped" view: a file is mapped if an entry, a file = section that hides it or a folder rule names it. */
static void test_file_mapped(void)
{
  static char a[2048];
  memset(&recs, 0, sizeof recs);
  strcpy(a,
    "[ROOT]\ncategory = HOMEBREW\nfolder = .\nfiles = um2\nimage = {dir}/{file}\n"
    "[PORTS]\ncategory = UVMC2\nfolder = PORTS/UM2/\nfiles = um2 bin\nimage = {dir}/{file}\n"
    "[OFF RULE]\ncategory = X\nfolder = off\nfiles = bin\nimage = {dir}/{file}\nhide = 1\n"
    "[ENT]\ncategory = ARCADE\nimage = sub/deep_game.bin\n"
    "[HID ENT]\ncategory = ARCADE\nimage = sub/ent_hidden.bin\nhide = 1\n"
    "[HID1]\nfile = hidden.um2\nhide = 1\n"
    "[HID2]\nfile = elsewhere/x.bin\nhide = 1\n"
    "[MOVED]\nfile = sub2/moved.bin\ncategory = ARCADE\n");
  nmParseMenuText(&recs, a, "menu");
  CHECK(mt_file_mapped(&recs, "Zeta_game.um2") == 1);              /* a folder rule takes it */
  CHECK(mt_file_mapped(&recs, "ZETA_GAME.UM2") == 1);              /* the card does not care about case */
  CHECK(mt_file_mapped(&recs, "AlienStorm.bin") == 0);             /* the root rule takes .um2 only */
  CHECK(mt_file_mapped(&recs, "hidden.um2") == 1);                 /* hidden is a decision about it: mapped */
  CHECK(mt_file_mapped(&recs, "elsewhere/x.bin") == 1);            /* hidden, in a folder no rule covers */
  CHECK(mt_file_mapped(&recs, "MENU.um2") == 1);                   /* the menu's own files: nothing to map them to */
  CHECK(mt_file_mapped(&recs, "autostart.um2") == 1);
  CHECK(mt_file_mapped(&recs, "PORTS/UM2/demon.um2") == 1);
  CHECK(mt_file_mapped(&recs, "ports/um2/DEMON.BIN") == 1);        /* this rule takes .bin too */
  CHECK(mt_file_mapped(&recs, "PORTS/UM2/deep/x.um2") == 0);       /* a rule does not reach into subfolders */
  CHECK(mt_file_mapped(&recs, "PORTS/demon.um2") == 0);
  CHECK(mt_file_mapped(&recs, "off/a.bin") == 0);                  /* a hidden rule maps nothing */
  CHECK(mt_file_mapped(&recs, "sub/deep_game.bin") == 1);          /* an entry names it */
  CHECK(mt_file_mapped(&recs, "SUB/Deep_Game.BIN") == 1);
  CHECK(mt_file_mapped(&recs, "sub/ent_hidden.bin") == 1);         /* a hidden entry names it too */
  CHECK(mt_file_mapped(&recs, "sub2/moved.bin") == 0);             /* a file = section lists nothing without a rule behind it */
  CHECK(mt_file_mapped(&recs, "sub/other.bin") == 0);
}

/* examples/menu.ini is copied by other people: it must stay parseable and say what its comments say. */
static void test_example_ini(void)
{
  static char a[16384];
  FILE *f = fopen("examples/menu.ini", "rb");
  CHECK(f != 0);
  if (!f) return;
  size_t n = fread(a, 1, sizeof a - 1, f);
  fclose(f);
  a[n] = 0;
  memset(&recs, 0, sizeof recs);
  nmParseMenuText(&recs, a, "menu");
  CHECK(recs.n == 16 && recs.dropped == 0);                         /* its 16 sections */
  /* the sections its numbered parts promise: a root rule, a PORTS rule, a hiding section and a one-file entry */
  CHECK(mt_file_mapped(&recs, "anything.um2") == 1);                 /* part 1: the root rule takes um2 */
  CHECK(mt_file_mapped(&recs, "PORTS/UM2/enduro.um2") == 1);
  CHECK(mt_file_mapped(&recs, "unlisted.txt") == 0);
  CHECK(mt_file_mapped(&recs, "mhavoc.um2") == 1);                   /* part 3: an entry */
  CHECK(mt_file_mapped(&recs, "ZSB.bin") == 1);                      /* part 4: hidden */
  CHECK(mt_file_mapped(&recs, "sub/unlisted.bin") == 0);
}

/* A folder rule with files = and no image lists each file by its own path, as image = {dir}/{file} says. */
static void test_folder_default_image(void)
{
  static char a[256];
  strcpy(a, "[SHORT]\ncategory = ARCADE\nfolder = PORTS/UM2\nfiles = um2\n");
  memset(&recs, 0, sizeof recs);
  nmParseMenuText(&recs, a, "menu");
  const NmRec *r = nmFindRec(&recs, "f.ports_um2");
  CHECK(r && r->kind == NM_REC_FOLDER && !strcmp(nmStr(&recs, r->image), "{dir}/{file}"));
  CHECK(mt_file_mapped(&recs, "PORTS/UM2/x.um2") == 1 && mt_file_mapped(&recs, "PORTS/UM2/x.bin") == 0);
  /* a folder section with no files = is still a group of settings, not a rule (hiding a folder of built-ins) */
  strcpy(a, "[LIGHTPEN]\nfolder = lightpen\nhide = 1\n");
  memset(&recs, 0, sizeof recs);
  nmParseMenuText(&recs, a, "menu");
  CHECK(recs.n == 1 && recs.rec[0].kind == NM_REC_NOTES);
}

int main(void)
{
  test_compact_records(); test_top_level(); test_capacity(); test_no_all_games(); test_fav_mark(); test_builtins(); test_fav_standalone();
  test_file_mapped(); test_folder_default_image();
  test_example_ini(); test_favorites(); test_fav_toggle(); test_settings(); test_tree(); test_layers(); test_three_layers(); test_nav(); test_place(); test_scroll_marks(); test_wrap(); test_notes(); test_hint(); test_um2();
  printf("%d passed, %d failed\n", passes, fails);
  return fails != 0;
}
