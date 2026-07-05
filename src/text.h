/* Bitmap font rendering (text.bmp grid) and LCD digit display
 * (nums_ex.bmp). Ports of text_renderer.py and lcd_display.py. */
#ifndef KA_TEXT_H
#define KA_TEXT_H

#include <SDL.h>

#include "common.h"

typedef struct {
    SDL_Surface *font; /* text.bmp sheet; not owned */
} TextRenderer;

void text_renderer_init(TextRenderer *tr, SDL_Surface *font_bitmap);
/* Source position of ch in text.bmp; false if unmapped (space/unknown). */
bool text_char_pos(char ch, int *sx, int *sy);
void text_render(TextRenderer *tr, SDL_Surface *dst, const char *text,
                 int x, int y, int max_w);
/* Scrolling text clipped to width; offset in pixels into the loop. */
void text_render_scrolling(TextRenderer *tr, SDL_Surface *dst,
                           const char *text, int x, int y, int width,
                           int scroll_offset);
int text_width(const char *text);

typedef struct {
    SDL_Surface *digits; /* nums_ex.bmp sheet; not owned */
    bool show_remaining;
} LCDDisplay;

void lcd_init(LCDDisplay *lcd, SDL_Surface *digits_bitmap);
void lcd_toggle_mode(LCDDisplay *lcd);
void lcd_render_time(LCDDisplay *lcd, SDL_Surface *dst, int seconds,
                     int total_seconds, int x, int y);

#endif
