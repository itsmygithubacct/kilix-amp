#include "win_playlist.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "consts.h"
#include "font4x6.h"
#include "menu.h"

void playlist_window_init(PlaylistWindow *pw, Skin *skin, Playlist *pl)
{
    memset(pw, 0, sizeof(*pw));
    pw->skin = skin;
    pw->playlist = pl;
    pw->pl_w = PL_MIN_W;
    pw->pl_h = PL_DEFAULT_H;
    pw->anchor = -1;
    kwin_create(&pw->kw, "kilix-amp playlist", PL_MIN_W, PL_DEFAULT_H, true);
    SDL_SetWindowMinimumSize(pw->kw.win, PL_MIN_W, PL_MIN_H);
    kbuf_resize(&pw->kw.buf, pw->pl_w, pw->pl_h);
    pw->kw.logical_w = pw->pl_w;
    pw->kw.logical_h = pw->pl_h;
    text_renderer_init(&pw->text, skin_bitmap(skin, "text"));
}

void playlist_window_destroy(PlaylistWindow *pw)
{
    free(pw->selected);
    free(pw->selected_tracks);
    kwin_destroy(&pw->kw);
}

static int visible_items(const PlaylistWindow *pw)
{
    int list_h = pw->pl_h - PL_TITLE_H - 38; /* title + bottom bar */
    return KA_MAX(1, list_h / PL_ITEM_H);
}

static int max_scroll(const PlaylistWindow *pw)
{
    return KA_MAX(0, playlist_count(pw->playlist) - visible_items(pw));
}

static void ensure_selected_cap(PlaylistWindow *pw, int n)
{
    if (n <= pw->selected_cap)
        return;
    pw->selected = realloc(pw->selected, sizeof(bool) * (size_t)n);
    if (!pw->selected)
        abort();
    memset(pw->selected + pw->selected_cap, 0,
           sizeof(bool) * (size_t)(n - pw->selected_cap));
    pw->selected_cap = n;
}

static bool is_selected(const PlaylistWindow *pw, int i)
{
    return i >= 0 && i < pw->selected_cap && pw->selected[i];
}

/* Snapshot the Track pointers currently selected (stable identity). */
static void sync_selected_tracks(PlaylistWindow *pw)
{
    free(pw->selected_tracks);
    pw->selected_tracks = NULL;
    pw->selected_track_count = 0;
    int n = playlist_count(pw->playlist);
    int count = 0;
    for (int i = 0; i < KA_MIN(n, pw->selected_cap); i++)
        if (pw->selected[i])
            count++;
    if (count == 0)
        return;
    pw->selected_tracks = malloc(sizeof(Track *) * (size_t)count);
    if (!pw->selected_tracks)
        abort();
    for (int i = 0; i < KA_MIN(n, pw->selected_cap); i++)
        if (pw->selected[i])
            pw->selected_tracks[pw->selected_track_count++] =
                playlist_track(pw->playlist, i);
}

void playlist_window_on_playlist_changed(PlaylistWindow *pw)
{
    pw->scroll_offset = KA_CLAMP(pw->scroll_offset, 0, max_scroll(pw));

    /* Remap selection to new indices by Track identity. */
    int n = playlist_count(pw->playlist);
    ensure_selected_cap(pw, KA_MAX(n, 1));
    memset(pw->selected, 0, sizeof(bool) * (size_t)pw->selected_cap);
    for (int i = 0; i < n; i++) {
        Track *t = playlist_track(pw->playlist, i);
        for (int k = 0; k < pw->selected_track_count; k++) {
            if (pw->selected_tracks[k] == t) {
                pw->selected[i] = true;
                break;
            }
        }
    }
    sync_selected_tracks(pw);

    if (pw->anchor >= n)
        pw->anchor = -1;
}

void playlist_window_on_current_changed(PlaylistWindow *pw, int idx)
{
    /* Auto-scroll to keep current track visible */
    if (idx < pw->scroll_offset)
        pw->scroll_offset = idx;
    else if (idx >= pw->scroll_offset + visible_items(pw))
        pw->scroll_offset = idx - visible_items(pw) + 1;
}

void playlist_window_set_scale(PlaylistWindow *pw, float scale)
{
    pw->kw.logical_w = pw->pl_w;
    pw->kw.logical_h = pw->pl_h;
    kwin_set_scale_resizable(&pw->kw, scale, PL_MIN_W, PL_MIN_H);
}

void playlist_window_resize(PlaylistWindow *pw, int w, int h)
{
    pw->pl_w = KA_MAX(PL_MIN_W, w);
    pw->pl_h = KA_MAX(PL_MIN_H, h);
    pw->kw.logical_w = pw->pl_w;
    pw->kw.logical_h = pw->pl_h;
    kbuf_resize(&pw->kw.buf, pw->pl_w, pw->pl_h);
    pw->kw.buf.scale = kwin_scale(&pw->kw);
}

/* --- Rendering --- */

static void draw_items(PlaylistWindow *pw)
{
    SDL_Surface *buf = pw->kw.buf.buf;
    int list_x = 12;
    int list_y = PL_TITLE_H;
    int list_w = pw->pl_w - 12 - 20;
    int list_h = pw->pl_h - PL_TITLE_H - 38;

    SDL_Rect clip = {list_x, list_y, list_w, list_h};
    SDL_SetClipRect(buf, &clip);

    SkinColors *colors = &pw->skin->colors;
    for (int i = 0; i < visible_items(pw); i++) {
        int idx = pw->scroll_offset + i;
        if (idx >= playlist_count(pw->playlist))
            break;
        Track *track = playlist_track(pw->playlist, idx);
        int y = list_y + i * PL_ITEM_H;

        if (is_selected(pw, idx))
            ksurf_fill(buf, KRECT(list_x, y, list_w, PL_ITEM_H),
                       colors->pl_selectedbg);

        KColor color = idx == playlist_current_index(pw->playlist)
                           ? colors->pl_current
                           : colors->pl_normal;

        char *disp = track_display_title(track);
        char text[512];
        snprintf(text, sizeof(text), "%d. %s", idx + 1, disp);
        free(disp);
        /* Clip text to the room left of the duration column. */
        int max_chars = KA_MAX(0, (list_w - 40 - 4) / 5);
        if ((int)strlen(text) > max_chars)
            text[max_chars] = '\0';
        font4x6_draw(buf, list_x + 2, y + (PL_ITEM_H - 6) / 2, text, color);

        char *dur = track_duration_str(track);
        if (*dur) {
            int dw = font4x6_width(dur);
            font4x6_draw(buf, list_x + list_w - 2 - dw,
                         y + (PL_ITEM_H - 6) / 2, dur, color);
        }
        free(dur);
    }

    SDL_SetClipRect(buf, NULL);
}

static void draw_scrollbar(PlaylistWindow *pw)
{
    SDL_Surface *buf = pw->kw.buf.buf;
    int sb_x = pw->pl_w - 15;
    int sb_y = PL_TITLE_H;
    int sb_h = pw->pl_h - PL_TITLE_H - 38;

    if (playlist_count(pw->playlist) <= visible_items(pw))
        return;

    ksurf_fill(buf, KRECT(sb_x, sb_y, 8, sb_h), (KColor){30, 30, 35});

    double ratio =
        (double)visible_items(pw) / KA_MAX(1, playlist_count(pw->playlist));
    int thumb_h = KA_MAX(10, (int)(sb_h * ratio));
    int thumb_y = sb_y;
    if (max_scroll(pw) > 0)
        thumb_y = sb_y + (int)((double)pw->scroll_offset / max_scroll(pw) *
                               (sb_h - thumb_h));
    ksurf_fill(buf, KRECT(sb_x, thumb_y, 8, thumb_h), (KColor){80, 85, 90});
}

static void draw_info(PlaylistWindow *pw)
{
    SDL_Surface *buf = pw->kw.buf.buf;
    int info_y = pw->pl_h - 28;
    char *dur = playlist_total_duration_str(pw->playlist);
    char info[128];
    snprintf(info, sizeof(info), "%d TRACKS (%s)",
             playlist_count(pw->playlist), dur);
    free(dur);
    font4x6_draw_centered(buf, KRECT(12, info_y, pw->pl_w - 32, 14), info,
                          pw->skin->colors.pl_normal);
}

void playlist_window_render(PlaylistWindow *pw)
{
    SDL_Surface *buf = pw->kw.buf.buf;
    SkinColors *colors = &pw->skin->colors;

    ksurf_fill(buf, KRECT(0, 0, pw->pl_w, pw->pl_h), colors->pl_normalbg);

    SDL_Surface *pledit = skin_bitmap(pw->skin, "pledit");
    if (pledit) {
        bool act = pw->kw.active;
        /* Title bar: left corner, stretch, right corner */
        ksurf_blit(buf, 0, 0, pledit, act ? PL_TL_ACTIVE : PL_TL_INACTIVE);
        KRect tsrc =
            act ? PL_TITLE_STRETCH_ACTIVE : PL_TITLE_STRETCH_INACTIVE;
        int stretch_w = pw->pl_w - 25 - 125;
        for (int x = 25; x < 25 + stretch_w; x += tsrc.w) {
            int w = KA_MIN(tsrc.w, 25 + stretch_w - x);
            ksurf_blit(buf, x, 0, pledit,
                       KRECT(tsrc.x, tsrc.y, w, tsrc.h));
        }
        KRect trsrc = act ? PL_TR_ACTIVE : PL_TR_INACTIVE;
        ksurf_blit(buf, pw->pl_w - trsrc.w, 0, pledit, trsrc);

        /* Left/right edges */
        for (int y = PL_TITLE_H; y < pw->pl_h - 38; y += PL_LEFT_TILE.h) {
            int h = KA_MIN(PL_LEFT_TILE.h, pw->pl_h - 38 - y);
            ksurf_blit(buf, 0, y, pledit,
                       KRECT(PL_LEFT_TILE.x, PL_LEFT_TILE.y, PL_LEFT_TILE.w,
                             h));
        }
        for (int y = PL_TITLE_H; y < pw->pl_h - 38; y += PL_RIGHT_TILE.h) {
            int h = KA_MIN(PL_RIGHT_TILE.h, pw->pl_h - 38 - y);
            ksurf_blit(buf, pw->pl_w - PL_RIGHT_TILE.w, y, pledit,
                       KRECT(PL_RIGHT_TILE.x, PL_RIGHT_TILE.y,
                             PL_RIGHT_TILE.w, h));
        }

        /* Bottom bar */
        int bottom_y = pw->pl_h - 38;
        ksurf_blit(buf, 0, bottom_y, pledit, PL_BL_CORNER);
        ksurf_blit(buf, pw->pl_w - PL_BR_CORNER.w, bottom_y, pledit,
                   PL_BR_CORNER);
    }

    draw_items(pw);
    draw_scrollbar(pw);
    draw_info(pw);

    kwin_present(&pw->kw);
}

/* --- Selection helpers --- */

static void remove_selected(PlaylistWindow *pw)
{
    int n = playlist_count(pw->playlist);
    int *indices = malloc(sizeof(int) * (size_t)KA_MAX(n, 1));
    if (!indices)
        abort();
    int count = 0;
    for (int i = 0; i < KA_MIN(n, pw->selected_cap); i++)
        if (pw->selected[i])
            indices[count++] = i;
    /* Clear the identity snapshot before the model change so the remap in
     * on_playlist_changed drops everything. */
    pw->selected_track_count = 0;
    memset(pw->selected, 0, sizeof(bool) * (size_t)pw->selected_cap);
    pw->anchor = -1;
    playlist_remove(pw->playlist, indices, count);
    free(indices);
}

static void select_all(PlaylistWindow *pw)
{
    int n = playlist_count(pw->playlist);
    ensure_selected_cap(pw, KA_MAX(n, 1));
    for (int i = 0; i < n; i++)
        pw->selected[i] = true;
    sync_selected_tracks(pw);
}

static void clear_selection(PlaylistWindow *pw)
{
    memset(pw->selected, 0, sizeof(bool) * (size_t)pw->selected_cap);
    pw->selected_track_count = 0;
    pw->anchor = -1;
}

static void invert_selection(PlaylistWindow *pw)
{
    int n = playlist_count(pw->playlist);
    ensure_selected_cap(pw, KA_MAX(n, 1));
    for (int i = 0; i < n; i++)
        pw->selected[i] = !pw->selected[i];
    sync_selected_tracks(pw);
}

static int first_selected(PlaylistWindow *pw)
{
    for (int i = 0; i < pw->selected_cap; i++)
        if (pw->selected[i])
            return i;
    return -1;
}

static void show_context_menu(PlaylistWindow *pw)
{
    enum {
        M_ADD_FILES = 1, M_ADD_DIR, M_EDIT, M_REMOVE, M_CLEAR,
        M_SEL_ALL, M_SEL_NONE, M_SEL_INV,
        M_SORT_FILE, M_SORT_TITLE, M_SORT_DUR, M_SORT_PATH, M_REVERSE,
    };
    bool has_sel = first_selected(pw) >= 0;
    MenuItem items[20];
    int n = 0;
    items[n++] = (MenuItem){"ADD FILE(S)...", M_ADD_FILES};
    items[n++] = (MenuItem){"ADD DIRECTORY...", M_ADD_DIR};
    items[n++] = MENU_SEPARATOR;
    if (has_sel) {
        items[n++] = (MenuItem){"EDIT IN EDITOR...", M_EDIT};
        items[n++] = MENU_SEPARATOR;
    }
    items[n++] = (MenuItem){"REMOVE SELECTED", M_REMOVE};
    items[n++] = (MenuItem){"CLEAR PLAYLIST", M_CLEAR};
    items[n++] = MENU_SEPARATOR;
    items[n++] = (MenuItem){"SELECT ALL", M_SEL_ALL};
    items[n++] = (MenuItem){"SELECT NONE", M_SEL_NONE};
    items[n++] = (MenuItem){"INVERT SELECTION", M_SEL_INV};
    items[n++] = MENU_SEPARATOR;
    items[n++] = MENU_HEADER("SORT");
    items[n++] = (MenuItem){"BY FILENAME", M_SORT_FILE};
    items[n++] = (MenuItem){"BY TITLE", M_SORT_TITLE};
    items[n++] = (MenuItem){"BY DURATION", M_SORT_DUR};
    items[n++] = (MenuItem){"BY PATH", M_SORT_PATH};
    items[n++] = (MenuItem){"REVERSE", M_REVERSE};

    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    switch (menu_show(mx, my, items, n)) {
    case M_ADD_FILES:
        if (pw->cbs.add_files_requested)
            pw->cbs.add_files_requested(pw->cbs.ud);
        break;
    case M_ADD_DIR:
        if (pw->cbs.add_dir_requested)
            pw->cbs.add_dir_requested(pw->cbs.ud);
        break;
    case M_EDIT: {
        int idx = first_selected(pw);
        Track *t = playlist_track(pw->playlist, idx);
        if (t && pw->cbs.edit_in_editor)
            pw->cbs.edit_in_editor(pw->cbs.ud, t->filepath);
        break;
    }
    case M_REMOVE:
        remove_selected(pw);
        break;
    case M_CLEAR:
        clear_selection(pw);
        playlist_clear(pw->playlist);
        break;
    case M_SEL_ALL:
        select_all(pw);
        break;
    case M_SEL_NONE:
        clear_selection(pw);
        break;
    case M_SEL_INV:
        invert_selection(pw);
        break;
    case M_SORT_FILE:
        playlist_sort_by_filename(pw->playlist);
        break;
    case M_SORT_TITLE:
        playlist_sort_by_title(pw->playlist);
        break;
    case M_SORT_DUR:
        playlist_sort_by_duration(pw->playlist);
        break;
    case M_SORT_PATH:
        playlist_sort_by_path(pw->playlist);
        break;
    case M_REVERSE:
        playlist_reverse(pw->playlist);
        break;
    default:
        break;
    }
}

/* --- Mouse handling --- */

void playlist_window_mouse_press(PlaylistWindow *pw, int x, int y,
                                 int button, int clicks, uint16_t mod)
{
    int lx, ly;
    kwin_logical(&pw->kw, x, y, &lx, &ly);

    /* Resize handle (bottom-right corner) */
    if (lx > pw->pl_w - 20 && ly > pw->pl_h - 20 &&
        button == SDL_BUTTON_LEFT) {
        int gx, gy;
        SDL_GetGlobalMouseState(&gx, &gy);
        pw->resizing = true;
        pw->resize_start_w = pw->pl_w;
        pw->resize_start_h = pw->pl_h;
        pw->resize_start_x = gx;
        pw->resize_start_y = gy;
        SDL_CaptureMouse(SDL_TRUE);
        return;
    }

    /* Close button (top-right) */
    if (ly < PL_TITLE_H && lx > pw->pl_w - 15) {
        kwin_hide(&pw->kw);
        return;
    }

    /* Title bar drag */
    if (ly < PL_TITLE_H && button == SDL_BUTTON_LEFT) {
        kwin_begin_drag(&pw->kw);
        if (pw->cbs.drag_started)
            pw->cbs.drag_started(pw->cbs.ud);
        return;
    }

    int list_y = PL_TITLE_H;
    int list_h = pw->pl_h - PL_TITLE_H - 38;

    /* Playlist item click */
    if (button == SDL_BUTTON_LEFT && ly >= list_y && ly < list_y + list_h &&
        lx >= 12 && lx < pw->pl_w - 20) {
        int item_idx = pw->scroll_offset + (ly - list_y) / PL_ITEM_H;
        if (item_idx >= 0 && item_idx < playlist_count(pw->playlist)) {
            if (clicks >= 2) {
                if (pw->cbs.track_activated)
                    pw->cbs.track_activated(pw->cbs.ud, item_idx);
                return;
            }
            ensure_selected_cap(pw, playlist_count(pw->playlist));
            if (mod & KMOD_CTRL) {
                pw->selected[item_idx] = !pw->selected[item_idx];
                pw->anchor = item_idx;
            } else if (mod & KMOD_SHIFT) {
                int anchor = pw->anchor >= 0 ? pw->anchor : item_idx;
                memset(pw->selected, 0,
                       sizeof(bool) * (size_t)pw->selected_cap);
                for (int i = KA_MIN(anchor, item_idx);
                     i <= KA_MAX(anchor, item_idx); i++)
                    pw->selected[i] = true;
            } else {
                memset(pw->selected, 0,
                       sizeof(bool) * (size_t)pw->selected_cap);
                pw->selected[item_idx] = true;
                pw->anchor = item_idx;
            }
            sync_selected_tracks(pw);
        }
    }

    /* Scrollbar click - jump scroll */
    if (button == SDL_BUTTON_LEFT && lx >= pw->pl_w - 15 && ly >= list_y &&
        ly < list_y + list_h && list_h > 0) {
        double ratio = (double)(ly - list_y) / list_h;
        pw->scroll_offset = (int)(ratio * max_scroll(pw));
    }

    if (button == SDL_BUTTON_RIGHT)
        show_context_menu(pw);
}

void playlist_window_mouse_move(PlaylistWindow *pw, int x, int y)
{
    (void)x;
    (void)y;
    if (pw->kw.dragging) {
        if (pw->cbs.drag_moved) {
            int nx, ny;
            kwin_drag_pos(&pw->kw, &nx, &ny);
            pw->cbs.drag_moved(pw->cbs.ud, nx, ny);
        }
    } else if (pw->resizing) {
        int gx, gy;
        SDL_GetGlobalMouseState(&gx, &gy);
        float scale = kwin_scale(&pw->kw);
        int dx = (int)((gx - pw->resize_start_x) / scale);
        int dy = (int)((gy - pw->resize_start_y) / scale);
        int new_w = KA_MAX(PL_MIN_W, pw->resize_start_w + dx);
        int new_h = KA_MAX(PL_MIN_H, pw->resize_start_h + dy);
        SDL_SetWindowSize(pw->kw.win, (int)lroundf(new_w * scale),
                          (int)lroundf(new_h * scale));
        playlist_window_resize(pw, new_w, new_h);
    }
}

void playlist_window_mouse_release(PlaylistWindow *pw, int x, int y)
{
    (void)x;
    (void)y;
    if (pw->kw.dragging) {
        pw->kw.dragging = false;
        SDL_CaptureMouse(SDL_FALSE);
        if (pw->cbs.drag_ended)
            pw->cbs.drag_ended(pw->cbs.ud);
    } else if (pw->resizing) {
        pw->resizing = false;
        SDL_CaptureMouse(SDL_FALSE);
    }
}

void playlist_window_wheel(PlaylistWindow *pw, int delta_y)
{
    if (delta_y > 0)
        pw->scroll_offset = KA_MAX(0, pw->scroll_offset - 3);
    else
        pw->scroll_offset = KA_MIN(max_scroll(pw), pw->scroll_offset + 3);
}

void playlist_window_drop(PlaylistWindow *pw, const char *path)
{
    if (ka_is_dir(path)) {
        playlist_add_directory(pw->playlist, path, true);
        return;
    }
    char *ext = ka_ext_lower(path);
    if (strcmp(ext, ".m3u") == 0 || strcmp(ext, ".m3u8") == 0)
        playlist_load_m3u(pw->playlist, path);
    else if (strcmp(ext, ".pls") == 0)
        playlist_load_pls(pw->playlist, path);
    else
        playlist_add_file(pw->playlist, path);
    free(ext);
}
