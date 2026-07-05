/* Software-surface compositing helpers; port of skin_renderer.py plus the
 * pixel/line primitives the default-skin generator and visualizers need. */
#ifndef KA_RENDER_H
#define KA_RENDER_H

#include <SDL.h>

#include "common.h"

/* Off-screen buffer at logical (1x) size, presented at any scale. */
typedef struct {
    SDL_Surface *buf; /* ARGB8888 */
    int w, h;
    float scale;
} KBuffer;

void kbuf_init(KBuffer *kb, int w, int h);
void kbuf_resize(KBuffer *kb, int w, int h);
void kbuf_destroy(KBuffer *kb);
void kbuf_clear(KBuffer *kb, KColor c);
/* Scale-blit the buffer to the window's surface and flip. */
void kbuf_present(KBuffer *kb, SDL_Window *win);

SDL_Surface *ksurf_new(int w, int h, KColor fill);
void ksurf_fill(SDL_Surface *s, KRect r, KColor c);
void ksurf_pixel(SDL_Surface *s, int x, int y, KColor c);
void ksurf_line(SDL_Surface *s, int x0, int y0, int x1, int y1, KColor c);
/* 1:1 sprite blit of src_rect to (dx,dy). */
void ksurf_blit(SDL_Surface *dst, int dx, int dy, SDL_Surface *src,
                KRect src_rect);
/* Stretched blit for 9-slice tiles. */
void ksurf_blit_scaled(SDL_Surface *dst, KRect dst_rect, SDL_Surface *src,
                       KRect src_rect);

#endif
