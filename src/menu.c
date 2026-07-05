#include "menu.h"

#include <SDL.h>
#include <string.h>

#include "font4x6.h"
#include "render.h"

#define ITEM_H 13
#define SEP_H 5
#define PAD_X 8

#define MENU_BG      ((KColor){40, 40, 45})
#define MENU_BORDER  ((KColor){85, 88, 95})
#define MENU_TEXT    ((KColor){200, 200, 200})
#define MENU_HEAD    ((KColor){120, 130, 120})
#define MENU_HOVERBG ((KColor){0, 90, 0})
#define MENU_SEP     ((KColor){80, 80, 85})

static int item_y(const MenuItem *items, int idx)
{
    int y = 2;
    for (int i = 0; i < idx; i++)
        y += items[i].label ? ITEM_H : SEP_H;
    return y;
}

static int hit_item(const MenuItem *items, int n, int my)
{
    for (int i = 0; i < n; i++) {
        int y = item_y(items, i);
        int h = items[i].label ? ITEM_H : SEP_H;
        if (my >= y && my < y + h && items[i].label && items[i].id >= 0)
            return i;
    }
    return -1;
}

static void render_menu(SDL_Surface *s, const MenuItem *items, int n,
                        int hover)
{
    ksurf_fill(s, KRECT(0, 0, s->w, s->h), MENU_BG);
    ksurf_line(s, 0, 0, s->w - 1, 0, MENU_BORDER);
    ksurf_line(s, 0, 0, 0, s->h - 1, MENU_BORDER);
    ksurf_line(s, s->w - 1, 0, s->w - 1, s->h - 1, MENU_BORDER);
    ksurf_line(s, 0, s->h - 1, s->w - 1, s->h - 1, MENU_BORDER);

    for (int i = 0; i < n; i++) {
        int y = item_y(items, i);
        if (!items[i].label) {
            ksurf_line(s, 4, y + SEP_H / 2, s->w - 5, y + SEP_H / 2,
                       MENU_SEP);
            continue;
        }
        bool header = items[i].id < 0;
        if (i == hover && !header)
            ksurf_fill(s, KRECT(1, y, s->w - 2, ITEM_H), MENU_HOVERBG);
        font4x6_draw(s, header ? PAD_X - 4 : PAD_X, y + 3, items[i].label,
                     header ? MENU_HEAD : MENU_TEXT);
    }
}

int menu_show(int x, int y, const MenuItem *items, int n)
{
    int w = 60, h = 4;
    for (int i = 0; i < n; i++) {
        if (items[i].label) {
            w = KA_MAX(w, font4x6_width(items[i].label) + PAD_X * 2);
            h += ITEM_H;
        } else {
            h += SEP_H;
        }
    }

    /* Keep on screen */
    SDL_Rect geo = {0, 0, 0, 0};
    int di = SDL_GetPointDisplayIndex(&(SDL_Point){x, y});
    if (di >= 0 && SDL_GetDisplayUsableBounds(di, &geo) == 0) {
        if (x + w > geo.x + geo.w)
            x = geo.x + geo.w - w;
        if (y + h > geo.y + geo.h)
            y = geo.y + geo.h - h;
    }

    SDL_Window *win = SDL_CreateWindow(
        "menu", x, y, w, h,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
            SDL_WINDOW_SKIP_TASKBAR);
    if (!win)
        return -1;
    uint32_t wid = SDL_GetWindowID(win);
    SDL_RaiseWindow(win);

    int hover = -1, result = -1;
    bool done = false;
    while (!done) {
        SDL_Event ev;
        if (!SDL_WaitEventTimeout(&ev, 33))
            goto redraw;
        switch (ev.type) {
        case SDL_QUIT:
            done = true;
            SDL_PushEvent(&ev); /* let the main loop see it too */
            break;
        case SDL_KEYDOWN:
            if (ev.key.keysym.sym == SDLK_ESCAPE)
                done = true;
            break;
        case SDL_MOUSEMOTION:
            if (ev.motion.windowID == wid)
                hover = hit_item(items, n, ev.motion.y);
            else
                hover = -1;
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (ev.button.windowID == wid) {
                int i = hit_item(items, n, ev.button.y);
                if (i >= 0) {
                    result = items[i].id;
                    done = true;
                }
            } else {
                done = true; /* click outside dismisses */
            }
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.windowID == wid &&
                ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                done = true;
            break;
        default:
            break;
        }
    redraw:;
        SDL_Surface *surf = SDL_GetWindowSurface(win);
        if (surf) {
            render_menu(surf, items, n, hover);
            SDL_UpdateWindowSurface(win);
        }
    }

    SDL_DestroyWindow(win);
    return result;
}
