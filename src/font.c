#include "font.h"
#include "font_data.h"

const char *font_glyph_str(int c)
{
  if (c >= 'a' && c <= 'z') c -= 32;
  if (c < FONT_FIRST || c > FONT_LAST) return 0;
  return font_glyphs[c - FONT_FIRST];
}

int font_paths(int c)
{
  const char *g = font_glyph_str(c);
  int n = 0, in = 0;
  if (!g) return 0;
  for (; *g; g++) { if (*g == ' ') in = 0; else if (!in) { in = 1; n++; } }
  return n;
}

int font_strokes(int c)
{
  const char *g = font_glyph_str(c);
  int n = 0, digits = 0;
  if (!g) return 0;
  for (;; g++)
  {
    if (*g == ' ' || *g == 0) { if (digits >= 4) n += digits / 2 - 1; digits = 0; if (!*g) break; }
    else digits++;
  }
  return n;
}

int font_draw(int x, int y, const char *s, int size, font_line_fn line, void *u)
{
  const int32_t step = size * FONT_STEP;
  int32_t ox = (int32_t) x * 127, oy = ((int32_t) y - 8) * 127;
  int drawn = 0;
  for (; *s; s++, ox += FONT_ADV * step)
  {
    const char *g = font_glyph_str((unsigned char) *s);
    if (!g) continue;
    while (*g)
    {
      int32_t px = ox + (g[0] - '0') * step, py = oy + (g[1] - '0') * step;
      g += 2;
      while (*g && *g != ' ')
      {
        int32_t nx = ox + (g[0] - '0') * step, ny = oy + (g[1] - '0') * step;
        line(px, py, nx, ny, u);
        drawn++;
        px = nx; py = ny;
        g += 2;
      }
      if (*g == ' ') g++;
    }
  }
  return drawn;
}

int font_first_stroke(int x, int y, const char *s, int size, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
  const int32_t step = size * FONT_STEP;
  int32_t ox = (int32_t) x * 127, oy = ((int32_t) y - 8) * 127;
  for (; *s; s++, ox += FONT_ADV * step)
  {
    const char *g = font_glyph_str((unsigned char) *s);
    if (!g) continue;
    *x0 = ox + (g[0] - '0') * step;
    *y0 = oy + (g[1] - '0') * step;
    *x1 = ox + (g[2] - '0') * step;
    *y1 = oy + (g[3] - '0') * step;
    return 1;
  }
  return 0;
}
