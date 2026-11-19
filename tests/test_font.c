#include "font.h"
#include "font_data.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("FAIL %d: %s\n", __LINE__, #x); fails++; } else passes++; } while (0)
static int fails, passes;
typedef struct { int32_t minx, maxx, miny, maxy; int n; } B;
static void bx(int32_t a, int32_t b, int32_t c, int32_t d, void *u)
{
  B *k = u;
  int32_t xs[2] = { a, c }, ys[2] = { b, d };
  for (int i = 0; i < 2; i++)
  {
    if (xs[i] < k->minx) k->minx = xs[i];
    if (xs[i] > k->maxx) k->maxx = xs[i];
    if (ys[i] < k->miny) k->miny = ys[i];
    if (ys[i] > k->maxy) k->maxy = ys[i];
  }
  k->n++;
}
int main(void)
{
  /* every glyph is well formed: even runs of at least 2 points, on the grid */
  for (int c = FONT_FIRST; c <= FONT_LAST; c++)
  {
    const char *g = font_glyph_str(c);
    if (c >= 'a') continue;
    CHECK(g != 0 && g[0] != 0);
    int run = 0, bad = 0;
    for (const char *p = g;; p++)
    {
      if (*p == ' ' || *p == 0) { if (run < 4 || run % 2) bad = 1; run = 0; if (!*p) break; continue; }
      if (*p < '0' || (run % 2 == 0 ? *p > '4' : *p > '6')) bad = 1;
      run++;
    }
    if (bad) printf("bad glyph %c: %s\n", c, g);
    CHECK(!bad);
  }
  /* letters and digits can be told apart: no two the same */
  const char *set = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  for (const char *a = set; *a; a++) for (const char *b = a + 1; *b; b++)
    if (!strcmp(font_glyph_str(*a), font_glyph_str(*b))) { printf("same: %c %c\n", *a, *b); fails++; }
  CHECK(font_strokes('A') == 2 && font_paths('A') == 1);
  CHECK(font_strokes('a') == 2);
  CHECK(font_strokes(' ') == 0 && font_glyph_str(' ') == 0 && font_glyph_str(200) == 0);
  /* where it lands: same spot and width as the layout expects */
  B k = { 1 << 30, -(1 << 30), 1 << 30, -(1 << 30), 0 };
  int n = font_draw(-100, 80, "HH", 8, bx, &k);
  CHECK(n == 6 && k.n == 6);
  CHECK(k.minx == -100 * 127);
  CHECK(k.maxx == -100 * 127 + (6 + 4) * 8 * 22);
  CHECK(k.miny == 72 * 127 && k.maxy == 72 * 127 + 6 * 8 * 22);
  /* the first stroke, which the lead-in lines up with */
  int32_t a0, b0, a1, b1, st = 5 * FONT_STEP;
  CHECK(font_first_stroke(-100, 80, "  T", 5, &a0, &b0, &a1, &b1) == 1);         /* T starts along its top */
  CHECK(a0 == -100 * 127 + 2 * FONT_ADV * st && b0 == 72 * 127 + 6 * st && a1 == a0 + 4 * st && b1 == b0);
  CHECK(font_first_stroke(0, 8, "0", 5, &a0, &b0, &a1, &b1) == 1);               /* 0 starts up its left side */
  CHECK(a0 == 0 && b0 == 0 && a1 == 0 && b1 == 6 * st);
  CHECK(font_first_stroke(0, 0, "   ", 5, &a0, &b0, &a1, &b1) == 0);
  printf("%d passed, %d failed\n", passes, fails);
  return fails != 0;
}
