/* Audio effects for the editor pane - 21 pure functions.
 * Port of nixamp lib/editor_effects.py. All effects take an input buffer
 * and return a freshly allocated result (caller frees); generators return
 * new buffers. scipy dependencies were replaced in-house: FFT (dsp.c),
 * natural cubic spline (repair), linear-interp resampling (change_pitch). */
#ifndef KA_EFFECTS_H
#define KA_EFFECTS_H

#include "dsp.h"

/* --- Effects: (in, sr, params) -> new buffer --- */
AudioBuf fx_amplify(const AudioBuf *in, int sr, double gain_db);
AudioBuf fx_normalize(const AudioBuf *in, int sr, double peak_level_db);
AudioBuf fx_loudness_normalize(const AudioBuf *in, int sr,
                               double target_lufs);
AudioBuf fx_fade_in(const AudioBuf *in, int sr, double duration_sec);
AudioBuf fx_fade_out(const AudioBuf *in, int sr, double duration_sec);
AudioBuf fx_invert(const AudioBuf *in, int sr);
AudioBuf fx_reverse(const AudioBuf *in, int sr);
AudioBuf fx_truncate_silence(const AudioBuf *in, int sr, double threshold_db,
                             int min_silence_ms);
AudioBuf fx_compressor(const AudioBuf *in, int sr, double threshold_db,
                       double ratio, double attack_ms, double makeup_db);
AudioBuf fx_limiter(const AudioBuf *in, int sr, double threshold_db,
                    double release_ms);
AudioBuf fx_change_pitch(const AudioBuf *in, int sr, double semitones);
AudioBuf fx_graphic_eq(const AudioBuf *in, int sr,
                       const double gains_db[10]);
AudioBuf fx_click_removal(const AudioBuf *in, int sr, double threshold,
                          double width_ms);
AudioBuf fx_noise_reduction(const AudioBuf *in, int sr, double reduction_db);
AudioBuf fx_repair(const AudioBuf *in, int sr);
AudioBuf fx_reverb(const AudioBuf *in, int sr, double room_size,
                   double damping, double wet, double dry);

/* --- Generators: (sr, params) -> new buffer --- */
typedef enum { WAVE_SINE, WAVE_SQUARE, WAVE_SAWTOOTH, WAVE_TRIANGLE } WaveForm;
AudioBuf gen_tone(int sr, double duration_sec, double frequency,
                  double amplitude, WaveForm waveform);
AudioBuf gen_chirp(int sr, double duration_sec, double f0, double f1,
                   double amplitude);
AudioBuf gen_silence(int sr, double duration_sec, int channels);
typedef enum { NOISE_WHITE, NOISE_PINK, NOISE_BROWN } NoiseType;
AudioBuf gen_noise(int sr, double duration_sec, double amplitude,
                   NoiseType type);
AudioBuf gen_dtmf(int sr, const char *sequence, double tone_duration,
                  double gap_duration, double amplitude);

#endif
