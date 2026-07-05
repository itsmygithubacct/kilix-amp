/* Shared borderless-window base: logical-size buffer, 2x scale, dragging. */
#ifndef KA_KWINDOW_H
#define KA_KWINDOW_H

#include <SDL.h>

#include "render.h"

typedef struct {
    SDL_Window *win;
    KBuffer buf;
    int logical_w, logical_h; /* current logical size */
    bool visible;
    bool active;
    float scale; /* UI scale factor, SCALE_MIN..SCALE_MAX */
    bool dragging;
    int drag_off_x, drag_off_y; /* global mouse - window pos at drag start */
} KWindow;

void kwin_create(KWindow *kw, const char *title, int w, int h,
                 bool resizable);
void kwin_destroy(KWindow *kw);
void kwin_show(KWindow *kw);
void kwin_hide(KWindow *kw);
void kwin_set_visible(KWindow *kw, bool v);
uint32_t kwin_id(const KWindow *kw);
float kwin_scale(const KWindow *kw);
/* Fixed-size windows (main/EQ): resizes the OS window to logical*scale. */
void kwin_set_scale_fixed(KWindow *kw, float scale);
/* Resizable windows: rescales current logical size. */
void kwin_set_scale_resizable(KWindow *kw, float scale, int min_w,
                              int min_h);
/* Mouse event coords -> logical coords. */
void kwin_logical(const KWindow *kw, int x, int y, int *lx, int *ly);
void kwin_present(KWindow *kw);
void kwin_get_pos(const KWindow *kw, int *x, int *y);
void kwin_set_pos(KWindow *kw, int x, int y);
void kwin_get_size(const KWindow *kw, int *w, int *h); /* OS pixels */

/* Title-bar drag helpers (global mouse coordinates). */
void kwin_begin_drag(KWindow *kw);
/* Returns desired new window position for the current global mouse. */
void kwin_drag_pos(const KWindow *kw, int *nx, int *ny);

#endif
