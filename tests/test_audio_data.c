/* AudioData model tests: selection, undo/redo, effect/generator
 * application, export. Ports tests/test_audio_data.py and
 * test_editor_model.py. */
#include "ktest.h"

#include <limits.h>
#include <sndfile.h>

#include "audio_data.h"
#include "effects.h"

static char *g_dir;

static char *make_wav(const char *name, int frames, int channels)
{
    char *path = ka_path_join(g_dir, name);
    SF_INFO info = {0};
    info.samplerate = 44100;
    info.channels = channels;
    info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    SNDFILE *sf = sf_open(path, SFM_WRITE, &info);
    float *data = calloc((size_t)frames * channels, sizeof(float));
    for (int i = 0; i < frames * channels; i++)
        data[i] = 0.25f;
    sf_writef_float(sf, data, frames);
    sf_close(sf);
    free(data);
    return path;
}

static AudioData make_loaded(void)
{
    AudioData ad;
    audio_data_init(&ad);
    char *wav = make_wav("test.wav", 1000, 1);
    if (!ad_load(&ad, wav)) {
        printf("    FATAL: could not load test wav\n");
        exit(1);
    }
    free(wav);
    return ad;
}

static void test_load(void)
{
    AudioData ad = make_loaded();
    ASSERT_EQ_INT(ad_num_samples(&ad), 1000);
    ASSERT_EQ_INT(ad_channels(&ad), 1);
    ASSERT_EQ_INT(ad.sr, 44100);
    ASSERT_NEAR(ad_duration(&ad), 1000.0 / 44100.0, 1e-6);
    audio_data_destroy(&ad);

    AudioData ad2;
    audio_data_init(&ad2);
    ASSERT_FALSE(ad_load(&ad2, "/nonexistent.wav"));
    audio_data_destroy(&ad2);
}

static void test_selection(void)
{
    AudioData ad = make_loaded();
    ad_set_selection(&ad, 100, 200);
    ASSERT_EQ_INT(ad.sel_start, 100);
    ASSERT_EQ_INT(ad.sel_end, 200);
    ASSERT_TRUE(ad_has_selection(&ad));
    /* Reversed swaps */
    ad_set_selection(&ad, 300, 100);
    ASSERT_EQ_INT(ad.sel_start, 100);
    ASSERT_EQ_INT(ad.sel_end, 300);
    /* Clamped to buffer */
    ad_set_selection(&ad, -50, 99999);
    ASSERT_EQ_INT(ad.sel_start, 0);
    ASSERT_EQ_INT(ad.sel_end, 1000);
    ad_clear_selection(&ad);
    ASSERT_FALSE(ad_has_selection(&ad));
    ad_select_all(&ad);
    ASSERT_EQ_INT(ad.sel_end, 1000);
    audio_data_destroy(&ad);
}

static AudioBuf fx_double(const AudioBuf *in, int sr, void *params)
{
    (void)sr;
    (void)params;
    AudioBuf out = abuf_copy(in);
    for (int i = 0; i < abuf_samples(&out); i++)
        out.data[i] *= 2.0f;
    return out;
}

/* Length-changing effect: keep first half. */
static AudioBuf fx_halve(const AudioBuf *in, int sr, void *params)
{
    (void)sr;
    (void)params;
    AudioBuf out = abuf_alloc(in->frames / 2, in->channels);
    memcpy(out.data, in->data,
           sizeof(float) * (size_t)abuf_samples(&out));
    return out;
}

static AudioBuf fx_invalid_dimensions(const AudioBuf *in, int sr,
                                      void *params)
{
    (void)in;
    (void)sr;
    (void)params;
    AudioBuf out = {malloc(sizeof(float)), -1, 1};
    return out;
}

static AudioBuf fx_wrong_channels(const AudioBuf *in, int sr, void *params)
{
    (void)sr;
    (void)params;
    return abuf_alloc(in->frames, in->channels + 1);
}

static AudioBuf fx_oversized_splice(const AudioBuf *in, int sr, void *params)
{
    (void)in;
    (void)sr;
    (void)params;
    AudioBuf out = {malloc(sizeof(float)), INT_MAX, 1};
    return out;
}

static void test_apply_effect_full_and_undo(void)
{
    AudioData ad = make_loaded();
    ASSERT_FALSE(ad_can_undo(&ad));
    ad_apply_effect(&ad, fx_double, NULL);
    ASSERT_NEAR(ad.samples.data[0], 0.5, 1e-6);
    ASSERT_TRUE(ad_can_undo(&ad));

    ASSERT_TRUE(ad_undo(&ad));
    ASSERT_NEAR(ad.samples.data[0], 0.25, 1e-6);
    ASSERT_TRUE(ad_can_redo(&ad));

    ASSERT_TRUE(ad_redo(&ad));
    ASSERT_NEAR(ad.samples.data[0], 0.5, 1e-6);

    /* Redo stack cleared on new edit */
    ad_undo(&ad);
    ad_apply_effect(&ad, fx_double, NULL);
    ASSERT_FALSE(ad_can_redo(&ad));
    audio_data_destroy(&ad);
}

static void test_undo_redo_empty(void)
{
    AudioData ad;
    audio_data_init(&ad);
    ASSERT_FALSE(ad_undo(&ad));
    ASSERT_FALSE(ad_redo(&ad));
    audio_data_destroy(&ad);
}

static void test_apply_to_selection(void)
{
    AudioData ad = make_loaded();
    ad_set_selection(&ad, 100, 200);
    ad_apply_effect(&ad, fx_double, NULL);
    ASSERT_NEAR(ad.samples.data[50], 0.25, 1e-6);  /* outside */
    ASSERT_NEAR(ad.samples.data[150], 0.5, 1e-6);  /* inside */
    ASSERT_NEAR(ad.samples.data[250], 0.25, 1e-6); /* outside */
    ASSERT_EQ_INT(ad_num_samples(&ad), 1000);
    audio_data_destroy(&ad);
}

static void test_apply_length_changing_effect(void)
{
    AudioData ad = make_loaded();
    ad_set_selection(&ad, 0, 400);
    ad_apply_effect(&ad, fx_halve, NULL);
    ASSERT_EQ_INT(ad_num_samples(&ad), 800); /* 400 -> 200, spliced */
    ASSERT_EQ_INT(ad.sel_end, 200);          /* selection tracks result */
    audio_data_destroy(&ad);
}

static void test_invalid_effect_outputs_are_ignored(void)
{
    AudioData ad = make_loaded();
    float original = ad.samples.data[0];

    ad_apply_effect(&ad, fx_invalid_dimensions, NULL);
    ASSERT_EQ_INT(ad_num_samples(&ad), 1000);
    ASSERT_NEAR(ad.samples.data[0], original, 1e-7);
    ASSERT_FALSE(ad_can_undo(&ad));

    ad_apply_effect(&ad, fx_wrong_channels, NULL);
    ASSERT_EQ_INT(ad_num_samples(&ad), 1000);
    ASSERT_NEAR(ad.samples.data[0], original, 1e-7);
    ASSERT_FALSE(ad_can_undo(&ad));

    ad_set_selection(&ad, 0, 1);
    ad_apply_effect(&ad, fx_oversized_splice, NULL);
    ASSERT_EQ_INT(ad_num_samples(&ad), 1000);
    ASSERT_NEAR(ad.samples.data[0], original, 1e-7);
    ASSERT_FALSE(ad_can_undo(&ad));
    audio_data_destroy(&ad);
}

static AudioBuf gen_100(int sr, void *params)
{
    (void)sr;
    (void)params;
    AudioBuf b = abuf_alloc(100, 1);
    for (int i = 0; i < 100; i++)
        b.data[i] = 0.75f;
    return b;
}

static AudioBuf gen_empty(int sr, void *params)
{
    (void)sr;
    (void)params;
    return abuf_alloc(0, 1);
}

static AudioBuf gen_invalid(int sr, void *params)
{
    (void)sr;
    (void)params;
    return (AudioBuf){.frames = 1, .channels = 1};
}

static AudioBuf gen_oversized(int sr, void *params)
{
    (void)sr;
    (void)params;
    AudioBuf out = {malloc(sizeof(float)), INT_MAX, 1};
    return out;
}

static void test_generate_into_empty(void)
{
    AudioData ad;
    audio_data_init(&ad);
    ad_apply_generator(&ad, gen_100, NULL);
    ASSERT_TRUE(ad.loaded);
    ASSERT_EQ_INT(ad_num_samples(&ad), 100);
    ASSERT_EQ_INT(ad.sel_end, 100);
    audio_data_destroy(&ad);
}

static void test_generate_insert_at_cursor(void)
{
    AudioData ad = make_loaded();
    ad_set_cursor(&ad, 500);
    ad_apply_generator(&ad, gen_100, NULL);
    ASSERT_EQ_INT(ad_num_samples(&ad), 1100);
    ASSERT_EQ_INT(ad.cursor, 600);
    ASSERT_NEAR(ad.samples.data[550], 0.75, 1e-6);
    ASSERT_NEAR(ad.samples.data[499], 0.25, 1e-6);
    ASSERT_NEAR(ad.samples.data[601], 0.25, 1e-6);
    audio_data_destroy(&ad);
}

static void test_generate_replace_selection(void)
{
    AudioData ad = make_loaded();
    ad_set_selection(&ad, 100, 400);
    ad_apply_generator(&ad, gen_100, NULL);
    ASSERT_EQ_INT(ad_num_samples(&ad), 800); /* 1000 - 300 + 100 */
    ASSERT_EQ_INT(ad.sel_end, 200);
    ASSERT_NEAR(ad.samples.data[150], 0.75, 1e-6);
    audio_data_destroy(&ad);
}

static void test_invalid_generators_do_not_replace_audio(void)
{
    AudioData ad = make_loaded();
    float original = ad.samples.data[0];

    ad_apply_generator(&ad, gen_empty, NULL);
    ASSERT_EQ_INT(ad_num_samples(&ad), 1000);
    ASSERT_NEAR(ad.samples.data[0], original, 1e-7);
    ASSERT_FALSE(ad_can_undo(&ad));

    ad_apply_generator(&ad, gen_invalid, NULL);
    ASSERT_EQ_INT(ad_num_samples(&ad), 1000);
    ASSERT_NEAR(ad.samples.data[0], original, 1e-7);
    ASSERT_FALSE(ad_can_undo(&ad));

    ad_set_cursor(&ad, 1);
    ad_apply_generator(&ad, gen_oversized, NULL);
    ASSERT_EQ_INT(ad_num_samples(&ad), 1000);
    ASSERT_NEAR(ad.samples.data[0], original, 1e-7);
    ASSERT_FALSE(ad_can_undo(&ad));
    audio_data_destroy(&ad);
}

static void test_export_roundtrip(void)
{
    AudioData ad = make_loaded();
    char *out = ka_path_join(g_dir, "exported.wav");
    ASSERT_TRUE(ad_export(&ad, out));

    AudioData ad2;
    audio_data_init(&ad2);
    ASSERT_TRUE(ad_load(&ad2, out));
    ASSERT_EQ_INT(ad_num_samples(&ad2), 1000);
    ASSERT_NEAR(ad2.samples.data[0], 0.25, 0.001);
    audio_data_destroy(&ad2);
    audio_data_destroy(&ad);
    free(out);

    /* Export with nothing loaded fails */
    AudioData empty;
    audio_data_init(&empty);
    char *out2 = ka_path_join(g_dir, "nothing.wav");
    ASSERT_FALSE(ad_export(&empty, out2));
    free(out2);
    audio_data_destroy(&empty);
}

static void test_stereo_generator_layout_coercion(void)
{
    AudioData ad;
    audio_data_init(&ad);
    char *wav = make_wav("stereo.wav", 500, 2);
    ASSERT_TRUE(ad_load(&ad, wav));
    free(wav);
    ASSERT_EQ_INT(ad_channels(&ad), 2);
    /* Mono generated content is replicated across channels */
    ad_set_cursor(&ad, 0);
    ad_apply_generator(&ad, gen_100, NULL);
    ASSERT_EQ_INT(ad_num_samples(&ad), 600);
    ASSERT_NEAR(ad.samples.data[0], 0.75, 1e-6); /* L */
    ASSERT_NEAR(ad.samples.data[1], 0.75, 1e-6); /* R */
    audio_data_destroy(&ad);
}

int main(void)
{
    g_dir = kt_tmpdir();
    RUN(test_load);
    RUN(test_selection);
    RUN(test_apply_effect_full_and_undo);
    RUN(test_undo_redo_empty);
    RUN(test_apply_to_selection);
    RUN(test_apply_length_changing_effect);
    RUN(test_invalid_effect_outputs_are_ignored);
    RUN(test_generate_into_empty);
    RUN(test_generate_insert_at_cursor);
    RUN(test_generate_replace_selection);
    RUN(test_invalid_generators_do_not_replace_audio);
    RUN(test_export_roundtrip);
    RUN(test_stereo_generator_layout_coercion);
    return kt_summary("test_audio_data");
}
