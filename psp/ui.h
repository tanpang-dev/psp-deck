#ifndef DECK_UI_H
#define DECK_UI_H

#include <intraFont.h>

#define UI_W 480
#define UI_H 272

/* Colors are 0xAABBGGRR (PSP GU byte order). */
#define UI_RGB(r, g, b) (0xFF000000u | ((b) << 16) | ((g) << 8) | (r))

extern intraFont *ui_font;  /* flash0:/font/jpn0.pgf, UTF-8 */

int ui_init(void);          /* <0 if the font could not be loaded */
void ui_term(void);
void ui_begin(unsigned int clear_color);
void ui_end(void);          /* waits for vblank: the caller's loop runs at 60 fps */
void ui_rect(float x, float y, float w, float h, unsigned int color);
/* Draws text at (x, baseline y), shortened with "…" to fit max_w. Returns the drawn width. */
float ui_text(float x, float y, float size, unsigned int color, float max_w, const char *utf8);

/* Same, centered on cx. */
void ui_text_center(float cx, float y, float size, unsigned int color, float max_w, const char *utf8);

#endif
