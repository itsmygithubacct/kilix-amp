#include "headless.h"

#include <SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>

#include "audio.h"
#include "config.h"
#include "control.h"
#include "json.h"
#include "playlist.h"

typedef struct {
    Config *config;
    AudioEngine *audio;
    Playlist *playlist;
    bool running;
    char title[512];
    int volume;
    /* Loading is deferred to the next tick exactly as the windowed player
     * defers it: opening a file can take real time (a MIDI render most of
     * all), and a reply must not wait on the decoder. */
    bool pending_play, pending_pause;
    int pending_index;
    /* Stops an all-unplayable playlist from skipping forever under repeat. */
    int auto_skip_count;
    int reply_protocol;
} Headless;

static volatile sig_atomic_t g_signalled = 0;

static void on_signal(int sig)
{
    (void)sig;
    g_signalled = 1;
}

/* --- Playback --- */

static void hl_set_title_from_track(Headless *h, const Track *track)
{
    if (!track) {
        h->title[0] = '\0';
        return;
    }
    char *disp = track_display_title(track);
    snprintf(h->title, sizeof(h->title), "%s", disp);
    free(disp);
}

/* Queues the current track; the load itself happens on the next tick. */
static bool hl_queue_current(Headless *h)
{
    Track *track = playlist_current_track(h->playlist);
    if (!track && playlist_count(h->playlist) > 0)
        track = playlist_set_current(h->playlist, 0);
    if (!track)
        return false;
    h->pending_index = playlist_current_index(h->playlist);
    h->pending_play = true;
    h->pending_pause = false;
    hl_set_title_from_track(h, track);
    return true;
}

static bool hl_queue_index(Headless *h, int index)
{
    Track *track = playlist_set_current(h->playlist, index);
    if (!track)
        return false;
    h->pending_index = index;
    h->pending_play = true;
    h->pending_pause = false;
    hl_set_title_from_track(h, track);
    return true;
}

static void hl_play_pending(Headless *h)
{
    if (!h->pending_play)
        return;
    h->pending_play = false;
    Track *track = playlist_set_current(h->playlist, h->pending_index);
    if (!track)
        return;
    if (track->source_kind == KA_ENCODEC_FILE) audio_load(h->audio, track->filepath);
    else if (!audio_load_live(h->audio, track->source_kind, track->filepath, STDIN_FILENO)) return;
    audio_play(h->audio);
    if (h->pending_pause) audio_pause(h->audio);
    h->pending_pause = false;
}

static void hl_resume_or_play(Headless *h)
{
    if (h->pending_play) { h->pending_pause = false; return; }
    if (strcmp(audio_state(h->audio), "paused") == 0)
        audio_pause(h->audio); /* audio_pause toggles */
    else if (strcmp(audio_state(h->audio), "stopped") == 0)
        hl_queue_current(h);
}

static void hl_toggle(Headless *h)
{
    if (h->pending_play) { h->pending_pause = !h->pending_pause; return; }
    const char *state = audio_state(h->audio);
    if (strcmp(state, "playing") == 0 || strcmp(state, "paused") == 0
        || strcmp(state, "loading") == 0 || strcmp(state, "buffering") == 0)
        audio_pause(h->audio);
    else
        hl_queue_current(h);
}

/* --- Audio engine callbacks --- */

static void on_eos(void *ud)
{
    Headless *h = ud;
    if (audio_source_info(h->audio).live) return; /* Reconnect is an explicit user action. */
    Track *track = playlist_next_track(h->playlist);
    if (track) {
        h->pending_index = playlist_current_index(h->playlist);
        h->pending_play = true;
        hl_set_title_from_track(h, track);
    }
}

static void on_error(void *ud, const char *msg)
{
    Headless *h = ud;
    fprintf(stderr, "kilix-amp: %s\n", msg);
    if (audio_source_info(h->audio).live) return;
    if (h->auto_skip_count >= playlist_count(h->playlist)) {
        h->auto_skip_count = 0;
        return;
    }
    h->auto_skip_count++;
    Track *track = playlist_next_track(h->playlist);
    if (track) {
        h->pending_index = playlist_current_index(h->playlist);
        h->pending_play = true;
        hl_set_title_from_track(h, track);
    } else {
        h->auto_skip_count = 0;
    }
}

static void on_state_changed(void *ud, const char *state)
{
    Headless *h = ud;
    if (strcmp(state, "playing") == 0)
        h->auto_skip_count = 0;
}

static void on_tags(void *ud, const AudioTags *tags)
{
    Headless *h = ud;
    Track *track = playlist_current_track(h->playlist);
    if (!track)
        return;
    if (tags->title[0]) {
        free(track->title);
        track->title = ka_strdup(tags->title);
    }
    if (tags->artist[0]) {
        free(track->artist);
        track->artist = ka_strdup(tags->artist);
    }
    track->bitrate = tags->bitrate / 1000;
    track->sample_rate = tags->sample_rate;
    track->channels = tags->channels == 1 ? 1 : 2;
    hl_set_title_from_track(h, track);
}

/* --- Protocol --- */

static const char *hl_state_name(const Headless *h)
{
    /* A queued track has been accepted but not opened yet; saying "playing"
     * before the decoder has seen the file would be a guess. */
    if (h->pending_play)
        return h->pending_pause ? "paused" : "loading";
    return audio_state(h->audio);
}

static void hl_write_status(Headless *h, JsonBuf *reply)
{
    int pos_ms = audio_get_position_ms(h->audio);
    int dur_ms = audio_get_duration_ms(h->audio);
    const char *file = audio_current_file(h->audio);
    AudioSourceInfo info = audio_source_info(h->audio);
    if (h->reply_protocol == 2 && playlist_count(h->playlist) == 0) {
        file = NULL; info = (AudioSourceInfo){0}; pos_ms = dur_ms = 0;
    }
    if (h->reply_protocol == 2 && h->pending_play) {
        Track *pending = playlist_track(h->playlist, h->pending_index);
        if (pending) {
            file = pending->filepath;
            info = (AudioSourceInfo){0};
            info.source_kind = pending->source_kind;
            info.live = pending->source_kind != KA_ENCODEC_FILE;
            char *extension = ka_ext_lower(file);
            info.encodec = info.live || strcmp(extension, ".kenc") == 0;
            free(extension);
            pos_ms = dur_ms = 0;
        }
    }
    json_kv_str(reply, "state", hl_state_name(h));
    json_kv_str(reply, "title", h->title);
    json_kv_str(reply, "file", file ? file : "");
    json_kv_num(reply, "pos", pos_ms / 1000.0);
    if (h->reply_protocol == 2 && (info.live || !info.ready)) json_kv_null(reply, "len");
    else json_kv_num(reply, "len", dur_ms / 1000.0);
    json_kv_int(reply, "index", playlist_current_index(h->playlist));
    json_kv_int(reply, "count", playlist_count(h->playlist));
    json_kv_int(reply, "volume", h->volume);
    json_kv_bool(reply, "shuffle", playlist_shuffle(h->playlist));
    json_kv_int(reply, "repeat", playlist_repeat(h->playlist));
    if (h->reply_protocol == 2) {
        const char *kind = info.source_kind == KA_ENCODEC_STDIN ? "encodec-stdin"
            : info.source_kind == KA_ENCODEC_SOCKET ? "encodec-unix" : "file";
        json_kv_str(reply, "source_type", file && *file ? kind : "none");
        json_kv_str(reply, "codec", info.encodec ? "encodec" : file && *file ? "pcm" : "none");
        json_kv_int(reply, "profile", info.profile);
        json_kv_int(reply, "sample_rate", info.sample_rate);
        json_kv_int(reply, "channels", info.channels);
        json_kv_int(reply, "bitrate", info.bitrate);
        json_kv_int(reply, "threads", info.threads);
        json_kv_bool(reply, "model_ready", info.encodec && info.ready);
        json_kv_bool(reply, "ready", info.ready);
        json_kv_bool(reply, "live", info.live);
        json_kv_bool(reply, "buffering", h->pending_play || info.buffering);
        json_kv_bool(reply, "seekable", info.seekable);
        json_kv_bool(reply, "ended", info.ended);
        json_kv_bool(reply, "degraded", info.degraded);
        json_kv_bool(reply, "reconnect_required", info.reconnect_required);
        json_kv_bool(reply, "wire_valid", info.wire_valid);
        /* Decimal strings retain exact unsigned 64-bit wire values in clients
         * whose JSON numbers cannot represent every integer. */
        char wire[32];
        if (info.wire_valid) {
            snprintf(wire, sizeof(wire), "%" PRIu64, info.wire_pts_ms);
            json_kv_str(reply, "wire_pts_ms", wire);
            snprintf(wire, sizeof(wire), "%" PRIu64, info.wire_epoch);
            json_kv_str(reply, "wire_epoch", wire);
        } else {
            json_kv_null(reply, "wire_pts_ms"); json_kv_null(reply, "wire_epoch");
        }
        json_kv_int(reply, "source_error_code", info.error_code);
        json_kv_str(reply, "source_error_message", file && *file && !h->pending_play ? audio_error(h->audio) : "");
        json_kv_bool(reply, "source_error_recoverable", info.error_code != 0u);
    }
}

static void hl_refuse(Headless *h, JsonBuf *reply, const char *code, const char *message)
{
    json_kv_bool(reply, "ok", false);
    json_kv_str(reply, "error", message);
    if (h->reply_protocol == 2) {
        json_kv_str(reply, "error_code", code);
        json_kv_bool(reply, "error_recoverable", true);
    }
}

static void hl_write_playlist(Headless *h, JsonBuf *reply)
{
    json_arr_begin(reply, "items");
    int count = playlist_count(h->playlist);
    for (int i = 0; i < count; i++) {
        Track *track = playlist_track(h->playlist, i);
        json_arr_str(reply, track && track->filepath ? track->filepath : "");
    }
    json_arr_end(reply);
    json_kv_int(reply, "index", playlist_current_index(h->playlist));
    json_kv_int(reply, "count", count);
}

static void hl_handle(void *ud, const char *cmd, const char *request,
                      JsonBuf *reply)
{
    Headless *h = ud;
    long long number = 0;
    double seconds = 0;
    bool flag = false;
    long long protocol = 1;
    (void)json_get_int(request, "protocol", &protocol);
    h->reply_protocol = (int)protocol;
    bool live_source = audio_source_info(h->audio).live;
    if (h->pending_play) {
        Track *pending = playlist_track(h->playlist, h->pending_index);
        if (pending) live_source = pending->source_kind != KA_ENCODEC_FILE;
    }
    if (protocol == 1 && live_source
        && strcmp(cmd, "ping") && strcmp(cmd, "quit") && strcmp(cmd, "stop")) {
        hl_refuse(h, reply, "PROTOCOL_REQUIRED", "live source controls require protocol 2"); return;
    }

    /* Optional fields have to be correctly typed when present; they must not
     * accidentally select the no-argument toggle/resume behavior. */
    const char *integers[] = {"index", "level", "mode"};
    for (size_t i = 0u; i < 3u; ++i) {
        if (json_has_key(request, integers[i]) && !json_get_int(request, integers[i], &number)) {
            hl_refuse(h, reply, "INVALID_REQUEST", "integer argument has the wrong type"); return;
        }
    }
    if (json_has_key(request, "on") && !json_get_bool(request, "on", &flag)) {
        hl_refuse(h, reply, "INVALID_REQUEST", "on must be boolean"); return;
    }

    if (strcmp(cmd, "ping") == 0) {
        json_kv_bool(reply, "ok", true);
        if (protocol == 2) {
            json_kv_int(reply, "max_protocol", CONTROL_PROTOCOL_MAX);
#ifdef KA_WITH_ENCODEC
            json_kv_bool(reply, "encodec", true);
            json_kv_bool(reply, "live_sources", true);
#else
            json_kv_bool(reply, "encodec", false);
            json_kv_bool(reply, "live_sources", false);
#endif
        }
        return;
    }
    if (strcmp(cmd, "state") == 0) {
        json_kv_bool(reply, "ok", true);
        hl_write_status(h, reply);
        return;
    }
    if (strcmp(cmd, "playlist") == 0) {
        json_kv_bool(reply, "ok", true);
        hl_write_playlist(h, reply);
        return;
    }
    if (strcmp(cmd, "open") == 0 && protocol == 2) {
        char source_type[32], path[4096];
        if (!json_get_str_exact(request, "source_type", source_type, sizeof(source_type))
            || !json_get_str_exact(request, "path", path, sizeof(path)) || path[0] == '\0') {
            hl_refuse(h, reply, "INVALID_REQUEST", "open needs source_type and an exact nonempty path"); return;
        }
        KaEncodecKind kind;
        if (!strcmp(source_type, "file")) kind = KA_ENCODEC_FILE;
        else if (!strcmp(source_type, "encodec-unix")) kind = KA_ENCODEC_SOCKET;
        else { hl_refuse(h, reply, "UNSUPPORTED_SOURCE", "stdin is selected only at process startup; use file or encodec-unix"); return; }
        if (kind == KA_ENCODEC_FILE && (!ka_is_file(path) || !playlist_is_audio_ext(path))) {
            hl_refuse(h, reply, "SOURCE_UNAVAILABLE", "no playable file at that path"); return;
        }
        if (kind == KA_ENCODEC_SOCKET && (path[0] != '/' || strlen(path) >= 108u)) {
            hl_refuse(h, reply, "INVALID_REQUEST", "Unix source needs a bounded absolute socket path"); return;
        }
        audio_stop(h->audio);
        h->pending_play = false;
        playlist_clear(h->playlist);
        if (kind == KA_ENCODEC_FILE) playlist_add_file(h->playlist, path);
        else playlist_add_live(h->playlist, path, kind);
        if (!hl_queue_index(h, 0)) { hl_refuse(h, reply, "SOURCE_UNAVAILABLE", "source could not be queued"); return; }
    } else if (strcmp(cmd, "play") == 0) {
        if (json_get_int(request, "index", &number)) {
            if (number < 0 || number >= playlist_count(h->playlist) || !hl_queue_index(h, (int)number)) {
                hl_refuse(h, reply, "INVALID_REQUEST", "index out of range");
                hl_write_status(h, reply);
                return;
            }
        } else {
            hl_resume_or_play(h);
        }
    } else if (strcmp(cmd, "toggle") == 0) {
        hl_toggle(h);
    } else if (strcmp(cmd, "pause") == 0) {
        if (h->pending_play) h->pending_pause = true;
        else if (strcmp(audio_state(h->audio), "playing") == 0
            || strcmp(audio_state(h->audio), "loading") == 0
            || strcmp(audio_state(h->audio), "buffering") == 0)
            audio_pause(h->audio);
    } else if (strcmp(cmd, "stop") == 0) {
        h->pending_play = false;
        audio_stop(h->audio);
    } else if (strcmp(cmd, "next") == 0) {
        Track *track = playlist_next_track(h->playlist);
        if (track)
            hl_queue_index(h, playlist_current_index(h->playlist));
    } else if (strcmp(cmd, "previous") == 0 || strcmp(cmd, "prev") == 0) {
        Track *track = playlist_prev_track(h->playlist);
        if (track)
            hl_queue_index(h, playlist_current_index(h->playlist));
    } else if (strcmp(cmd, "seek") == 0) {
        if (protocol == 2 && (h->pending_play || !audio_source_info(h->audio).seekable)) {
            hl_refuse(h, reply, "NOT_SEEKABLE", "source is live, unavailable or still loading"); return;
        }
        if (!json_get_num(request, "pos", &seconds)) {
            hl_refuse(h, reply, "INVALID_REQUEST", "seek needs a \"pos\" in seconds");
            return;
        }
        if (protocol == 2 && seconds < 0) {
            hl_refuse(h, reply, "INVALID_REQUEST", "seek position is out of range"); return;
        }
        if (seconds < 0)
            seconds = 0;
        if (!isfinite(seconds) || seconds > (double)INT_MAX / 1000.0) {
            hl_refuse(h, reply, "INVALID_REQUEST", "seek position is out of range"); return;
        }
        audio_seek(h->audio, (int)(seconds * 1000.0));
    } else if (strcmp(cmd, "volume") == 0) {
        if (!json_get_int(request, "level", &number)) {
            hl_refuse(h, reply, "INVALID_REQUEST", "volume needs a \"level\" of 0-100");
            return;
        }
        h->volume = (int)KA_CLAMP(number, 0, 100);
        audio_set_volume(h->audio, h->volume);
    } else if (strcmp(cmd, "add") == 0) {
        char path[4096];
        if (!json_get_str_exact(request, "path", path, sizeof(path)) || !path[0]) {
            hl_refuse(h, reply, "INVALID_REQUEST", "add needs a \"path\" string");
            return;
        }
        int before = playlist_count(h->playlist);
        if (ka_is_dir(path))
            playlist_add_directory(h->playlist, path, true);
        else if (ka_is_file(path))
            playlist_add_file(h->playlist, path);
        if (playlist_count(h->playlist) == before) {
            hl_refuse(h, reply, "SOURCE_UNAVAILABLE", "nothing playable at that path");
            hl_write_status(h, reply);
            return;
        }
        if (playlist_current_index(h->playlist) < 0)
            playlist_set_current(h->playlist, before);
    } else if (strcmp(cmd, "clear") == 0) {
        h->pending_play = false;
        audio_stop(h->audio);
        playlist_clear(h->playlist);
        h->title[0] = '\0';
    } else if (strcmp(cmd, "shuffle") == 0) {
        if (json_get_bool(request, "on", &flag))
            playlist_set_shuffle(h->playlist, flag);
        else
            playlist_toggle_shuffle(h->playlist);
    } else if (strcmp(cmd, "repeat") == 0) {
        if (json_get_int(request, "mode", &number))
            playlist_set_repeat(h->playlist, (int)KA_CLAMP(number, 0, 2));
        else
            playlist_toggle_repeat(h->playlist);
    } else if (strcmp(cmd, "quit") == 0) {
        h->running = false;
        json_kv_bool(reply, "ok", true);
        return;
    } else {
        char message[128];
        snprintf(message, sizeof(message), "unknown command: %s", cmd);
        hl_refuse(h, reply, "UNKNOWN_COMMAND", message);
        return;
    }

    /* Every mutating command answers with the state it produced, so a client
     * needs one round trip rather than two to redraw. */
    json_kv_bool(reply, "ok", true);
    if (protocol == 1 && audio_source_info(h->audio).live) return;
    hl_write_status(h, reply);
}

/* --- Entry point --- */

int headless_run(const char *const *files, int n_files,
                 const char *socket_path, KaEncodecKind live_kind, const char *live_path)
{
    Headless h = {0};
    h.running = true;
    h.pending_index = -1;

    /* Writing to a client that vanished mid-reply must not kill the daemon;
     * send() also passes MSG_NOSIGNAL, this covers everything else. */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);

    /* No video, no skin, no windows: audio_new brings up the audio subsystem
     * on its own, so nothing here needs a display to exist. */
    if (SDL_Init(0) != 0) {
        fprintf(stderr, "kilix-amp: SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    char *resolved = socket_path && *socket_path
                         ? ka_strdup(socket_path)
                         : control_default_socket_path();
    char err[512] = {0};
    ControlServer *cs = control_listen(resolved, err, sizeof(err));
    if (!cs) {
        fprintf(stderr, "kilix-amp: %s\n", err);
        free(resolved);
        SDL_Quit();
        return 1;
    }

    /* Settings are read, never written back: a headless run must not overwrite
     * the window layout and volume a windowed session is still using. */
    h.config = config_open(NULL);
    h.playlist = playlist_new();
    playlist_set_shuffle(h.playlist, config_shuffle(h.config));
    playlist_set_repeat(h.playlist, config_repeat(h.config));

    h.audio = audio_new();
    audio_set_callbacks(h.audio, &(AudioCallbacks){
        .state_changed = on_state_changed,
        .tag_found = on_tags,
        .eos = on_eos,
        .error = on_error,
        .ud = &h,
    });

    h.volume = config_volume(h.config);
    audio_set_volume(h.audio, h.volume);
    audio_set_balance(h.audio, config_balance(h.config));
    if (config_eq_enabled(h.config)) {
        audio_set_eq_enabled(h.audio, true);
        float bands[10];
        config_get_eq_bands(h.config, bands);
        for (int i = 0; i < 10; i++)
            audio_set_eq_band(h.audio, i, bands[i]);
    }
    audio_set_preamp(h.audio, config_eq_preamp(h.config));

    for (int i = 0; i < n_files; i++) {
        if (ka_is_dir(files[i]))
            playlist_add_directory(h.playlist, files[i], true);
        else if (ka_is_file(files[i]))
            playlist_add_file(h.playlist, files[i]);
    }
    if (live_kind != KA_ENCODEC_FILE) playlist_add_live(h.playlist, live_path, live_kind);
    if (playlist_count(h.playlist) > 0) {
        playlist_set_current(h.playlist, 0);
        hl_queue_current(&h);
    }

    printf("kilix-amp: control socket %s (protocol %d)\n",
           control_socket_path(cs), CONTROL_PROTOCOL);
    fflush(stdout);

    /* Same smoke-test hatch the windowed player honours. */
    uint32_t exit_after = 0;
    const char *exit_env = getenv("KILIXAMP_EXIT_AFTER_MS");
    if (exit_env)
        exit_after = (uint32_t)strtoul(exit_env, NULL, 10);
    uint32_t start_ticks = SDL_GetTicks();

    while (h.running && !g_signalled) {
        uint32_t now = SDL_GetTicks();
        if (exit_after && now - start_ticks >= exit_after)
            break;
        hl_play_pending(&h);
        audio_poll(h.audio);
        control_poll(cs, hl_handle, &h, now);
        SDL_Delay(5);
    }

    control_close(cs);
    audio_cleanup(h.audio);
    playlist_free(h.playlist);
    config_close(h.config);
    free(resolved);
    SDL_Quit();
    return 0;
}
