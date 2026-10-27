#ifndef KA_NATIVE_AUDIO_H
#define KA_NATIVE_AUDIO_H
#include "audio.h"
typedef struct NativeAudioEngine NativeAudioEngine;

NativeAudioEngine *native_audio_new(void);
void native_audio_set_callbacks(NativeAudioEngine *ae, const AudioCallbacks *cbs);
/* Prepare a file for playback (stops current playback first). */
void native_audio_load(NativeAudioEngine *ae, const char *filepath);
bool native_audio_load_live(NativeAudioEngine *ae, KaEncodecKind kind, const char *path, int input_fd);
void native_audio_play(NativeAudioEngine *ae);
void native_audio_pause(NativeAudioEngine *ae); /* toggles pause <-> resume */
void native_audio_stop(NativeAudioEngine *ae);
void native_audio_seek(NativeAudioEngine *ae, int position_ms);
void native_audio_set_volume(NativeAudioEngine *ae, int volume);   /* 0-100 */
void native_audio_set_balance(NativeAudioEngine *ae, int balance); /* -100..100 */
void native_audio_set_preamp(NativeAudioEngine *ae, double db);
/* Band gains in dB (-12..+12); bands at 29,59,119,237,474,947,1889,3770,
 * 7523,15011 Hz (matching gst equalizer-10bands). */
void native_audio_set_eq_band(NativeAudioEngine *ae, int band, double gain_db);
void native_audio_set_eq_enabled(NativeAudioEngine *ae, bool enabled);
int native_audio_get_position_ms(NativeAudioEngine *ae);
int native_audio_get_duration_ms(NativeAudioEngine *ae);
const char *native_audio_state(const NativeAudioEngine *ae);
const char *native_audio_current_file(const NativeAudioEngine *ae); /* NULL if none */
AudioSourceInfo native_audio_source_info(const NativeAudioEngine *ae);
const char *native_audio_error(const NativeAudioEngine *ae);
/* Drive decoding + event emission; call every main-loop tick. */
void native_audio_poll(NativeAudioEngine *ae);
void native_audio_cleanup(NativeAudioEngine *ae); /* also frees the engine */

#endif
