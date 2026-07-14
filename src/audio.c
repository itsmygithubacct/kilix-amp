#include "audio.h"

#include <SDL.h>
#include <errno.h>
#include <fluidsynth.h>
#include <limits.h>
#include <math.h>
#include <sndfile.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dsp.h"

#define EQ_BANDS 10
#define OUT_CHANNELS 2
#define FEED_CHUNK_FRAMES 2048
#define QUEUE_TARGET_SEC 0.25
#define SPECTRUM_FFT 1024
#define SPECTRUM_INTERVAL_MS 50
#define POSITION_INTERVAL_MS 100
#define MIDI_RENDER_RATE 44100

/* Same centers as gst equalizer-10bands. */
static const double EQ_FREQS[EQ_BANDS] = {29,   59,   119,  237,  474,
                                          947,  1889, 3770, 7523, 15011};

struct AudioEngine {
    AudioCallbacks cbs;

    SNDFILE *sf;
    SF_INFO info;
    char *current_file;
    char state[16]; /* "stopped" | "playing" | "paused" */
    bool is_midi;
    AudioBuf midi_pcm;  /* Interleaved stereo float render. */
    int64_t midi_pos;   /* Next rendered frame to queue. */

    SDL_AudioDeviceID dev;
    int dev_rate;

    /* Processing chain state */
    double volume;  /* 0..1 */
    double pan;     /* -1..1 */
    double preamp;  /* linear gain */
    double eq_gains[EQ_BANDS];
    bool eq_enabled;
    Biquad eq[EQ_BANDS][OUT_CHANNELS];

    /* Position accounting, in output frames */
    int64_t fed_frames;   /* frames pushed to SDL since load/seek base */
    bool file_exhausted;

    /* Spectrum tap: ring of recent post-chain mono samples */
    float spec_ring[SPECTRUM_FFT];
    int spec_pos;
    uint32_t last_spectrum_ms;
    uint32_t last_position_ms;
};

static void emit_state(AudioEngine *ae, const char *state)
{
    snprintf(ae->state, sizeof(ae->state), "%s", state);
    if (ae->cbs.state_changed)
        ae->cbs.state_changed(ae->cbs.ud, state);
}

static void emit_error(AudioEngine *ae, const char *msg)
{
    if (ae->cbs.error)
        ae->cbs.error(ae->cbs.ud, msg);
}

static void redesign_band(AudioEngine *ae, int band)
{
    double fs = ae->info.samplerate > 0 ? ae->info.samplerate : 44100;
    double gain = ae->eq_enabled ? ae->eq_gains[band] : 0.0;
    for (int ch = 0; ch < OUT_CHANNELS; ch++) {
        double x1 = ae->eq[band][ch].x1, x2 = ae->eq[band][ch].x2;
        double y1 = ae->eq[band][ch].y1, y2 = ae->eq[band][ch].y2;
        biquad_design_peaking(&ae->eq[band][ch], fs, EQ_FREQS[band], gain,
                              1.0);
        ae->eq[band][ch].x1 = x1;
        ae->eq[band][ch].x2 = x2;
        ae->eq[band][ch].y1 = y1;
        ae->eq[band][ch].y2 = y2;
    }
}

static void redesign_all(AudioEngine *ae)
{
    for (int b = 0; b < EQ_BANDS; b++) {
        redesign_band(ae, b);
        for (int ch = 0; ch < OUT_CHANNELS; ch++)
            biquad_reset(&ae->eq[b][ch]);
    }
}

AudioEngine *audio_new(void)
{
    AudioEngine *ae = calloc(1, sizeof(*ae));
    if (!ae)
        abort();
    snprintf(ae->state, sizeof(ae->state), "stopped");
    ae->volume = 1.0;
    ae->preamp = 1.0;
    ae->eq_enabled = true;
    ae->info.samplerate = 44100;
    redesign_all(ae);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        fprintf(stderr, "kilix-amp: SDL audio init failed: %s\n",
                SDL_GetError());
    return ae;
}

void audio_set_callbacks(AudioEngine *ae, const AudioCallbacks *cbs)
{
    ae->cbs = *cbs;
}

static void close_device(AudioEngine *ae)
{
    if (ae->dev) {
        SDL_CloseAudioDevice(ae->dev);
        ae->dev = 0;
    }
}

static bool open_device(AudioEngine *ae, int rate)
{
    if (ae->dev && ae->dev_rate == rate)
        return true;
    close_device(ae);
    SDL_AudioSpec want = {0};
    want.freq = rate;
    want.format = AUDIO_F32SYS;
    want.channels = OUT_CHANNELS;
    want.samples = 1024;
    ae->dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (!ae->dev) {
        emit_error(ae, SDL_GetError());
        return false;
    }
    ae->dev_rate = rate;
    return true;
}

static void close_file(AudioEngine *ae)
{
    if (ae->sf) {
        sf_close(ae->sf);
        ae->sf = NULL;
    }
}

static void clear_midi(AudioEngine *ae)
{
    abuf_free(&ae->midi_pcm);
    ae->is_midi = false;
    ae->midi_pos = 0;
}

static bool is_midi_path(const char *path)
{
    char *ext = ka_ext_lower(path);
    bool ok = strcmp(ext, ".mid") == 0 || strcmp(ext, ".midi") == 0;
    free(ext);
    return ok;
}

static char *find_soundfont(void)
{
    const char *env = getenv("KILIX_AMP_SOUNDFONT");
    if (env && *env && ka_is_file(env))
        return ka_strdup(env);

    static const char *paths[] = {
        "/usr/share/sounds/sf2/FluidR3_GM.sf2",
        "/usr/share/sounds/sf2/TimGM6mb.sf2",
        "/usr/share/sounds/sf2/default-GM.sf2",
        "/usr/share/soundfonts/FluidR3_GM.sf2",
        "/usr/share/soundfonts/TimGM6mb.sf2",
        "/usr/share/soundfonts/default-GM.sf2",
    };
    for (size_t i = 0; i < KA_LEN(paths); i++)
        if (ka_is_file(paths[i]))
            return ka_strdup(paths[i]);
    return NULL;
}

static char *temp_wav_path(void)
{
    const char *tmp = getenv("TMPDIR");
    if (!tmp || !*tmp)
        tmp = "/tmp";
    char *tmpl = ka_asprintf("%s/kilix-amp-midi-XXXXXX.wav", tmp);
    int fd = mkstemps(tmpl, 4);
    if (fd < 0) {
        free(tmpl);
        return NULL;
    }
    close(fd);
    return tmpl;
}

static bool render_midi_to_wav(const char *midi_path, char **wav_path,
                               char **err)
{
    *wav_path = NULL;
    *err = NULL;

    char *sf2 = find_soundfont();
    if (!sf2) {
        *err = ka_strdup("No SoundFont found for MIDI playback");
        return false;
    }

    char *out = temp_wav_path();
    if (!out) {
        *err = ka_asprintf("Cannot create MIDI temp file: %s",
                           strerror(errno));
        free(sf2);
        return false;
    }

    fluid_settings_t *settings = new_fluid_settings();
    fluid_synth_t *synth = NULL;
    fluid_player_t *player = NULL;
    fluid_file_renderer_t *renderer = NULL;
    bool ok = false;

    if (!settings) {
        *err = ka_strdup("Cannot initialize FluidSynth settings");
        goto done;
    }

    fluid_settings_setnum(settings, "synth.sample-rate", MIDI_RENDER_RATE);
    fluid_settings_setnum(settings, "synth.gain", 0.55);
    fluid_settings_setint(settings, "audio.period-size", 1024);
    fluid_settings_setstr(settings, "audio.file.name", out);
    fluid_settings_setstr(settings, "audio.file.type", "wav");
    fluid_settings_setstr(settings, "audio.file.format", "float");

    synth = new_fluid_synth(settings);
    if (!synth) {
        *err = ka_strdup("Cannot initialize FluidSynth synth");
        goto done;
    }
    if (fluid_synth_sfload(synth, sf2, 1) == FLUID_FAILED) {
        *err = ka_asprintf("Cannot load SoundFont: %s", sf2);
        goto done;
    }

    player = new_fluid_player(synth);
    if (!player || fluid_player_add(player, midi_path) == FLUID_FAILED) {
        *err = ka_asprintf("Cannot load MIDI: %s", ka_basename(midi_path));
        goto done;
    }

    renderer = new_fluid_file_renderer(synth);
    if (!renderer) {
        *err = ka_strdup("Cannot initialize FluidSynth file renderer");
        goto done;
    }

    if (fluid_player_play(player) == FLUID_FAILED) {
        *err = ka_strdup("Cannot start MIDI renderer");
        goto done;
    }

    int blocks = 0;
    int max_blocks = MIDI_RENDER_RATE * 60 * 60 / 1024; /* 1 hour guard. */
    while (fluid_player_get_status(player) == FLUID_PLAYER_PLAYING) {
        if (fluid_file_renderer_process_block(renderer) == FLUID_FAILED) {
            *err = ka_strdup("MIDI render failed");
            goto done;
        }
        if (++blocks > max_blocks) {
            *err = ka_strdup("MIDI render exceeded one hour");
            goto done;
        }
    }
    ok = true;

done:
    if (player) {
        fluid_player_stop(player);
        fluid_player_join(player);
    }
    if (renderer)
        delete_fluid_file_renderer(renderer);
    if (player)
        delete_fluid_player(player);
    if (synth)
        delete_fluid_synth(synth);
    if (settings)
        delete_fluid_settings(settings);
    free(sf2);

    if (ok) {
        *wav_path = out;
    } else {
        unlink(out);
        free(out);
    }
    return ok;
}

static bool load_rendered_wav(AudioEngine *ae, const char *wav_path,
                              char **err)
{
    *err = NULL;
    SF_INFO info = {0};
    SNDFILE *sf = sf_open(wav_path, SFM_READ, &info);
    if (!sf) {
        *err = ka_strdup("Cannot read rendered MIDI audio");
        return false;
    }
    if (info.frames <= 0 || info.frames > INT_MAX || info.channels <= 0 ||
        info.samplerate <= 0) {
        sf_close(sf);
        *err = ka_strdup("Rendered MIDI audio is invalid");
        return false;
    }

    AudioBuf pcm = abuf_alloc((int)info.frames, OUT_CHANNELS);
    const int chunk_frames = FEED_CHUNK_FRAMES;
    float *tmp = malloc(sizeof(float) * (size_t)chunk_frames *
                        (size_t)info.channels);
    if (!tmp)
        abort();

    int64_t out_frame = 0;
    sf_count_t got;
    while ((got = sf_readf_float(sf, tmp, chunk_frames)) > 0) {
        for (sf_count_t f = 0; f < got; f++) {
            float l, r;
            if (info.channels == 1) {
                l = r = tmp[f];
            } else {
                l = tmp[f * info.channels];
                r = tmp[f * info.channels + 1];
            }
            pcm.data[out_frame * OUT_CHANNELS] = l;
            pcm.data[out_frame * OUT_CHANNELS + 1] = r;
            out_frame++;
        }
    }
    free(tmp);
    sf_close(sf);

    ae->midi_pcm = pcm;
    ae->is_midi = true;
    ae->midi_pos = 0;
    memset(&ae->info, 0, sizeof(ae->info));
    ae->info.samplerate = info.samplerate;
    ae->info.channels = OUT_CHANNELS;
    ae->info.frames = pcm.frames;
    return true;
}

static bool load_midi(AudioEngine *ae, const char *filepath, char **err)
{
    char *wav = NULL;
    if (!render_midi_to_wav(filepath, &wav, err))
        return false;

    bool ok = load_rendered_wav(ae, wav, err);
    unlink(wav);
    free(wav);
    return ok;
}

static void emit_tags(AudioEngine *ae)
{
    if (!ae->cbs.tag_found)
        return;
    AudioTags tags = {0};
    if (ae->is_midi) {
        char *stem = ka_stem(ae->current_file);
        snprintf(tags.title, sizeof(tags.title), "%s", stem);
        free(stem);
    } else if (ae->sf) {
        const char *title = sf_get_string(ae->sf, SF_STR_TITLE);
        const char *artist = sf_get_string(ae->sf, SF_STR_ARTIST);
        if (title)
            snprintf(tags.title, sizeof(tags.title), "%s", title);
        if (artist)
            snprintf(tags.artist, sizeof(tags.artist), "%s", artist);
    }
    tags.sample_rate = ae->info.samplerate;
    tags.channels = ae->info.channels;
    struct stat st;
    if (!ae->is_midi && ae->current_file &&
        stat(ae->current_file, &st) == 0 &&
        ae->info.frames > 0 && ae->info.samplerate > 0) {
        double dur = (double)ae->info.frames / ae->info.samplerate;
        if (dur > 0)
            tags.bitrate = (int)(st.st_size * 8.0 / dur);
    }
    ae->cbs.tag_found(ae->cbs.ud, &tags);
}

void audio_load(AudioEngine *ae, const char *filepath)
{
    if (!ka_is_file(filepath)) {
        char *msg = ka_asprintf("File not found: %s", filepath);
        emit_error(ae, msg);
        free(msg);
        return;
    }
    audio_stop(ae);
    close_file(ae);
    clear_midi(ae);
    free(ae->current_file);
    ae->current_file = ka_strdup(filepath);

    memset(&ae->info, 0, sizeof(ae->info));
    if (is_midi_path(filepath)) {
        char *err = NULL;
        if (!load_midi(ae, filepath, &err)) {
            emit_error(ae, err ? err : "Cannot decode MIDI");
            free(err);
            return;
        }
        ae->fed_frames = 0;
        ae->file_exhausted = false;
        redesign_all(ae);
        emit_tags(ae);
        return;
    }

    ae->sf = sf_open(filepath, SFM_READ, &ae->info);
    if (!ae->sf) {
        char *msg = ka_asprintf("Cannot decode: %s", ka_basename(filepath));
        emit_error(ae, msg);
        free(msg);
        return;
    }
    ae->fed_frames = 0;
    ae->file_exhausted = false;
    redesign_all(ae);
    emit_tags(ae);
}

/* Read FEED_CHUNK_FRAMES from the file, run the processing chain, convert
 * to stereo, and queue on the device. Returns frames queued (0 on EOF). */
static int feed_chunk(AudioEngine *ae)
{
    int in_ch = ae->info.channels;
    float *in = malloc(sizeof(float) * FEED_CHUNK_FRAMES * (size_t)in_ch);
    float *out = malloc(sizeof(float) * FEED_CHUNK_FRAMES * OUT_CHANNELS);
    if (!in || !out)
        abort();

    sf_count_t got = sf_readf_float(ae->sf, in, FEED_CHUNK_FRAMES);
    if (got <= 0) {
        free(in);
        free(out);
        ae->file_exhausted = true;
        return 0;
    }

    double lgain = KA_MIN(1.0, 1.0 - ae->pan);
    double rgain = KA_MIN(1.0, 1.0 + ae->pan);

    for (int f = 0; f < got; f++) {
        float l, r;
        if (in_ch == 1) {
            l = r = in[f];
        } else {
            l = in[f * in_ch];
            r = in[f * in_ch + 1];
        }
        float s[OUT_CHANNELS] = {l, r};
        for (int ch = 0; ch < OUT_CHANNELS; ch++) {
            double v = s[ch] * ae->preamp;
            if (ae->eq_enabled) {
                for (int b = 0; b < EQ_BANDS; b++)
                    if (fabs(ae->eq_gains[b]) >= 0.01)
                        v = biquad_process(&ae->eq[b][ch], (float)v);
            }
            v *= ae->volume;
            v *= (ch == 0) ? lgain : rgain;
            s[ch] = (float)KA_CLAMP(v, -1.0, 1.0);
        }
        out[f * OUT_CHANNELS] = s[0];
        out[f * OUT_CHANNELS + 1] = s[1];
        /* Spectrum tap: post-chain mono mix */
        ae->spec_ring[ae->spec_pos] = (s[0] + s[1]) * 0.5f;
        ae->spec_pos = (ae->spec_pos + 1) % SPECTRUM_FFT;
    }

    SDL_QueueAudio(ae->dev, out,
                   (uint32_t)(got * OUT_CHANNELS * sizeof(float)));
    ae->fed_frames += got;
    free(in);
    free(out);
    return (int)got;
}

static int feed_midi_chunk(AudioEngine *ae)
{
    int64_t remain = ae->midi_pcm.frames - ae->midi_pos;
    if (remain <= 0) {
        ae->file_exhausted = true;
        return 0;
    }
    int got = (int)KA_MIN((int64_t)FEED_CHUNK_FRAMES, remain);
    float *out = malloc(sizeof(float) * (size_t)got * OUT_CHANNELS);
    if (!out)
        abort();

    double lgain = KA_MIN(1.0, 1.0 - ae->pan);
    double rgain = KA_MIN(1.0, 1.0 + ae->pan);

    for (int f = 0; f < got; f++) {
        int64_t src = ae->midi_pos + f;
        float s[OUT_CHANNELS] = {
            ae->midi_pcm.data[src * OUT_CHANNELS],
            ae->midi_pcm.data[src * OUT_CHANNELS + 1],
        };
        for (int ch = 0; ch < OUT_CHANNELS; ch++) {
            double v = s[ch] * ae->preamp;
            if (ae->eq_enabled) {
                for (int b = 0; b < EQ_BANDS; b++)
                    if (fabs(ae->eq_gains[b]) >= 0.01)
                        v = biquad_process(&ae->eq[b][ch], (float)v);
            }
            v *= ae->volume;
            v *= (ch == 0) ? lgain : rgain;
            s[ch] = (float)KA_CLAMP(v, -1.0, 1.0);
        }
        out[f * OUT_CHANNELS] = s[0];
        out[f * OUT_CHANNELS + 1] = s[1];
        ae->spec_ring[ae->spec_pos] = (s[0] + s[1]) * 0.5f;
        ae->spec_pos = (ae->spec_pos + 1) % SPECTRUM_FFT;
    }

    SDL_QueueAudio(ae->dev, out,
                   (uint32_t)(got * OUT_CHANNELS * sizeof(float)));
    free(out);
    ae->midi_pos += got;
    ae->fed_frames += got;
    return got;
}

static bool has_loaded_audio(const AudioEngine *ae)
{
    return ae->is_midi ? ae->midi_pcm.data != NULL : ae->sf != NULL;
}

static int64_t queued_frames(AudioEngine *ae)
{
    if (!ae->dev)
        return 0;
    return SDL_GetQueuedAudioSize(ae->dev) /
           (OUT_CHANNELS * sizeof(float));
}

void audio_play(AudioEngine *ae)
{
    if (!ae->current_file)
        return;
    if (strcmp(ae->state, "stopped") == 0) {
        if (ae->is_midi) {
            ae->midi_pos = 0;
        } else if (!ae->sf) { /* reopen after stop */
            memset(&ae->info, 0, sizeof(ae->info));
            ae->sf = sf_open(ae->current_file, SFM_READ, &ae->info);
            if (!ae->sf) {
                char *msg = ka_asprintf("Cannot decode: %s",
                                        ka_basename(ae->current_file));
                emit_error(ae, msg);
                free(msg);
                return;
            }
        }
        if (ae->sf)
            sf_seek(ae->sf, 0, SEEK_SET);
        ae->fed_frames = 0;
        ae->file_exhausted = false;
    }
    if (!has_loaded_audio(ae))
        return;
    if (!open_device(ae, ae->info.samplerate))
        return;
    SDL_PauseAudioDevice(ae->dev, 0);
    emit_state(ae, "playing");
}

void audio_pause(AudioEngine *ae)
{
    if (strcmp(ae->state, "playing") == 0) {
        if (ae->dev)
            SDL_PauseAudioDevice(ae->dev, 1);
        emit_state(ae, "paused");
    } else if (strcmp(ae->state, "paused") == 0) {
        if (ae->dev)
            SDL_PauseAudioDevice(ae->dev, 0);
        emit_state(ae, "playing");
    }
}

void audio_stop(AudioEngine *ae)
{
    if (ae->dev) {
        SDL_PauseAudioDevice(ae->dev, 1);
        SDL_ClearQueuedAudio(ae->dev);
    }
    if (ae->sf)
        sf_seek(ae->sf, 0, SEEK_SET);
    if (ae->is_midi)
        ae->midi_pos = 0;
    ae->fed_frames = 0;
    ae->file_exhausted = false;
    emit_state(ae, "stopped");
}

void audio_seek(AudioEngine *ae, int position_ms)
{
    if (!has_loaded_audio(ae) || ae->info.samplerate <= 0)
        return;
    sf_count_t frame = (sf_count_t)((int64_t)position_ms *
                                    ae->info.samplerate / 1000);
    frame = KA_CLAMP(frame, 0, ae->info.frames);
    if (ae->is_midi)
        ae->midi_pos = frame;
    else
        sf_seek(ae->sf, frame, SEEK_SET);
    if (ae->dev)
        SDL_ClearQueuedAudio(ae->dev);
    ae->fed_frames = frame;
    ae->file_exhausted = false;
}

void audio_set_volume(AudioEngine *ae, int volume)
{
    ae->volume = KA_CLAMP(volume, 0, 100) / 100.0;
}

void audio_set_balance(AudioEngine *ae, int balance)
{
    ae->pan = KA_CLAMP(balance / 100.0, -1.0, 1.0);
}

void audio_set_preamp(AudioEngine *ae, double db)
{
    ae->preamp = pow(10.0, db / 20.0);
}

void audio_set_eq_band(AudioEngine *ae, int band, double gain_db)
{
    if (band < 0 || band > 9)
        return;
    /* Always remember the requested gain so it can be restored on
     * re-enable. */
    ae->eq_gains[band] = gain_db;
    if (ae->eq_enabled)
        redesign_band(ae, band);
}

void audio_set_eq_enabled(AudioEngine *ae, bool enabled)
{
    ae->eq_enabled = enabled;
    for (int b = 0; b < EQ_BANDS; b++)
        redesign_band(ae, b);
}

int audio_get_position_ms(AudioEngine *ae)
{
    if (!has_loaded_audio(ae) || ae->info.samplerate <= 0)
        return 0;
    int64_t playing = ae->fed_frames - queued_frames(ae);
    if (playing < 0)
        playing = 0;
    return (int)(playing * 1000 / ae->info.samplerate);
}

int audio_get_duration_ms(AudioEngine *ae)
{
    if (!has_loaded_audio(ae) || ae->info.samplerate <= 0)
        return 0;
    return (int)(ae->info.frames * 1000 / ae->info.samplerate);
}

const char *audio_state(const AudioEngine *ae)
{
    return ae->state;
}

const char *audio_current_file(const AudioEngine *ae)
{
    return ae->current_file;
}

/* Compute the 75-band spectrum from the tap ring (linear band spacing over
 * 0..fs/2, like gst spectrum) and emit normalized 0..1 values. */
static void emit_spectrum(AudioEngine *ae)
{
    if (!ae->cbs.spectrum)
        return;
    float frame[SPECTRUM_FFT];
    for (int i = 0; i < SPECTRUM_FFT; i++) {
        int idx = (ae->spec_pos + i) % SPECTRUM_FFT;
        /* Hann window */
        double w = 0.5 * (1.0 - cos(2.0 * M_PI * i / (SPECTRUM_FFT - 1)));
        frame[i] = (float)(ae->spec_ring[idx] * w);
    }
    double mag[SPECTRUM_FFT / 2 + 1];
    fft_real_mag(frame, SPECTRUM_FFT, mag);

    float out[AUDIO_SPECTRUM_BANDS];
    int bins = SPECTRUM_FFT / 2;
    for (int band = 0; band < AUDIO_SPECTRUM_BANDS; band++) {
        int lo = band * bins / AUDIO_SPECTRUM_BANDS;
        int hi = (band + 1) * bins / AUDIO_SPECTRUM_BANDS;
        if (hi <= lo)
            hi = lo + 1;
        double avg = 0.0;
        for (int k = lo; k < hi; k++)
            avg += mag[k];
        avg /= (hi - lo);
        /* Full-scale sine ~= 0 dB with Hann window (coherent gain 0.5). */
        double db = 20.0 * log10(4.0 * avg / SPECTRUM_FFT + 1e-10);
        double norm = (db + 80.0) / 80.0;
        out[band] = (float)KA_CLAMP(norm, 0.0, 1.0);
    }
    ae->cbs.spectrum(ae->cbs.ud, out, AUDIO_SPECTRUM_BANDS);
}

void audio_poll(AudioEngine *ae)
{
    uint32_t now = SDL_GetTicks();

    if (strcmp(ae->state, "playing") == 0 && has_loaded_audio(ae) &&
        ae->dev) {
        int64_t target =
            (int64_t)(QUEUE_TARGET_SEC * ae->info.samplerate);
        while (!ae->file_exhausted && queued_frames(ae) < target) {
            int got = ae->is_midi ? feed_midi_chunk(ae) : feed_chunk(ae);
            if (got == 0)
                break;
        }

        if (ae->file_exhausted && queued_frames(ae) == 0) {
            audio_stop(ae);
            if (ae->cbs.eos)
                ae->cbs.eos(ae->cbs.ud);
            return;
        }

        if (now - ae->last_position_ms >= POSITION_INTERVAL_MS) {
            ae->last_position_ms = now;
            if (ae->cbs.position_changed)
                ae->cbs.position_changed(ae->cbs.ud,
                                         audio_get_position_ms(ae),
                                         audio_get_duration_ms(ae));
        }
        if (now - ae->last_spectrum_ms >= SPECTRUM_INTERVAL_MS) {
            ae->last_spectrum_ms = now;
            emit_spectrum(ae);
        }
    }
}

void audio_cleanup(AudioEngine *ae)
{
    if (!ae)
        return;
    close_device(ae);
    close_file(ae);
    clear_midi(ae);
    free(ae->current_file);
    free(ae);
}
