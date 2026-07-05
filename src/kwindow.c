#include "kwindow.h"

#include <math.h>

#include "consts.h"

void kwin_create(KWindow *kw, const char *title, int w, int h,
                 bool resizable)
{
    memset(kw, 0, sizeof(*kw));
    uint32_t flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN;
    if (resizable)
        flags |= SDL_WINDOW_RESIZABLE;
    kw->win = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED, w, h, flags);
    if (!kw->win) {
        SDL_Log("kwin_create: %s", SDL_GetError());
        abort();
    }
    kbuf_init(&kw->buf, w, h);
    kw->logical_w = w;
    kw->logical_h = h;
    kw->scale = 1.0f;
    kw->active = true;
}

void kwin_destroy(KWindow *kw)
{
    kbuf_destroy(&kw->buf);
    if (kw->win) {
        SDL_DestroyWindow(kw->win);
        kw->win = NULL;
    }
}

void kwin_show(KWindow *kw)
{
    SDL_ShowWindow(kw->win);
    kw->visible = true;
}

void kwin_hide(KWindow *kw)
{
    SDL_HideWindow(kw->win);
    kw->visible = false;
}

void kwin_set_visible(KWindow *kw, bool v)
{
    if (v)
        kwin_show(kw);
    else
        kwin_hide(kw);
}

uint32_t kwin_id(const KWindow *kw)
{
    return SDL_GetWindowID(kw->win);
}

float kwin_scale(const KWindow *kw)
{
    return kw->scale > 0.0f ? kw->scale : 1.0f;
}

static int scaled(int v, float scale)
{
    return (int)lroundf((float)v * scale);
}

void kwin_set_scale_fixed(KWindow *kw, float scale)
{
    kw->scale = KA_CLAMP(scale, SCALE_MIN, SCALE_MAX);
    kw->buf.scale = kw->scale;
    SDL_SetWindowSize(kw->win, scaled(kw->logical_w, kw->scale),
                      scaled(kw->logical_h, kw->scale));
}

void kwin_set_scale_resizable(KWindow *kw, float scale, int min_w,
                              int min_h)
{
    kw->scale = KA_CLAMP(scale, SCALE_MIN, SCALE_MAX);
    kw->buf.scale = kw->scale;
    SDL_SetWindowMinimumSize(kw->win, scaled(min_w, kw->scale),
                             scaled(min_h, kw->scale));
    SDL_SetWindowSize(kw->win, scaled(kw->logical_w, kw->scale),
                      scaled(kw->logical_h, kw->scale));
}

void kwin_logical(const KWindow *kw, int x, int y, int *lx, int *ly)
{
    float scale = kwin_scale(kw);
    *lx = (int)((float)x / scale);
    *ly = (int)((float)y / scale);
}

void kwin_present(KWindow *kw)
{
    kbuf_present(&kw->buf, kw->win);
}

void kwin_get_pos(const KWindow *kw, int *x, int *y)
{
    SDL_GetWindowPosition(kw->win, x, y);
}

void kwin_set_pos(KWindow *kw, int x, int y)
{
    SDL_SetWindowPosition(kw->win, x, y);
}

void kwin_get_size(const KWindow *kw, int *w, int *h)
{
    SDL_GetWindowSize(kw->win, w, h);
}

void kwin_begin_drag(KWindow *kw)
{
    int mx, my, wx, wy;
    SDL_GetGlobalMouseState(&mx, &my);
    kwin_get_pos(kw, &wx, &wy);
    kw->dragging = true;
    kw->drag_off_x = mx - wx;
    kw->drag_off_y = my - wy;
    SDL_CaptureMouse(SDL_TRUE);
}

void kwin_drag_pos(const KWindow *kw, int *nx, int *ny)
{
    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    *nx = mx - kw->drag_off_x;
    *ny = my - kw->drag_off_y;
}
