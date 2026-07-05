/* In-house 4x6 pixel font (uppercase + digits + symbols).
 * Used to bake the default skin's text.bmp and for generator labels,
 * replacing Qt font rendering. Glyphs are 6 rows, bit 3 = leftmost pixel. */
#ifndef KA_FONT4X6_H
#define KA_FONT4X6_H

#include <SDL.h>

#include "common.h"

/* 6 row bytes for c (lowercase folded to uppercase); NULL if no glyph.
 * '\x85' is the ellipsis cell used by text.bmp. */
const uint8_t *font4x6_glyph(unsigned char c);

int font4x6_width(const char *text); /* advance is 5px per char */
void font4x6_draw(SDL_Surface *s, int x, int y, const char *text, KColor c);
void font4x6_draw_centered(SDL_Surface *s, KRect r, const char *text,
                           KColor c);

#endif
