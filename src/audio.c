/* Exactly one output engine is alive at a time. Native EnCodec preserves its
 * licensed model admission and live wire protocol; mpv owns all general media. */
#include "audio.h"
#include "native_audio.h"
#include "mpv_audio.h"
#include <stdlib.h>
#include <string.h>

struct AudioEngine {
    NativeAudioEngine *native;
    MpvAudioEngine *mpv;
    AudioCallbacks cbs;
    int volume, balance;
    double preamp, eq[10];
    bool eq_enabled, stdin_used;
};

AudioEngine *audio_new(void)
{
    AudioEngine *a = calloc(1, sizeof(*a));
    if (!a) abort();
    a->mpv = mpv_audio_new();
    a->volume = 100;
    return a;
}

void audio_set_callbacks(AudioEngine *a, const AudioCallbacks *cbs)
{
    a->cbs = *cbs;
    if (a->native) native_audio_set_callbacks(a->native, cbs);
    if (a->mpv) mpv_audio_set_callbacks(a->mpv, cbs);
}

static void select_engine(AudioEngine *a, bool native)
{
    if (native == (a->native != NULL)) return;
    /* Retire the old engine before opening the next. This also discards old
     * mpv events, so they cannot change a newly selected native source. */
    AudioCallbacks silent = {0};
    if (a->native) {
        native_audio_set_callbacks(a->native, &silent);
        native_audio_cleanup(a->native);
        a->native = NULL;
    }
    if (a->mpv) {
        mpv_audio_set_callbacks(a->mpv, &silent);
        mpv_audio_cleanup(a->mpv);
        a->mpv = NULL;
    }
    if (native) a->native = native_audio_new();
    else a->mpv = mpv_audio_new();
    audio_set_callbacks(a, &a->cbs);
    audio_set_volume(a, a->volume);
    audio_set_balance(a, a->balance);
    audio_set_preamp(a, a->preamp);
    audio_set_eq_enabled(a, a->eq_enabled);
    for (int i = 0; i < 10; i++) audio_set_eq_band(a, i, a->eq[i]);
}

void audio_load(AudioEngine *a, const char *path)
{
    char *ext = ka_ext_lower(path);
    select_engine(a, !strcmp(ext, ".kenc"));
    free(ext);
    if (a->native) native_audio_load(a->native, path);
    else mpv_audio_load(a->mpv, path);
}

bool audio_load_live(AudioEngine *a, KaEncodecKind kind, const char *path, int fd)
{
    if (kind == KA_ENCODEC_STDIN && a->stdin_used) return false;
    select_engine(a, true);
    bool opened = native_audio_load_live(a->native, kind, path, fd);
    if (opened && kind == KA_ENCODEC_STDIN) a->stdin_used = true;
    return opened;
}

#define FORWARD0(name) \
    void audio_##name(AudioEngine *a) { \
        if (a->native) native_audio_##name(a->native); \
        else mpv_audio_##name(a->mpv); \
    }
FORWARD0(play)
FORWARD0(pause)
FORWARD0(stop)
FORWARD0(poll)

void audio_set_paused(AudioEngine *a, bool paused)
{
    if (a->mpv) mpv_audio_set_paused(a->mpv, paused);
    else if (strcmp(native_audio_state(a->native), "stopped")) {
        bool is_paused = !strcmp(native_audio_state(a->native), "paused");
        if (is_paused != paused) native_audio_pause(a->native);
    }
}

void audio_seek(AudioEngine *a, int ms)
{
    if (a->native) native_audio_seek(a->native, ms);
    else mpv_audio_seek(a->mpv, ms);
}

#define SETTING(name, type, member, value) \
    void audio_set_##name(AudioEngine *a, type v) { \
        a->member = (value); \
        if (a->native) native_audio_set_##name(a->native, a->member); \
        else mpv_audio_set_##name(a->mpv, a->member); \
    }
SETTING(volume, int, volume, KA_CLAMP(v, 0, 100))
SETTING(balance, int, balance, KA_CLAMP(v, -100, 100))
SETTING(preamp, double, preamp, KA_CLAMP(v, -12, 12))
SETTING(eq_enabled, bool, eq_enabled, v)

void audio_set_eq_band(AudioEngine *a, int band, double gain)
{
    if (band < 0 || band >= 10) return;
    a->eq[band] = KA_CLAMP(gain, -12, 12);
    if (a->native) native_audio_set_eq_band(a->native, band, a->eq[band]);
    else mpv_audio_set_eq_band(a->mpv, band, a->eq[band]);
}

#define GETTER(name, type, qualifier) \
    type audio_##name(qualifier AudioEngine *a) { \
        return a->native ? native_audio_##name(a->native) : mpv_audio_##name(a->mpv); \
    }
GETTER(get_position_ms, int, )
GETTER(get_duration_ms, int, )
GETTER(state, const char *, const)
GETTER(current_file, const char *, const)
GETTER(source_info, AudioSourceInfo, const)
GETTER(error, const char *, const)

void audio_cleanup(AudioEngine *a)
{
    if (!a) return;
    if (a->native) native_audio_cleanup(a->native);
    if (a->mpv) mpv_audio_cleanup(a->mpv);
    free(a);
}
