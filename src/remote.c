#include "remote.h"
#include "control.h"
#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#define REPLY_LIMIT (32 * 1024 * 1024)
#define REQUEST_TIMEOUT 400

static bool ready(int fd, short events, uint32_t deadline)
{
    int left = (int32_t)(deadline - SDL_GetTicks());
    if (left <= 0) { errno = ETIMEDOUT; return false; }
    struct pollfd p = {.fd = fd, .events = events};
    int result = poll(&p, 1, left);
    if (result == 0) errno = ETIMEDOUT;
    return result > 0 && (p.revents & events);
}

/* Read an entire record: stream sockets can split UTF-8 and large playlists
 * anywhere. One deadline bounds the whole request, not each individual read. */
static char *exchange(Remote *r, const char *request)
{
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (strlen(r->path) >= sizeof(address.sun_path)) {
        snprintf(r->error, sizeof(r->error), "Control socket path is too long");
        return NULL;
    }
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", r->path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return NULL;
    uint32_t deadline = SDL_GetTicks() + REQUEST_TIMEOUT;
    struct stat before, after, parent;
    char *dir = ka_dirname(r->path);
    bool safe_parent = lstat(dir, &parent) == 0 && S_ISDIR(parent.st_mode)
        && parent.st_uid == geteuid() && !(parent.st_mode & 0077);
    free(dir);
    if (!safe_parent || lstat(r->path, &before) != 0 || !S_ISSOCK(before.st_mode)
        || before.st_uid != geteuid() || (before.st_mode & 0077)) {
        errno = EACCES; goto failed;
    }
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) goto failed;
    struct ucred peer;
    socklen_t peer_size = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) != 0
        || peer.uid != geteuid() || lstat(r->path, &after) != 0
        || before.st_dev != after.st_dev || before.st_ino != after.st_ino) {
        errno = EACCES; goto failed;
    }
    char *wire = ka_asprintf("%s\n", request);
    size_t length = strlen(wire), sent = 0;
    if (length > CONTROL_MAX_REQUEST) { free(wire); errno = EMSGSIZE; goto failed; }
    while (sent < length) {
        if (!ready(fd, POLLOUT, deadline)) { free(wire); goto failed; }
        ssize_t n = send(fd, wire + sent, length - sent, MSG_NOSIGNAL);
        if (n <= 0) { free(wire); goto failed; }
        sent += (size_t)n;
    }
    free(wire);
    char *reply = NULL;
    size_t used = 0;
    for (;;) {
        if (!ready(fd, POLLIN, deadline)) { free(reply); goto failed; }
        char chunk[16384];
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0 || used + (size_t)n > REPLY_LIMIT) { free(reply); goto failed; }
        reply = realloc(reply, used + (size_t)n + 1);
        if (!reply) abort();
        memcpy(reply + used, chunk, (size_t)n);
        used += (size_t)n;
        reply[used] = 0;
        char *end = memchr(reply, '\n', used);
        if (end) {
            *end = 0;
            close(fd);
            long long protocol;
            if (strlen(reply) != (size_t)(end - reply) || !json_validate_document(reply, REPLY_LIMIT)) {
                free(reply);
                snprintf(r->error, sizeof(r->error), "Malformed playback service reply");
                r->connected = false;
                return NULL;
            }
            if (!json_get_int(reply, "protocol", &protocol) || protocol != CONTROL_PROTOCOL_MAX) {
                free(reply);
                snprintf(r->error, sizeof(r->error), "Incompatible playback service protocol");
                r->connected = false;
                return NULL;
            }
            r->connected = true;
            return reply;
        }
    }
failed:
    close(fd);
    r->connected = false;
    memset(r->spectrum, 0, sizeof(r->spectrum));
    snprintf(r->error, sizeof(r->error), "Playback service unavailable: %s", strerror(errno));
    return NULL;
}

static bool handshake(Remote *r)
{
    char *reply = exchange(r, "{\"cmd\":\"ping\",\"protocol\":2}");
    long long service = 0;
    bool ok = false;
    if (reply) {
        json_get_bool(reply, "ok", &ok);
        json_get_int(reply, "service", &service);
        free(reply);
    }
    r->connected = ok && service >= 2;
    if (ok && service < 2)
        snprintf(r->error, sizeof(r->error), "An older Amp service is running; stop it before starting this version");
    return r->connected;
}

static bool start(Remote *r, bool live_stdin)
{
    if (handshake(r)) {
        if (!live_stdin) return true;
        snprintf(r->error, sizeof(r->error), "stdin requires a new service; use a separate --socket or an EnCodec Unix stream");
        return false;
    }
    /* A live incompatible service is reported, never killed or replaced. */
    if (strstr(r->error, "older Amp") || strstr(r->error, "Incompatible")) return false;
    char *parent_path = ka_dirname(r->path);
    mode_t previous_mask = umask(0077);
    bool made_parent = ka_mkdirs(parent_path);
    umask(previous_mask);
    struct stat parent;
    bool private_parent = made_parent && lstat(parent_path, &parent) == 0
        && S_ISDIR(parent.st_mode) && parent.st_uid == geteuid()
        && !(parent.st_mode & 0077);
    free(parent_path);
    if (!private_parent) {
        snprintf(r->error, sizeof(r->error), "Control socket needs a private owned parent directory");
        return false;
    }
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n < 0) return false;
    exe[n] = 0;
    pid_t child = fork();
    if (child == 0) {
        if (setsid() < 0) _exit(1);
        pid_t daemon = fork();
        if (daemon < 0) _exit(1);
        if (daemon > 0) _exit(0);
        unsetenv("KILIXAMP_EXIT_AFTER_MS");
        close_range(3, ~0U, 0);
        int input = live_stdin ? -1 : open("/dev/null", O_RDONLY);
        if (input >= 0) { dup2(input, 0); if (input > 2) close(input); }
        char *log = ka_asprintf("%s.log", r->path);
        char *dir = ka_dirname(log);
        ka_mkdirs(dir);
        free(dir);
        int out = open(log, O_CREAT | O_APPEND | O_WRONLY | O_NOFOLLOW, 0600);
        free(log);
        if (out >= 0) { dup2(out, 1); dup2(out, 2); if (out > 2) close(out); }
        execl(exe, exe, "--headless", "--socket", r->path, live_stdin ? "--encodec-stdin" : (char *)NULL, (char *)NULL);
        _exit(127);
    }
    if (child < 0) return false;
    while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
    uint32_t deadline = SDL_GetTicks() + 5000;
    while ((int32_t)(deadline - SDL_GetTicks()) > 0) {
        if (handshake(r)) return true;
        SDL_Delay(25);
    }
    return false;
}

bool remote_start(Remote *r) { return start(r, false); }
bool remote_start_stdin(Remote *r) { return start(r, true); }

static void floats(const char *reply, const char *key, float *values, int count)
{
    char *array = json_get_raw(reply, key);
    const char *cursor = array;
    for (int i = 0; i < count; i++) {
        char *value = json_array_next(&cursor);
        if (!value) break;
        values[i] = strtof(value, NULL);
        free(value);
    }
    free(array);
}

static void absorb(Remote *r, const char *reply)
{
    double n = 0;
    long long revision = -1, pid = 0;
    json_get_int(reply, "queue_revision", &revision);
    json_get_int(reply, "pid", &pid);
    if (revision != r->revision || pid != r->service_pid) {
        char *listing = exchange(r, "{\"cmd\":\"snapshot\",\"protocol\":2}");
        if (listing && playlist_apply_snapshot(r->playlist, listing)) {
            json_get_int(listing, "queue_revision", &r->revision);
            json_get_int(listing, "pid", &r->service_pid);
            absorb(r, listing);
            free(listing);
            return;
        }
        free(listing);
    }
    json_get_str(reply, "state", r->state, sizeof(r->state));
    json_get_str(reply, "title", r->title, sizeof(r->title));
    json_get_str(reply, "last_error", r->error, sizeof(r->error));
    if (!*r->error && (int32_t)(r->error_until - SDL_GetTicks()) > 0)
        snprintf(r->error, sizeof(r->error), "%s", r->command_error);
    if (json_get_num(reply, "pos", &n)) r->position = (int)(n * 1000);
    r->duration = json_get_num(reply, "len", &n) ? (int)(n * 1000) : 0;
    if (json_get_num(reply, "volume", &n)) r->volume = (int)n;
    if (json_get_num(reply, "balance", &n)) r->balance = (int)n;
    if (json_get_num(reply, "index", &n)) r->index = (int)n;
    if (json_get_num(reply, "repeat", &n)) r->repeat = (int)n;
    json_get_bool(reply, "shuffle", &r->shuffle);
    json_get_bool(reply, "eq_enabled", &r->eq_enabled);
    json_get_num(reply, "preamp", &r->preamp);
    floats(reply, "eq", r->eq, 10);
    floats(reply, "spectrum", r->spectrum, AUDIO_SPECTRUM_BANDS);
    json_get_str(reply, "track_title", r->tags.title, sizeof(r->tags.title));
    json_get_str(reply, "artist", r->tags.artist, sizeof(r->tags.artist));
    if (json_get_num(reply, "sample_rate", &n)) r->tags.sample_rate = (int)n;
    if (json_get_num(reply, "channels", &n)) r->tags.channels = (int)n;
    if (json_get_num(reply, "bitrate", &n)) r->tags.bitrate = (int)n;
    r->source = (AudioSourceInfo){0};
    json_get_bool(reply, "live", &r->source.live);
    json_get_bool(reply, "ready", &r->source.ready);
    json_get_bool(reply, "seekable", &r->source.seekable);
    json_get_bool(reply, "ended", &r->source.ended);
    json_get_bool(reply, "degraded", &r->source.degraded);
    json_get_bool(reply, "buffering", &r->source.buffering);
    json_get_bool(reply, "reconnect_required", &r->source.reconnect_required);
    if (r->index != playlist_current_index(r->playlist)) playlist_set_current(r->playlist, r->index);
    playlist_set_shuffle(r->playlist, r->shuffle);
    playlist_set_repeat(r->playlist, r->repeat);
}

bool remote_poll(Remote *r, bool force)
{
    uint32_t now = SDL_GetTicks();
    if (!force && now - r->last_poll < (r->connected ? 50u : 1000u)) return false;
    r->last_poll = now;
    char *reply = exchange(r, "{\"cmd\":\"state\",\"protocol\":2}");
    if (reply) { absorb(r, reply); free(reply); }
    return true;
}

void remote_request(Remote *r, JsonBuf *request)
{
    json_kv_int(request, "protocol", CONTROL_PROTOCOL_MAX);
    json_obj_end(request);
    char *reply = exchange(r, json_text(request));
    if (reply) {
        absorb(r, reply);
        bool ok = false;
        json_get_bool(reply, "ok", &ok);
        if (!ok) {
            json_get_str(reply, "error", r->error, sizeof(r->error));
            snprintf(r->command_error, sizeof(r->command_error), "%s", r->error);
            r->error_until = SDL_GetTicks() + 4000;
        } else {
            r->error_until = 0;
        }
        free(reply);
    }
    r->last_poll = 0;
}

static void queue_edit(void *ud, JsonBuf *request)
{
    Remote *r = ud;
    char cmd[32] = "";
    json_get_str(json_text(request), "cmd", cmd, sizeof(cmd));
    if (strcmp(cmd, "add") != 0)
        json_kv_int(request, "queue_revision", r->revision);
    remote_request(r, request);
}

Remote *remote_new(Playlist *playlist, const char *path)
{
    Remote *r = calloc(1, sizeof(*r));
    if (!r) abort();
    r->playlist = playlist;
    r->path = path && *path ? ka_strdup(path) : control_default_socket_path();
    r->revision = -1;
    snprintf(r->state, sizeof(r->state), "stopped");
    playlist_set_remote(playlist, queue_edit, r);
    return r;
}
static JsonBuf command(const char *cmd)
{
    JsonBuf j = {0};
    json_obj_begin(&j);
    json_kv_str(&j, "cmd", cmd);
    return j;
}
void remote_command(Remote *r, const char *cmd)
{
    JsonBuf j = command(cmd);
    remote_request(r, &j); json_free(&j);
}
void remote_number(Remote *r, const char *cmd, const char *key, double value)
{
    JsonBuf j = command(cmd);
    if (!strcmp(key, "level")) json_kv_int(&j, key, (long long)value);
    else json_kv_num(&j, key, value);
    remote_request(r, &j); json_free(&j);
}
void remote_boolean(Remote *r, const char *cmd, const char *key, bool value)
{
    JsonBuf j = command(cmd);
    json_kv_bool(&j, key, value);
    remote_request(r, &j); json_free(&j);
}
void remote_play_index(Remote *r, int index)
{
    JsonBuf j = command("play");
    json_kv_int(&j, "index", index);
    json_kv_int(&j, "queue_revision", r->revision);
    remote_request(r, &j); json_free(&j);
}
void remote_eq_band(Remote *r, int band, double gain)
{
    JsonBuf j = command("eq_band");
    json_kv_int(&j, "band", band);
    json_kv_num(&j, "gain", gain);
    remote_request(r, &j); json_free(&j);
}
void remote_free(Remote *r)
{
    if (!r) return;
    playlist_set_remote(r->playlist, NULL, NULL);
    free(r->path); free(r);
}
