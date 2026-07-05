/* Equalizer window - 275x116 with 11 vertical sliders.
 * Port of nixamp lib/eq_window.py. */
#ifndef KA_WIN_EQ_H
#define KA_WIN_EQ_H

#include "kwindow.h"
#include "skin.h"
#include "widgets.h"

typedef struct {
    void (*eq_changed)(void *ud, int band, double gain_db);
    void (*preamp_changed)(void *ud, double gain_db);
    void (*eq_enabled_changed)(void *ud, bool enabled);
    void (*preset_selected)(void *ud, const char *name);
    void (*drag_started)(void *ud);
    void (*drag_moved)(void *ud, int x, int y);
    void (*drag_ended)(void *ud);
    void *ud;
} EQWindowCallbacks;

typedef struct EQWindow EQWindow;

/* Per-band slider callback context. */
typedef struct {
    EQWindow *ew;
    int band;
} EQBandThunk;

struct EQWindow {
    KWindow kw;
    Skin *skin;
    HitMap hitmap;
    EQWindowCallbacks cbs;
    bool eq_enabled;

    SkinToggle on_toggle, auto_toggle;
    SkinButton presets_btn;
    SkinSlider preamp_slider;
    SkinSlider band_sliders[10];
    EQBandThunk band_thunks[10];
};

void eq_window_init(EQWindow *ew, Skin *skin);
void eq_window_destroy(EQWindow *ew);
void eq_window_render(EQWindow *ew);
void eq_window_set_scale(EQWindow *ew, float scale);
void eq_window_set_eq_enabled(EQWindow *ew, bool enabled);
void eq_window_set_bands(EQWindow *ew, const float bands[10]);
void eq_window_set_preamp(EQWindow *ew, double val);

void eq_window_mouse_press(EQWindow *ew, int x, int y, int button);
void eq_window_mouse_move(EQWindow *ew, int x, int y);
void eq_window_mouse_release(EQWindow *ew, int x, int y);

#endif
