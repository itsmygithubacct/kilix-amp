/* Audio playback engine: libsndfile decode -> preamp/EQ/volume/pan chain ->
 * SDL queued audio, with in-house FFT spectrum taps.
 * Port of nixamp lib/audio_engine.py (GStreamer pipeline replaced). */
#ifndef KA_AUDIO_H
#define KA_AUDIO_H

#include "common.h"

#define AUDIO_SPECTRUM_BANDS 75

typedef struct {
    char title[256];
    char artist[256];
    int bitrate;     /* bits per second (0 if unknown) */
    int sample_rate; /* Hz */
    int channels;
} AudioTags;

typedef struct {
    void (*state_changed)(void *ud, const char *state); /* playing/paused/stopped */
    void (*position_changed)(void *ud, int pos_ms, int dur_ms);
    void (*tag_found)(void *ud, const AudioTags *tags);
    void (*spectrum)(void *ud, const float *mags, int n); /* n=75, 0..1 */
    void (*eos)(void *ud);
    void (*error)(void *ud, const char *msg);
    void *ud;
} AudioCallbacks;

typedef struct AudioEngine AudioEngine;

AudioEngine *audio_new(void);
void audio_set_callbacks(AudioEngine *ae, const AudioCallbacks *cbs);
/* Prepare a file for playback (stops current playback first). */
void audio_load(AudioEngine *ae, const char *filepath);
void audio_play(AudioEngine *ae);
void audio_pause(AudioEngine *ae); /* toggles pause <-> resume */
void audio_stop(AudioEngine *ae);
void audio_seek(AudioEngine *ae, int position_ms);
void audio_set_volume(AudioEngine *ae, int volume);   /* 0-100 */
void audio_set_balance(AudioEngine *ae, int balance); /* -100..100 */
void audio_set_preamp(AudioEngine *ae, double db);
/* Band gains in dB (-12..+12); bands at 29,59,119,237,474,947,1889,3770,
 * 7523,15011 Hz (matching gst equalizer-10bands). */
void audio_set_eq_band(AudioEngine *ae, int band, double gain_db);
void audio_set_eq_enabled(AudioEngine *ae, bool enabled);
int audio_get_position_ms(AudioEngine *ae);
int audio_get_duration_ms(AudioEngine *ae);
const char *audio_state(const AudioEngine *ae);
const char *audio_current_file(const AudioEngine *ae); /* NULL if none */
/* Drive decoding + event emission; call every main-loop tick. */
void audio_poll(AudioEngine *ae);
void audio_cleanup(AudioEngine *ae); /* also frees the engine */

#endif
