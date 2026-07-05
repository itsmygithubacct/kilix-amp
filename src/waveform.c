#include "waveform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font4x6.h"
#include "render.h"

#define WAVE_COLOR      ((KColor){0, 200, 0})
#define WAVE_RMS_COLOR  ((KColor){0, 140, 0})
#define CENTER_COLOR    ((KColor){60, 60, 60})
#define CURSOR_COLOR    ((KColor){255, 255, 255})
#define SELECTION_COLOR ((KColor){20, 60, 120}) /* pre-blended overlay tint */
#define RULER_BG        ((KColor){30, 30, 35})
#define RULER_TEXT      ((KColor){180, 180, 180})
#define RULER_TICK      ((KColor){100, 100, 100})
#define WF_BG_COLOR     ((KColor){20, 20, 25})

void waveform_cache_init(WaveformCache *wc)
{
    memset(wc, 0, sizeof(*wc));
    wc->view_start = -1;
    wc->view_end = -1;
}

static void free_cols(WaveformCache *wc)
{
    free(wc->mins);
    free(wc->maxs);
    free(wc->rms);
    wc->mins = wc->maxs = wc->rms = NULL;
}

static void free_blocks(WaveformCache *wc)
{
    free(wc->block_min);
    free(wc->block_max);
    free(wc->block_rms);
    wc->block_min = wc->block_max = wc->block_rms = NULL;
    wc->n_blocks = 0;
}

void waveform_cache_destroy(WaveformCache *wc)
{
    free_cols(wc);
    free_blocks(wc);
}

void waveform_precompute_blocks(WaveformCache *wc, const AudioBuf *samples)
{
    free_blocks(wc);
    if (!samples || samples->frames == 0)
        return;

    int n = samples->frames;
    int ch = samples->channels;
    int bs = WF_BLOCK_SIZE;
    int n_blocks = (n + bs - 1) / bs; /* full blocks + remainder block */

    wc->block_min = malloc(sizeof(double) * (size_t)n_blocks * ch);
    wc->block_max = malloc(sizeof(double) * (size_t)n_blocks * ch);
    wc->block_rms = malloc(sizeof(double) * (size_t)n_blocks * ch);
    if (!wc->block_min || !wc->block_max || !wc->block_rms)
        abort();
    wc->n_blocks = n_blocks;
    wc->block_channels = ch;

    for (int b = 0; b < n_blocks; b++) {
        int s = b * bs;
        int e = KA_MIN(s + bs, n);
        for (int c = 0; c < ch; c++) {
            double mn = 1e30, mx = -1e30, sum2 = 0.0;
            for (int f = s; f < e; f++) {
                double v = samples->data[f * ch + c];
                mn = KA_MIN(mn, v);
                mx = KA_MAX(mx, v);
                sum2 += v * v;
            }
            wc->block_min[b * ch + c] = mn;
            wc->block_max[b * ch + c] = mx;
            wc->block_rms[b * ch + c] = sqrt(sum2 / KA_MAX(1, e - s));
        }
    }
}

static void alloc_cols(WaveformCache *wc, int width, int channels)
{
    free_cols(wc);
    wc->mins = calloc((size_t)width * channels, sizeof(double));
    wc->maxs = calloc((size_t)width * channels, sizeof(double));
    wc->rms = calloc((size_t)width * channels, sizeof(double));
    if (!wc->mins || !wc->maxs || !wc->rms)
        abort();
}

static void compute_direct(WaveformCache *wc, const AudioBuf *samples,
                           int view_start, int view_end, int width)
{
    int ch = samples->channels;
    int n_samples = view_end - view_start;
    for (int col = 0; col < width; col++) {
        int s = view_start + (int)((int64_t)col * n_samples / width);
        int e = view_start + (int)((int64_t)(col + 1) * n_samples / width);
        if (e <= s)
            e = s + 1;
        if (e > view_end)
            e = view_end;
        if (s >= e)
            continue;
        for (int c = 0; c < ch; c++) {
            double mn = 1e30, mx = -1e30, sum2 = 0.0;
            for (int f = s; f < e; f++) {
                double v = samples->data[f * ch + c];
                mn = KA_MIN(mn, v);
                mx = KA_MAX(mx, v);
                sum2 += v * v;
            }
            wc->mins[col * ch + c] = mn;
            wc->maxs[col * ch + c] = mx;
            wc->rms[col * ch + c] = sqrt(sum2 / (e - s));
        }
    }
}

static void compute_from_blocks(WaveformCache *wc, int view_start,
                                int view_end, int width)
{
    int ch = wc->block_channels;
    int bs = WF_BLOCK_SIZE;
    int block_start = view_start / bs;
    int block_end = KA_MIN(wc->n_blocks, (view_end + bs - 1) / bs);
    int n_blocks = block_end - block_start;
    if (n_blocks <= 0)
        return;

    for (int col = 0; col < width; col++) {
        int s = block_start + (int)((int64_t)col * n_blocks / width);
        int e = block_start + (int)((int64_t)(col + 1) * n_blocks / width);
        if (e <= s)
            e = s + 1;
        if (e > block_end)
            e = block_end;
        if (s >= e)
            continue;
        for (int c = 0; c < ch; c++) {
            double mn = 1e30, mx = -1e30, sum2 = 0.0;
            for (int b = s; b < e; b++) {
                mn = KA_MIN(mn, wc->block_min[b * ch + c]);
                mx = KA_MAX(mx, wc->block_max[b * ch + c]);
                double r = wc->block_rms[b * ch + c];
                sum2 += r * r;
            }
            wc->mins[col * ch + c] = mn;
            wc->maxs[col * ch + c] = mx;
            wc->rms[col * ch + c] = sqrt(sum2 / (e - s));
        }
    }
}

bool waveform_compute(WaveformCache *wc, const AudioBuf *samples,
                      int view_start, int view_end, int display_width)
{
    if (!samples || display_width < 1) {
        free_cols(wc);
        return false;
    }
    view_start = KA_MAX(0, view_start);
    view_end = KA_MIN(samples->frames, view_end);

    if (wc->view_start == view_start && wc->view_end == view_end &&
        wc->width == display_width && wc->mins)
        return false;

    wc->view_start = view_start;
    wc->view_end = view_end;
    wc->width = display_width;

    int n_samples = view_end - view_start;
    if (n_samples <= 0) {
        free_cols(wc);
        return false;
    }
    wc->channels = samples->channels;
    alloc_cols(wc, display_width, samples->channels);

    double samples_per_pixel = (double)n_samples / display_width;
    if (wc->block_min && samples_per_pixel > WF_BLOCK_SIZE * 2)
        compute_from_blocks(wc, view_start, view_end, display_width);
    else
        compute_direct(wc, samples, view_start, view_end, display_width);
    return true;
}

void waveform_invalidate(WaveformCache *wc)
{
    wc->view_start = -1;
    free_cols(wc);
}

bool waveform_valid(const WaveformCache *wc)
{
    return wc->mins != NULL;
}

void waveform_draw(const WaveformCache *wc, SDL_Surface *dst, KRect rect,
                   int sel_start_px, int sel_end_px, int cursor_px)
{
    int x0 = rect.x, y0 = rect.y, w = rect.w, h = rect.h;
    ksurf_fill(dst, rect, WF_BG_COLOR);
    if (!waveform_valid(wc) || w < 1)
        return;

    int channels = wc->channels;
    int lane_h = h / KA_MAX(1, channels);

    for (int ch = 0; ch < channels; ch++) {
        int lane_y = y0 + ch * lane_h;
        int mid_y = lane_y + lane_h / 2;

        ksurf_line(dst, x0, mid_y, x0 + w - 1, mid_y, CENTER_COLOR);

        for (int col = 0; col < KA_MIN(w, wc->width); col++) {
            double mn = wc->mins[col * channels + ch];
            double mx = wc->maxs[col * channels + ch];
            int py_top = mid_y - (int)(mx * (lane_h / 2));
            int py_bot = mid_y - (int)(mn * (lane_h / 2));
            if (py_top == py_bot)
                py_bot = py_top + 1;
            ksurf_line(dst, x0 + col, py_top, x0 + col, py_bot, WAVE_COLOR);
        }

        for (int col = 0; col < KA_MIN(w, wc->width); col++) {
            double r = wc->rms[col * channels + ch];
            int py_top = mid_y - (int)(r * (lane_h / 2));
            int py_bot = mid_y + (int)(r * (lane_h / 2));
            if (py_top == py_bot)
                py_bot = py_top + 1;
            ksurf_line(dst, x0 + col, py_top, x0 + col, py_bot,
                       WAVE_RMS_COLOR);
        }
    }

    /* Selection overlay: additive blue tint over the selected columns. */
    if (sel_start_px >= 0 && sel_end_px >= 0 && sel_start_px != sel_end_px) {
        int sx = KA_MAX(x0, x0 + sel_start_px);
        int ex = KA_MIN(x0 + w, x0 + sel_end_px);
        for (int x = sx; x < ex; x++)
            for (int y = y0; y < y0 + h; y++) {
                if (x < 0 || y < 0 || x >= dst->w || y >= dst->h)
                    continue;
                uint32_t *px =
                    (uint32_t *)((uint8_t *)dst->pixels + y * dst->pitch) +
                    x;
                uint8_t r, g, b;
                SDL_GetRGB(*px, dst->format, &r, &g, &b);
                r = (uint8_t)KA_MIN(255, r + SELECTION_COLOR.r);
                g = (uint8_t)KA_MIN(255, g + SELECTION_COLOR.g);
                b = (uint8_t)KA_MIN(255, b + SELECTION_COLOR.b);
                *px = SDL_MapRGB(dst->format, r, g, b);
            }
    }

    if (cursor_px >= 0 && cursor_px <= w) {
        int cx = KA_MIN(cursor_px, w - 1);
        ksurf_line(dst, x0 + cx, y0, x0 + cx, y0 + h - 1, CURSOR_COLOR);
    }
}

void waveform_draw_ruler(SDL_Surface *dst, KRect rect, int view_start,
                         int view_end, int sr)
{
    int x0 = rect.x, y0 = rect.y, w = rect.w, h = rect.h;
    ksurf_fill(dst, rect, RULER_BG);
    if (sr <= 0 || view_end <= view_start)
        return;

    double duration = (double)(view_end - view_start) / sr;
    double start_time = (double)view_start / sr;

    double tick_interval;
    if (duration < 0.1)
        tick_interval = 0.01;
    else if (duration < 1.0)
        tick_interval = 0.1;
    else if (duration < 10.0)
        tick_interval = 1.0;
    else if (duration < 60.0)
        tick_interval = 5.0;
    else if (duration < 300.0)
        tick_interval = 30.0;
    else
        tick_interval = 60.0;

    double t = (floor(start_time / tick_interval) + 1) * tick_interval;
    while (t < start_time + duration) {
        int px = (int)((t - start_time) / duration * w);
        if (px >= 0 && px < w) {
            ksurf_line(dst, x0 + px, y0 + h - 4, x0 + px, y0 + h - 1,
                       RULER_TICK);
            char label[32];
            if (tick_interval >= 1.0)
                snprintf(label, sizeof(label), "%d:%02d", (int)t / 60,
                         (int)t % 60);
            else
                snprintf(label, sizeof(label), "%.2f", t);
            font4x6_draw(dst, x0 + px + 2, y0 + h - 9, label, RULER_TEXT);
        }
        t += tick_interval;
    }
}
