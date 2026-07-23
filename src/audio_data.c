#include "audio_data.h"

#include <sndfile.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

void audio_data_init(AudioData *ad)
{
    memset(ad, 0, sizeof(*ad));
    ad->sr = 44100;
    ad->filepath = ka_strdup("");
}

static void clear_stack(ADState *stack, int *count)
{
    for (int i = 0; i < *count; i++)
        abuf_free(&stack[i].samples);
    *count = 0;
}

void audio_data_destroy(AudioData *ad)
{
    abuf_free(&ad->samples);
    clear_stack(ad->undo_stack, &ad->undo_count);
    clear_stack(ad->redo_stack, &ad->redo_count);
    free(ad->filepath);
    ad->filepath = NULL;
}

int ad_channels(const AudioData *ad)
{
    return ad->loaded ? ad->samples.channels : 0;
}

int ad_num_samples(const AudioData *ad)
{
    return ad->loaded ? ad->samples.frames : 0;
}

double ad_duration(const AudioData *ad)
{
    if (!ad->loaded || ad->sr == 0)
        return 0.0;
    return (double)ad->samples.frames / ad->sr;
}

bool ad_has_selection(const AudioData *ad)
{
    return ad->sel_start != ad->sel_end;
}

void ad_set_cursor(AudioData *ad, int val)
{
    ad->cursor = KA_CLAMP(val, 0, ad_num_samples(ad));
}

static void emit_data(AudioData *ad)
{
    if (ad->data_changed)
        ad->data_changed(ad->ud);
}

static void emit_selection(AudioData *ad)
{
    if (ad->selection_changed)
        ad->selection_changed(ad->ud);
}

/* Adaptive undo limit based on data size. */
static int max_undo(const AudioData *ad)
{
    if (!ad->loaded)
        return 30;
    double size_mb = (double)abuf_samples(&ad->samples) * sizeof(float) /
                     (1024.0 * 1024.0);
    if (size_mb > 200)
        return 5;
    if (size_mb > 50)
        return 10;
    return 30;
}

bool ad_load(AudioData *ad, const char *filepath)
{
    SF_INFO info = {0};
    SNDFILE *sf = sf_open(filepath, SFM_READ, &info);
    if (!sf)
        return false;
    if (info.samplerate <= 0 ||
        !abuf_dimensions_valid((int64_t)info.frames, info.channels)) {
        sf_close(sf);
        return false;
    }
    AudioBuf buf = abuf_alloc((int)info.frames, info.channels);
    sf_count_t got = sf_readf_float(sf, buf.data, info.frames);
    sf_close(sf);
    if (got < info.frames)
        buf.frames = (int)KA_MAX(got, 0);

    abuf_free(&ad->samples);
    ad->samples = buf;
    ad->loaded = true;
    ad->sr = info.samplerate;
    free(ad->filepath);
    ad->filepath = ka_strdup(filepath);
    ad->sel_start = ad->sel_end = 0;
    ad->cursor = 0;
    clear_stack(ad->undo_stack, &ad->undo_count);
    clear_stack(ad->redo_stack, &ad->redo_count);
    emit_data(ad);
    emit_selection(ad);
    return true;
}

static int format_for_ext(const char *filepath)
{
    char *ext = ka_ext_lower(filepath);
    int fmt = 0;
    if (strcmp(ext, ".wav") == 0)
        fmt = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    else if (strcmp(ext, ".flac") == 0)
        fmt = SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
    else if (strcmp(ext, ".ogg") == 0 || strcmp(ext, ".oga") == 0)
        fmt = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
    else if (strcmp(ext, ".opus") == 0)
        fmt = SF_FORMAT_OGG | SF_FORMAT_OPUS;
    else if (strcmp(ext, ".aiff") == 0 || strcmp(ext, ".aif") == 0)
        fmt = SF_FORMAT_AIFF | SF_FORMAT_PCM_16;
    else if (strcmp(ext, ".mp3") == 0)
        fmt = SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III;
    else
        fmt = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    free(ext);
    return fmt;
}

bool ad_export(AudioData *ad, const char *filepath)
{
    if (!ad->loaded)
        return false;
    SF_INFO info = {0};
    info.samplerate = ad->sr;
    info.channels = ad->samples.channels;
    info.format = format_for_ext(filepath);
    SNDFILE *sf = sf_open(filepath, SFM_WRITE, &info);
    if (!sf)
        return false;
    /* Integer PCM subtypes can't represent values outside [-1, 1]; clip
     * unless writing a float subtype. */
    int subtype = info.format & SF_FORMAT_SUBMASK;
    bool is_float =
        subtype == SF_FORMAT_FLOAT || subtype == SF_FORMAT_DOUBLE;
    int n = abuf_samples(&ad->samples);
    float *out = ad->samples.data;
    float *clipped = NULL;
    if (!is_float) {
        clipped = malloc(sizeof(float) * (size_t)KA_MAX(n, 1));
        if (!clipped)
            abort();
        for (int i = 0; i < n; i++)
            clipped[i] = (float)KA_CLAMP(ad->samples.data[i], -1.0, 1.0);
        out = clipped;
    }
    sf_writef_float(sf, out, ad->samples.frames);
    sf_close(sf);
    free(clipped);
    return true;
}

void ad_set_selection(AudioData *ad, int start, int end)
{
    int n = ad_num_samples(ad);
    start = KA_CLAMP(start, 0, n);
    end = KA_CLAMP(end, 0, n);
    if (start > end) {
        int t = start;
        start = end;
        end = t;
    }
    ad->sel_start = start;
    ad->sel_end = end;
    emit_selection(ad);
}

void ad_clear_selection(AudioData *ad)
{
    ad->sel_start = ad->sel_end = 0;
    emit_selection(ad);
}

void ad_select_all(AudioData *ad)
{
    ad->sel_start = 0;
    ad->sel_end = ad_num_samples(ad);
    emit_selection(ad);
}

static void push_state(ADState *stack, int *count, int limit,
                       const AudioBuf *samples, int sel_start, int sel_end)
{
    if (*count == limit || *count == AD_MAX_UNDO) {
        /* Drop the oldest entry. */
        abuf_free(&stack[0].samples);
        memmove(&stack[0], &stack[1], sizeof(ADState) * (size_t)(*count - 1));
        (*count)--;
    }
    stack[*count].samples = abuf_copy(samples);
    stack[*count].sel_start = sel_start;
    stack[*count].sel_end = sel_end;
    (*count)++;
}

static void push_undo(AudioData *ad)
{
    if (!ad->loaded)
        return;
    push_state(ad->undo_stack, &ad->undo_count, max_undo(ad), &ad->samples,
               ad->sel_start, ad->sel_end);
    clear_stack(ad->redo_stack, &ad->redo_count);
}

bool ad_undo(AudioData *ad)
{
    if (ad->undo_count == 0 || !ad->loaded)
        return false;
    push_state(ad->redo_stack, &ad->redo_count, AD_MAX_UNDO, &ad->samples,
               ad->sel_start, ad->sel_end);
    ADState *s = &ad->undo_stack[--ad->undo_count];
    abuf_free(&ad->samples);
    ad->samples = s->samples;
    s->samples = (AudioBuf){0};
    ad->sel_start = s->sel_start;
    ad->sel_end = s->sel_end;
    emit_data(ad);
    emit_selection(ad);
    return true;
}

bool ad_redo(AudioData *ad)
{
    if (ad->redo_count == 0 || !ad->loaded)
        return false;
    push_state(ad->undo_stack, &ad->undo_count, AD_MAX_UNDO, &ad->samples,
               ad->sel_start, ad->sel_end);
    ADState *s = &ad->redo_stack[--ad->redo_count];
    abuf_free(&ad->samples);
    ad->samples = s->samples;
    s->samples = (AudioBuf){0};
    ad->sel_start = s->sel_start;
    ad->sel_end = s->sel_end;
    emit_data(ad);
    emit_selection(ad);
    return true;
}

bool ad_can_undo(const AudioData *ad) { return ad->undo_count > 0; }
bool ad_can_redo(const AudioData *ad) { return ad->redo_count > 0; }

/* Clamp cursor and selection into the current buffer range. */
static void reclamp(AudioData *ad)
{
    int n = ad_num_samples(ad);
    ad->cursor = KA_MIN(ad->cursor, n);
    ad->sel_start = KA_CLAMP(ad->sel_start, 0, n);
    ad->sel_end = KA_CLAMP(ad->sel_end, 0, n);
}

/* Slice [start,end) of the buffer as a view-copy. */
static AudioBuf slice_copy(const AudioBuf *b, int start, int end)
{
    AudioBuf out = abuf_alloc(end - start, b->channels);
    memcpy(out.data, b->data + (size_t)start * b->channels,
           sizeof(float) * (size_t)(end - start) * b->channels);
    return out;
}

static bool audio_buf_valid(const AudioBuf *b)
{
    return b && b->data && abuf_dimensions_valid(b->frames, b->channels);
}

/* Concatenate three frame ranges into a new buffer.  The sum is checked
 * before narrowing back to AudioBuf's int frame count. */
static bool splice3(const AudioBuf *pre, const AudioBuf *mid,
                    const AudioBuf *post, int channels, AudioBuf *result)
{
    *result = (AudioBuf){0};
    if (!audio_buf_valid(pre) || !audio_buf_valid(mid) ||
        !audio_buf_valid(post) || pre->channels != channels ||
        mid->channels != channels || post->channels != channels)
        return false;

    int64_t frames = (int64_t)pre->frames + mid->frames + post->frames;
    if (!abuf_dimensions_valid(frames, channels))
        return false;

    AudioBuf out = abuf_alloc((int)frames, channels);
    float *p = out.data;
    memcpy(p, pre->data, sizeof(float) * (size_t)pre->frames * channels);
    p += (size_t)pre->frames * channels;
    memcpy(p, mid->data, sizeof(float) * (size_t)mid->frames * channels);
    p += (size_t)mid->frames * channels;
    memcpy(p, post->data, sizeof(float) * (size_t)post->frames * channels);
    *result = out;
    return true;
}

void ad_apply_effect(AudioData *ad, EffectFn fn, void *params)
{
    if (!ad->loaded || !fn)
        return;

    int start, end;
    if (ad_has_selection(ad)) {
        start = ad->sel_start;
        end = ad->sel_end;
    } else {
        start = 0;
        end = ad_num_samples(ad);
    }

    AudioBuf region = slice_copy(&ad->samples, start, end);
    AudioBuf processed = fn(&region, ad->sr, params);
    abuf_free(&region);
    if (!audio_buf_valid(&processed) ||
        processed.channels != ad->samples.channels) {
        abuf_free(&processed);
        return;
    }

    if (processed.frames == end - start) {
        /* Same length - replace in-place */
        push_undo(ad);
        memcpy(ad->samples.data + (size_t)start * ad->samples.channels,
               processed.data,
               sizeof(float) * (size_t)processed.frames *
                   ad->samples.channels);
        abuf_free(&processed);
    } else {
        /* Different length - splice */
        AudioBuf pre = slice_copy(&ad->samples, 0, start);
        AudioBuf post =
            slice_copy(&ad->samples, end, ad->samples.frames);
        AudioBuf spliced;
        bool ok = splice3(&pre, &processed, &post, ad->samples.channels,
                          &spliced);
        int processed_end = ok ? start + processed.frames : start;
        abuf_free(&pre);
        abuf_free(&post);
        abuf_free(&processed);
        if (!ok)
            return;

        push_undo(ad);
        ad->sel_end = processed_end;
        abuf_free(&ad->samples);
        ad->samples = spliced;
    }

    reclamp(ad);
    emit_data(ad);
}

/* Coerce generated audio to match the buffer's channel count. */
static bool match_layout(const AudioBuf *gen, int channels, AudioBuf *result)
{
    *result = (AudioBuf){0};
    if (!audio_buf_valid(gen) ||
        !abuf_dimensions_valid(gen->frames, channels))
        return false;
    if (gen->channels == channels) {
        *result = abuf_copy(gen);
        return true;
    }
    AudioBuf out = abuf_alloc(gen->frames, channels);
    for (int f = 0; f < gen->frames; f++)
        for (int ch = 0; ch < channels; ch++) {
            if (gen->channels == 1) {
                /* mono generated -> replicate across channels */
                out.data[f * channels + ch] = gen->data[f];
            } else if (channels == 1) {
                /* downmix generated to mono */
                double sum = 0.0;
                for (int gc = 0; gc < gen->channels; gc++)
                    sum += gen->data[f * gen->channels + gc];
                out.data[f] = (float)(sum / gen->channels);
            } else {
                /* differing counts -> tile channels */
                out.data[f * channels + ch] =
                    gen->data[f * gen->channels + ch % gen->channels];
            }
        }
    *result = out;
    return true;
}

void ad_apply_generator(AudioData *ad, GeneratorFn fn, void *params)
{
    if (!fn)
        return;
    AudioBuf generated = fn(ad->sr, params);
    if (!audio_buf_valid(&generated)) {
        abuf_free(&generated);
        return;
    }

    /* A zero-length generation request is a successful no-op.  In
     * particular, invalid generator parameters return a valid empty buffer
     * and must not replace already-loaded audio. */
    if (generated.frames == 0) {
        abuf_free(&generated);
        return;
    }

    if (!ad->loaded) {
        abuf_free(&ad->samples);
        ad->samples = generated;
        ad->loaded = true;
        ad->sel_start = 0;
        ad->sel_end = generated.frames;
        emit_data(ad);
        emit_selection(ad);
        return;
    }

    bool replacing = ad_has_selection(ad);
    int start = replacing ? ad->sel_start : ad->cursor;
    int end = replacing ? ad->sel_end : ad->cursor;
    int64_t result_frames = (int64_t)start + generated.frames +
                            (ad->samples.frames - end);
    if (!abuf_dimensions_valid(result_frames, ad->samples.channels)) {
        abuf_free(&generated);
        return;
    }

    AudioBuf coerced;
    bool layout_ok = match_layout(&generated, ad->samples.channels,
                                  &coerced);
    abuf_free(&generated);
    if (!layout_ok)
        return;

    AudioBuf pre = slice_copy(&ad->samples, 0, start);
    AudioBuf post = slice_copy(&ad->samples, end, ad->samples.frames);
    AudioBuf spliced;
    bool splice_ok = splice3(&pre, &coerced, &post, ad->samples.channels,
                             &spliced);
    abuf_free(&pre);
    abuf_free(&post);
    if (!splice_ok) {
        abuf_free(&coerced);
        return;
    }

    int generated_end = start + coerced.frames;
    abuf_free(&coerced);

    push_undo(ad);
    if (replacing)
        ad->sel_end = generated_end;
    else
        ad->cursor = generated_end;
    abuf_free(&ad->samples);
    ad->samples = spliced;

    reclamp(ad);
    emit_data(ad);
}
