/* Effects + generator tests, including empty-input edge cases.
 * Ports tests/test_editor_effects.py and test_effects_edge.py. */
#include "ktest.h"

#include "effects.h"

static AudioBuf make_ramp(int frames, int channels)
{
    AudioBuf b = abuf_alloc(frames, channels);
    for (int f = 0; f < frames; f++)
        for (int c = 0; c < channels; c++)
            b.data[f * channels + c] =
                0.5f * (float)f / (float)(frames > 1 ? frames - 1 : 1);
    return b;
}

static void test_amplify(void)
{
    AudioBuf in = abuf_alloc(4, 1);
    for (int i = 0; i < 4; i++)
        in.data[i] = 0.25f;
    AudioBuf out = fx_amplify(&in, 44100, 6.0206); /* ~x2 */
    ASSERT_NEAR(out.data[0], 0.5, 0.01);
    abuf_free(&out);
    /* Clips at 1.0 */
    out = fx_amplify(&in, 44100, 40.0);
    ASSERT_NEAR(out.data[0], 1.0, 1e-6);
    abuf_free(&out);
    abuf_free(&in);
}

static void test_normalize(void)
{
    AudioBuf in = abuf_alloc(4, 1);
    in.data[2] = 0.5f;
    AudioBuf out = fx_normalize(&in, 44100, 0.0);
    ASSERT_NEAR(out.data[2], 1.0, 1e-5);
    abuf_free(&out);
    abuf_free(&in);
}

static void test_normalize_empty(void)
{
    AudioBuf in = abuf_alloc(0, 1);
    AudioBuf out = fx_normalize(&in, 44100, 0.0);
    ASSERT_EQ_INT(out.frames, 0);
    abuf_free(&out);
    abuf_free(&in);
    /* Stereo empty */
    in = abuf_alloc(0, 2);
    out = fx_normalize(&in, 44100, 0.0);
    ASSERT_EQ_INT(out.frames, 0);
    abuf_free(&out);
    abuf_free(&in);
}

static void test_fade_in_out(void)
{
    AudioBuf in = abuf_alloc(100, 1);
    for (int i = 0; i < 100; i++)
        in.data[i] = 1.0f;
    AudioBuf out = fx_fade_in(&in, 100, 1.0); /* whole buffer at sr=100 */
    ASSERT_NEAR(out.data[0], 0.0, 1e-6);
    ASSERT_NEAR(out.data[99], 1.0, 1e-6);
    abuf_free(&out);
    out = fx_fade_out(&in, 100, 1.0);
    ASSERT_NEAR(out.data[0], 1.0, 1e-6);
    ASSERT_NEAR(out.data[99], 0.0, 1e-6);
    abuf_free(&out);
    abuf_free(&in);
}

static void test_fade_out_empty(void)
{
    AudioBuf in = abuf_alloc(0, 2);
    AudioBuf out = fx_fade_out(&in, 44100, 1.0);
    ASSERT_EQ_INT(out.frames, 0);
    ASSERT_EQ_INT(out.channels, 2);
    abuf_free(&out);
    abuf_free(&in);
}

static void test_invert_reverse(void)
{
    AudioBuf in = make_ramp(10, 2);
    AudioBuf inv = fx_invert(&in, 44100);
    ASSERT_NEAR(inv.data[19], -in.data[19], 1e-7);
    abuf_free(&inv);
    AudioBuf rev = fx_reverse(&in, 44100);
    ASSERT_NEAR(rev.data[0], in.data[18], 1e-7); /* frame 9 ch 0 */
    ASSERT_NEAR(rev.data[1], in.data[19], 1e-7);
    abuf_free(&rev);
    abuf_free(&in);
}

static void test_truncate_silence(void)
{
    /* 100 loud + 5000 silent + 100 loud @ sr=10000, min_silence 200ms
     * = 2000 samples kept of the silent run */
    int sr = 10000;
    AudioBuf in = abuf_alloc(5200, 1);
    for (int i = 0; i < 100; i++)
        in.data[i] = 0.5f;
    for (int i = 5100; i < 5200; i++)
        in.data[i] = 0.5f;
    AudioBuf out = fx_truncate_silence(&in, sr, -40.0, 200);
    ASSERT_EQ_INT(out.frames, 100 + 2000 + 100);
    abuf_free(&out);
    abuf_free(&in);

    /* All silence -> empty */
    in = abuf_alloc(1000, 1);
    out = fx_truncate_silence(&in, sr, -40.0, 200);
    ASSERT_EQ_INT(out.frames, 0);
    abuf_free(&out);
    abuf_free(&in);
}

static void test_compressor_empty(void)
{
    AudioBuf in = abuf_alloc(0, 1);
    AudioBuf out = fx_compressor(&in, 44100, -20.0, 4.0, 10.0, 0.0);
    ASSERT_EQ_INT(out.frames, 0);
    abuf_free(&out);
    abuf_free(&in);
}

static void test_compressor_reduces_loud(void)
{
    AudioBuf in = abuf_alloc(4410, 1);
    for (int i = 0; i < in.frames; i++)
        in.data[i] = 0.9f;
    AudioBuf out = fx_compressor(&in, 44100, -20.0, 4.0, 1.0, 0.0);
    /* Steady loud signal well above threshold gets attenuated */
    ASSERT_TRUE(out.data[4000] < 0.5f);
    abuf_free(&out);
    abuf_free(&in);
}

static void test_limiter(void)
{
    AudioBuf in = abuf_alloc(100, 1);
    for (int i = 0; i < 100; i++)
        in.data[i] = (i == 50) ? 1.0f : 0.1f;
    AudioBuf out = fx_limiter(&in, 44100, -6.0, 50.0);
    double threshold = pow(10.0, -6.0 / 20.0);
    ASSERT_TRUE(out.data[50] <= threshold + 1e-4);
    /* Signal below threshold before the peak is untouched */
    ASSERT_NEAR(out.data[10], 0.1, 1e-5);
    abuf_free(&out);
    abuf_free(&in);
}

static void test_change_pitch(void)
{
    AudioBuf in = make_ramp(1000, 1);
    /* +12 semitones halves the length */
    AudioBuf out = fx_change_pitch(&in, 44100, 12.0);
    ASSERT_EQ_INT(out.frames, 500);
    abuf_free(&out);
    /* ~0 semitones is a no-op copy */
    out = fx_change_pitch(&in, 44100, 0.001);
    ASSERT_EQ_INT(out.frames, 1000);
    abuf_free(&out);
    abuf_free(&in);
}

static void test_graphic_eq_flat_is_identity(void)
{
    AudioBuf in = make_ramp(64, 1);
    double gains[10] = {0};
    AudioBuf out = fx_graphic_eq(&in, 44100, gains);
    for (int i = 0; i < 64; i++)
        ASSERT_NEAR(out.data[i], in.data[i], 1e-6);
    abuf_free(&out);
    abuf_free(&in);
}

static void test_repair_short_buffer(void)
{
    AudioBuf in = make_ramp(3, 1);
    AudioBuf out = fx_repair(&in, 44100); /* < 4 frames: unchanged copy */
    ASSERT_EQ_INT(out.frames, 3);
    abuf_free(&out);
    abuf_free(&in);
}

static void test_reverb_produces_tail_energy(void)
{
    int sr = 44100;
    AudioBuf in = abuf_alloc(8000, 1);
    in.data[0] = 1.0f; /* impulse */
    AudioBuf out = fx_reverb(&in, sr, 0.5, 0.5, 0.5, 0.5);
    double tail = 0.0;
    for (int i = 4000; i < 8000; i++)
        tail += fabs(out.data[i]);
    ASSERT_TRUE(tail > 0.01);
    abuf_free(&out);
    abuf_free(&in);
}

/* --- Generators --- */

static void test_generate_tone(void)
{
    AudioBuf t = gen_tone(1000, 1.0, 100.0, 0.8, WAVE_SINE);
    ASSERT_EQ_INT(t.frames, 1000);
    ASSERT_EQ_INT(t.channels, 1);
    /* Peak amplitude bounded by the parameter (sampling never quite hits
     * the analytic sine peak at this f/sr ratio) */
    float peak = 0;
    for (int i = 0; i < t.frames; i++)
        peak = KA_MAX(peak, fabsf(t.data[i]));
    ASSERT_TRUE(peak <= 0.8f + 1e-6f && peak > 0.75f);
    /* ~100 zero crossings per 1000 samples at f=100, sr=1000?
     * f/sr = 0.1 cycles/sample -> 100 cycles -> ~200 crossings */
    int crossings = 0;
    for (int i = 1; i < t.frames; i++)
        if ((t.data[i - 1] >= 0) != (t.data[i] >= 0))
            crossings++;
    ASSERT_TRUE(crossings >= 190 && crossings <= 210);
    abuf_free(&t);
}

static void test_generate_silence_channels_zero_is_mono(void)
{
    AudioBuf s = gen_silence(44100, 0.1, 0);
    ASSERT_EQ_INT(s.channels, 1);
    ASSERT_EQ_INT(s.frames, 4410);
    for (int i = 0; i < s.frames; i++)
        ASSERT_TRUE(s.data[i] == 0.0f);
    abuf_free(&s);
}

static void test_generate_noise(void)
{
    AudioBuf n = gen_noise(44100, 0.1, 0.5, NOISE_WHITE);
    ASSERT_EQ_INT(n.frames, 4410);
    float peak = 0;
    for (int i = 0; i < n.frames; i++)
        peak = KA_MAX(peak, fabsf(n.data[i]));
    ASSERT_NEAR(peak, 0.5, 0.01); /* normalized then scaled */
    abuf_free(&n);
    n = gen_noise(44100, 0.1, 0.5, NOISE_PINK);
    ASSERT_EQ_INT(n.frames, 4410);
    abuf_free(&n);
    n = gen_noise(44100, 0.1, 0.5, NOISE_BROWN);
    ASSERT_EQ_INT(n.frames, 4410);
    abuf_free(&n);
}

static void test_generate_dtmf(void)
{
    AudioBuf d = gen_dtmf(8000, "12", 0.2, 0.05, 0.8);
    /* two tones with gaps */
    ASSERT_EQ_INT(d.frames, 2 * (1600 + 400));
    abuf_free(&d);
    /* Invalid chars produce empty output */
    d = gen_dtmf(8000, "xyz!", 0.2, 0.05, 0.8);
    ASSERT_EQ_INT(d.frames, 0);
    abuf_free(&d);
}

static void test_generate_chirp(void)
{
    AudioBuf c = gen_chirp(8000, 0.5, 100.0, 1000.0, 0.8);
    ASSERT_EQ_INT(c.frames, 4000);
    float peak = 0;
    for (int i = 0; i < c.frames; i++)
        peak = KA_MAX(peak, fabsf(c.data[i]));
    ASSERT_NEAR(peak, 0.8, 0.01);
    abuf_free(&c);
}

static void test_noise_reduction_short_input_unchanged(void)
{
    AudioBuf in = make_ramp(100, 1); /* < fft_size */
    AudioBuf out = fx_noise_reduction(&in, 44100, 12.0);
    for (int i = 0; i < 100; i++)
        ASSERT_NEAR(out.data[i], in.data[i], 1e-6);
    abuf_free(&out);
    abuf_free(&in);
}

int main(void)
{
    RUN(test_amplify);
    RUN(test_normalize);
    RUN(test_normalize_empty);
    RUN(test_fade_in_out);
    RUN(test_fade_out_empty);
    RUN(test_invert_reverse);
    RUN(test_truncate_silence);
    RUN(test_compressor_empty);
    RUN(test_compressor_reduces_loud);
    RUN(test_limiter);
    RUN(test_change_pitch);
    RUN(test_graphic_eq_flat_is_identity);
    RUN(test_repair_short_buffer);
    RUN(test_reverb_produces_tail_energy);
    RUN(test_generate_tone);
    RUN(test_generate_silence_channels_zero_is_mono);
    RUN(test_generate_noise);
    RUN(test_generate_dtmf);
    RUN(test_generate_chirp);
    RUN(test_noise_reduction_short_input_unchanged);
    return kt_summary("test_effects");
}
