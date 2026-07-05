#include "win_main.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "consts.h"
#include "menu.h"

void main_window_init(MainWindow *mw, Skin *skin)
{
    memset(mw, 0, sizeof(*mw));
    mw->skin = skin;
    kwin_create(&mw->kw, "kilix-amp", MAIN_W, MAIN_H, false);

    text_renderer_init(&mw->text, skin_bitmap(skin, "text"));
    lcd_init(&mw->lcd, skin_bitmap(skin, "nums_ex"));
    spectrum_init(&mw->spectrum);
    spectrum_set_viscolors(&mw->spectrum, skin->colors.viscolors);

    snprintf(mw->title_text, sizeof(mw->title_text), "KILIX-AMP");
    snprintf(mw->play_state, sizeof(mw->play_state), "stopped");
    mw->channels = 2;
    mw->pl_visible = true;

    main_window_setup_widgets(mw);
}

static void cb_prev(void *ud)   { MainWindow *m = ud; if (m->cbs.prev) m->cbs.prev(m->cbs.ud); }
static void cb_play(void *ud)   { MainWindow *m = ud; if (m->cbs.play) m->cbs.play(m->cbs.ud); }
static void cb_pause(void *ud)  { MainWindow *m = ud; if (m->cbs.pause) m->cbs.pause(m->cbs.ud); }
static void cb_stop(void *ud)   { MainWindow *m = ud; if (m->cbs.stop) m->cbs.stop(m->cbs.ud); }
static void cb_next(void *ud)   { MainWindow *m = ud; if (m->cbs.next) m->cbs.next(m->cbs.ud); }
static void cb_eject(void *ud)  { MainWindow *m = ud; if (m->cbs.eject) m->cbs.eject(m->cbs.ud); }

static void cb_volume(void *ud, int v)
{
    MainWindow *m = ud;
    if (m->cbs.volume_changed)
        m->cbs.volume_changed(m->cbs.ud, v);
}

static void cb_balance(void *ud, int v)
{
    MainWindow *m = ud;
    if (m->cbs.balance_changed)
        m->cbs.balance_changed(m->cbs.ud, v);
}

static void cb_seek(void *ud, int v)
{
    MainWindow *m = ud;
    if (m->cbs.seek_requested)
        m->cbs.seek_requested(m->cbs.ud, v);
}

static void cb_shuffle(void *ud, bool v)
{
    MainWindow *m = ud;
    if (m->cbs.shuffle_toggled)
        m->cbs.shuffle_toggled(m->cbs.ud, v);
}

static void cb_repeat(void *ud, bool v)
{
    MainWindow *m = ud;
    (void)v;
    if (m->cbs.repeat_toggled)
        m->cbs.repeat_toggled(m->cbs.ud, v);
}

static void cb_eq_toggle(void *ud, bool v)
{
    MainWindow *m = ud;
    m->eq_visible = v;
    if (m->cbs.eq_toggled)
        m->cbs.eq_toggled(m->cbs.ud, v);
}

static void cb_pl_toggle(void *ud, bool v)
{
    MainWindow *m = ud;
    m->pl_visible = v;
    if (m->cbs.pl_toggled)
        m->cbs.pl_toggled(m->cbs.ud, v);
}

void main_window_setup_widgets(MainWindow *mw)
{
    hitmap_init(&mw->hitmap);

    mw->btn_prev = skin_button(BTN_PREV, CBUTTONS_PREV_N, CBUTTONS_PREV_P,
                               "cbuttons", cb_prev, mw);
    mw->btn_play = skin_button(BTN_PLAY, CBUTTONS_PLAY_N, CBUTTONS_PLAY_P,
                               "cbuttons", cb_play, mw);
    mw->btn_pause = skin_button(BTN_PAUSE, CBUTTONS_PAUSE_N,
                                CBUTTONS_PAUSE_P, "cbuttons", cb_pause, mw);
    mw->btn_stop = skin_button(BTN_STOP, CBUTTONS_STOP_N, CBUTTONS_STOP_P,
                               "cbuttons", cb_stop, mw);
    mw->btn_next = skin_button(BTN_NEXT, CBUTTONS_NEXT_N, CBUTTONS_NEXT_P,
                               "cbuttons", cb_next, mw);
    mw->btn_eject = skin_button(BTN_EJECT, CBUTTONS_EJECT_N,
                                CBUTTONS_EJECT_P, "cbuttons", cb_eject, mw);
    hitmap_add_button(&mw->hitmap, &mw->btn_prev);
    hitmap_add_button(&mw->hitmap, &mw->btn_play);
    hitmap_add_button(&mw->hitmap, &mw->btn_pause);
    hitmap_add_button(&mw->hitmap, &mw->btn_stop);
    hitmap_add_button(&mw->hitmap, &mw->btn_next);
    hitmap_add_button(&mw->hitmap, &mw->btn_eject);

    mw->volume_slider = skin_slider(
        VOLUME_AREA, VOLUME_THUMB_NORMAL_SRC, VOLUME_THUMB_PRESSED_SRC,
        VOLUME_THUMB_W, VOLUME_THUMB_H, "volume", SLIDER_HORIZONTAL, 0, 100,
        80, cb_volume, mw);
    hitmap_add_slider(&mw->hitmap, &mw->volume_slider);

    mw->balance_slider = skin_slider(
        BALANCE_AREA, BALANCE_THUMB_NORMAL_SRC, BALANCE_THUMB_PRESSED_SRC,
        BALANCE_THUMB_W, BALANCE_THUMB_H, "balance", SLIDER_HORIZONTAL,
        -100, 100, 0, cb_balance, mw);
    hitmap_add_slider(&mw->hitmap, &mw->balance_slider);

    mw->pos_slider =
        skin_slider(POS_BAR, POS_THUMB_NORMAL, POS_THUMB_PRESSED, 29, 10,
                    "posbar", SLIDER_HORIZONTAL, 0, 100, 0, cb_seek, mw);
    hitmap_add_slider(&mw->hitmap, &mw->pos_slider);

    mw->shuffle_toggle =
        skin_toggle(SHUFFLE_BTN, SHUFFLE_ON_N, SHUFFLE_ON_P, SHUFFLE_OFF_N,
                    SHUFFLE_OFF_P, "shufrep", false, cb_shuffle, mw);
    mw->repeat_toggle =
        skin_toggle(REPEAT_BTN, REPEAT_ON_N, REPEAT_ON_P, REPEAT_OFF_N,
                    REPEAT_OFF_P, "shufrep", false, cb_repeat, mw);
    mw->eq_toggle = skin_toggle(EQ_BTN, EQ_ON_N, EQ_ON_P, EQ_OFF_N, EQ_OFF_P,
                                "shufrep", false, cb_eq_toggle, mw);
    mw->pl_toggle = skin_toggle(PL_BTN, PL_ON_N, PL_ON_P, PL_OFF_N, PL_OFF_P,
                                "shufrep", true, cb_pl_toggle, mw);
    hitmap_add_toggle(&mw->hitmap, &mw->shuffle_toggle);
    hitmap_add_toggle(&mw->hitmap, &mw->repeat_toggle);
    hitmap_add_toggle(&mw->hitmap, &mw->eq_toggle);
    hitmap_add_toggle(&mw->hitmap, &mw->pl_toggle);
}

void main_window_reload_skin(MainWindow *mw)
{
    text_renderer_init(&mw->text, skin_bitmap(mw->skin, "text"));
    lcd_init(&mw->lcd, skin_bitmap(mw->skin, "nums_ex"));
    spectrum_set_viscolors(&mw->spectrum, mw->skin->colors.viscolors);
}

/* --- Public setters (mirroring the Python API) --- */

void main_window_set_title(MainWindow *mw, const char *text)
{
    snprintf(mw->title_text, sizeof(mw->title_text), "%s", text);
    mw->scroll_offset = 0;
}

void main_window_set_play_state(MainWindow *mw, const char *state)
{
    snprintf(mw->play_state, sizeof(mw->play_state), "%s", state);
}

void main_window_set_position(MainWindow *mw, int pos_ms, int dur_ms)
{
    mw->position_sec = pos_ms / 1000;
    mw->duration_sec = dur_ms / 1000;
    if (!mw->seeking && dur_ms > 0)
        skin_slider_set_value(&mw->pos_slider,
                              (int)((int64_t)pos_ms * 100 / dur_ms));
}

void main_window_set_track_info(MainWindow *mw, int bitrate, int sample_rate,
                                int channels)
{
    mw->bitrate = bitrate;
    mw->sample_rate = sample_rate;
    mw->channels = channels;
}

void main_window_set_spectrum_data(MainWindow *mw, const float *mags, int n)
{
    spectrum_update(&mw->spectrum, mags, n);
}

void main_window_set_volume(MainWindow *mw, int vol)
{
    skin_slider_set_value(&mw->volume_slider, vol);
}

void main_window_set_balance(MainWindow *mw, int bal)
{
    skin_slider_set_value(&mw->balance_slider, bal);
}

void main_window_set_shuffle(MainWindow *mw, bool on)
{
    mw->shuffle_toggle.state = on;
}

void main_window_set_repeat(MainWindow *mw, bool on)
{
    mw->repeat_toggle.state = on;
}

void main_window_set_eq_visible(MainWindow *mw, bool v)
{
    mw->eq_visible = v;
    mw->eq_toggle.state = v;
}

void main_window_set_pl_visible(MainWindow *mw, bool v)
{
    mw->pl_visible = v;
    mw->pl_toggle.state = v;
}

void main_window_set_scale(MainWindow *mw, float scale)
{
    kwin_set_scale_fixed(&mw->kw, scale);
}

void main_window_scroll_tick(MainWindow *mw)
{
    mw->scroll_offset += SCROLL_SPEED;
}

/* --- Rendering --- */

static void blit_sheet(MainWindow *mw, const char *sheet, int dx, int dy,
                       KRect src)
{
    SDL_Surface *bmp = skin_bitmap(mw->skin, sheet);
    if (bmp)
        ksurf_blit(mw->kw.buf.buf, dx, dy, bmp, src);
}

void main_window_render(MainWindow *mw)
{
    SDL_Surface *buf = mw->kw.buf.buf;

    SDL_Surface *main_bmp = skin_bitmap(mw->skin, "main");
    if (main_bmp)
        ksurf_blit(buf, 0, 0, main_bmp, KRECT(0, 0, MAIN_W, MAIN_H));
    else
        kbuf_clear(&mw->kw.buf, (KColor){0, 0, 0});

    /* Title bar + close button */
    blit_sheet(mw, "titlebar", 0, 0,
               mw->kw.active ? TITLE_BAR_ACTIVE_SRC
                             : TITLE_BAR_INACTIVE_SRC);
    blit_sheet(mw, "titlebar", CLOSE_BTN.x, CLOSE_BTN.y, CLOSE_SRC_NORMAL);

    /* Transport buttons */
    SkinButton *btns[] = {&mw->btn_prev, &mw->btn_play, &mw->btn_pause,
                          &mw->btn_stop, &mw->btn_next, &mw->btn_eject};
    for (size_t i = 0; i < KA_LEN(btns); i++)
        blit_sheet(mw, "cbuttons", btns[i]->region.x, btns[i]->region.y,
                   skin_button_src(btns[i]));

    /* Volume slider: frame by level + thumb */
    SDL_Surface *volume_bmp = skin_bitmap(mw->skin, "volume");
    if (volume_bmp) {
        int frame_idx = mw->volume_slider.value * 27 / 100;
        ksurf_blit(buf, VOLUME_AREA.x, VOLUME_AREA.y, volume_bmp,
                   KRECT(0, frame_idx * VOLUME_BG_FRAME_H,
                         VOLUME_BG_FRAME_W, VOLUME_BG_FRAME_H));
        KRect tr = skin_slider_thumb_rect(&mw->volume_slider);
        ksurf_blit(buf, tr.x, tr.y, volume_bmp,
                   skin_slider_thumb_src(&mw->volume_slider));
    }

    /* Balance slider */
    SDL_Surface *balance_bmp = skin_bitmap(mw->skin, "balance");
    if (balance_bmp) {
        int frame_idx = (mw->balance_slider.value + 100) * 27 / 200;
        ksurf_blit(buf, BALANCE_AREA.x, BALANCE_AREA.y, balance_bmp,
                   KRECT(0, frame_idx * BALANCE_BG_FRAME_H,
                         BALANCE_BG_FRAME_W, BALANCE_BG_FRAME_H));
        KRect tr = skin_slider_thumb_rect(&mw->balance_slider);
        ksurf_blit(buf, tr.x, tr.y, balance_bmp,
                   skin_slider_thumb_src(&mw->balance_slider));
    }

    /* Position bar */
    SDL_Surface *posbar_bmp = skin_bitmap(mw->skin, "posbar");
    if (posbar_bmp) {
        ksurf_blit(buf, POS_BAR.x, POS_BAR.y, posbar_bmp, POS_BAR_BG_SRC);
        KRect tr = skin_slider_thumb_rect(&mw->pos_slider);
        ksurf_blit(buf, tr.x, tr.y, posbar_bmp,
                   skin_slider_thumb_src(&mw->pos_slider));
    }

    /* Toggles */
    SkinToggle *toggles[] = {&mw->shuffle_toggle, &mw->repeat_toggle,
                             &mw->eq_toggle, &mw->pl_toggle};
    for (size_t i = 0; i < KA_LEN(toggles); i++)
        blit_sheet(mw, "shufrep", toggles[i]->region.x,
                   toggles[i]->region.y, skin_toggle_src(toggles[i]));

    /* Scrolling title text */
    text_render_scrolling(&mw->text, buf, mw->title_text, TITLE_DISPLAY.x,
                          TITLE_DISPLAY.y, TITLE_DISPLAY.w,
                          mw->scroll_offset);

    /* LCD time */
    lcd_render_time(&mw->lcd, buf, mw->position_sec, mw->duration_sec,
                    TIME_DIGIT_1.x, TIME_DIGIT_1.y);

    /* Bitrate / sample rate */
    if (mw->bitrate > 0) {
        char kbps[16];
        snprintf(kbps, sizeof(kbps), "%d", mw->bitrate);
        kbps[3] = '\0';
        text_render(&mw->text, buf, kbps, KBPS_DISPLAY.x, KBPS_DISPLAY.y,
                    -1);
    }
    if (mw->sample_rate > 0) {
        char khz[16];
        snprintf(khz, sizeof(khz), "%d", mw->sample_rate / 1000);
        khz[2] = '\0';
        text_render(&mw->text, buf, khz, KHZ_DISPLAY.x, KHZ_DISPLAY.y, -1);
    }

    /* Mono/Stereo */
    if (mw->channels == 1) {
        blit_sheet(mw, "monoster", MONO_INDICATOR.x, MONO_INDICATOR.y,
                   MONO_ON_SRC);
        blit_sheet(mw, "monoster", STEREO_INDICATOR.x, STEREO_INDICATOR.y,
                   STEREO_OFF_SRC);
    } else {
        blit_sheet(mw, "monoster", STEREO_INDICATOR.x, STEREO_INDICATOR.y,
                   STEREO_ON_SRC);
        blit_sheet(mw, "monoster", MONO_INDICATOR.x, MONO_INDICATOR.y,
                   MONO_OFF_SRC);
    }

    /* Play status indicator */
    KRect status_src = PLAYPAUS_STOP;
    if (strcmp(mw->play_state, "playing") == 0)
        status_src = PLAYPAUS_PLAY;
    else if (strcmp(mw->play_state, "paused") == 0)
        status_src = PLAYPAUS_PAUSE;
    blit_sheet(mw, "playpaus", PLAYPAUS_AREA.x, PLAYPAUS_AREA.y, status_src);

    /* Visualization */
    spectrum_render(&mw->spectrum, buf, VIS_AREA.x, VIS_AREA.y);

    kwin_present(&mw->kw);
}

/* --- Mouse handling --- */

static void show_context_menu(MainWindow *mw)
{
    enum { M_OPEN = 1, M_ABOUT, M_CLOSE, M_SIZE_BASE = 10 };
    MenuItem items[8 + SCALE_PRESET_COUNT];
    char size_labels[SCALE_PRESET_COUNT][32];
    int n = 0;
    items[n++] = (MenuItem){"OPEN FILE...", M_OPEN};
    items[n++] = MENU_SEPARATOR;
    items[n++] = MENU_HEADER("SIZE");
    float cur = kwin_scale(&mw->kw);
    for (int s = 0; s < SCALE_PRESET_COUNT; s++) {
        float sc = SCALE_PRESETS[s];
        snprintf(size_labels[s], sizeof(size_labels[s]),
                 "%s%gX (%d PX)", fabsf(sc - cur) < 0.01f ? "> " : "  ",
                 sc, (int)lroundf(MAIN_W * sc));
        items[n++] = (MenuItem){size_labels[s], M_SIZE_BASE + s};
    }
    items[n++] = MENU_SEPARATOR;
    items[n++] = (MenuItem){"ABOUT KILIX-AMP", M_ABOUT};
    items[n++] = MENU_SEPARATOR;
    items[n++] = (MenuItem){"CLOSE", M_CLOSE};

    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    int choice = menu_show(mx, my, items, n);

    if (choice == M_OPEN) {
        if (mw->cbs.eject)
            mw->cbs.eject(mw->cbs.ud);
    } else if (choice == M_CLOSE) {
        if (mw->cbs.quit)
            mw->cbs.quit(mw->cbs.ud);
    } else if (choice >= M_SIZE_BASE &&
               choice < M_SIZE_BASE + SCALE_PRESET_COUNT) {
        float scale = SCALE_PRESETS[choice - M_SIZE_BASE];
        main_window_set_scale(mw, scale);
        if (mw->cbs.scale_changed)
            mw->cbs.scale_changed(mw->cbs.ud, scale);
    }
}

void main_window_mouse_press(MainWindow *mw, int x, int y, int button)
{
    if (button == SDL_BUTTON_RIGHT) {
        show_context_menu(mw);
        return;
    }

    int lx, ly;
    kwin_logical(&mw->kw, x, y, &lx, &ly);

    /* Title bar buttons first */
    if (krect_contains(CLOSE_BTN, lx, ly)) {
        if (mw->cbs.quit)
            mw->cbs.quit(mw->cbs.ud);
        return;
    }
    if (krect_contains(MINIMIZE_BTN, lx, ly)) {
        SDL_MinimizeWindow(mw->kw.win);
        return;
    }

    /* Clickable display areas */
    if (krect_contains(TIME_DISPLAY, lx, ly)) {
        lcd_toggle_mode(&mw->lcd);
        return;
    }
    if (krect_contains(VIS_AREA, lx, ly)) {
        spectrum_cycle_mode(&mw->spectrum);
        return;
    }

    /* Interactive widgets */
    HitKind hit = hitmap_mouse_press(&mw->hitmap, lx, ly, NULL);
    if (hit != HIT_NONE) {
        if (hit == HIT_SLIDER && mw->hitmap.captured == &mw->pos_slider)
            mw->seeking = true;
        return;
    }

    /* Title bar drag (last - only if nothing else was hit) */
    if (krect_contains(TITLE_BAR, lx, ly)) {
        kwin_begin_drag(&mw->kw);
        if (mw->cbs.drag_started)
            mw->cbs.drag_started(mw->cbs.ud);
    }
}

void main_window_mouse_move(MainWindow *mw, int x, int y)
{
    if (mw->kw.dragging) {
        if (mw->cbs.drag_moved) {
            int nx, ny;
            kwin_drag_pos(&mw->kw, &nx, &ny);
            mw->cbs.drag_moved(mw->cbs.ud, nx, ny);
        }
    } else if (hitmap_is_captured(&mw->hitmap)) {
        int lx, ly;
        kwin_logical(&mw->kw, x, y, &lx, &ly);
        hitmap_mouse_move(&mw->hitmap, lx, ly);
    }
}

void main_window_mouse_release(MainWindow *mw, int x, int y)
{
    if (mw->kw.dragging) {
        mw->kw.dragging = false;
        SDL_CaptureMouse(SDL_FALSE);
        if (mw->cbs.drag_ended)
            mw->cbs.drag_ended(mw->cbs.ud);
    } else {
        int lx, ly;
        kwin_logical(&mw->kw, x, y, &lx, &ly);
        mw->seeking = false;
        hitmap_mouse_release(&mw->hitmap, lx, ly);
    }
}

void main_window_destroy(MainWindow *mw)
{
    kwin_destroy(&mw->kw);
}
