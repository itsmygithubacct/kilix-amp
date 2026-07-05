/* Playlist editor window - resizable with scrollbar and track list.
 * Port of nixamp lib/playlist_window.py. */
#ifndef KA_WIN_PLAYLIST_H
#define KA_WIN_PLAYLIST_H

#include "kwindow.h"
#include "playlist.h"
#include "skin.h"
#include "text.h"

typedef struct {
    void (*track_activated)(void *ud, int index);
    void (*add_files_requested)(void *ud);
    void (*add_dir_requested)(void *ud);
    void (*edit_in_editor)(void *ud, const char *filepath);
    void (*drag_started)(void *ud);
    void (*drag_moved)(void *ud, int x, int y);
    void (*drag_ended)(void *ud);
    void *ud;
} PlaylistWindowCallbacks;

typedef struct {
    KWindow kw;
    Skin *skin;
    Playlist *playlist;
    TextRenderer text;
    PlaylistWindowCallbacks cbs;

    int pl_w, pl_h; /* logical size */
    int scroll_offset;
    /* Selection as index flags plus stable Track-pointer snapshot for
     * remapping after reorder/remove. */
    bool *selected;
    int selected_cap;
    Track **selected_tracks;
    int selected_track_count;
    int anchor; /* -1 = none */

    bool resizing;
    int resize_start_w, resize_start_h;
    int resize_start_x, resize_start_y;
} PlaylistWindow;

void playlist_window_init(PlaylistWindow *pw, Skin *skin, Playlist *pl);
void playlist_window_destroy(PlaylistWindow *pw);
void playlist_window_render(PlaylistWindow *pw);
void playlist_window_set_scale(PlaylistWindow *pw, float scale);
void playlist_window_resize(PlaylistWindow *pw, int w, int h); /* logical */
/* Model-changed hook (reclamps scroll, remaps selection). */
void playlist_window_on_playlist_changed(PlaylistWindow *pw);
void playlist_window_on_current_changed(PlaylistWindow *pw, int idx);

void playlist_window_mouse_press(PlaylistWindow *pw, int x, int y,
                                 int button, int clicks, uint16_t mod);
void playlist_window_mouse_move(PlaylistWindow *pw, int x, int y);
void playlist_window_mouse_release(PlaylistWindow *pw, int x, int y);
void playlist_window_wheel(PlaylistWindow *pw, int delta_y);
/* Handle a file/dir/playlist path dropped onto the window. */
void playlist_window_drop(PlaylistWindow *pw, const char *path);

#endif
