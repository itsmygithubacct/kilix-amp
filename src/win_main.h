/* Main player window - 275x116 with all controls.
 * Port of nixamp lib/main_window.py. */
#ifndef KA_WIN_MAIN_H
#define KA_WIN_MAIN_H

#include "kwindow.h"
#include "skin.h"
#include "spectrum.h"
#include "text.h"
#include "widgets.h"

typedef struct {
    void (*prev)(void *ud);
    void (*play)(void *ud);
    void (*pause)(void *ud);
    void (*stop)(void *ud);
    void (*next)(void *ud);
    void (*eject)(void *ud);
    void (*volume_changed)(void *ud, int v);
    void (*balance_changed)(void *ud, int v);
    void (*seek_requested)(void *ud, int pct); /* 0-100 */
    void (*shuffle_toggled)(void *ud, bool v);
    void (*repeat_toggled)(void *ud, bool v);
    void (*eq_toggled)(void *ud, bool v);
    void (*pl_toggled)(void *ud, bool v);
    void (*scale_changed)(void *ud, float scale);
    void (*drag_started)(void *ud);
    void (*drag_moved)(void *ud, int x, int y);
    void (*drag_ended)(void *ud);
    void (*quit)(void *ud);
    void *ud;
} MainWindowCallbacks;

typedef struct {
    KWindow kw;
    Skin *skin;
    HitMap hitmap;
    TextRenderer text;
    LCDDisplay lcd;
    SpectrumAnalyzer spectrum;
    MainWindowCallbacks cbs;

    char title_text[512];
    int scroll_offset;
    char play_state[16];
    int position_sec, duration_sec;
    int bitrate, sample_rate, channels;
    bool eq_visible, pl_visible;
    bool seeking;

    SkinButton btn_prev, btn_play, btn_pause, btn_stop, btn_next, btn_eject;
    SkinSlider volume_slider, balance_slider, pos_slider;
    SkinToggle shuffle_toggle, repeat_toggle, eq_toggle, pl_toggle;
} MainWindow;

void main_window_init(MainWindow *mw, Skin *skin);
void main_window_destroy(MainWindow *mw);
void main_window_setup_widgets(MainWindow *mw);
void main_window_reload_skin(MainWindow *mw);
void main_window_render(MainWindow *mw);
void main_window_scroll_tick(MainWindow *mw);

void main_window_set_title(MainWindow *mw, const char *text);
void main_window_set_play_state(MainWindow *mw, const char *state);
void main_window_set_position(MainWindow *mw, int pos_ms, int dur_ms);
void main_window_set_track_info(MainWindow *mw, int bitrate, int sample_rate,
                                int channels);
void main_window_set_spectrum_data(MainWindow *mw, const float *mags, int n);
void main_window_set_volume(MainWindow *mw, int vol);
void main_window_set_balance(MainWindow *mw, int bal);
void main_window_set_shuffle(MainWindow *mw, bool on);
void main_window_set_repeat(MainWindow *mw, bool on);
void main_window_set_eq_visible(MainWindow *mw, bool v);
void main_window_set_pl_visible(MainWindow *mw, bool v);
void main_window_set_scale(MainWindow *mw, float scale);

void main_window_mouse_press(MainWindow *mw, int x, int y, int button);
void main_window_mouse_move(MainWindow *mw, int x, int y);
void main_window_mouse_release(MainWindow *mw, int x, int y);

#endif
