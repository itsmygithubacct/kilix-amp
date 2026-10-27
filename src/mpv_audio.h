#ifndef KA_MPV_AUDIO_H
#define KA_MPV_AUDIO_H
#include "audio.h"
typedef struct MpvAudioEngine MpvAudioEngine;

MpvAudioEngine *mpv_audio_new(void);
void mpv_audio_set_callbacks(MpvAudioEngine *ae, const AudioCallbacks *cbs);
/* Prepare a file for playback (stops current playback first). */
void mpv_audio_load(MpvAudioEngine *ae, const char *filepath);
void mpv_audio_play(MpvAudioEngine *ae);
void mpv_audio_pause(MpvAudioEngine *ae); /* toggles pause <-> resume */
void mpv_audio_stop(MpvAudioEngine *ae);
void mpv_audio_seek(MpvAudioEngine *ae, int position_ms);
void mpv_audio_set_volume(MpvAudioEngine *ae, int volume);   /* 0-100 */
void mpv_audio_set_balance(MpvAudioEngine *ae, int balance); /* -100..100 */
void mpv_audio_set_preamp(MpvAudioEngine *ae, double db);
/* Band gains in dB (-12..+12); bands at 29,59,119,237,474,947,1889,3770,
 * 7523,15011 Hz (matching gst equalizer-10bands). */
void mpv_audio_set_eq_band(MpvAudioEngine *ae, int band, double gain_db);
void mpv_audio_set_eq_enabled(MpvAudioEngine *ae, bool enabled);
int mpv_audio_get_position_ms(MpvAudioEngine *ae);
int mpv_audio_get_duration_ms(MpvAudioEngine *ae);
const char *mpv_audio_state(const MpvAudioEngine *ae);
const char *mpv_audio_current_file(const MpvAudioEngine *ae); /* NULL if none */
AudioSourceInfo mpv_audio_source_info(const MpvAudioEngine *ae);
const char *mpv_audio_error(const MpvAudioEngine *ae);
/* Drive decoding + event emission; call every main-loop tick. */
void mpv_audio_poll(MpvAudioEngine *ae);
void mpv_audio_cleanup(MpvAudioEngine *ae); /* also frees the engine */

void mpv_audio_set_paused(MpvAudioEngine *ae, bool paused);
#endif
