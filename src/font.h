#ifndef FONT_H
#define FONT_H
#include <stdint.h>
/* Grid step in deflection units (127 = one VPy unit) per point of text size: size 8 = a 8.3 unit wide character. */
#define FONT_STEP   22
#define FONT_ADV    6               /* grid steps per character: 4 + a gap of 2 */
typedef void (*font_line_fn)(int32_t x0, int32_t y0, int32_t x1, int32_t y1, void *u);
const char *font_glyph_str(int c);  /* lower case folds up; 0 for a space or a character it does not have */
int  font_strokes(int c);           /* strokes in the glyph */
int  font_paths(int c);             /* pen paths: blanked jumps + 1, 0 if empty */
/* x, y: the top-left corner in VPy units, as vpy_print_text. Returns the strokes drawn. */
int  font_draw(int x, int y, const char *s, int size, font_line_fn line, void *u);
/* The first stroke of s, in deflection units, as font_draw places it. 0 if s draws nothing. */
int  font_first_stroke(int x, int y, const char *s, int size, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1);
#endif
