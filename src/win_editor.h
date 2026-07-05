/* Editor window - audio waveform display with effects, selection, and
 * undo/redo. Port of nixamp lib/editor_window.py. */
#ifndef KA_WIN_EDITOR_H
#define KA_WIN_EDITOR_H

#include "audio_data.h"
#include "kwindow.h"
#include "skin.h"
#include "waveform.h"

typedef struct {
    void (*drag_started)(void *ud);
    void (*drag_moved)(void *ud, int x, int y);
    void (*drag_ended)(void *ud);
    void *ud;
} EditorWindowCallbacks;

typedef struct {
    KWindow kw;
    Skin *skin;
    AudioData audio_data;
    WaveformCache cache;
    EditorWindowCallbacks cbs;

    int ed_w, ed_h; /* logical size */
    int view_start, view_end; /* sample indices */

    bool resizing;
    int resize_start_w, resize_start_h;
    int resize_start_x, resize_start_y;
    bool selecting;
    int sel_anchor;
    const char *hover_btn; /* action name or NULL */
    char status_msg[256];
} EditorWindow;

void editor_window_init(EditorWindow *ew, Skin *skin);
void editor_window_destroy(EditorWindow *ew);
void editor_window_render(EditorWindow *ew);
void editor_window_set_scale(EditorWindow *ew, float scale);
void editor_window_resize(EditorWindow *ew, int w, int h); /* logical */
bool editor_window_load_file(EditorWindow *ew, const char *filepath);

void editor_window_mouse_press(EditorWindow *ew, int x, int y, int button,
                               uint16_t mod);
void editor_window_mouse_move(EditorWindow *ew, int x, int y);
void editor_window_mouse_release(EditorWindow *ew, int x, int y);
void editor_window_wheel(EditorWindow *ew, int delta_y, uint16_t mod);
void editor_window_key(EditorWindow *ew, SDL_Keycode key, uint16_t mod);
void editor_window_drop(EditorWindow *ew, const char *path);

#endif
