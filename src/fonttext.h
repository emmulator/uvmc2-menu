#ifndef FONTTEXT_H
#define FONTTEXT_H
/* Text in the menu's font (font.c) through VPy's stroke buffer: the menu and transfer.um2 both draw with these, so
 * their text always matches. x, y: the top-left corner in VPy units; size: points (the list uses 8). */
void font_text(int x, int y, const char *s, int size);
/* The same for small text after a long jump: a very dim lead-in stroke in the direction of the first stroke, without
 * which the first letter came out bent on the console (3 clean, 1 and 2 and O bent). */
void font_text_small(int x, int y, const char *s, int size);
#endif
