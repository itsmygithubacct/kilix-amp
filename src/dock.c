#include "dock.h"

#include <stdlib.h>
#include <string.h>

#include "consts.h"

void dock_init(DockManager *dm)
{
    memset(dm, 0, sizeof(*dm));
    dm->dragging = -1;
}

void dock_register(DockManager *dm, const char *name, KWindow *win)
{
    if (dm->count < DOCK_MAX_WINDOWS) {
        dm->names[dm->count] = name;
        dm->windows[dm->count] = win;
        dm->count++;
    }
}

static int find(DockManager *dm, const char *name)
{
    for (int i = 0; i < dm->count; i++)
        if (strcmp(dm->names[i], name) == 0)
            return i;
    return -1;
}

/* Check if window i is touching/docked to anchor a. */
static bool is_docked_to(DockManager *dm, int i, int a)
{
    int wx, wy, ww, wh, ax, ay, aw, ah;
    kwin_get_pos(dm->windows[i], &wx, &wy);
    kwin_get_size(dm->windows[i], &ww, &wh);
    kwin_get_pos(dm->windows[a], &ax, &ay);
    kwin_get_size(dm->windows[a], &aw, &ah);

    const int tolerance = 2;

    /* Below anchor (and x-aligned) */
    if (abs(wy - (ay + ah)) <= tolerance && abs(wx - ax) <= DOCK_DISTANCE)
        return true;
    /* Above anchor */
    if (abs((wy + wh) - ay) <= tolerance && abs(wx - ax) <= DOCK_DISTANCE)
        return true;
    /* Right of anchor */
    if (abs(wx - (ax + aw)) <= tolerance && abs(wy - ay) <= DOCK_DISTANCE)
        return true;
    /* Left of anchor */
    if (abs((wx + ww) - ax) <= tolerance && abs(wy - ay) <= DOCK_DISTANCE)
        return true;
    return false;
}

void dock_start_drag(DockManager *dm, const char *name)
{
    int anchor = find(dm, name);
    dm->dragging = anchor;
    if (anchor < 0)
        return;
    int ax, ay;
    kwin_get_pos(dm->windows[anchor], &ax, &ay);
    for (int i = 0; i < dm->count; i++) {
        dm->docked[i] = false;
        if (i == anchor || !dm->windows[i]->visible)
            continue;
        if (is_docked_to(dm, i, anchor)) {
            int wx, wy;
            kwin_get_pos(dm->windows[i], &wx, &wy);
            dm->docked[i] = true;
            dm->off_x[i] = wx - ax;
            dm->off_y[i] = wy - ay;
        }
    }
}

static void snap_to_screen(KWindow *win, int *x, int *y)
{
    int idx = SDL_GetWindowDisplayIndex(win->win);
    SDL_Rect geo;
    if (idx < 0 || SDL_GetDisplayUsableBounds(idx, &geo) != 0)
        return;
    int w, h;
    kwin_get_size(win, &w, &h);
    if (abs(*x - geo.x) < DOCK_DISTANCE)
        *x = geo.x;
    if (abs(*y - geo.y) < DOCK_DISTANCE)
        *y = geo.y;
    if (abs(*x + w - (geo.x + geo.w)) < DOCK_DISTANCE)
        *x = geo.x + geo.w - w;
    if (abs(*y + h - (geo.y + geo.h)) < DOCK_DISTANCE)
        *y = geo.y + geo.h - h;
}

static void snap_to_windows(DockManager *dm, int idx, int *px, int *py)
{
    int x = *px, y = *py;
    int w, h;
    kwin_get_size(dm->windows[idx], &w, &h);

    bool snapped_x = false, snapped_y = false;

    for (int i = 0; i < dm->count; i++) {
        if (i == idx || !dm->windows[i]->visible)
            continue;
        if (dm->docked[i]) /* being dragged along */
            continue;

        int ox, oy, ow, oh;
        kwin_get_pos(dm->windows[i], &ox, &oy);
        kwin_get_size(dm->windows[i], &ow, &oh);

        /* Y-axis snapping (top/bottom edges) with horizontal overlap */
        bool h_near =
            x < ox + ow + DOCK_DISTANCE && x + w > ox - DOCK_DISTANCE;
        if (h_near && !snapped_y) {
            if (abs(y - (oy + oh)) < DOCK_DISTANCE) {
                y = oy + oh;
                snapped_y = true;
            } else if (abs((y + h) - oy) < DOCK_DISTANCE) {
                y = oy - h;
                snapped_y = true;
            } else if (abs(y - oy) < DOCK_DISTANCE) {
                y = oy;
                snapped_y = true;
            } else if (abs((y + h) - (oy + oh)) < DOCK_DISTANCE) {
                y = oy + oh - h;
                snapped_y = true;
            }
        }

        /* X-axis snapping (left/right edges) with vertical overlap */
        bool v_near =
            y < oy + oh + DOCK_DISTANCE && y + h > oy - DOCK_DISTANCE;
        if (v_near && !snapped_x) {
            if (abs(x - (ox + ow)) < DOCK_DISTANCE) {
                x = ox + ow;
                snapped_x = true;
            } else if (abs((x + w) - ox) < DOCK_DISTANCE) {
                x = ox - w;
                snapped_x = true;
            } else if (abs(x - ox) < DOCK_DISTANCE) {
                x = ox;
                snapped_x = true;
            } else if (abs((x + w) - (ox + ow)) < DOCK_DISTANCE) {
                x = ox + ow - w;
                snapped_x = true;
            }
        }
    }
    *px = x;
    *py = y;
}

void dock_drag_move(DockManager *dm, const char *name, int x, int y)
{
    int idx = find(dm, name);
    if (idx < 0)
        return;
    snap_to_screen(dm->windows[idx], &x, &y);
    snap_to_windows(dm, idx, &x, &y);
    kwin_set_pos(dm->windows[idx], x, y);

    for (int i = 0; i < dm->count; i++)
        if (dm->docked[i] && dm->windows[i]->visible)
            kwin_set_pos(dm->windows[i], x + dm->off_x[i],
                         y + dm->off_y[i]);
}

void dock_end_drag(DockManager *dm)
{
    dm->dragging = -1;
    memset(dm->docked, 0, sizeof(dm->docked));
}

void dock_position_default(DockManager *dm)
{
    int main_idx = find(dm, "main");
    if (main_idx < 0)
        return;
    KWindow *main_win = dm->windows[main_idx];
    int di = SDL_GetWindowDisplayIndex(main_win->win);
    SDL_Rect geo;
    if (di < 0 || SDL_GetDisplayUsableBounds(di, &geo) != 0)
        return;

    int mw, mh;
    kwin_get_size(main_win, &mw, &mh);
    int mx = geo.x + geo.w / 2 - mw / 2;
    int my = geo.y + geo.h / 2 - 200;
    kwin_set_pos(main_win, mx, my);

    int eq_idx = find(dm, "eq");
    int eq_h = 0;
    if (eq_idx >= 0) {
        kwin_set_pos(dm->windows[eq_idx], mx, my + mh);
        if (dm->windows[eq_idx]->visible) {
            int w;
            kwin_get_size(dm->windows[eq_idx], &w, &eq_h);
        }
    }

    int pl_idx = find(dm, "playlist");
    if (pl_idx >= 0)
        kwin_set_pos(dm->windows[pl_idx], mx, my + mh + eq_h);

    int ed_idx = find(dm, "editor");
    if (ed_idx >= 0 && dm->windows[ed_idx]->visible)
        kwin_set_pos(dm->windows[ed_idx], mx + mw, my);
}
