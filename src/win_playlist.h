/* Playlist panel in the player window, with a scrollbar and resize grip.
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
    void (*set_visible)(void *ud, bool visible);
    void *ud;
} PlaylistWindowCallbacks;

typedef struct {
    long long entry_id;
    bool selected;
} PlaylistRow;

typedef struct {
    KWindow *player;
    KBuffer buf;
    Skin *skin;
    Playlist *playlist;
    TextRenderer text;
    PlaylistWindowCallbacks cbs;

    int scroll_offset;
    PlaylistRow *rows;
    int row_count;
    long long service_pid, current_entry_id;
    int anchor; /* -1 = none */

    int thumb_grab_y; /* offset inside the scrollbar thumb, -1 when released */
    bool resizing;
    int resize_start_w, resize_start_h;
    int resize_start_x, resize_start_y;
} PlaylistWindow;

void playlist_window_init(PlaylistWindow *pw, KWindow *player, Skin *skin, Playlist *pl);
void playlist_window_destroy(PlaylistWindow *pw);
void playlist_window_render(PlaylistWindow *pw);
void playlist_window_resize(PlaylistWindow *pw, int w, int h); /* logical */
/* Model-changed hook (reclamps scroll, remaps selection). */
void playlist_window_on_playlist_changed(PlaylistWindow *pw);
void playlist_window_on_current_changed(PlaylistWindow *pw, int idx);

/* Coordinates are native pixels relative to the shared player window. */
void playlist_window_mouse_press(PlaylistWindow *pw, int x, int y,
                                 int button, int clicks, uint16_t mod);
void playlist_window_mouse_move(PlaylistWindow *pw, int y);
void playlist_window_end_drag(PlaylistWindow *pw);
void playlist_window_wheel(PlaylistWindow *pw, int delta_y);
/* Handle a file/dir/playlist path dropped onto the window. */
void playlist_window_drop(PlaylistWindow *pw, const char *path);

#endif
