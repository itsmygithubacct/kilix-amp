/* DSP primitive tests: FFT correctness, biquad peaking behavior, spectrum
 * analyzer bar/peak physics. Covers ground from test_audio_engine.py. */
#include "ktest.h"

#include <limits.h>

#include "dsp.h"
#include "spectrum.h"

static void test_fft_impulse_is_flat(void)
{
    /* FFT of a unit impulse has |X_k| == 1 for all k */
    float in[64] = {0};
    in[0] = 1.0f;
    double mag[33];
    fft_real_mag(in, 64, mag);
    for (int k = 0; k <= 32; k++)
        ASSERT_NEAR(mag[k], 1.0, 1e-9);
}

static void test_fft_sine_peaks_at_bin(void)
{
    /* Full-scale sine at bin 8 of a 64-point FFT: |X_8| = N/2 = 32 */
    float in[64];
    for (int i = 0; i < 64; i++)
        in[i] = (float)sin(2.0 * M_PI * 8 * i / 64);
    double mag[33];
    fft_real_mag(in, 64, mag);
    ASSERT_NEAR(mag[8], 32.0, 1e-6);
    ASSERT_NEAR(mag[7], 0.0, 1e-6);
    ASSERT_NEAR(mag[9], 0.0, 1e-6);
}

static void test_fft_roundtrip(void)
{
    double re[32], im[32], orig[32];
    for (int i = 0; i < 32; i++) {
        orig[i] = re[i] = sin(i * 0.7) + 0.3 * cos(i * 2.1);
        im[i] = 0.0;
    }
    fft_complex(re, im, 32, false);
    fft_complex(re, im, 32, true);
    for (int i = 0; i < 32; i++)
        ASSERT_NEAR(re[i], orig[i], 1e-9);
}

static void test_biquad_zero_gain_is_identity(void)
{
    Biquad bq = {0};
    biquad_design_peaking(&bq, 44100, 1000, 0.0, 1.0);
    for (int i = 0; i < 100; i++) {
        float x = (float)sin(i * 0.3);
        ASSERT_NEAR(biquad_process(&bq, x), x, 1e-6);
    }
}

static void test_biquad_boost_amplifies_center(void)
{
    /* +12dB at 1kHz should roughly quadruple a 1kHz sine's amplitude */
    Biquad bq = {0};
    biquad_design_peaking(&bq, 44100, 1000, 12.0, 1.0);
    double peak = 0;
    int n = 44100 / 2;
    for (int i = 0; i < n; i++) {
        float x = (float)sin(2.0 * M_PI * 1000 * i / 44100);
        float y = biquad_process(&bq, x);
        if (i > n / 2) /* skip transient */
            peak = KA_MAX(peak, fabs(y));
    }
    ASSERT_NEAR(peak, pow(10.0, 12.0 / 20.0), 0.15);
}

static void test_biquad_above_nyquist_is_identity(void)
{
    Biquad bq = {0};
    biquad_design_peaking(&bq, 22050, 15011, 12.0, 1.0);
    for (int i = 0; i < 4096; i++) {
        float x = i == 0 ? 1.0f : 0.0f;
        float y = biquad_process(&bq, x);
        ASSERT_TRUE(isfinite(y));
        ASSERT_NEAR(y, x, 1e-7);
    }
}

static void test_audio_buffer_dimension_boundaries(void)
{
    ASSERT_TRUE(abuf_dimensions_valid(0, 1));
    ASSERT_TRUE(abuf_dimensions_valid(1024, 2));
    ASSERT_TRUE(abuf_dimensions_valid(INT_MAX, 1));
    ASSERT_TRUE(abuf_dimensions_valid(INT_MAX / 2, 2));
    ASSERT_FALSE(abuf_dimensions_valid((int64_t)INT_MAX / 2 + 1, 2));
    ASSERT_FALSE(abuf_dimensions_valid(INT_MAX, 2));
    ASSERT_FALSE(abuf_dimensions_valid(-1, 1));
    ASSERT_FALSE(abuf_dimensions_valid(1, 0));
}

static void test_spectrum_bars_rise_and_fall(void)
{
    SpectrumAnalyzer sa;
    spectrum_init(&sa);
    float mags[SPEC_BARS];
    for (int i = 0; i < SPEC_BARS; i++)
        mags[i] = 1.0f;
    spectrum_update(&sa, mags, SPEC_BARS);
    ASSERT_NEAR(sa.bar_heights[0], SPEC_BAR_H, 1e-5);
    ASSERT_NEAR(sa.peak_heights[0], SPEC_BAR_H, 1e-5);

    /* Bars fall by fall_speed per update once input drops */
    for (int i = 0; i < SPEC_BARS; i++)
        mags[i] = 0.0f;
    spectrum_update(&sa, mags, SPEC_BARS);
    ASSERT_NEAR(sa.bar_heights[0], SPEC_BAR_H - 1.0, 1e-5);

    /* Peak holds for PEAK_HOLD_FRAMES low updates total (one already
     * consumed above), then gravity kicks in */
    for (int i = 0; i < PEAK_HOLD_FRAMES - 1; i++)
        spectrum_update(&sa, mags, SPEC_BARS);
    ASSERT_NEAR(sa.peak_heights[0], SPEC_BAR_H, 1e-5);
    spectrum_update(&sa, mags, SPEC_BARS);
    ASSERT_TRUE(sa.peak_heights[0] < SPEC_BAR_H);

    spectrum_clear(&sa);
    ASSERT_NEAR(sa.bar_heights[0], 0.0, 1e-9);
}

static void test_spectrum_resamples_input(void)
{
    SpectrumAnalyzer sa;
    spectrum_init(&sa);
    /* 150 input bands resampled down to 75 */
    float mags[150];
    for (int i = 0; i < 150; i++)
        mags[i] = 1.0f;
    spectrum_update(&sa, mags, 150);
    ASSERT_NEAR(sa.bar_heights[SPEC_BARS - 1], SPEC_BAR_H, 1e-5);
}

static void test_mode_cycling(void)
{
    SpectrumAnalyzer sa;
    spectrum_init(&sa);
    ASSERT_EQ_INT(sa.mode, VIS_MODE_SPECTRUM);
    spectrum_cycle_mode(&sa);
    ASSERT_EQ_INT(sa.mode, VIS_MODE_OSCILLOSCOPE);
    spectrum_cycle_mode(&sa);
    ASSERT_EQ_INT(sa.mode, VIS_MODE_OFF);
    spectrum_cycle_mode(&sa);
    ASSERT_EQ_INT(sa.mode, VIS_MODE_SPECTRUM);
}

int main(void)
{
    RUN(test_fft_impulse_is_flat);
    RUN(test_fft_sine_peaks_at_bin);
    RUN(test_fft_roundtrip);
    RUN(test_biquad_zero_gain_is_identity);
    RUN(test_biquad_boost_amplifies_center);
    RUN(test_biquad_above_nyquist_is_identity);
    RUN(test_audio_buffer_dimension_boundaries);
    RUN(test_spectrum_bars_rise_and_fall);
    RUN(test_spectrum_resamples_input);
    RUN(test_mode_cycling);
    return kt_summary("test_dsp");
}
