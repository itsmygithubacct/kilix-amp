#include "win_editor.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "consts.h"
#include "dialog.h"
#include "effects.h"
#include "filedialog.h"
#include "font4x6.h"
#include "menu.h"
#include "widgets.h"

/* Toolbar buttons: (label, action); "|" = separator. */
static const struct {
    const char *label;
    const char *action;
} TOOLBAR[] = {
    {"OPEN", "open"},   {"SAVE", "save"},   {"|", NULL},
    {"UNDO", "undo"},   {"REDO", "redo"},   {"|", NULL},
    {"Z+", "zoom_in"},  {"Z-", "zoom_out"}, {"FIT", "fit"},
    {"SEL", "zoom_sel"}, {"|", NULL},       {"EFFECTS", "effects"},
};

#define TB_BG     ((KColor){40, 40, 45})
#define TB_BTN    ((KColor){50, 50, 58})
#define TB_HOVER  ((KColor){70, 70, 80})
#define TB_BORDER ((KColor){80, 80, 90})
#define TB_DIM    ((KColor){80, 80, 80})
#define INFO_BG   ((KColor){30, 30, 35})

static void on_data_changed(void *ud);

void editor_window_init(EditorWindow *ew, Skin *skin)
{
    memset(ew, 0, sizeof(*ew));
    ew->skin = skin;
    ew->ed_w = ED_MIN_W;
    ew->ed_h = ED_DEFAULT_H;
    kwin_create(&ew->kw, "kilix-amp editor", ED_MIN_W, ED_DEFAULT_H, true);
    SDL_SetWindowMinimumSize(ew->kw.win, ED_MIN_W, ED_MIN_H);
    kbuf_resize(&ew->kw.buf, ew->ed_w, ew->ed_h);
    ew->kw.logical_w = ew->ed_w;
    ew->kw.logical_h = ew->ed_h;

    audio_data_init(&ew->audio_data);
    ew->audio_data.data_changed = on_data_changed;
    ew->audio_data.ud = ew;
    waveform_cache_init(&ew->cache);
}

void editor_window_destroy(EditorWindow *ew)
{
    waveform_cache_destroy(&ew->cache);
    audio_data_destroy(&ew->audio_data);
    kwin_destroy(&ew->kw);
}

static void set_status(EditorWindow *ew, const char *msg)
{
    snprintf(ew->status_msg, sizeof(ew->status_msg), "%s", msg);
}

bool editor_window_load_file(EditorWindow *ew, const char *filepath)
{
    if (ad_load(&ew->audio_data, filepath)) {
        ew->view_start = 0;
        ew->view_end = ad_num_samples(&ew->audio_data);
        waveform_precompute_blocks(&ew->cache, &ew->audio_data.samples);
        waveform_invalidate(&ew->cache);
        ew->status_msg[0] = '\0';
        return true;
    }
    char msg[256];
    snprintf(msg, sizeof(msg), "FAILED TO LOAD: %s", ka_basename(filepath));
    set_status(ew, msg);
    return false;
}

static void on_data_changed(void *ud)
{
    EditorWindow *ew = ud;
    waveform_precompute_blocks(&ew->cache, &ew->audio_data.samples);
    waveform_invalidate(&ew->cache);
    int n = ad_num_samples(&ew->audio_data);
    if (ew->view_end > n)
        ew->view_end = n;
    if (ew->view_start > ew->view_end)
        ew->view_start = 0;
}

/* --- Geometry helpers --- */

static KRect toolbar_rect(const EditorWindow *ew)
{
    return KRECT(0, ED_TITLE_H, ew->ed_w, ED_TOOLBAR_H);
}

static KRect ruler_rect(const EditorWindow *ew)
{
    return KRECT(0, ED_TITLE_H + ED_TOOLBAR_H, ew->ed_w, ED_RULER_H);
}

static KRect waveform_rect(const EditorWindow *ew)
{
    int top = ED_TITLE_H + ED_TOOLBAR_H + ED_RULER_H;
    int bottom = ew->ed_h - ED_INFO_BAR_H - 4;
    return KRECT(0, top, ew->ed_w, KA_MAX(1, bottom - top));
}

static KRect info_rect(const EditorWindow *ew)
{
    return KRECT(0, ew->ed_h - ED_INFO_BAR_H - 4, ew->ed_w, ED_INFO_BAR_H);
}

static KRect resize_handle_rect(const EditorWindow *ew)
{
    return KRECT(ew->ed_w - 20, ew->ed_h - 20, 20, 20);
}

/* Compute toolbar button rects; returns count. */
static int button_rects(const EditorWindow *ew, KRect *rects,
                        const char **labels, const char **actions)
{
    (void)ew;
    int n = 0;
    int x = 4;
    int y = ED_TITLE_H + 2;
    int h = ED_TOOLBAR_H - 4;
    for (size_t i = 0; i < KA_LEN(TOOLBAR); i++) {
        if (strcmp(TOOLBAR[i].label, "|") == 0) {
            x += 4;
            rects[n] = KRECT(x - 2, y, 1, h);
            labels[n] = "|";
            actions[n] = NULL;
            n++;
            continue;
        }
        int w = KA_MAX(24, font4x6_width(TOOLBAR[i].label) + 8);
        rects[n] = KRECT(x, y, w, h);
        labels[n] = TOOLBAR[i].label;
        actions[n] = TOOLBAR[i].action;
        n++;
        x += w + 2;
    }
    return n;
}

/* --- Rendering --- */

static void draw_toolbar(EditorWindow *ew)
{
    SDL_Surface *buf = ew->kw.buf.buf;
    ksurf_fill(buf, toolbar_rect(ew), TB_BG);

    KRect rects[KA_LEN(TOOLBAR)];
    const char *labels[KA_LEN(TOOLBAR)], *actions[KA_LEN(TOOLBAR)];
    int n = button_rects(ew, rects, labels, actions);

    for (int i = 0; i < n; i++) {
        if (!actions[i]) {
            ksurf_line(buf, rects[i].x, rects[i].y + 2, rects[i].x,
                       rects[i].y + rects[i].h - 2, TB_BORDER);
            continue;
        }
        bool hover = ew->hover_btn && strcmp(ew->hover_btn, actions[i]) == 0;
        ksurf_fill(buf, rects[i], hover ? TB_HOVER : TB_BTN);
        ksurf_line(buf, rects[i].x, rects[i].y, rects[i].x + rects[i].w - 1,
                   rects[i].y, TB_BORDER);
        ksurf_line(buf, rects[i].x, rects[i].y + rects[i].h - 1,
                   rects[i].x + rects[i].w - 1,
                   rects[i].y + rects[i].h - 1, TB_BORDER);
        ksurf_line(buf, rects[i].x, rects[i].y, rects[i].x,
                   rects[i].y + rects[i].h - 1, TB_BORDER);
        ksurf_line(buf, rects[i].x + rects[i].w - 1, rects[i].y,
                   rects[i].x + rects[i].w - 1,
                   rects[i].y + rects[i].h - 1, TB_BORDER);

        bool can_use = true;
        if (strcmp(actions[i], "undo") == 0)
            can_use = ad_can_undo(&ew->audio_data);
        else if (strcmp(actions[i], "redo") == 0)
            can_use = ad_can_redo(&ew->audio_data);
        else if (strcmp(actions[i], "save") == 0 ||
                 strcmp(actions[i], "zoom_sel") == 0 ||
                 strcmp(actions[i], "effects") == 0)
            can_use = ew->audio_data.loaded;

        KColor color = can_use ? ew->skin->colors.pl_current : TB_DIM;
        font4x6_draw_centered(buf, rects[i], labels[i], color);
    }
}

static void draw_info(EditorWindow *ew)
{
    SDL_Surface *buf = ew->kw.buf.buf;
    KRect rect = info_rect(ew);
    ksurf_fill(buf, rect, INFO_BG);

    KColor color = ew->skin->colors.pl_normal;
    if (ew->status_msg[0]) {
        font4x6_draw_centered(buf, rect, ew->status_msg, color);
        return;
    }
    if (!ew->audio_data.loaded) {
        font4x6_draw_centered(buf, rect, "NO AUDIO", color);
        return;
    }

    char info[256];
    int ch = ad_channels(&ew->audio_data);
    if (ad_has_selection(&ew->audio_data)) {
        int s = ew->audio_data.sel_start, e = ew->audio_data.sel_end;
        snprintf(info, sizeof(info),
                 "%dHZ | %s | %.1fS | SEL: %d-%d (%.3fS)",
                 ew->audio_data.sr, ch == 2 ? "STEREO" : "MONO",
                 ad_duration(&ew->audio_data), s, e,
                 (double)(e - s) / ew->audio_data.sr);
    } else {
        snprintf(info, sizeof(info), "%dHZ | %s | %.1fS", ew->audio_data.sr,
                 ch == 2 ? "STEREO" : "MONO", ad_duration(&ew->audio_data));
    }
    font4x6_draw_centered(buf, rect, info, color);
}

void editor_window_render(EditorWindow *ew)
{
    SDL_Surface *buf = ew->kw.buf.buf;
    SkinColors *colors = &ew->skin->colors;

    ksurf_fill(buf, KRECT(0, 0, ew->ed_w, ew->ed_h), colors->pl_normalbg);

    SDL_Surface *pledit = skin_bitmap(ew->skin, "pledit");
    if (pledit) {
        bool act = ew->kw.active;
        ksurf_blit(buf, 0, 0, pledit, act ? PL_TL_ACTIVE : PL_TL_INACTIVE);
        KRect tsrc =
            act ? PL_TITLE_STRETCH_ACTIVE : PL_TITLE_STRETCH_INACTIVE;
        int stretch_w = ew->ed_w - 25 - 125;
        for (int x = 25; x < 25 + stretch_w; x += tsrc.w) {
            int w = KA_MIN(tsrc.w, 25 + stretch_w - x);
            ksurf_blit(buf, x, 0, pledit, KRECT(tsrc.x, tsrc.y, w, tsrc.h));
        }
        KRect trsrc = act ? PL_TR_ACTIVE : PL_TR_INACTIVE;
        ksurf_blit(buf, ew->ed_w - trsrc.w, 0, pledit, trsrc);

        for (int y = ED_TITLE_H; y < ew->ed_h - 4; y += PL_LEFT_TILE.h) {
            int h = KA_MIN(PL_LEFT_TILE.h, ew->ed_h - 4 - y);
            ksurf_blit(buf, 0, y, pledit,
                       KRECT(PL_LEFT_TILE.x, PL_LEFT_TILE.y, PL_LEFT_TILE.w,
                             h));
        }
        for (int y = ED_TITLE_H; y < ew->ed_h - 4; y += PL_RIGHT_TILE.h) {
            int h = KA_MIN(PL_RIGHT_TILE.h, ew->ed_h - 4 - y);
            ksurf_blit(buf, ew->ed_w - PL_RIGHT_TILE.w, y, pledit,
                       KRECT(PL_RIGHT_TILE.x, PL_RIGHT_TILE.y,
                             PL_RIGHT_TILE.w, h));
        }
    }
    ksurf_fill(buf, KRECT(0, ew->ed_h - 4, ew->ed_w, 4),
               (KColor){30, 30, 35});

    /* Title text */
    char title[300] = "EDITOR";
    if (ew->audio_data.filepath && *ew->audio_data.filepath)
        snprintf(title, sizeof(title), "EDITOR: %s",
                 ka_basename(ew->audio_data.filepath));
    font4x6_draw(buf, 28, (ED_TITLE_H - 6) / 2, title, colors->pl_current);

    draw_toolbar(ew);

    waveform_draw_ruler(buf, ruler_rect(ew), ew->view_start, ew->view_end,
                        ew->audio_data.sr);

    KRect wf = waveform_rect(ew);
    if (ew->audio_data.loaded) {
        waveform_compute(&ew->cache, &ew->audio_data.samples,
                         ew->view_start, ew->view_end, wf.w);

        int sel_start_px = -1, sel_end_px = -1, cursor_px = -1;
        int view_range = KA_MAX(1, ew->view_end - ew->view_start);
        if (ad_has_selection(&ew->audio_data)) {
            sel_start_px =
                (int)((int64_t)(ew->audio_data.sel_start - ew->view_start) *
                      wf.w / view_range);
            sel_end_px =
                (int)((int64_t)(ew->audio_data.sel_end - ew->view_start) *
                      wf.w / view_range);
        } else {
            cursor_px =
                (int)((int64_t)(ew->audio_data.cursor - ew->view_start) *
                      wf.w / view_range);
        }
        waveform_draw(&ew->cache, buf, wf, sel_start_px, sel_end_px,
                      cursor_px);
    } else {
        ksurf_fill(buf, wf, (KColor){20, 20, 25});
        font4x6_draw_centered(
            buf, KRECT(wf.x, wf.y, wf.w, wf.h / 2 + 6), "NO AUDIO LOADED",
            (KColor){100, 100, 100});
        font4x6_draw_centered(
            buf, KRECT(wf.x, wf.y + wf.h / 2, wf.w, 12),
            "RIGHT-CLICK PLAYLIST - EDIT IN EDITOR",
            (KColor){100, 100, 100});
    }

    draw_info(ew);
    kwin_present(&ew->kw);
}

void editor_window_set_scale(EditorWindow *ew, float scale)
{
    ew->kw.logical_w = ew->ed_w;
    ew->kw.logical_h = ew->ed_h;
    kwin_set_scale_resizable(&ew->kw, scale, ED_MIN_W, ED_MIN_H);
}

void editor_window_resize(EditorWindow *ew, int w, int h)
{
    ew->ed_w = KA_MAX(ED_MIN_W, w);
    ew->ed_h = KA_MAX(ED_MIN_H, h);
    ew->kw.logical_w = ew->ed_w;
    ew->kw.logical_h = ew->ed_h;
    kbuf_resize(&ew->kw.buf, ew->ed_w, ew->ed_h);
    ew->kw.buf.scale = kwin_scale(&ew->kw);
    waveform_invalidate(&ew->cache);
}

/* --- View helpers --- */

static int px_to_sample(const EditorWindow *ew, int px_x)
{
    KRect wf = waveform_rect(ew);
    if (wf.w <= 0)
        return 0;
    double frac = KA_CLAMP((double)(px_x - wf.x) / wf.w, 0.0, 1.0);
    return ew->view_start +
           (int)(frac * (ew->view_end - ew->view_start));
}

static void action_zoom_in(EditorWindow *ew)
{
    if (!ew->audio_data.loaded)
        return;
    int view_range = ew->view_end - ew->view_start;
    int new_range = KA_MAX(100, view_range / 2);
    int center = (ew->view_start + ew->view_end) / 2;
    ew->view_start = KA_MAX(0, center - new_range / 2);
    ew->view_end = KA_MIN(ad_num_samples(&ew->audio_data),
                          ew->view_start + new_range);
    waveform_invalidate(&ew->cache);
}

static void action_zoom_out(EditorWindow *ew)
{
    if (!ew->audio_data.loaded)
        return;
    int view_range = ew->view_end - ew->view_start;
    int new_range = KA_MIN(ad_num_samples(&ew->audio_data), view_range * 2);
    int center = (ew->view_start + ew->view_end) / 2;
    ew->view_start = KA_MAX(0, center - new_range / 2);
    ew->view_end = KA_MIN(ad_num_samples(&ew->audio_data),
                          ew->view_start + new_range);
    waveform_invalidate(&ew->cache);
}

static void action_fit(EditorWindow *ew)
{
    if (!ew->audio_data.loaded)
        return;
    ew->view_start = 0;
    ew->view_end = ad_num_samples(&ew->audio_data);
    waveform_invalidate(&ew->cache);
}

static void action_zoom_sel(EditorWindow *ew)
{
    if (!ad_has_selection(&ew->audio_data))
        return;
    ew->view_start = ew->audio_data.sel_start;
    ew->view_end = ew->audio_data.sel_end;
    waveform_invalidate(&ew->cache);
}

static void action_open(EditorWindow *ew)
{
    char *path = filedialog_open_file("Open Audio File");
    if (path) {
        editor_window_load_file(ew, path);
        free(path);
    }
}

static void action_save(EditorWindow *ew)
{
    if (!ew->audio_data.loaded)
        return;
    char *suggested = NULL;
    if (ew->audio_data.filepath && *ew->audio_data.filepath) {
        char *dir = ka_dirname(ew->audio_data.filepath);
        char *stem = ka_stem(ew->audio_data.filepath);
        suggested = ka_asprintf("%s/%s_edited.wav", dir, stem);
        free(dir);
        free(stem);
    }
    char *path = filedialog_save_file("Export Audio", suggested);
    free(suggested);
    if (!path)
        return;
    char msg[256];
    if (ad_export(&ew->audio_data, path))
        snprintf(msg, sizeof(msg), "SAVED: %s", ka_basename(path));
    else
        snprintf(msg, sizeof(msg), "FAILED TO SAVE: %s", ka_basename(path));
    set_status(ew, msg);
    free(path);
}

/* --- Effect application: dialogs then AudioData thunks --- */

typedef struct {
    double p[10];
    const char *s;
} FxParams;

#define DEF_FX(name, call)                                                  \
    static AudioBuf fx_thunk_##name(const AudioBuf *in, int sr, void *v)    \
    {                                                                       \
        FxParams *fp = v;                                                   \
        (void)fp;                                                           \
        return call;                                                        \
    }

DEF_FX(amplify, fx_amplify(in, sr, fp->p[0]))
DEF_FX(normalize, fx_normalize(in, sr, fp->p[0]))
DEF_FX(loudness, fx_loudness_normalize(in, sr, fp->p[0]))
DEF_FX(compressor, fx_compressor(in, sr, fp->p[0], fp->p[1], fp->p[2], fp->p[3]))
DEF_FX(limiter, fx_limiter(in, sr, fp->p[0], fp->p[1]))
DEF_FX(fade_in, fx_fade_in(in, sr, fp->p[0]))
DEF_FX(fade_out, fx_fade_out(in, sr, fp->p[0]))
DEF_FX(change_pitch, fx_change_pitch(in, sr, fp->p[0]))
DEF_FX(graphic_eq, fx_graphic_eq(in, sr, fp->p))
DEF_FX(click_removal, fx_click_removal(in, sr, fp->p[0], fp->p[1]))
DEF_FX(noise_reduction, fx_noise_reduction(in, sr, fp->p[0]))
DEF_FX(repair, fx_repair(in, sr))
DEF_FX(reverb, fx_reverb(in, sr, fp->p[0], fp->p[1], fp->p[2], fp->p[3]))
DEF_FX(invert, fx_invert(in, sr))
DEF_FX(reverse, fx_reverse(in, sr))

static bool param_to_int(double value, int *result)
{
    if (!isfinite(value) || value < INT_MIN || value > INT_MAX)
        return false;
    *result = (int)value;
    return true;
}

static AudioBuf fx_thunk_truncate_silence(const AudioBuf *in, int sr,
                                          void *v)
{
    FxParams *fp = v;
    int min_silence_ms;
    if (!param_to_int(fp->p[1], &min_silence_ms))
        return (AudioBuf){0};
    return fx_truncate_silence(in, sr, fp->p[0], min_silence_ms);
}

static AudioBuf gen_thunk_tone(int sr, void *v)
{
    FxParams *fp = v;
    WaveForm wf = WAVE_SINE;
    if (fp->s) {
        if (strcmp(fp->s, "square") == 0)
            wf = WAVE_SQUARE;
        else if (strcmp(fp->s, "sawtooth") == 0)
            wf = WAVE_SAWTOOTH;
        else if (strcmp(fp->s, "triangle") == 0)
            wf = WAVE_TRIANGLE;
    }
    return gen_tone(sr, fp->p[0], fp->p[1], fp->p[2], wf);
}

static AudioBuf gen_thunk_chirp(int sr, void *v)
{
    FxParams *fp = v;
    return gen_chirp(sr, fp->p[0], fp->p[1], fp->p[2], fp->p[3]);
}

static AudioBuf gen_thunk_silence(int sr, void *v)
{
    FxParams *fp = v;
    int channels;
    if (!param_to_int(fp->p[1], &channels))
        return abuf_alloc(0, 1);
    return gen_silence(sr, fp->p[0], channels);
}

static AudioBuf gen_thunk_noise(int sr, void *v)
{
    FxParams *fp = v;
    NoiseType nt = NOISE_WHITE;
    if (fp->s) {
        if (strcmp(fp->s, "pink") == 0)
            nt = NOISE_PINK;
        else if (strcmp(fp->s, "brown") == 0)
            nt = NOISE_BROWN;
    }
    return gen_noise(sr, fp->p[0], fp->p[1], nt);
}

static AudioBuf gen_thunk_dtmf(int sr, void *v)
{
    FxParams *fp = v;
    return gen_dtmf(sr, fp->s ? fp->s : "", fp->p[0], fp->p[1], fp->p[2]);
}

static void show_effects_menu(EditorWindow *ew)
{
    enum {
        FX_AMPLIFY = 1, FX_NORMALIZE, FX_LOUDNESS, FX_COMPRESSOR,
        FX_LIMITER, FX_FADE_IN, FX_FADE_OUT, FX_PITCH, FX_EQ,
        FX_CLICK, FX_NOISE_RED, FX_REPAIR, FX_REVERB, FX_INVERT,
        FX_REVERSE, FX_TRUNCATE, FX_GEN_TONE, FX_GEN_CHIRP, FX_GEN_SILENCE,
        FX_GEN_NOISE, FX_GEN_DTMF,
    };
    /* Submenus flattened under headers. */
    MenuItem items[] = {
        MENU_HEADER("AMPLITUDE"),
        {"AMPLIFY...", FX_AMPLIFY},
        {"NORMALIZE...", FX_NORMALIZE},
        {"LOUDNESS NORMALIZE...", FX_LOUDNESS},
        {"COMPRESSOR...", FX_COMPRESSOR},
        {"LIMITER...", FX_LIMITER},
        MENU_HEADER("FADES"),
        {"FADE IN...", FX_FADE_IN},
        {"FADE OUT...", FX_FADE_OUT},
        MENU_HEADER("PITCH & EQ"),
        {"CHANGE PITCH...", FX_PITCH},
        {"GRAPHIC EQ...", FX_EQ},
        MENU_HEADER("REPAIR"),
        {"CLICK REMOVAL...", FX_CLICK},
        {"NOISE REDUCTION...", FX_NOISE_RED},
        {"REPAIR", FX_REPAIR},
        MENU_SEPARATOR,
        {"REVERB...", FX_REVERB},
        MENU_HEADER("TRANSFORM"),
        {"INVERT", FX_INVERT},
        {"REVERSE", FX_REVERSE},
        {"TRUNCATE SILENCE...", FX_TRUNCATE},
        MENU_HEADER("GENERATE"),
        {"TONE...", FX_GEN_TONE},
        {"CHIRP...", FX_GEN_CHIRP},
        {"SILENCE...", FX_GEN_SILENCE},
        {"NOISE...", FX_GEN_NOISE},
        {"DTMF...", FX_GEN_DTMF},
    };
    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    int choice = menu_show(mx, my, items, (int)KA_LEN(items));
    if (choice < 0)
        return;

    AudioData *ad = &ew->audio_data;
    FxParams fp = {0};
    DialogField f[6];

    switch (choice) {
    case FX_AMPLIFY:
        f[0] = dialog_field_num("gain_db", "GAIN (DB):", 6.0);
        if (dialog_run("Amplify", f, 1)) {
            fp.p[0] = dialog_get_num(f, 1, "gain_db", 6.0);
            ad_apply_effect(ad, fx_thunk_amplify, &fp);
        }
        break;
    case FX_NORMALIZE:
        f[0] = dialog_field_num("peak", "PEAK LEVEL (DB):", 0.0);
        if (dialog_run("Normalize", f, 1)) {
            fp.p[0] = dialog_get_num(f, 1, "peak", 0.0);
            ad_apply_effect(ad, fx_thunk_normalize, &fp);
        }
        break;
    case FX_LOUDNESS:
        f[0] = dialog_field_num("lufs", "TARGET LUFS:", -14.0);
        if (dialog_run("Loudness Normalize", f, 1)) {
            fp.p[0] = dialog_get_num(f, 1, "lufs", -14.0);
            ad_apply_effect(ad, fx_thunk_loudness, &fp);
        }
        break;
    case FX_COMPRESSOR:
        f[0] = dialog_field_num("thresh", "THRESHOLD (DB):", -20.0);
        f[1] = dialog_field_num("ratio", "RATIO:", 4.0);
        f[2] = dialog_field_num("attack", "ATTACK (MS):", 10.0);
        f[3] = dialog_field_num("makeup", "MAKEUP GAIN (DB):", 0.0);
        if (dialog_run("Compressor", f, 4)) {
            fp.p[0] = dialog_get_num(f, 4, "thresh", -20.0);
            fp.p[1] = dialog_get_num(f, 4, "ratio", 4.0);
            fp.p[2] = dialog_get_num(f, 4, "attack", 10.0);
            fp.p[3] = dialog_get_num(f, 4, "makeup", 0.0);
            ad_apply_effect(ad, fx_thunk_compressor, &fp);
        }
        break;
    case FX_LIMITER:
        f[0] = dialog_field_num("thresh", "THRESHOLD (DB):", -1.0);
        f[1] = dialog_field_num("release", "RELEASE (MS):", 50.0);
        if (dialog_run("Limiter", f, 2)) {
            fp.p[0] = dialog_get_num(f, 2, "thresh", -1.0);
            fp.p[1] = dialog_get_num(f, 2, "release", 50.0);
            ad_apply_effect(ad, fx_thunk_limiter, &fp);
        }
        break;
    case FX_FADE_IN:
    case FX_FADE_OUT:
        f[0] = dialog_field_num("dur", "DURATION (SEC):", 1.0);
        if (dialog_run(choice == FX_FADE_IN ? "Fade In" : "Fade Out", f,
                       1)) {
            fp.p[0] = dialog_get_num(f, 1, "dur", 1.0);
            ad_apply_effect(ad,
                            choice == FX_FADE_IN ? fx_thunk_fade_in
                                                 : fx_thunk_fade_out,
                            &fp);
        }
        break;
    case FX_PITCH:
        f[0] = dialog_field_num("semi", "SEMITONES:", 0.0);
        if (dialog_run("Change Pitch", f, 1)) {
            fp.p[0] = dialog_get_num(f, 1, "semi", 0.0);
            ad_apply_effect(ad, fx_thunk_change_pitch, &fp);
        }
        break;
    case FX_EQ: {
        f[0] = dialog_field_text("gains", "10 GAINS (DB, CSV):",
                                 "0,0,0,0,0,0,0,0,0,0");
        if (dialog_run("Graphic EQ", f, 1)) {
            char *copy = ka_strdup(dialog_get_str(f, 1, "gains", ""));
            char *save = NULL;
            int i = 0;
            for (char *tok = strtok_r(copy, ",", &save); tok && i < 10;
                 tok = strtok_r(NULL, ",", &save))
                fp.p[i++] = atof(tok);
            free(copy);
            ad_apply_effect(ad, fx_thunk_graphic_eq, &fp);
        }
        break;
    }
    case FX_CLICK:
        f[0] = dialog_field_num("thresh", "THRESHOLD:", 5.0);
        f[1] = dialog_field_num("width", "WIDTH (MS):", 1.0);
        if (dialog_run("Click Removal", f, 2)) {
            fp.p[0] = dialog_get_num(f, 2, "thresh", 5.0);
            fp.p[1] = dialog_get_num(f, 2, "width", 1.0);
            ad_apply_effect(ad, fx_thunk_click_removal, &fp);
        }
        break;
    case FX_NOISE_RED:
        f[0] = dialog_field_num("red", "REDUCTION (DB):", 12.0);
        if (dialog_run("Noise Reduction", f, 1)) {
            fp.p[0] = dialog_get_num(f, 1, "red", 12.0);
            ad_apply_effect(ad, fx_thunk_noise_reduction, &fp);
        }
        break;
    case FX_REPAIR:
        ad_apply_effect(ad, fx_thunk_repair, &fp);
        break;
    case FX_REVERB:
        f[0] = dialog_field_num("room", "ROOM SIZE (0-1):", 0.5);
        f[1] = dialog_field_num("damp", "DAMPING (0-1):", 0.5);
        f[2] = dialog_field_num("wet", "WET:", 0.3);
        f[3] = dialog_field_num("dry", "DRY:", 0.7);
        if (dialog_run("Reverb", f, 4)) {
            fp.p[0] = dialog_get_num(f, 4, "room", 0.5);
            fp.p[1] = dialog_get_num(f, 4, "damp", 0.5);
            fp.p[2] = dialog_get_num(f, 4, "wet", 0.3);
            fp.p[3] = dialog_get_num(f, 4, "dry", 0.7);
            ad_apply_effect(ad, fx_thunk_reverb, &fp);
        }
        break;
    case FX_INVERT:
        ad_apply_effect(ad, fx_thunk_invert, &fp);
        break;
    case FX_REVERSE:
        ad_apply_effect(ad, fx_thunk_reverse, &fp);
        break;
    case FX_TRUNCATE:
        f[0] = dialog_field_num("thresh", "THRESHOLD (DB):", -40.0);
        f[1] = dialog_field_num("minsil", "MIN SILENCE (MS):", 200.0);
        if (dialog_run("Truncate Silence", f, 2)) {
            fp.p[0] = dialog_get_num(f, 2, "thresh", -40.0);
            fp.p[1] = dialog_get_num(f, 2, "minsil", 200.0);
            ad_apply_effect(ad, fx_thunk_truncate_silence, &fp);
        }
        break;
    case FX_GEN_TONE: {
        static const char *waves[] = {"sine", "square", "sawtooth",
                                      "triangle", NULL};
        f[0] = dialog_field_num("dur", "DURATION (SEC):", 1.0);
        f[1] = dialog_field_num("freq", "FREQUENCY (HZ):", 440.0);
        f[2] = dialog_field_num("amp", "AMPLITUDE (0-1):", 0.8);
        f[3] = dialog_field_combo("wave", "WAVEFORM:", waves);
        if (dialog_run("Generate Tone", f, 4)) {
            fp.p[0] = dialog_get_num(f, 4, "dur", 1.0);
            fp.p[1] = dialog_get_num(f, 4, "freq", 440.0);
            fp.p[2] = dialog_get_num(f, 4, "amp", 0.8);
            fp.s = dialog_get_str(f, 4, "wave", "sine");
            ad_apply_generator(ad, gen_thunk_tone, &fp);
        }
        break;
    }
    case FX_GEN_CHIRP:
        f[0] = dialog_field_num("dur", "DURATION (SEC):", 1.0);
        f[1] = dialog_field_num("f0", "START FREQ (HZ):", 20.0);
        f[2] = dialog_field_num("f1", "END FREQ (HZ):", 20000.0);
        f[3] = dialog_field_num("amp", "AMPLITUDE (0-1):", 0.8);
        if (dialog_run("Generate Chirp", f, 4)) {
            fp.p[0] = dialog_get_num(f, 4, "dur", 1.0);
            fp.p[1] = dialog_get_num(f, 4, "f0", 20.0);
            fp.p[2] = dialog_get_num(f, 4, "f1", 20000.0);
            fp.p[3] = dialog_get_num(f, 4, "amp", 0.8);
            ad_apply_generator(ad, gen_thunk_chirp, &fp);
        }
        break;
    case FX_GEN_SILENCE:
        f[0] = dialog_field_num("dur", "DURATION (SEC):", 1.0);
        f[1] = dialog_field_num("ch", "CHANNELS:",
                                KA_MAX(1, ad_channels(ad)));
        if (dialog_run("Generate Silence", f, 2)) {
            fp.p[0] = dialog_get_num(f, 2, "dur", 1.0);
            fp.p[1] = dialog_get_num(f, 2, "ch", 1);
            ad_apply_generator(ad, gen_thunk_silence, &fp);
        }
        break;
    case FX_GEN_NOISE: {
        static const char *types[] = {"white", "pink", "brown", NULL};
        f[0] = dialog_field_num("dur", "DURATION (SEC):", 1.0);
        f[1] = dialog_field_num("amp", "AMPLITUDE (0-1):", 0.5);
        f[2] = dialog_field_combo("type", "TYPE:", types);
        if (dialog_run("Generate Noise", f, 3)) {
            fp.p[0] = dialog_get_num(f, 3, "dur", 1.0);
            fp.p[1] = dialog_get_num(f, 3, "amp", 0.5);
            fp.s = dialog_get_str(f, 3, "type", "white");
            ad_apply_generator(ad, gen_thunk_noise, &fp);
        }
        break;
    }
    case FX_GEN_DTMF:
        f[0] = dialog_field_text("seq", "SEQUENCE:", "12345");
        f[1] = dialog_field_num("tone", "TONE DUR (SEC):", 0.2);
        f[2] = dialog_field_num("gap", "GAP DUR (SEC):", 0.05);
        f[3] = dialog_field_num("amp", "AMPLITUDE (0-1):", 0.8);
        if (dialog_run("Generate DTMF", f, 4)) {
            fp.s = dialog_get_str(f, 4, "seq", "");
            fp.p[0] = dialog_get_num(f, 4, "tone", 0.2);
            fp.p[1] = dialog_get_num(f, 4, "gap", 0.05);
            fp.p[2] = dialog_get_num(f, 4, "amp", 0.8);
            ad_apply_generator(ad, gen_thunk_dtmf, &fp);
        }
        break;
    default:
        break;
    }
}

static void handle_action(EditorWindow *ew, const char *action)
{
    if (strcmp(action, "open") == 0)
        action_open(ew);
    else if (strcmp(action, "save") == 0)
        action_save(ew);
    else if (strcmp(action, "undo") == 0)
        ad_undo(&ew->audio_data);
    else if (strcmp(action, "redo") == 0)
        ad_redo(&ew->audio_data);
    else if (strcmp(action, "zoom_in") == 0)
        action_zoom_in(ew);
    else if (strcmp(action, "zoom_out") == 0)
        action_zoom_out(ew);
    else if (strcmp(action, "fit") == 0)
        action_fit(ew);
    else if (strcmp(action, "zoom_sel") == 0)
        action_zoom_sel(ew);
    else if (strcmp(action, "effects") == 0)
        show_effects_menu(ew);
}

/* --- Mouse handling --- */

void editor_window_mouse_press(EditorWindow *ew, int x, int y, int button,
                               uint16_t mod)
{
    (void)button;
    int lx, ly;
    kwin_logical(&ew->kw, x, y, &lx, &ly);

    if (krect_contains(resize_handle_rect(ew), lx, ly)) {
        int gx, gy;
        SDL_GetGlobalMouseState(&gx, &gy);
        ew->resizing = true;
        ew->resize_start_w = ew->ed_w;
        ew->resize_start_h = ew->ed_h;
        ew->resize_start_x = gx;
        ew->resize_start_y = gy;
        SDL_CaptureMouse(SDL_TRUE);
        return;
    }

    if (ly < ED_TITLE_H && lx > ew->ed_w - 15) {
        kwin_hide(&ew->kw);
        return;
    }

    if (ly < ED_TITLE_H) {
        kwin_begin_drag(&ew->kw);
        if (ew->cbs.drag_started)
            ew->cbs.drag_started(ew->cbs.ud);
        return;
    }

    if (krect_contains(toolbar_rect(ew), lx, ly)) {
        KRect rects[KA_LEN(TOOLBAR)];
        const char *labels[KA_LEN(TOOLBAR)], *actions[KA_LEN(TOOLBAR)];
        int n = button_rects(ew, rects, labels, actions);
        for (int i = 0; i < n; i++)
            if (actions[i] && krect_contains(rects[i], lx, ly)) {
                handle_action(ew, actions[i]);
                return;
            }
        return;
    }

    KRect wf = waveform_rect(ew);
    if (krect_contains(wf, lx, ly) && ew->audio_data.loaded) {
        int sample = px_to_sample(ew, lx);
        if (mod & KMOD_SHIFT) {
            /* Extend selection from nearest edge */
            if (ad_has_selection(&ew->audio_data)) {
                if (abs(sample - ew->audio_data.sel_start) <
                    abs(sample - ew->audio_data.sel_end))
                    ew->sel_anchor = ew->audio_data.sel_end;
                else
                    ew->sel_anchor = ew->audio_data.sel_start;
            } else {
                ew->sel_anchor = ew->audio_data.cursor;
            }
            ad_set_selection(&ew->audio_data,
                             KA_MIN(ew->sel_anchor, sample),
                             KA_MAX(ew->sel_anchor, sample));
        } else {
            ew->selecting = true;
            ew->sel_anchor = sample;
            ad_set_cursor(&ew->audio_data, sample);
            ad_clear_selection(&ew->audio_data);
        }
    }
}

void editor_window_mouse_move(EditorWindow *ew, int x, int y)
{
    int lx, ly;
    kwin_logical(&ew->kw, x, y, &lx, &ly);

    if (ew->kw.dragging) {
        if (ew->cbs.drag_moved) {
            int nx, ny;
            kwin_drag_pos(&ew->kw, &nx, &ny);
            ew->cbs.drag_moved(ew->cbs.ud, nx, ny);
        }
    } else if (ew->resizing) {
        int gx, gy;
        SDL_GetGlobalMouseState(&gx, &gy);
        float scale = kwin_scale(&ew->kw);
        int new_w =
            KA_MAX(ED_MIN_W, ew->resize_start_w + (gx - ew->resize_start_x));
        int new_h =
            KA_MAX(ED_MIN_H, ew->resize_start_h + (gy - ew->resize_start_y));
        SDL_SetWindowSize(ew->kw.win, (int)lroundf(new_w * scale),
                          (int)lroundf(new_h * scale));
        editor_window_resize(ew, new_w, new_h);
    } else if (ew->selecting) {
        int sample = px_to_sample(ew, lx);
        ad_set_selection(&ew->audio_data, KA_MIN(ew->sel_anchor, sample),
                         KA_MAX(ew->sel_anchor, sample));
    } else {
        /* Toolbar hover */
        ew->hover_btn = NULL;
        if (krect_contains(toolbar_rect(ew), lx, ly)) {
            KRect rects[KA_LEN(TOOLBAR)];
            const char *labels[KA_LEN(TOOLBAR)], *actions[KA_LEN(TOOLBAR)];
            int n = button_rects(ew, rects, labels, actions);
            for (int i = 0; i < n; i++)
                if (actions[i] && krect_contains(rects[i], lx, ly)) {
                    ew->hover_btn = actions[i];
                    break;
                }
        }
    }
}

void editor_window_mouse_release(EditorWindow *ew, int x, int y)
{
    (void)x;
    (void)y;
    if (ew->kw.dragging) {
        ew->kw.dragging = false;
        SDL_CaptureMouse(SDL_FALSE);
        if (ew->cbs.drag_ended)
            ew->cbs.drag_ended(ew->cbs.ud);
    } else if (ew->resizing) {
        ew->resizing = false;
        SDL_CaptureMouse(SDL_FALSE);
    } else if (ew->selecting) {
        ew->selecting = false;
    }
}

void editor_window_wheel(EditorWindow *ew, int delta_y, uint16_t mod)
{
    if (!ew->audio_data.loaded)
        return;

    if (mod & KMOD_SHIFT) {
        /* Horizontal scroll */
        int view_range = ew->view_end - ew->view_start;
        int scroll_amount = KA_MAX(1, view_range / 10);
        if (delta_y > 0) {
            ew->view_start = KA_MAX(0, ew->view_start - scroll_amount);
            ew->view_end = ew->view_start + view_range;
        } else {
            ew->view_end = KA_MIN(ad_num_samples(&ew->audio_data),
                                  ew->view_end + scroll_amount);
            ew->view_start = ew->view_end - view_range;
        }
        waveform_invalidate(&ew->cache);
        return;
    }

    /* Zoom around mouse position */
    KRect wf = waveform_rect(ew);
    if (wf.w <= 0)
        return;
    int mx, my, wx, wy;
    SDL_GetGlobalMouseState(&mx, &my);
    kwin_get_pos(&ew->kw, &wx, &wy);
    int lx = (int)((mx - wx) / kwin_scale(&ew->kw));
    double frac = KA_CLAMP((double)(lx - wf.x) / wf.w, 0.0, 1.0);

    int view_range = ew->view_end - ew->view_start;
    int center_sample = ew->view_start + (int)(frac * view_range);

    int new_range;
    if (delta_y > 0)
        new_range = KA_MAX(100, (int)(view_range * 0.7));
    else
        new_range =
            KA_MIN(ad_num_samples(&ew->audio_data), (int)(view_range * 1.4));

    int new_start = center_sample - (int)(frac * new_range);
    int new_end = new_start + new_range;
    if (new_start < 0) {
        new_start = 0;
        new_end = KA_MIN(new_range, ad_num_samples(&ew->audio_data));
    }
    if (new_end > ad_num_samples(&ew->audio_data)) {
        new_end = ad_num_samples(&ew->audio_data);
        new_start = KA_MAX(0, new_end - new_range);
    }
    ew->view_start = new_start;
    ew->view_end = new_end;
    waveform_invalidate(&ew->cache);
}

void editor_window_key(EditorWindow *ew, SDL_Keycode key, uint16_t mod)
{
    if (mod & KMOD_CTRL) {
        if (key == SDLK_z)
            ad_undo(&ew->audio_data);
        else if (key == SDLK_y)
            ad_redo(&ew->audio_data);
        else if (key == SDLK_a)
            ad_select_all(&ew->audio_data);
    }
}

void editor_window_drop(EditorWindow *ew, const char *path)
{
    if (ka_is_file(path))
        editor_window_load_file(ew, path);
}
