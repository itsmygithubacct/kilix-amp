/* Spectrum analyzer and oscilloscope visualization.
 * Port of nixamp lib/spectrum.py (renders into an SDL surface). */
#ifndef KA_SPECTRUM_H
#define KA_SPECTRUM_H

#include <SDL.h>

#include "consts.h"

enum {
    VIS_MODE_SPECTRUM = 0,
    VIS_MODE_OSCILLOSCOPE = 1,
    VIS_MODE_OFF = 2,
    VIS_NUM_MODES = 3,
};

typedef struct {
    int mode;
    KColor viscolors[VISCOLOR_COUNT];
    float bar_heights[SPEC_BARS];
    float peak_heights[SPEC_BARS];
    float peak_velocities[SPEC_BARS];
    int peak_hold[SPEC_BARS];
    float waveform[SPEC_AREA_W];
    float fall_speed;
} SpectrumAnalyzer;

void spectrum_init(SpectrumAnalyzer *sa);
void spectrum_set_viscolors(SpectrumAnalyzer *sa,
                            const KColor colors[VISCOLOR_COUNT]);
void spectrum_cycle_mode(SpectrumAnalyzer *sa);
/* New FFT magnitude data (0..1); resampled to SPEC_BARS if n differs. */
void spectrum_update(SpectrumAnalyzer *sa, const float *magnitudes, int n);
/* Oscilloscope waveform data (-1..1); resampled to SPEC_AREA_W. */
void spectrum_update_waveform(SpectrumAnalyzer *sa, const float *samples,
                              int n);
/* Draw the current visualization into dst at (dx, dy). */
void spectrum_render(SpectrumAnalyzer *sa, SDL_Surface *dst, int dx, int dy);
void spectrum_clear(SpectrumAnalyzer *sa);

#endif
