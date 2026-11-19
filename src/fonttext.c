/* fonttext.c: see fonttext.h. */
#include "fonttext.h"
#include "font.h"
#include <vpy.h>

#define TEXT_LIGHT 90
#define LEAD_LEN   (2 * 127)
#define LEAD_LIGHT 1

static void line(int32_t a, int32_t b, int32_t c, int32_t d, void *u) { (void) u; vpy_draw_line_dev(a, b, c, d, TEXT_LIGHT); }

void font_text(int x, int y, const char *s, int size) { font_draw(x, y, s, size, line, 0); }

void font_text_small(int x, int y, const char *s, int size)
{
  int32_t x0, y0, x1, y1;
  if (font_first_stroke(x, y, s, size, &x0, &y0, &x1, &y1))
  {
    int32_t dx = x1 - x0, dy = y1 - y0;
    int32_t m = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (m > 0) vpy_draw_line_dev(x0 - dx * LEAD_LEN / m, y0 - dy * LEAD_LEN / m, x0, y0, LEAD_LIGHT);
  }
  font_text(x, y, s, size);
}
