#include "win_playlist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "consts.h"
#include "font4x6.h"
#include "menu.h"

void playlist_window_init(PlaylistWindow *pw, KWindow *player, Skin *skin, Playlist *pl)
{
    memset(pw, 0, sizeof(*pw));
    pw->skin = skin;
    pw->playlist = pl;
    pw->player = player;
    pw->anchor = -1;
    kbuf_init(&pw->buf, PL_MIN_W, PL_DEFAULT_H);
    text_renderer_init(&pw->text, skin_bitmap(skin, "text"));
    playlist_window_on_playlist_changed(pw);
}

void playlist_window_destroy(PlaylistWindow *pw)
{
    free(pw->rows);
    kbuf_destroy(&pw->buf);
}

static int visible_items(const PlaylistWindow *pw)
{
    int list_h = pw->buf.h - PL_TITLE_H - 38; /* title + bottom bar */
    return KA_MAX(1, list_h / PL_ITEM_H);
}

static int max_scroll(const PlaylistWindow *pw)
{
    return KA_MAX(0, playlist_count(pw->playlist) - visible_items(pw));
}

static int compare_entry_id(const void *left, const void *right)
{
    const PlaylistRow *a = left, *b = right;
    return (a->entry_id > b->entry_id) - (a->entry_id < b->entry_id);
}

void playlist_window_on_playlist_changed(PlaylistWindow *pw)
{
    pw->scroll_offset = KA_CLAMP(pw->scroll_offset, 0, max_scroll(pw));
    PlaylistRow *old = pw->rows;
    int selected_count = 0;
    long long anchor_id = 0;
    long long pid = playlist_snapshot_pid(pw->playlist);
    if (pid == pw->service_pid) {
        if (pw->anchor >= 0)
            anchor_id = old[pw->anchor].entry_id;
        for (int i = 0; i < pw->row_count; i++)
            if (old[i].selected && old[i].entry_id > 0)
                old[selected_count++] = old[i];
        if (selected_count > 1)
            qsort(old, (size_t)selected_count, sizeof(*old), compare_entry_id);
    } else {
        pw->current_entry_id = 0;
    }

    pw->service_pid = pid;
    pw->anchor = -1;
    pw->row_count = playlist_count(pw->playlist);
    pw->rows = calloc((size_t)pw->row_count, sizeof(*pw->rows));
    if (pw->row_count && !pw->rows)
        abort();
    for (int i = 0; i < pw->row_count; i++) {
        PlaylistRow *row = &pw->rows[i];
        row->entry_id = playlist_track(pw->playlist, i)->entry_id;
        if (selected_count)
            row->selected = bsearch(row, old, (size_t)selected_count,
                                    sizeof(*old), compare_entry_id) != NULL;
        if (anchor_id > 0 && row->entry_id == anchor_id)
            pw->anchor = i;
    }
    free(old);
}

void playlist_window_on_current_changed(PlaylistWindow *pw, int idx)
{
    Track *current = playlist_track(pw->playlist, idx);
    if (!current) {
        pw->current_entry_id = 0;
        return;
    }
    /* A queue or metadata refresh can report the same playing entry again.
     * Keep the user's scroll position until playback changes tracks. */
    if (current->entry_id > 0 && current->entry_id == pw->current_entry_id)
        return;
    pw->current_entry_id = current->entry_id;
    if (idx < pw->scroll_offset)
        pw->scroll_offset = idx;
    else if (idx >= pw->scroll_offset + visible_items(pw))
        pw->scroll_offset = idx - visible_items(pw) + 1;
}

void playlist_window_resize(PlaylistWindow *pw, int w, int h)
{
    w = KA_MAX(PL_MIN_W, w);
    h = KA_MAX(PL_MIN_H, h);
    if (w != pw->buf.w || h != pw->buf.h) {
        kbuf_resize(&pw->buf, w, h);
        pw->scroll_offset = KA_MIN(pw->scroll_offset, max_scroll(pw));
    }
}

/* --- Rendering --- */

static void draw_items(PlaylistWindow *pw)
{
    SDL_Surface *buf = pw->buf.buf;
    int list_x = 12;
    int list_y = PL_TITLE_H;
    int list_w = pw->buf.w - 12 - 20;
    int list_h = pw->buf.h - PL_TITLE_H - 38;

    SDL_Rect clip = {list_x, list_y, list_w, list_h};
    SDL_SetClipRect(buf, &clip);

    SkinColors *colors = &pw->skin->colors;
    for (int i = 0; i < visible_items(pw); i++) {
        int idx = pw->scroll_offset + i;
        if (idx >= pw->row_count)
            break;
        Track *track = playlist_track(pw->playlist, idx);
        int y = list_y + i * PL_ITEM_H;

        if (pw->rows[idx].selected)
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
    SDL_Surface *buf = pw->buf.buf;
    int sb_x = pw->buf.w - 15;
    int sb_y = PL_TITLE_H;
    int sb_h = pw->buf.h - PL_TITLE_H - 38;

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
    SDL_Surface *buf = pw->buf.buf;
    int info_y = pw->buf.h - 28;
    char *dur = playlist_total_duration_str(pw->playlist);
    char info[128];
    snprintf(info, sizeof(info), "%d TRACKS (%s)",
             playlist_count(pw->playlist), dur);
    free(dur);
    font4x6_draw_centered(buf, KRECT(12, info_y, pw->buf.w - 32, 14), info,
                          pw->skin->colors.pl_normal);
}

void playlist_window_render(PlaylistWindow *pw)
{
    SDL_Surface *buf = pw->buf.buf;
    SkinColors *colors = &pw->skin->colors;

    ksurf_fill(buf, KRECT(0, 0, pw->buf.w, pw->buf.h), colors->pl_normalbg);

    SDL_Surface *pledit = skin_bitmap(pw->skin, "pledit");
    if (pledit) {
        bool act = pw->player->active;
        /* Title bar: left corner, stretch, right corner */
        ksurf_blit(buf, 0, 0, pledit, act ? PL_TL_ACTIVE : PL_TL_INACTIVE);
        KRect tsrc =
            act ? PL_TITLE_STRETCH_ACTIVE : PL_TITLE_STRETCH_INACTIVE;
        int stretch_w = pw->buf.w - 25 - 125;
        for (int x = 25; x < 25 + stretch_w; x += tsrc.w) {
            int w = KA_MIN(tsrc.w, 25 + stretch_w - x);
            ksurf_blit(buf, x, 0, pledit,
                       KRECT(tsrc.x, tsrc.y, w, tsrc.h));
        }
        KRect trsrc = act ? PL_TR_ACTIVE : PL_TR_INACTIVE;
        ksurf_blit(buf, pw->buf.w - trsrc.w, 0, pledit, trsrc);

        /* Left/right edges */
        for (int y = PL_TITLE_H; y < pw->buf.h - 38; y += PL_LEFT_TILE.h) {
            int h = KA_MIN(PL_LEFT_TILE.h, pw->buf.h - 38 - y);
            ksurf_blit(buf, 0, y, pledit,
                       KRECT(PL_LEFT_TILE.x, PL_LEFT_TILE.y, PL_LEFT_TILE.w,
                             h));
        }
        for (int y = PL_TITLE_H; y < pw->buf.h - 38; y += PL_RIGHT_TILE.h) {
            int h = KA_MIN(PL_RIGHT_TILE.h, pw->buf.h - 38 - y);
            ksurf_blit(buf, pw->buf.w - PL_RIGHT_TILE.w, y, pledit,
                       KRECT(PL_RIGHT_TILE.x, PL_RIGHT_TILE.y,
                             PL_RIGHT_TILE.w, h));
        }

        /* Bottom bar */
        int bottom_y = pw->buf.h - 38;
        ksurf_blit(buf, 0, bottom_y, pledit, PL_BL_CORNER);
        ksurf_blit(buf, pw->buf.w - PL_BR_CORNER.w, bottom_y, pledit,
                   PL_BR_CORNER);
    }

    draw_items(pw);
    draw_scrollbar(pw);
    draw_info(pw);
}

/* --- Selection helpers --- */

static void remove_selected(PlaylistWindow *pw)
{
    int *indices = malloc(sizeof(*indices) * (size_t)pw->row_count);
    if (pw->row_count && !indices)
        abort();
    int count = 0;
    for (int i = 0; i < pw->row_count; i++)
        if (pw->rows[i].selected)
            indices[count++] = i;
    if (count)
        playlist_remove(pw->playlist, indices, count);
    free(indices);
}

static void select_all(PlaylistWindow *pw)
{
    for (int i = 0; i < pw->row_count; i++)
        pw->rows[i].selected = true;
}

static void clear_selection(PlaylistWindow *pw)
{
    for (int i = 0; i < pw->row_count; i++)
        pw->rows[i].selected = false;
    pw->anchor = -1;
}

static void invert_selection(PlaylistWindow *pw)
{
    for (int i = 0; i < pw->row_count; i++)
        pw->rows[i].selected = !pw->rows[i].selected;
}

static int first_selected(PlaylistWindow *pw)
{
    for (int i = 0; i < pw->row_count; i++)
        if (pw->rows[i].selected)
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
    kwin_logical(pw->player, x, y, &lx, &ly);
    ly -= MAIN_H;

    /* Resize handle (bottom-right corner) */
    if (lx > pw->buf.w - 20 && ly > pw->buf.h - 20 &&
        button == SDL_BUTTON_LEFT) {
        int wx, wy;
        kwin_get_pos(pw->player, &wx, &wy);
        pw->resizing = true;
        kwin_get_size(pw->player, &pw->resize_start_w, &pw->resize_start_h);
        pw->resize_start_x = wx + x;
        pw->resize_start_y = wy + y;
        SDL_CaptureMouse(SDL_TRUE);
        return;
    }

    /* Close button (top-right) */
    if (button == SDL_BUTTON_LEFT && ly < PL_TITLE_H && lx > pw->buf.w - 15) {
        if (pw->cbs.set_visible)
            pw->cbs.set_visible(pw->cbs.ud, false);
        return;
    }

    /* Title bar drag */
    if (ly < PL_TITLE_H && button == SDL_BUTTON_LEFT) {
        kwin_begin_drag(pw->player, x, y);
        if (pw->cbs.drag_started)
            pw->cbs.drag_started(pw->cbs.ud);
        return;
    }

    int list_y = PL_TITLE_H;
    int list_h = pw->buf.h - PL_TITLE_H - 38;

    /* Playlist item click */
    if (button == SDL_BUTTON_LEFT && ly >= list_y && ly < list_y + list_h &&
        lx >= 12 && lx < pw->buf.w - 20) {
        int item_idx = pw->scroll_offset + (ly - list_y) / PL_ITEM_H;
        if (item_idx >= 0 && item_idx < pw->row_count) {
            if (clicks >= 2) {
                if (pw->cbs.track_activated)
                    pw->cbs.track_activated(pw->cbs.ud, item_idx);
                return;
            }
            if (mod & KMOD_CTRL) {
                pw->rows[item_idx].selected = !pw->rows[item_idx].selected;
                pw->anchor = item_idx;
            } else if (mod & KMOD_SHIFT) {
                int anchor = pw->anchor >= 0 ? pw->anchor : item_idx;
                clear_selection(pw);
                pw->anchor = anchor;
                for (int i = KA_MIN(anchor, item_idx);
                     i <= KA_MAX(anchor, item_idx); i++)
                    pw->rows[i].selected = true;
            } else {
                clear_selection(pw);
                pw->rows[item_idx].selected = true;
                pw->anchor = item_idx;
            }
        }
    }

    /* Scrollbar click - jump scroll */
    if (button == SDL_BUTTON_LEFT && lx >= pw->buf.w - 15 && ly >= list_y &&
        ly < list_y + list_h && list_h > 0) {
        double ratio = (double)(ly - list_y) / list_h;
        pw->scroll_offset = (int)(ratio * max_scroll(pw));
    }

    if (button == SDL_BUTTON_RIGHT)
        show_context_menu(pw);
}

void playlist_window_drag_resize(PlaylistWindow *pw)
{
    int gx, gy;
    SDL_GetGlobalMouseState(&gx, &gy);
    int min_w, min_h;
    SDL_GetWindowMinimumSize(pw->player->win, &min_w, &min_h);
    SDL_SetWindowSize(pw->player->win,
                      KA_MAX(min_w, pw->resize_start_w + gx - pw->resize_start_x),
                      KA_MAX(min_h, pw->resize_start_h + gy - pw->resize_start_y));
}

void playlist_window_end_resize(PlaylistWindow *pw)
{
    if (pw->resizing) {
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
    playlist_add_file(pw->playlist, path);
}
