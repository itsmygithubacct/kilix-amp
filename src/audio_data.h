/* Audio buffer model with selection, undo/redo, and effect application.
 * Port of nixamp lib/audio_data.py (numpy/soundfile -> AudioBuf/sndfile). */
#ifndef KA_AUDIO_DATA_H
#define KA_AUDIO_DATA_H

#include "dsp.h"

#define AD_MAX_UNDO 30

typedef struct {
    AudioBuf samples;
    int sel_start, sel_end;
} ADState;

typedef struct AudioData {
    AudioBuf samples; /* frames==0 means empty/unloaded */
    bool loaded;
    int sr;
    int sel_start, sel_end;
    int cursor;
    char *filepath;

    ADState undo_stack[AD_MAX_UNDO];
    int undo_count;
    ADState redo_stack[AD_MAX_UNDO];
    int redo_count;

    void (*data_changed)(void *ud);
    void (*selection_changed)(void *ud);
    void *ud;
} AudioData;

/* Effect thunk: applied region + sr -> new buffer. params is caller-owned. */
typedef AudioBuf (*EffectFn)(const AudioBuf *in, int sr, void *params);
/* Generator thunk: sr -> new buffer. */
typedef AudioBuf (*GeneratorFn)(int sr, void *params);

void audio_data_init(AudioData *ad);
void audio_data_destroy(AudioData *ad);

int ad_channels(const AudioData *ad);
int ad_num_samples(const AudioData *ad); /* frames */
double ad_duration(const AudioData *ad);
bool ad_has_selection(const AudioData *ad);
void ad_set_cursor(AudioData *ad, int val);

bool ad_load(AudioData *ad, const char *filepath);
bool ad_export(AudioData *ad, const char *filepath);

void ad_set_selection(AudioData *ad, int start, int end);
void ad_clear_selection(AudioData *ad);
void ad_select_all(AudioData *ad);

bool ad_undo(AudioData *ad);
bool ad_redo(AudioData *ad);
bool ad_can_undo(const AudioData *ad);
bool ad_can_redo(const AudioData *ad);

/* Apply fn to the selection (or whole buffer when none). Splices when the
 * result length differs. */
void ad_apply_effect(AudioData *ad, EffectFn fn, void *params);
/* Insert generated audio at cursor, or replace the selection. */
void ad_apply_generator(AudioData *ad, GeneratorFn fn, void *params);

#endif
