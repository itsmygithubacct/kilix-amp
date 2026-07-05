#include "render.h"

#include <stdlib.h>

SDL_Surface *ksurf_new(int w, int h, KColor fill)
{
    SDL_Surface *s =
        SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) {
        SDL_Log("ksurf_new: %s", SDL_GetError());
        abort();
    }
    SDL_FillRect(s, NULL, SDL_MapRGB(s->format, fill.r, fill.g, fill.b));
    return s;
}

void kbuf_init(KBuffer *kb, int w, int h)
{
    kb->buf = ksurf_new(w, h, (KColor){0, 0, 0});
    kb->w = w;
    kb->h = h;
    kb->scale = 1.0f;
}

void kbuf_resize(KBuffer *kb, int w, int h)
{
    float scale = kb->scale;
    SDL_FreeSurface(kb->buf);
    kbuf_init(kb, w, h);
    kb->scale = scale;
}

void kbuf_destroy(KBuffer *kb)
{
    if (kb->buf) {
        SDL_FreeSurface(kb->buf);
        kb->buf = NULL;
    }
}

void kbuf_clear(KBuffer *kb, KColor c)
{
    SDL_FillRect(kb->buf, NULL,
                 SDL_MapRGB(kb->buf->format, c.r, c.g, c.b));
}

void kbuf_present(KBuffer *kb, SDL_Window *win)
{
    SDL_Surface *ws = SDL_GetWindowSurface(win);
    if (!ws)
        return;
    if (kb->scale == 1.0f) {
        SDL_BlitSurface(kb->buf, NULL, ws, NULL);
    } else {
        /* Map the logical buffer onto the whole window surface so rounding
         * never leaves unfilled slivers at fractional scales. */
        SDL_Rect dst = {0, 0, ws->w, ws->h};
        SDL_BlitScaled(kb->buf, NULL, ws, &dst);
    }
    SDL_UpdateWindowSurface(win);
}

void ksurf_fill(SDL_Surface *s, KRect r, KColor c)
{
    SDL_Rect sr = {r.x, r.y, r.w, r.h};
    SDL_FillRect(s, &sr, SDL_MapRGB(s->format, c.r, c.g, c.b));
}

void ksurf_pixel(SDL_Surface *s, int x, int y, KColor c)
{
    if (x < 0 || y < 0 || x >= s->w || y >= s->h)
        return;
    uint32_t *px = (uint32_t *)((uint8_t *)s->pixels + y * s->pitch) + x;
    *px = SDL_MapRGB(s->format, c.r, c.g, c.b);
}

void ksurf_line(SDL_Surface *s, int x0, int y0, int x1, int y1, KColor c)
{
    /* Bresenham */
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        ksurf_pixel(s, x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void ksurf_blit(SDL_Surface *dst, int dx, int dy, SDL_Surface *src,
                KRect src_rect)
{
    if (!src)
        return;
    SDL_Rect sr = {src_rect.x, src_rect.y, src_rect.w, src_rect.h};
    SDL_Rect dr = {dx, dy, src_rect.w, src_rect.h};
    SDL_BlitSurface(src, &sr, dst, &dr);
}

void ksurf_blit_scaled(SDL_Surface *dst, KRect dst_rect, SDL_Surface *src,
                       KRect src_rect)
{
    if (!src)
        return;
    SDL_Rect sr = {src_rect.x, src_rect.y, src_rect.w, src_rect.h};
    SDL_Rect dr = {dst_rect.x, dst_rect.y, dst_rect.w, dst_rect.h};
    SDL_BlitScaled(src, &sr, dst, &dr);
}
