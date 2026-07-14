/* kilix-amp - a Winamp 2.x clone for Linux, in C11 + SDL2.
 * C rewrite of nixamp; this file ports nixamp.py (arg parsing, component
 * wiring, hotkeys, config persistence, main loop).
 *
 * Usage: kilix-amp [-h] [--skin PATH] [--double-size] [FILE ...]
 */

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio.h"
#include "common.h"
#include "config.h"
#include "consts.h"
#include "dock.h"
#include "filedialog.h"
#include "playlist.h"
#include "skin.h"
#include "skin_default.h"
#include "win_editor.h"
#include "win_eq.h"
#include "win_main.h"
#include "win_playlist.h"

typedef struct {
    Config *config;
    AudioEngine *audio;
    Playlist *playlist;
    Skin skin;
    MainWindow main_win;
    EQWindow eq_win;
    PlaylistWindow pl_win;
    EditorWindow ed_win;
    DockManager dock;
    bool running;
    /* Guards against an all-bad playlist looping forever under
     * REPEAT_ALL. */
    int auto_skip_count;
    bool pending_play;
    int pending_play_index;
} App;

/* --- Core playback actions --- */

static void set_loading_title(App *app, Track *track)
{
    char *disp = track_display_title(track);
    char *title = ka_asprintf("LOADING: %s", disp);
    main_window_set_title(&app->main_win, title);
    free(title);
    free(disp);
}

static void show_playlist(App *app)
{
    if (!app->pl_win.kw.visible)
        kwin_show(&app->pl_win.kw);
    int px, py, pw, ph;
    kwin_get_pos(&app->pl_win.kw, &px, &py);
    kwin_get_size(&app->pl_win.kw, &pw, &ph);
    SDL_Rect pl_rect = {px, py, pw, ph};
    bool on_screen = false;
    int displays = SDL_GetNumVideoDisplays();
    for (int i = 0; i < displays && !on_screen; i++) {
        SDL_Rect geo;
        if (SDL_GetDisplayUsableBounds(i, &geo) == 0)
            on_screen = SDL_HasIntersection(&pl_rect, &geo);
    }
    if (!on_screen) {
        SDL_Rect geo;
        if (SDL_GetDisplayUsableBounds(0, &geo) == 0) {
            int mx, my, mw, mh;
            kwin_get_pos(&app->main_win.kw, &mx, &my);
            kwin_get_size(&app->main_win.kw, &mw, &mh);
            int y = my + mh;
            if (y + ph > geo.y + geo.h)
                y = geo.y + 20;
            kwin_set_pos(&app->pl_win.kw, mx, y);
        }
    }
    config_set_pl_visible(app->config, true);
    main_window_set_pl_visible(&app->main_win, true);
}

static void render_visible_windows(App *app)
{
    if (app->main_win.kw.visible)
        main_window_render(&app->main_win);
    if (app->eq_win.kw.visible)
        eq_window_render(&app->eq_win);
    if (app->pl_win.kw.visible)
        playlist_window_render(&app->pl_win);
    if (app->ed_win.kw.visible)
        editor_window_render(&app->ed_win);
}

static void queue_current(App *app)
{
    Track *track = playlist_current_track(app->playlist);
    if (!track && playlist_count(app->playlist) > 0)
        track = playlist_set_current(app->playlist, 0);
    if (track) {
        app->pending_play_index = playlist_current_index(app->playlist);
        app->pending_play = true;
        set_loading_title(app, track);
    }
}

static void queue_track(App *app, Track *track)
{
    if (!track)
        return;
    app->pending_play_index = playlist_current_index(app->playlist);
    app->pending_play = true;
    set_loading_title(app, track);
}

static void play_pending(App *app)
{
    if (!app->pending_play)
        return;
    app->pending_play = false;

    Track *track = playlist_set_current(app->playlist, app->pending_play_index);
    if (!track)
        return;
    audio_load(app->audio, track->filepath);
    audio_play(app->audio);
    char *disp = track_display_title(track);
    main_window_set_title(&app->main_win, disp);
    free(disp);
}

static void on_play(void *ud)
{
    App *app = ud;
    if (strcmp(audio_state(app->audio), "paused") == 0)
        audio_pause(app->audio); /* unpause */
    else
        queue_current(app);
}

static void on_pause(void *ud)
{
    App *app = ud;
    audio_pause(app->audio);
}

static void on_stop(void *ud)
{
    App *app = ud;
    audio_stop(app->audio);
    main_window_set_play_state(&app->main_win, "stopped");
}

static void on_prev(void *ud)
{
    App *app = ud;
    queue_track(app, playlist_prev_track(app->playlist));
}

static void on_next(void *ud)
{
    App *app = ud;
    queue_track(app, playlist_next_track(app->playlist));
}

static void on_eject(void *ud)
{
    App *app = ud;
    int count = 0;
    char **files = filedialog_open_files("Open Audio Files", &count);
    if (!files)
        return;
    int before = playlist_count(app->playlist);
    playlist_add_files(app->playlist, (const char **)files, count);
    int added = playlist_count(app->playlist) - before;
    if (playlist_current_index(app->playlist) < 0 && added > 0)
        playlist_set_current(app->playlist, before);
    if (added > 0)
        show_playlist(app);
    queue_current(app);
    for (int i = 0; i < count; i++)
        free(files[i]);
    free(files);
}

static void on_volume(void *ud, int v)
{
    App *app = ud;
    audio_set_volume(app->audio, v);
    config_set_volume(app->config, v);
    main_window_set_volume(&app->main_win, v);
}

static void on_balance(void *ud, int v)
{
    App *app = ud;
    audio_set_balance(app->audio, v);
    config_set_balance(app->config, v);
}

static void on_seek(void *ud, int pct)
{
    App *app = ud;
    int dur = audio_get_duration_ms(app->audio);
    if (dur > 0)
        audio_seek(app->audio, (int)((int64_t)pct * dur / 100));
}

static void on_shuffle(void *ud, bool v)
{
    App *app = ud;
    playlist_set_shuffle(app->playlist, v);
    config_set_shuffle(app->config, v);
}

static void on_toggle_repeat(void *ud, bool v)
{
    (void)v;
    App *app = ud;
    playlist_toggle_repeat(app->playlist);
    config_set_repeat(app->config, playlist_repeat(app->playlist));
    main_window_set_repeat(&app->main_win,
                           playlist_repeat(app->playlist) != REPEAT_OFF);
}

/* --- Audio engine callbacks --- */

static void on_eos(void *ud)
{
    App *app = ud;
    Track *track = playlist_next_track(app->playlist);
    if (track)
        queue_track(app, track);
    else
        main_window_set_play_state(&app->main_win, "stopped");
}

static void on_error(void *ud, const char *msg)
{
    App *app = ud;
    char buf[512];
    snprintf(buf, sizeof(buf), "ERROR: %s", msg);
    main_window_set_title(&app->main_win, buf);
    main_window_set_play_state(&app->main_win, "stopped");
    /* Stop auto-skipping once we've failed on every track. */
    if (app->auto_skip_count >= playlist_count(app->playlist)) {
        app->auto_skip_count = 0;
        return;
    }
    app->auto_skip_count++;
    Track *track = playlist_next_track(app->playlist);
    if (track)
        queue_track(app, track);
    else
        app->auto_skip_count = 0;
}

static void on_state_changed(void *ud, const char *state)
{
    App *app = ud;
    if (strcmp(state, "playing") == 0)
        app->auto_skip_count = 0;
    main_window_set_play_state(&app->main_win, state);
}

static void on_position(void *ud, int pos_ms, int dur_ms)
{
    App *app = ud;
    main_window_set_position(&app->main_win, pos_ms, dur_ms);
}

static void on_tags(void *ud, const AudioTags *tags)
{
    App *app = ud;
    Track *track = playlist_current_track(app->playlist);
    if (!track)
        return;
    if (tags->title[0]) {
        free(track->title);
        track->title = ka_strdup(tags->title);
    }
    if (tags->artist[0]) {
        free(track->artist);
        track->artist = ka_strdup(tags->artist);
    }
    char *disp = track_display_title(track);
    main_window_set_title(&app->main_win, disp);
    free(disp);

    track->bitrate = tags->bitrate / 1000;
    main_window_set_track_info(&app->main_win, track->bitrate,
                               tags->sample_rate,
                               tags->channels == 1 ? 1 : 2);
}

static void on_spectrum(void *ud, const float *mags, int n)
{
    App *app = ud;
    main_window_set_spectrum_data(&app->main_win, mags, n);
}

/* --- EQ window callbacks --- */

static void on_eq_band(void *ud, int band, double gain)
{
    App *app = ud;
    audio_set_eq_band(app->audio, band, gain);
    float bands[10];
    config_get_eq_bands(app->config, bands);
    bands[band] = (float)gain;
    config_set_eq_bands(app->config, bands);
}

static void on_eq_enabled(void *ud, bool enabled)
{
    App *app = ud;
    audio_set_eq_enabled(app->audio, enabled);
    config_set_eq_enabled(app->config, enabled);
}

static void on_preamp(void *ud, double v)
{
    App *app = ud;
    audio_set_preamp(app->audio, v);
    config_set_eq_preamp(app->config, v);
}

static void on_preset(void *ud, const char *name)
{
    App *app = ud;
    for (int i = 0; i < EQ_PRESET_COUNT; i++) {
        if (strcmp(EQ_PRESETS[i].name, name) == 0) {
            eq_window_set_bands(&app->eq_win, EQ_PRESETS[i].gains);
            for (int b = 0; b < 10; b++)
                audio_set_eq_band(app->audio, b, EQ_PRESETS[i].gains[b]);
            config_set_eq_bands(app->config, EQ_PRESETS[i].gains);
            return;
        }
    }
}

/* --- Window visibility toggles --- */

static void on_eq_toggle(void *ud, bool visible)
{
    App *app = ud;
    kwin_set_visible(&app->eq_win.kw, visible);
    config_set_eq_visible(app->config, visible);
    main_window_set_eq_visible(&app->main_win, visible);
}

static void on_pl_toggle(void *ud, bool visible)
{
    App *app = ud;
    kwin_set_visible(&app->pl_win.kw, visible);
    config_set_pl_visible(app->config, visible);
    main_window_set_pl_visible(&app->main_win, visible);
}

static void on_ed_toggle(App *app, bool visible)
{
    kwin_set_visible(&app->ed_win.kw, visible);
    config_set_ed_visible(app->config, visible);
}

static void on_scale(void *ud, float scale)
{
    App *app = ud;
    /* main_win already scaled itself; mirror across the other windows. */
    eq_window_set_scale(&app->eq_win, scale);
    playlist_window_set_scale(&app->pl_win, scale);
    editor_window_set_scale(&app->ed_win, scale);
    config_set_scale(app->config, scale);
}

static void on_quit_requested(void *ud)
{
    App *app = ud;
    app->running = false;
}

/* --- Playlist window callbacks --- */

static void on_track_activated(void *ud, int idx)
{
    App *app = ud;
    queue_track(app, playlist_set_current(app->playlist, idx));
}

static void on_add_files(void *ud)
{
    App *app = ud;
    int count = 0;
    char **files = filedialog_open_files("Add Audio Files", &count);
    if (!files)
        return;
    playlist_add_files(app->playlist, (const char **)files, count);
    for (int i = 0; i < count; i++)
        free(files[i]);
    free(files);
}

static void on_add_dir(void *ud)
{
    App *app = ud;
    char *dir = filedialog_choose_dir("Add Directory");
    if (dir) {
        playlist_add_directory(app->playlist, dir, true);
        free(dir);
    }
}

static void on_edit_in_editor(void *ud, const char *filepath)
{
    App *app = ud;
    editor_window_load_file(&app->ed_win, filepath);
    if (!app->ed_win.kw.visible) {
        kwin_show(&app->ed_win.kw);
        config_set_ed_visible(app->config, true);
    }
}

/* --- Docking glue --- */

#define DEF_DRAG(win_field, name)                                           \
    static void drag_start_##name(void *ud)                                 \
    {                                                                       \
        App *app = ud;                                                      \
        dock_start_drag(&app->dock, #name);                                 \
    }                                                                       \
    static void drag_move_##name(void *ud, int x, int y)                    \
    {                                                                       \
        App *app = ud;                                                      \
        dock_drag_move(&app->dock, #name, x, y);                            \
    }                                                                       \
    static void drag_end_##name(void *ud)                                   \
    {                                                                       \
        App *app = ud;                                                      \
        dock_end_drag(&app->dock);                                          \
    }

DEF_DRAG(main_win, main)
DEF_DRAG(eq_win, eq)
DEF_DRAG(pl_win, playlist)
DEF_DRAG(ed_win, editor)

/* --- Playlist model callbacks --- */

static void on_playlist_changed(void *ud)
{
    App *app = ud;
    playlist_window_on_playlist_changed(&app->pl_win);
}

static void on_current_changed(void *ud, int idx)
{
    App *app = ud;
    playlist_window_on_current_changed(&app->pl_win, idx);
}

/* --- Hotkeys --- */

/* D hotkey: cycle through the SIZE menu presets. */
static void cycle_scale(App *app)
{
    float cur = kwin_scale(&app->main_win.kw);
    /* Find the nearest preset, then advance to the next one. */
    int nearest = 0;
    for (int i = 1; i < SCALE_PRESET_COUNT; i++)
        if (fabsf(SCALE_PRESETS[i] - cur) <
            fabsf(SCALE_PRESETS[nearest] - cur))
            nearest = i;
    float scale = SCALE_PRESETS[(nearest + 1) % SCALE_PRESET_COUNT];
    main_window_set_scale(&app->main_win, scale);
    on_scale(app, scale);
}

static void handle_hotkey(App *app, SDL_Keycode key, uint16_t mod)
{
    bool alt = mod & KMOD_ALT;
    bool ctrl = mod & KMOD_CTRL;

    if (alt) {
        switch (key) {
        case SDLK_g:
            on_eq_toggle(app, !app->eq_win.kw.visible);
            break;
        case SDLK_e:
            on_pl_toggle(app, !app->pl_win.kw.visible);
            break;
        case SDLK_d:
            on_ed_toggle(app, !app->ed_win.kw.visible);
            break;
        default:
            break;
        }
        return;
    }
    if (ctrl)
        return;

    switch (key) {
    case SDLK_z:
        on_prev(app);
        break;
    case SDLK_x:
        on_play(app);
        break;
    case SDLK_c:
        on_pause(app);
        break;
    case SDLK_v:
        on_stop(app);
        break;
    case SDLK_b:
        on_next(app);
        break;
    case SDLK_l:
        on_eject(app);
        break;
    case SDLK_LEFT:
        audio_seek(app->audio,
                   KA_MAX(0, audio_get_position_ms(app->audio) - 5000));
        break;
    case SDLK_RIGHT:
        audio_seek(app->audio, audio_get_position_ms(app->audio) + 5000);
        break;
    case SDLK_UP:
        on_volume(app, KA_MIN(100, app->main_win.volume_slider.value + 5));
        break;
    case SDLK_DOWN:
        on_volume(app, KA_MAX(0, app->main_win.volume_slider.value - 5));
        break;
    case SDLK_s: {
        bool v = !playlist_shuffle(app->playlist);
        on_shuffle(app, v);
        main_window_set_shuffle(&app->main_win, v);
        break;
    }
    case SDLK_r:
        on_toggle_repeat(app, false);
        break;
    case SDLK_d:
        cycle_scale(app);
        break;
    default:
        break;
    }
}

/* --- Event dispatch --- */

static void dispatch_event(App *app, const SDL_Event *ev)
{
    uint32_t main_id = kwin_id(&app->main_win.kw);
    uint32_t eq_id = kwin_id(&app->eq_win.kw);
    uint32_t pl_id = kwin_id(&app->pl_win.kw);
    uint32_t ed_id = kwin_id(&app->ed_win.kw);

    switch (ev->type) {
    case SDL_QUIT:
        app->running = false;
        break;

    case SDL_MOUSEBUTTONDOWN: {
        uint32_t wid = ev->button.windowID;
        uint16_t mod = SDL_GetModState();
        if (wid == main_id)
            main_window_mouse_press(&app->main_win, ev->button.x,
                                    ev->button.y, ev->button.button);
        else if (wid == eq_id)
            eq_window_mouse_press(&app->eq_win, ev->button.x, ev->button.y,
                                  ev->button.button);
        else if (wid == pl_id)
            playlist_window_mouse_press(&app->pl_win, ev->button.x,
                                        ev->button.y, ev->button.button,
                                        ev->button.clicks, mod);
        else if (wid == ed_id)
            editor_window_mouse_press(&app->ed_win, ev->button.x,
                                      ev->button.y, ev->button.button, mod);
        break;
    }

    case SDL_MOUSEMOTION: {
        uint32_t wid = ev->motion.windowID;
        if (wid == main_id || app->main_win.kw.dragging)
            main_window_mouse_move(&app->main_win, ev->motion.x,
                                   ev->motion.y);
        if (wid == eq_id || app->eq_win.kw.dragging)
            eq_window_mouse_move(&app->eq_win, ev->motion.x, ev->motion.y);
        if (wid == pl_id || app->pl_win.kw.dragging || app->pl_win.resizing)
            playlist_window_mouse_move(&app->pl_win, ev->motion.x,
                                       ev->motion.y);
        if (wid == ed_id || app->ed_win.kw.dragging || app->ed_win.resizing)
            editor_window_mouse_move(&app->ed_win, ev->motion.x,
                                     ev->motion.y);
        break;
    }

    case SDL_MOUSEBUTTONUP: {
        uint32_t wid = ev->button.windowID;
        if (wid == main_id || app->main_win.kw.dragging ||
            hitmap_is_captured(&app->main_win.hitmap))
            main_window_mouse_release(&app->main_win, ev->button.x,
                                      ev->button.y);
        if (wid == eq_id || app->eq_win.kw.dragging ||
            hitmap_is_captured(&app->eq_win.hitmap))
            eq_window_mouse_release(&app->eq_win, ev->button.x,
                                    ev->button.y);
        if (wid == pl_id || app->pl_win.kw.dragging || app->pl_win.resizing)
            playlist_window_mouse_release(&app->pl_win, ev->button.x,
                                          ev->button.y);
        if (wid == ed_id || app->ed_win.kw.dragging ||
            app->ed_win.resizing || app->ed_win.selecting)
            editor_window_mouse_release(&app->ed_win, ev->button.x,
                                        ev->button.y);
        break;
    }

    case SDL_MOUSEWHEEL: {
        uint32_t wid = ev->wheel.windowID;
        if (wid == pl_id)
            playlist_window_wheel(&app->pl_win, ev->wheel.y);
        else if (wid == ed_id)
            editor_window_wheel(&app->ed_win, ev->wheel.y,
                                SDL_GetModState());
        break;
    }

    case SDL_KEYDOWN: {
        uint32_t wid = ev->key.windowID;
        uint16_t mod = SDL_GetModState();
        if (wid == ed_id && (mod & KMOD_CTRL)) {
            editor_window_key(&app->ed_win, ev->key.keysym.sym, mod);
            break;
        }
        handle_hotkey(app, ev->key.keysym.sym, mod);
        break;
    }

    case SDL_DROPFILE: {
        uint32_t wid = ev->drop.windowID;
        if (ev->drop.file) {
            if (wid == ed_id)
                editor_window_drop(&app->ed_win, ev->drop.file);
            else
                playlist_window_drop(&app->pl_win, ev->drop.file);
            SDL_free(ev->drop.file);
        }
        break;
    }

    case SDL_WINDOWEVENT: {
        uint32_t wid = ev->window.windowID;
        KWindow *kw = NULL;
        if (wid == main_id)
            kw = &app->main_win.kw;
        else if (wid == eq_id)
            kw = &app->eq_win.kw;
        else if (wid == pl_id)
            kw = &app->pl_win.kw;
        else if (wid == ed_id)
            kw = &app->ed_win.kw;
        if (!kw)
            break;
        switch (ev->window.event) {
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            kw->active = true;
            break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            kw->active = false;
            break;
        case SDL_WINDOWEVENT_CLOSE:
            if (wid == main_id)
                app->running = false;
            else
                kwin_hide(kw);
            break;
        case SDL_WINDOWEVENT_SIZE_CHANGED: {
            float sc = kwin_scale(kw);
            int lw = (int)lroundf(ev->window.data1 / sc);
            int lh = (int)lroundf(ev->window.data2 / sc);
            if (wid == pl_id)
                playlist_window_resize(&app->pl_win, lw, lh);
            else if (wid == ed_id)
                editor_window_resize(&app->ed_win, lw, lh);
            break;
        }
        default:
            break;
        }
        break;
    }

    default:
        break;
    }
}

/* --- Startup --- */

static char *default_skin_dir(void)
{
    const char *xdg = getenv("XDG_DATA_HOME");
    if (xdg && *xdg)
        return ka_asprintf("%s/kilix-amp/skins/default", xdg);
    const char *home = getenv("HOME");
    if (!home)
        home = ".";
    return ka_asprintf("%s/.local/share/kilix-amp/skins/default", home);
}

static void save_state(App *app)
{
    int x, y;
    kwin_get_pos(&app->main_win.kw, &x, &y);
    config_set_window_pos(app->config, "main", x, y);
    kwin_get_pos(&app->eq_win.kw, &x, &y);
    config_set_window_pos(app->config, "eq", x, y);
    kwin_get_pos(&app->pl_win.kw, &x, &y);
    config_set_window_pos(app->config, "playlist", x, y);
    config_set_pl_width(app->config, app->pl_win.pl_w);
    config_set_pl_height(app->config, app->pl_win.pl_h);
    kwin_get_pos(&app->ed_win.kw, &x, &y);
    config_set_window_pos(app->config, "editor", x, y);
    config_set_ed_width(app->config, app->ed_win.ed_w);
    config_set_ed_height(app->config, app->ed_win.ed_h);
    /* Re-derive visibility from the live windows so closing one via its
     * own close button does not persist a stale true. */
    config_set_eq_visible(app->config, app->eq_win.kw.visible);
    config_set_pl_visible(app->config, app->pl_win.kw.visible);
    config_set_ed_visible(app->config, app->ed_win.kw.visible);
    config_sync(app->config);
}

int main(int argc, char **argv)
{
    const char *skin_arg = "";
    float cli_scale = 0.0f; /* 0 = not given, use config */
    const char *files[1024];
    int n_files = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: kilix-amp [-h] [--skin PATH] [--scale N] "
                   "[--double-size] [FILE ...]\n"
                   "  --scale N      UI scale factor (%g-%g, e.g. 1.5)\n"
                   "  --double-size  same as --scale 2\n",
                   (double)SCALE_MIN, (double)SCALE_MAX);
            return 0;
        } else if (strcmp(argv[i], "--skin") == 0 && i + 1 < argc) {
            skin_arg = argv[++i];
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            /* KA_CLAMP is a macro: keep the ++i out of its arguments */
            float v = (float)atof(argv[++i]);
            cli_scale = KA_CLAMP(v, SCALE_MIN, SCALE_MAX);
        } else if (strcmp(argv[i], "--double-size") == 0) {
            cli_scale = 2.0f;
        } else if (n_files < (int)KA_LEN(files)) {
            files[n_files++] = argv[i];
        }
    }

    srand((unsigned)time(NULL));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "kilix-amp: SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Generate default skin if needed */
    char *skin_dir = default_skin_dir();
    char *probe = ka_path_join(skin_dir, "main.bmp");
    if (!ka_is_file(probe)) {
        printf("Generating default skin...\n");
        if (skin_default_generate(skin_dir))
            printf("Default skin generated at %s\n", skin_dir);
        else
            fprintf(stderr, "Warning: could not generate default skin\n");
    }
    free(probe);

    App *app = calloc(1, sizeof(*app));
    if (!app)
        abort();
    app->running = true;
    app->config = config_open(NULL);

    /* Load skin: --skin arg, then config, then default */
    skin_init(&app->skin);
    const char *skin_path = *skin_arg ? skin_arg
                                      : config_skin_path(app->config);
    if (!*skin_path)
        skin_path = skin_dir;
    if (!skin_load(&app->skin, skin_path)) {
        fprintf(stderr,
                "Warning: Could not load skin from %s, using default\n",
                skin_path);
        skin_load(&app->skin, skin_dir);
    }

    app->audio = audio_new();
    app->playlist = playlist_new();
    playlist_set_shuffle(app->playlist, config_shuffle(app->config));
    playlist_set_repeat(app->playlist, config_repeat(app->config));

    /* Create windows */
    main_window_init(&app->main_win, &app->skin);
    eq_window_init(&app->eq_win, &app->skin);
    playlist_window_init(&app->pl_win, &app->skin, app->playlist);
    editor_window_init(&app->ed_win, &app->skin);

    /* Restore saved window sizes (clamped to minimums) */
    playlist_window_resize(&app->pl_win,
                           KA_MAX(PL_MIN_W, config_pl_width(app->config)),
                           KA_MAX(PL_MIN_H, config_pl_height(app->config)));
    SDL_SetWindowSize(app->pl_win.kw.win, app->pl_win.pl_w,
                      app->pl_win.pl_h);
    editor_window_resize(&app->ed_win,
                         KA_MAX(ED_MIN_W, config_ed_width(app->config)),
                         KA_MAX(ED_MIN_H, config_ed_height(app->config)));
    SDL_SetWindowSize(app->ed_win.kw.win, app->ed_win.ed_w,
                      app->ed_win.ed_h);

    /* Apply config */
    main_window_set_volume(&app->main_win, config_volume(app->config));
    main_window_set_balance(&app->main_win, config_balance(app->config));
    main_window_set_shuffle(&app->main_win, config_shuffle(app->config));
    main_window_set_repeat(&app->main_win,
                           config_repeat(app->config) > 0);
    audio_set_volume(app->audio, config_volume(app->config));

    if (config_eq_enabled(app->config)) {
        eq_window_set_eq_enabled(&app->eq_win, true);
        audio_set_eq_enabled(app->audio, true);
        float bands[10];
        config_get_eq_bands(app->config, bands);
        eq_window_set_bands(&app->eq_win, bands);
        for (int i = 0; i < 10; i++)
            audio_set_eq_band(app->audio, i, bands[i]);
    }
    eq_window_set_preamp(&app->eq_win, config_eq_preamp(app->config));
    audio_set_preamp(app->audio, config_eq_preamp(app->config));

    float ui_scale =
        cli_scale > 0.0f
            ? cli_scale
            : (float)KA_CLAMP(config_scale(app->config), SCALE_MIN,
                              SCALE_MAX);
    if (ui_scale != 1.0f) {
        main_window_set_scale(&app->main_win, ui_scale);
        on_scale(app, ui_scale);
    }

    /* --- Wire callbacks --- */
    app->main_win.cbs = (MainWindowCallbacks){
        .prev = on_prev,
        .play = on_play,
        .pause = on_pause,
        .stop = on_stop,
        .next = on_next,
        .eject = on_eject,
        .volume_changed = on_volume,
        .balance_changed = on_balance,
        .seek_requested = on_seek,
        .shuffle_toggled = on_shuffle,
        .repeat_toggled = on_toggle_repeat,
        .eq_toggled = on_eq_toggle,
        .pl_toggled = on_pl_toggle,
        .scale_changed = on_scale,
        .drag_started = drag_start_main,
        .drag_moved = drag_move_main,
        .drag_ended = drag_end_main,
        .quit = on_quit_requested,
        .ud = app,
    };

    audio_set_callbacks(app->audio, &(AudioCallbacks){
        .state_changed = on_state_changed,
        .position_changed = on_position,
        .tag_found = on_tags,
        .spectrum = on_spectrum,
        .eos = on_eos,
        .error = on_error,
        .ud = app,
    });

    app->eq_win.cbs = (EQWindowCallbacks){
        .eq_changed = on_eq_band,
        .preamp_changed = on_preamp,
        .eq_enabled_changed = on_eq_enabled,
        .preset_selected = on_preset,
        .drag_started = drag_start_eq,
        .drag_moved = drag_move_eq,
        .drag_ended = drag_end_eq,
        .ud = app,
    };

    app->pl_win.cbs = (PlaylistWindowCallbacks){
        .track_activated = on_track_activated,
        .add_files_requested = on_add_files,
        .add_dir_requested = on_add_dir,
        .edit_in_editor = on_edit_in_editor,
        .drag_started = drag_start_playlist,
        .drag_moved = drag_move_playlist,
        .drag_ended = drag_end_playlist,
        .ud = app,
    };

    app->ed_win.cbs = (EditorWindowCallbacks){
        .drag_started = drag_start_editor,
        .drag_moved = drag_move_editor,
        .drag_ended = drag_end_editor,
        .ud = app,
    };

    playlist_set_callbacks(app->playlist, on_playlist_changed,
                           on_current_changed, app);

    /* --- Docking --- */
    dock_init(&app->dock);
    dock_register(&app->dock, "main", &app->main_win.kw);
    dock_register(&app->dock, "eq", &app->eq_win.kw);
    dock_register(&app->dock, "playlist", &app->pl_win.kw);
    dock_register(&app->dock, "editor", &app->ed_win.kw);

    /* --- Position windows --- */
    int x, y;
    bool have_main_pos = config_get_window_pos(app->config, "main", &x, &y);
    if (have_main_pos)
        kwin_set_pos(&app->main_win.kw, x, y);
    if (config_get_window_pos(app->config, "eq", &x, &y))
        kwin_set_pos(&app->eq_win.kw, x, y);
    if (config_get_window_pos(app->config, "playlist", &x, &y))
        kwin_set_pos(&app->pl_win.kw, x, y);
    if (config_get_window_pos(app->config, "editor", &x, &y))
        kwin_set_pos(&app->ed_win.kw, x, y);

    /* Show windows */
    kwin_show(&app->main_win.kw);
    if (config_eq_visible(app->config)) {
        kwin_show(&app->eq_win.kw);
        main_window_set_eq_visible(&app->main_win, true);
    }
    if (config_pl_visible(app->config)) {
        kwin_show(&app->pl_win.kw);
        main_window_set_pl_visible(&app->main_win, true);
    }
    if (config_ed_visible(app->config))
        kwin_show(&app->ed_win.kw);

    if (!have_main_pos)
        dock_position_default(&app->dock);

    /* --- Load files from command line --- */
    for (int i = 0; i < n_files; i++) {
        if (ka_is_dir(files[i]))
            playlist_add_directory(app->playlist, files[i], true);
        else if (ka_is_file(files[i]))
            playlist_add_file(app->playlist, files[i]);
    }
    if (n_files > 0 && playlist_count(app->playlist) > 0) {
        show_playlist(app);
        if (playlist_current_index(app->playlist) < 0)
            playlist_set_current(app->playlist, 0);
        queue_current(app);
    }

    /* --- Main loop --- */
    /* KILIXAMP_EXIT_AFTER_MS: quit cleanly after N ms (headless CI/smoke
     * runs, where no user can close the window). */
    uint32_t exit_after = 0;
    const char *exit_env = getenv("KILIXAMP_EXIT_AFTER_MS");
    if (exit_env)
        exit_after = (uint32_t)strtoul(exit_env, NULL, 10);
    uint32_t start_ticks = SDL_GetTicks();

    uint32_t last_scroll = 0, last_render = 0;
    while (app->running) {
        if (exit_after && SDL_GetTicks() - start_ticks >= exit_after)
            app->running = false;
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            dispatch_event(app, &ev);

        audio_poll(app->audio);

        uint32_t now = SDL_GetTicks();
        if (now - last_scroll >= 1000 / SCROLL_FPS) {
            last_scroll = now;
            main_window_scroll_tick(&app->main_win);
        }
        if (now - last_render >= 33) { /* ~30fps repaint */
            last_render = now;
            render_visible_windows(app);
        }
        if (app->pending_play) {
            render_visible_windows(app);
            last_render = SDL_GetTicks();
            play_pending(app);
        }
        SDL_Delay(5);
    }

    /* --- Save config on exit --- */
    save_state(app);

    audio_cleanup(app->audio);
    main_window_destroy(&app->main_win);
    eq_window_destroy(&app->eq_win);
    playlist_window_destroy(&app->pl_win);
    editor_window_destroy(&app->ed_win);
    playlist_free(app->playlist);
    skin_destroy(&app->skin);
    config_close(app->config);
    free(skin_dir);
    free(app);
    SDL_Quit();
    return 0;
}
