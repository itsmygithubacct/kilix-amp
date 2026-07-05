#include "win_eq.h"

#include <math.h>
#include <string.h>

#include "consts.h"
#include "menu.h"

static void cb_enabled(void *ud, bool v)
{
    EQWindow *e = ud;
    if (e->cbs.eq_enabled_changed)
        e->cbs.eq_enabled_changed(e->cbs.ud, v);
}

static void cb_preamp(void *ud, int v)
{
    EQWindow *e = ud;
    if (e->cbs.preamp_changed)
        e->cbs.preamp_changed(e->cbs.ud, (double)v);
}

static void cb_band(void *ud, int v)
{
    EQBandThunk *t = ud;
    if (t->ew->cbs.eq_changed)
        t->ew->cbs.eq_changed(t->ew->cbs.ud, t->band, (double)v);
}

static void show_presets_menu(EQWindow *ew)
{
    MenuItem items[EQ_PRESET_COUNT];
    for (int i = 0; i < EQ_PRESET_COUNT; i++)
        items[i] = (MenuItem){EQ_PRESETS[i].name, i};

    int wx, wy;
    kwin_get_pos(&ew->kw, &wx, &wy);
    float scale = kwin_scale(&ew->kw);
    int choice = menu_show(
        wx + (int)lroundf(EQ_PRESETS_BTN.x * scale),
        wy + (int)lroundf((EQ_PRESETS_BTN.y + EQ_PRESETS_BTN.h) * scale),
        items, EQ_PRESET_COUNT);
    if (choice >= 0 && ew->cbs.preset_selected)
        ew->cbs.preset_selected(ew->cbs.ud, EQ_PRESETS[choice].name);
}

static void cb_presets(void *ud)
{
    show_presets_menu(ud);
}

void eq_window_init(EQWindow *ew, Skin *skin)
{
    memset(ew, 0, sizeof(*ew));
    ew->skin = skin;
    kwin_create(&ew->kw, "kilix-amp equalizer", EQ_W, EQ_H, false);

    hitmap_init(&ew->hitmap);

    ew->on_toggle =
        skin_toggle(EQ_ON_BTN, EQ_ON_ON_SRC, EQ_ON_ON_SRC, EQ_ON_OFF_SRC,
                    EQ_ON_OFF_SRC, "eqmain", false, cb_enabled, ew);
    hitmap_add_toggle(&ew->hitmap, &ew->on_toggle);

    ew->auto_toggle =
        skin_toggle(EQ_AUTO_BTN, EQ_AUTO_ON_SRC, EQ_AUTO_ON_SRC,
                    EQ_AUTO_OFF_SRC, EQ_AUTO_OFF_SRC, "eqmain", false, NULL,
                    NULL);
    hitmap_add_toggle(&ew->hitmap, &ew->auto_toggle);

    ew->presets_btn = skin_button(EQ_PRESETS_BTN, EQ_PRESETS_N_SRC,
                                  EQ_PRESETS_P_SRC, "eqmain", cb_presets, ew);
    hitmap_add_button(&ew->hitmap, &ew->presets_btn);

    ew->preamp_slider = skin_slider(
        KRECT(EQ_PREAMP_X, EQ_SLIDER_Y, EQ_SLIDER_W, EQ_SLIDER_H),
        EQ_SLIDER_THUMB_N, EQ_SLIDER_THUMB_P, EQ_SLIDER_W, 11, "eqmain",
        SLIDER_VERTICAL, -12, 12, 0, cb_preamp, ew);
    hitmap_add_slider(&ew->hitmap, &ew->preamp_slider);

    for (int i = 0; i < 10; i++) {
        ew->band_thunks[i] = (EQBandThunk){ew, i};
        ew->band_sliders[i] = skin_slider(
            KRECT(EQ_BAND_X[i], EQ_SLIDER_Y, EQ_SLIDER_W, EQ_SLIDER_H),
            EQ_SLIDER_THUMB_N, EQ_SLIDER_THUMB_P, EQ_SLIDER_W, 11, "eqmain",
            SLIDER_VERTICAL, -12, 12, 0, cb_band, &ew->band_thunks[i]);
        hitmap_add_slider(&ew->hitmap, &ew->band_sliders[i]);
    }
}

void eq_window_set_scale(EQWindow *ew, float scale)
{
    kwin_set_scale_fixed(&ew->kw, scale);
}

void eq_window_set_eq_enabled(EQWindow *ew, bool enabled)
{
    ew->eq_enabled = enabled;
    ew->on_toggle.state = enabled;
}

void eq_window_set_bands(EQWindow *ew, const float bands[10])
{
    for (int i = 0; i < 10; i++)
        skin_slider_set_value(&ew->band_sliders[i], (int)lroundf(bands[i]));
}

void eq_window_set_preamp(EQWindow *ew, double val)
{
    skin_slider_set_value(&ew->preamp_slider, (int)lround(val));
}

/* EQ frequency response curve in the graph area (linear interpolation). */
static void draw_eq_graph(EQWindow *ew)
{
    SDL_Surface *buf = ew->kw.buf.buf;
    KRect g = EQ_GRAPH;
    KColor green = {0, 198, 0};

    int preamp = ew->preamp_slider.value;
    int prev_y = -1;
    for (int i = 0; i < g.w; i++) {
        double band_idx = (double)i * 10 / g.w;
        int lo = (int)band_idx;
        int hi = KA_MIN(lo + 1, 9);
        double frac = band_idx - lo;
        double val = ew->band_sliders[lo].value * (1 - frac) +
                     ew->band_sliders[hi].value * frac + preamp;
        /* Map -24..+24 to graph height */
        int ny = g.y + g.h / 2 - (int)(val / 24 * (g.h / 2));
        ny = KA_CLAMP(ny, g.y, g.y + g.h - 1);
        if (prev_y >= 0)
            ksurf_line(buf, g.x + i - 1, prev_y, g.x + i, ny, green);
        prev_y = ny;
    }
}

void eq_window_render(EQWindow *ew)
{
    SDL_Surface *buf = ew->kw.buf.buf;
    SDL_Surface *eqmain = skin_bitmap(ew->skin, "eqmain");
    if (!eqmain) {
        kbuf_clear(&ew->kw.buf, (KColor){0, 0, 0});
    } else {
        ksurf_blit(buf, 0, 0, eqmain, EQ_BG_SRC);
        ksurf_blit(buf, 0, 0, eqmain,
                   ew->kw.active ? EQ_TITLE_ACTIVE_SRC
                                 : EQ_TITLE_INACTIVE_SRC);
        ksurf_blit(buf, EQ_ON_BTN.x, EQ_ON_BTN.y, eqmain,
                   skin_toggle_src(&ew->on_toggle));
        ksurf_blit(buf, EQ_AUTO_BTN.x, EQ_AUTO_BTN.y, eqmain,
                   skin_toggle_src(&ew->auto_toggle));
        ksurf_blit(buf, EQ_PRESETS_BTN.x, EQ_PRESETS_BTN.y, eqmain,
                   skin_button_src(&ew->presets_btn));

        SkinSlider *sliders[11];
        sliders[0] = &ew->preamp_slider;
        for (int i = 0; i < 10; i++)
            sliders[i + 1] = &ew->band_sliders[i];
        for (int i = 0; i < 11; i++) {
            KRect tr = skin_slider_thumb_rect(sliders[i]);
            ksurf_blit(buf, tr.x, tr.y, eqmain,
                       skin_slider_thumb_src(sliders[i]));
        }
    }

    draw_eq_graph(ew);
    kwin_present(&ew->kw);
}

void eq_window_mouse_press(EQWindow *ew, int x, int y, int button)
{
    (void)button;
    int lx, ly;
    kwin_logical(&ew->kw, x, y, &lx, &ly);

    if (krect_contains(EQ_CLOSE_BTN, lx, ly)) {
        kwin_hide(&ew->kw);
        return;
    }

    if (hitmap_mouse_press(&ew->hitmap, lx, ly, NULL) != HIT_NONE)
        return;

    if (krect_contains(EQ_TITLE_BAR, lx, ly)) {
        kwin_begin_drag(&ew->kw);
        if (ew->cbs.drag_started)
            ew->cbs.drag_started(ew->cbs.ud);
    }
}

void eq_window_mouse_move(EQWindow *ew, int x, int y)
{
    if (ew->kw.dragging) {
        if (ew->cbs.drag_moved) {
            int nx, ny;
            kwin_drag_pos(&ew->kw, &nx, &ny);
            ew->cbs.drag_moved(ew->cbs.ud, nx, ny);
        }
    } else if (hitmap_is_captured(&ew->hitmap)) {
        int lx, ly;
        kwin_logical(&ew->kw, x, y, &lx, &ly);
        hitmap_mouse_move(&ew->hitmap, lx, ly);
    }
}

void eq_window_mouse_release(EQWindow *ew, int x, int y)
{
    if (ew->kw.dragging) {
        ew->kw.dragging = false;
        SDL_CaptureMouse(SDL_FALSE);
        if (ew->cbs.drag_ended)
            ew->cbs.drag_ended(ew->cbs.ud);
    } else {
        int lx, ly;
        kwin_logical(&ew->kw, x, y, &lx, &ly);
        hitmap_mouse_release(&ew->hitmap, lx, ly);
    }
}

void eq_window_destroy(EQWindow *ew)
{
    kwin_destroy(&ew->kw);
}
