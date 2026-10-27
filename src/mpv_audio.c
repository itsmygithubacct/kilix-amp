/* One managed kilix-mpv process owns decoding, clocks, filters and output. */
#include "mpv_audio.h"
#include "dsp.h"
#include "json.h"
#include "midi.h"
#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define IPC_LIMIT (1024 * 1024)
static const int eq_freqs[10] = {29, 59, 119, 237, 474, 947, 1889, 3770, 7523, 15011};
struct MpvAudioEngine {
    AudioCallbacks cbs;
    pid_t pid, midi_pid;
    int fd, meter_fd, plugin_fd;
    char *input, *output, *current_file, *midi_wav;
    size_t input_len, output_len;
    char state[16], last_error[512];
    bool loaded, want_play, eq_enabled, filters_dirty;
    int volume, balance, pos, duration;
    long long generation, entry, event_entry, loaded_entry;
    double preamp, gains[10];
    AudioTags tags;
    float samples[1024];
    bool have_samples;
    uint32_t last_position, last_spectrum;
};

static void state(MpvAudioEngine *a, const char *value)
{
    if (strcmp(a->state, value) == 0) return;
    snprintf(a->state, sizeof(a->state), "%s", value);
    if (a->cbs.state_changed) a->cbs.state_changed(a->cbs.ud, value);
}
static void error(MpvAudioEngine *a, const char *message)
{
    snprintf(a->last_error, sizeof(a->last_error), "%s", message);
    state(a, "stopped");
    if (a->cbs.error) a->cbs.error(a->cbs.ud, message);
}
static void send_command(MpvAudioEngine *a, const char **args, int count, long long id)
{
    if (a->fd < 0) return;
    JsonBuf j = {0};
    json_obj_begin(&j);
    json_arr_begin(&j, "command");
    for (int i = 0; i < count; i++) {
        if (i == 1 && strcmp(args[0], "observe_property") == 0)
            json_arr_int(&j, strtoll(args[i], NULL, 10));
        else
            json_arr_str(&j, args[i]);
    }
    json_arr_end(&j);
    json_kv_int(&j, "request_id", id);
    json_obj_end(&j);
    if (a->output_len + j.len + 1 > IPC_LIMIT) {
        json_free(&j);
        error(a, "mpv command queue is full");
        return;
    }
    a->output = realloc(a->output, a->output_len + j.len + 2);
    if (!a->output) abort();
    memcpy(a->output + a->output_len, j.buf, j.len);
    a->output_len += j.len;
    a->output[a->output_len++] = '\n';
    a->output[a->output_len] = 0;
    json_free(&j);
}
static void property(MpvAudioEngine *a, const char *name, const char *value)
{
    const char *args[] = {"set_property", name, value};
    send_command(a, args, 3, 0);
}
static void flush(MpvAudioEngine *a)
{
    while (a->fd >= 0 && a->output_len) {
        ssize_t n = send(a->fd, a->output, a->output_len, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n <= 0) break;
        a->output_len -= (size_t)n;
        memmove(a->output, a->output + n, a->output_len);
    }
}
static char *meter_library(void)
{
    const char *override = getenv("KILIX_AMP_METER");
    if (override && *override) return ka_strdup(override);
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n < 0) return NULL;
    exe[n] = 0;
    char *dir = ka_dirname(exe);
    char *path = ka_path_join(dir, "libkilix-meter.so");
    free(dir);
    return path;
}
static void filters(MpvAudioEngine *a)
{
    if (a->fd < 0) return;
    char graph[2048] = "lavfi=[aformat=channel_layouts=stereo,";
    size_t used = strlen(graph);
    for (int i = 0; i < 10; i++) {
        if (a->eq_enabled && a->gains[i] != 0)
            used += (size_t)snprintf(graph + used, sizeof(graph) - used,
                "equalizer=f=%d:t=q:w=1:g=%.3f,", eq_freqs[i], a->gains[i]);
    }
    double gain = (a->volume / 100.0) * pow(10.0, a->preamp / 20.0);
    double left = a->balance > 0 ? 1.0 - a->balance / 100.0 : 1.0;
    double right = a->balance < 0 ? 1.0 + a->balance / 100.0 : 1.0;
    snprintf(graph + used, sizeof(graph) - used,
        "volume=%.8f,pan=stereo|c0=%.5f*c0|c1=%.5f*c1,"
        "ladspa=file=/proc/self/fd/%d:plugin=kilix_meter]", gain, left, right, a->plugin_fd);
    const char *args[] = {"af", "set", graph};
    send_command(a, args, 3, 2);
    a->filters_dirty = false;
}
static bool start_mpv(MpvAudioEngine *a)
{
    if (a->pid > 0) return true;
    char *library = meter_library();
    int plugin = library ? open(library, O_RDONLY | O_CLOEXEC) : -1;
    free(library);
    if (plugin < 0) {
        error(a, "Cannot open libkilix-meter.so beside kilix-amp; run make all");
        return false;
    }
    int ipc[2], meter[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ipc) < 0) {
        close(plugin);
        error(a, "Cannot create mpv control connection");
        return false;
    }
    if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, meter) < 0) {
        close(ipc[0]); close(ipc[1]); close(plugin);
        error(a, "Cannot create mpv spectrum connection");
        return false;
    }
    pid_t pid = fork();
    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (getppid() == 1) _exit(1);
        close(ipc[0]); close(meter[0]);
        fcntl(ipc[1], F_SETFD, 0);
        fcntl(meter[1], F_SETFD, 0);
        fcntl(plugin, F_SETFD, 0);
        char ipc_arg[80], meter_arg[32], ao_arg[128];
        snprintf(ipc_arg, sizeof(ipc_arg), "--input-ipc-client=fd://%d", ipc[1]);
        snprintf(meter_arg, sizeof(meter_arg), "%d", meter[1]);
        setenv("KILIX_AMP_SPECTRUM_FD", meter_arg, 1);
        const char *exe = getenv("KILIX_MPV");
        if (!exe || !*exe) exe = "kilix-mpv";
        const char *ao = getenv("KILIX_AMP_AUDIO_OUTPUT");
        snprintf(ao_arg, sizeof(ao_arg), "--ao=%s", ao ? ao : "");
        /* Omit --ao for physical output autodetection. "auto" is a device
         * selector, not an audio driver name. The override is for tests. */
        execlp(exe, exe, "--no-config", "--no-terminal", "--idle=yes", "--pause=yes",
            "--no-video", "--audio-channels=stereo", "--volume=100", "--load-scripts=no",
            "--audio-buffer=0.1", ipc_arg, ao && *ao ? ao_arg : (char *)NULL, (char *)NULL);
        fprintf(stderr, "kilix-amp: cannot execute %s: %s\n", exe, strerror(errno));
        _exit(127);
    }
    close(ipc[1]); close(meter[1]); close(plugin);
    if (pid < 0) {
        close(ipc[0]); close(meter[0]);
        error(a, "Cannot start kilix-mpv");
        return false;
    }
    a->pid = pid;
    a->fd = ipc[0];
    a->meter_fd = meter[0];
    a->plugin_fd = plugin; /* descriptor number retained by the child */
    fcntl(a->fd, F_SETFL, fcntl(a->fd, F_GETFL) | O_NONBLOCK);
    const char *properties[] = {"time-pos", "duration", "pause", "metadata", "audio-params", "audio-bitrate"};
    for (int i = 0; i < 6; i++) {
        char id[16];
        snprintf(id, sizeof(id), "%d", i + 1);
        const char *args[] = {"observe_property", id, properties[i]};
        send_command(a, args, 3, 0);
    }
    filters(a);
    return true;
}
MpvAudioEngine *mpv_audio_new(void)
{
    MpvAudioEngine *a = calloc(1, sizeof(*a));
    if (!a) abort();
    a->fd = a->meter_fd = -1;
    a->volume = 100;
    a->entry = a->event_entry = a->loaded_entry = -1;
    snprintf(a->state, sizeof(a->state), "stopped");
    return a;
}
void mpv_audio_set_callbacks(MpvAudioEngine *a, const AudioCallbacks *cbs) { a->cbs = *cbs; }
static void cancel_midi(MpvAudioEngine *a)
{
    if (a->midi_pid > 0) {
        kill(a->midi_pid, SIGKILL);
        while (waitpid(a->midi_pid, NULL, 0) < 0 && errno == EINTR) {}
        a->midi_pid = 0;
    }
    if (a->midi_wav) {
        unlink(a->midi_wav);
        free(a->midi_wav);
        a->midi_wav = NULL;
    }
}
void mpv_audio_stop(MpvAudioEngine *a)
{
    a->generation++;
    a->entry = a->loaded_entry = -1;
    cancel_midi(a);
    const char *args[] = {"stop"};
    send_command(a, args, 1, 0);
    a->loaded = a->want_play = a->have_samples = false;
    a->pos = 0;
    state(a, "stopped");
}
static void load_media(MpvAudioEngine *a, const char *path)
{
    const char *args[] = {"loadfile", path, "replace"};
    send_command(a, args, 3, 100 + a->generation);
    property(a, "pause", a->want_play ? "no" : "yes");
}
void mpv_audio_load(MpvAudioEngine *a, const char *path)
{
    mpv_audio_stop(a);
    free(a->current_file);
    a->current_file = ka_strdup(path);
    a->duration = 0;
    a->last_error[0] = 0;
    memset(&a->tags, 0, sizeof(a->tags));
    if (!start_mpv(a)) return;
    state(a, "loading");
    char *ext = ka_ext_lower(path);
    bool midi = strcmp(ext, ".mid") == 0 || strcmp(ext, ".midi") == 0;
    free(ext);
    if (!midi) { load_media(a, path); return; }
    const char *tmp = getenv("TMPDIR");
    a->midi_wav = ka_asprintf("%s/kilix-amp-midi-XXXXXX.wav", tmp && *tmp ? tmp : "/tmp");
    int fd = mkstemps(a->midi_wav, 4);
    if (fd < 0) {
        cancel_midi(a);
        error(a, "Cannot create MIDI render file");
        return;
    }
    close(fd);
    a->midi_pid = fork();
    if (a->midi_pid == 0) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() == 1) _exit(1);
        close_range(3, ~0U, 0);
        setenv("OMP_NUM_THREADS", "1", 1);
        char *message = NULL;
        bool ok = midi_render_wav(path, a->midi_wav, &message);
        if (!ok) fprintf(stderr, "kilix-amp: %s\n", message ? message : "MIDI render failed");
        free(message);
        _exit(ok ? 0 : 1);
    }
    if (a->midi_pid < 0) {
        a->midi_pid = 0;
        cancel_midi(a);
        error(a, "Cannot start MIDI renderer");
    }
}
void mpv_audio_play(MpvAudioEngine *a)
{
    if (strcmp(a->state, "stopped") == 0) return;
    a->want_play = true;
    property(a, "pause", "no");
}
void mpv_audio_pause(MpvAudioEngine *a)
{
    mpv_audio_set_paused(a, a->want_play);
}
void mpv_audio_set_paused(MpvAudioEngine *a, bool paused)
{
    if (strcmp(a->state, "stopped") == 0) return;
    a->want_play = !paused;
    property(a, "pause", a->want_play ? "no" : "yes");
}
void mpv_audio_seek(MpvAudioEngine *a, int ms)
{
    char value[64];
    snprintf(value, sizeof(value), "%.3f", KA_CLAMP(ms, 0, KA_MAX(a->duration, 0)) / 1000.0);
    const char *args[] = {"seek", value, "absolute+exact"};
    if (a->loaded) send_command(a, args, 3, 0);
}
void mpv_audio_set_volume(MpvAudioEngine *a, int v) { a->volume = KA_CLAMP(v, 0, 100); a->filters_dirty = true; }
void mpv_audio_set_balance(MpvAudioEngine *a, int v) { a->balance = KA_CLAMP(v, -100, 100); a->filters_dirty = true; }
void mpv_audio_set_preamp(MpvAudioEngine *a, double v) { a->preamp = KA_CLAMP(v, -12, 12); a->filters_dirty = true; }
void mpv_audio_set_eq_enabled(MpvAudioEngine *a, bool v) { a->eq_enabled = v; a->filters_dirty = true; }
void mpv_audio_set_eq_band(MpvAudioEngine *a, int band, double gain)
{
    if (band >= 0 && band < 10) {
        a->gains[band] = KA_CLAMP(gain, -12, 12);
        a->filters_dirty = true;
    }
}
int mpv_audio_get_position_ms(MpvAudioEngine *a) { return a->pos; }
int mpv_audio_get_duration_ms(MpvAudioEngine *a) { return a->duration; }
const char *mpv_audio_state(const MpvAudioEngine *a) { return a->state; }
const char *mpv_audio_current_file(const MpvAudioEngine *a) { return a->current_file; }
static void event(MpvAudioEngine *a, const char *line)
{
    char name[64] = "", prop[64] = "";
    json_get_str(line, "event", name, sizeof(name));
    if (strcmp(name, "start-file") == 0) {
        json_get_int(line, "playlist_entry_id", &a->event_entry);
    } else if (strcmp(name, "file-loaded") == 0) {
        a->loaded_entry = a->event_entry;
        if (a->entry >= 0 && a->entry == a->loaded_entry) {
            a->loaded = true;
            if (a->midi_wav) unlink(a->midi_wav); /* mpv now owns an open fd */
            state(a, a->want_play ? "playing" : "paused");
        }
    } else if (strcmp(name, "end-file") == 0) {
        long long entry = -1;
        json_get_int(line, "playlist_entry_id", &entry);
        if (entry < 0 || entry != a->entry) return;
        char reason[64] = "";
        json_get_str(line, "reason", reason, sizeof(reason));
        if (strcmp(reason, "eof") == 0 && a->loaded) {
            a->loaded = false;
            state(a, "stopped");
            if (a->cbs.eos) a->cbs.eos(a->cbs.ud);
        } else if (strcmp(reason, "error") == 0) {
            char message[512] = "mpv could not play this file";
            json_get_str(line, "file_error", message, sizeof(message));
            a->loaded = false;
            error(a, message);
        }
    } else if (strcmp(name, "property-change") == 0) {
        if (a->entry < 0 || a->entry != a->event_entry) return;
        json_get_str(line, "name", prop, sizeof(prop));
        double n = 0;
        bool flag;
        if (strcmp(prop, "time-pos") == 0 && json_get_num(line, "data", &n))
            a->pos = (int)KA_CLAMP(n * 1000, 0, INT_MAX);
        else if (strcmp(prop, "duration") == 0 && json_get_num(line, "data", &n))
            a->duration = (int)KA_CLAMP(n * 1000, 0, INT_MAX);
        else if (strcmp(prop, "pause") == 0 && json_get_bool(line, "data", &flag)) {
            if (a->loaded) {
                a->want_play = !flag;
                state(a, flag ? "paused" : "playing");
            }
        } else if (strcmp(prop, "metadata") == 0 || strcmp(prop, "audio-params") == 0 || strcmp(prop, "audio-bitrate") == 0) {
            char *data = json_get_raw(line, "data");
            if (data && strcmp(prop, "metadata") == 0) {
                if (!json_get_str(data, "title", a->tags.title, sizeof(a->tags.title)))
                    json_get_str(data, "TITLE", a->tags.title, sizeof(a->tags.title));
                if (!json_get_str(data, "artist", a->tags.artist, sizeof(a->tags.artist)))
                    json_get_str(data, "ARTIST", a->tags.artist, sizeof(a->tags.artist));
            } else if (data && strcmp(prop, "audio-params") == 0) {
                if (json_get_num(data, "samplerate", &n)) a->tags.sample_rate = (int)n;
                if (json_get_num(data, "channel-count", &n)) a->tags.channels = (int)n;
            } else if (json_get_num(line, "data", &n)) {
                a->tags.bitrate = (int)KA_CLAMP(n, 0, INT_MAX);
            }
            free(data);
            if (a->cbs.tag_found) a->cbs.tag_found(a->cbs.ud, &a->tags);
        }
    } else {
        long long id = -1;
        char message[256];
        if (!json_get_int(line, "request_id", &id)) return;
        if (id == 100 + a->generation) {
            if (json_get_str(line, "error", message, sizeof(message)) && strcmp(message, "success") != 0) {
                error(a, message);
                return;
            }
            char *data = json_get_raw(line, "data");
            if (data) json_get_int(data, "playlist_entry_id", &a->entry);
            free(data);
            if (a->entry >= 0 && a->entry == a->loaded_entry) {
                a->loaded = true;
                state(a, a->want_play ? "playing" : "paused");
            }
        } else if (id == 2 && json_get_str(line, "error", message, sizeof(message)) && strcmp(message, "success") != 0) {
            error(a, message);
        }
    }
}
static void spectrum(MpvAudioEngine *a)
{
    float out[AUDIO_SPECTRUM_BANDS] = {0};
    if (a->have_samples && strcmp(a->state, "playing") == 0) {
        float frame[1024];
        for (int i = 0; i < 1024; i++) frame[i] = (float)(a->samples[i] * 0.5 * (1 - cos(2 * M_PI * i / 1023)));
        double mag[513];
        fft_real_mag(frame, 1024, mag);
        for (int b = 0; b < AUDIO_SPECTRUM_BANDS; b++) {
            int lo = b * 512 / AUDIO_SPECTRUM_BANDS, hi = (b + 1) * 512 / AUDIO_SPECTRUM_BANDS;
            double avg = 0;
            for (int k = lo; k < hi; k++) avg += mag[k];
            double db = 20 * log10(4 * avg / (hi - lo) / 1024 + 1e-10);
            out[b] = (float)KA_CLAMP((db + 80) / 80, 0, 1);
        }
    }
    if (a->cbs.spectrum) a->cbs.spectrum(a->cbs.ud, out, AUDIO_SPECTRUM_BANDS);
}
void mpv_audio_poll(MpvAudioEngine *a)
{
    if (a->midi_pid > 0) {
        int status;
        if (waitpid(a->midi_pid, &status, WNOHANG) == a->midi_pid) {
            a->midi_pid = 0;
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) load_media(a, a->midi_wav);
            else { cancel_midi(a); error(a, "MIDI render failed (check SoundFont and service log)"); }
        }
    }
    if (a->filters_dirty) filters(a);
    flush(a);
    char chunk[8192];
    ssize_t n;
    while (a->fd >= 0 && (n = recv(a->fd, chunk, sizeof(chunk), MSG_DONTWAIT)) > 0) {
        if (a->input_len + (size_t)n > IPC_LIMIT) { kill(a->pid, SIGTERM); break; }
        a->input = realloc(a->input, a->input_len + (size_t)n + 1);
        if (!a->input) abort();
        memcpy(a->input + a->input_len, chunk, (size_t)n);
        a->input_len += (size_t)n;
        a->input[a->input_len] = 0;
        char *end;
        while ((end = memchr(a->input, '\n', a->input_len))) {
            *end = 0;
            event(a, a->input);
            size_t used = (size_t)(end - a->input) + 1;
            a->input_len -= used;
            memmove(a->input, a->input + used, a->input_len + 1);
        }
    }
    while (a->meter_fd >= 0 && recv(a->meter_fd, a->samples, sizeof(a->samples), MSG_DONTWAIT) == sizeof(a->samples)) a->have_samples = true;
    if (a->pid > 0 && waitpid(a->pid, NULL, WNOHANG) == a->pid) {
        a->pid = 0;
        close(a->fd); close(a->meter_fd);
        a->fd = a->meter_fd = -1;
        a->input_len = a->output_len = 0;
        a->loaded = a->have_samples = false;
        cancel_midi(a);
        error(a, "kilix-mpv exited; check that it is installed (or set KILIX_MPV)");
    }
    uint32_t now = SDL_GetTicks();
    if (now - a->last_position >= 100) {
        a->last_position = now;
        if (a->cbs.position_changed) a->cbs.position_changed(a->cbs.ud, a->pos, a->duration);
    }
    if (now - a->last_spectrum >= 50) { a->last_spectrum = now; spectrum(a); }
}
void mpv_audio_cleanup(MpvAudioEngine *a)
{
    if (!a) return;
    cancel_midi(a);
    if (a->fd >= 0) close(a->fd);
    if (a->meter_fd >= 0) close(a->meter_fd);
    if (a->pid > 0) {
        kill(a->pid, SIGTERM);
        for (int i = 0; i < 100 && waitpid(a->pid, NULL, WNOHANG) == 0; i++) SDL_Delay(5);
        if (waitpid(a->pid, NULL, WNOHANG) == 0) { kill(a->pid, SIGKILL); waitpid(a->pid, NULL, 0); }
    }
    free(a->input); free(a->output); free(a->current_file);
    free(a);
}

AudioSourceInfo mpv_audio_source_info(const MpvAudioEngine *a)
{
    return (AudioSourceInfo){.ready = a->loaded, .seekable = a->loaded,
        .buffering = !strcmp(a->state, "loading"),
        .sample_rate = (unsigned)a->tags.sample_rate, .channels = (unsigned)a->tags.channels,
        .bitrate = (unsigned)a->tags.bitrate, .source_kind = KA_ENCODEC_FILE,
        .error_code = *a->last_error ? 1u : 0u};
}
const char *mpv_audio_error(const MpvAudioEngine *a) { return a->last_error; }
