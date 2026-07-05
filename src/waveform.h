/* Waveform display rendering: per-pixel min/max/rms cache and drawing.
 * Port of nixamp lib/waveform_renderer.py. */
#ifndef KA_WAVEFORM_H
#define KA_WAVEFORM_H

#include <SDL.h>

#include "dsp.h"

#define WF_BLOCK_SIZE 100

typedef struct {
    /* Per-pixel-column stats, [width][channels] flattened. */
    double *mins, *maxs, *rms;
    int view_start, view_end;
    int width;
    int channels;
    /* Pre-decimated blocks for fast zoomed-out views. */
    double *block_min, *block_max, *block_rms;
    int n_blocks;
    int block_channels;
} WaveformCache;

void waveform_cache_init(WaveformCache *wc);
void waveform_cache_destroy(WaveformCache *wc);
/* Pre-decimate audio into blocks for fast zoomed-out rendering. */
void waveform_precompute_blocks(WaveformCache *wc, const AudioBuf *samples);
/* Compute per-pixel min/max/rms for the visible range; true if recomputed. */
bool waveform_compute(WaveformCache *wc, const AudioBuf *samples,
                      int view_start, int view_end, int display_width);
void waveform_invalidate(WaveformCache *wc);
bool waveform_valid(const WaveformCache *wc);

/* Draw waveform into dst at rect; sel/cursor in pixels (-1 = none). */
void waveform_draw(const WaveformCache *wc, SDL_Surface *dst, KRect rect,
                   int sel_start_px, int sel_end_px, int cursor_px);
/* Draw time ruler with tick marks and labels. */
void waveform_draw_ruler(SDL_Surface *dst, KRect rect, int view_start,
                         int view_end, int sr);

#endif
