#include "spectrum.h"

#include <string.h>

#include "render.h"

void spectrum_init(SpectrumAnalyzer *sa)
{
    memset(sa, 0, sizeof(*sa));
    sa->mode = VIS_MODE_SPECTRUM;
    memcpy(sa->viscolors, DEFAULT_VISCOLORS, sizeof(sa->viscolors));
    sa->fall_speed = 1.0f;
}

void spectrum_set_viscolors(SpectrumAnalyzer *sa,
                            const KColor colors[VISCOLOR_COUNT])
{
    memcpy(sa->viscolors, colors, sizeof(sa->viscolors));
}

void spectrum_cycle_mode(SpectrumAnalyzer *sa)
{
    sa->mode = (sa->mode + 1) % VIS_NUM_MODES;
}

/* Linear resample of src[0..n) to dst[0..m). */
static void resample_lin(const float *src, int n, float *dst, int m)
{
    for (int i = 0; i < m; i++) {
        double pos = m > 1 ? (double)i * (n - 1) / (m - 1) : 0.0;
        int i0 = (int)pos;
        int i1 = KA_MIN(i0 + 1, n - 1);
        double t = pos - i0;
        dst[i] = (float)(src[i0] * (1.0 - t) + src[i1] * t);
    }
}

void spectrum_update(SpectrumAnalyzer *sa, const float *magnitudes, int n)
{
    if (n == 0)
        return;
    float mags[SPEC_BARS];
    if (n != SPEC_BARS)
        resample_lin(magnitudes, n, mags, SPEC_BARS);
    else
        memcpy(mags, magnitudes, sizeof(mags));

    for (int i = 0; i < SPEC_BARS; i++) {
        float m = KA_CLAMP(mags[i], 0.0f, 1.0f);
        float target = m * SPEC_BAR_H;
        /* Smooth fall-off for bars */
        if (target >= sa->bar_heights[i])
            sa->bar_heights[i] = target;
        else
            sa->bar_heights[i] =
                KA_MAX(target, sa->bar_heights[i] - sa->fall_speed);

        /* Peaks with hold + gravity */
        if (sa->bar_heights[i] >= sa->peak_heights[i]) {
            sa->peak_heights[i] = sa->bar_heights[i];
            sa->peak_velocities[i] = 0;
            sa->peak_hold[i] = PEAK_HOLD_FRAMES;
        } else if (sa->peak_hold[i] > 0) {
            sa->peak_hold[i]--;
        } else {
            sa->peak_velocities[i] += PEAK_GRAVITY;
            sa->peak_heights[i] -= sa->peak_velocities[i];
            if (sa->peak_heights[i] < 0)
                sa->peak_heights[i] = 0;
        }
    }
}

void spectrum_update_waveform(SpectrumAnalyzer *sa, const float *samples,
                              int n)
{
    if (n == 0)
        return;
    float buf[SPEC_AREA_W];
    if (n != SPEC_AREA_W)
        resample_lin(samples, n, buf, SPEC_AREA_W);
    else
        memcpy(buf, samples, sizeof(buf));
    for (int i = 0; i < SPEC_AREA_W; i++)
        sa->waveform[i] = KA_CLAMP(buf[i], -1.0f, 1.0f);
}

static void render_spectrum(SpectrumAnalyzer *sa, SDL_Surface *dst, int dx,
                            int dy)
{
    for (int i = 0; i < SPEC_BARS && i < SPEC_AREA_W; i++) {
        int bar_h = (int)sa->bar_heights[i];
        int peak_h = (int)sa->peak_heights[i];

        for (int y = 0; y < bar_h; y++) {
            /* Color gradient: colors 2-16 mapped bottom (low) to top. */
            int color_idx =
                KA_CLAMP(2 + (int)((double)y / SPEC_BAR_H * 14), 2, 16);
            ksurf_pixel(dst, dx + i, dy + SPEC_AREA_H - 1 - y,
                        sa->viscolors[color_idx]);
        }

        if (peak_h > 0 && peak_h < SPEC_AREA_H) {
            int py = SPEC_AREA_H - 1 - KA_MIN(peak_h, SPEC_AREA_H - 1);
            ksurf_pixel(dst, dx + i, dy + py, sa->viscolors[23]);
        }
    }
}

static void render_oscilloscope(SpectrumAnalyzer *sa, SDL_Surface *dst,
                                int dx, int dy)
{
    int center = SPEC_AREA_H / 2;
    for (int x = 0; x < SPEC_AREA_W; x++) {
        int y = center - (int)(sa->waveform[x] * (center - 1));
        y = KA_CLAMP(y, 0, SPEC_AREA_H - 1);
        /* Color by distance from center (colors 18-22). */
        int dist = abs(y - center);
        int color_idx = 18;
        if (center > 0)
            color_idx += KA_MIN(4, dist / KA_MAX(1, center / 5));
        ksurf_pixel(dst, dx + x, dy + y, sa->viscolors[color_idx]);
    }
}

void spectrum_render(SpectrumAnalyzer *sa, SDL_Surface *dst, int dx, int dy)
{
    ksurf_fill(dst, KRECT(dx, dy, SPEC_AREA_W, SPEC_AREA_H),
               sa->viscolors[0]);
    if (sa->mode == VIS_MODE_SPECTRUM)
        render_spectrum(sa, dst, dx, dy);
    else if (sa->mode == VIS_MODE_OSCILLOSCOPE)
        render_oscilloscope(sa, dst, dx, dy);
    /* VIS_MODE_OFF: just background */
}

void spectrum_clear(SpectrumAnalyzer *sa)
{
    memset(sa->bar_heights, 0, sizeof(sa->bar_heights));
    memset(sa->peak_heights, 0, sizeof(sa->peak_heights));
    memset(sa->peak_velocities, 0, sizeof(sa->peak_velocities));
    memset(sa->peak_hold, 0, sizeof(sa->peak_hold));
    memset(sa->waveform, 0, sizeof(sa->waveform));
}
