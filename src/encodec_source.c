#include "encodec_source.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifdef KA_WITH_ENCODEC
#include <kilix_encodec_file.h>
#endif

extern char **environ;

#define WORKER_FD 3
#define MAGIC UINT32_C(0x4b414543)
#define WIRE_VERSION 2u
#define PCM_FRAMES 2048u
#define MAX_PAYLOAD (PCM_FRAMES * 2u * sizeof(float))
#define PATH_PAYLOAD (3u * PATH_MAX)
#define MAX_CHILDREN 8u
#define META_LIVE 1u
#define META_DEGRADED 2u
#define META_WIRE 4u
#define MAX_LIVE_SAMPLES (UINT64_C(24000) * 86400u)
#define LIVE_TIMEOUT_MS 5000u
#define LIVE_ERR_TIMEOUT 7u
#define LIVE_ERR_DISCONNECTED 8u
#define LIVE_ERR_ENDPOINT 9u

enum { REQUEST_LOAD = 1, REQUEST_SEEK = 2, REPLY_READY = 10,
    REPLY_PCM = 11, REPLY_END = 12, REPLY_ERROR = 13, REPLY_STATE = 14 };

/* Private same-executable IPC, not a network or persisted media format. */
typedef struct {
    uint32_t magic, version, kind, bytes;
    uint64_t generation, position, samples;
    uint32_t rate, channels, profile, codebooks, result, reserved;
    uint64_t wire_pts_ms, wire_epoch;
} Header;
_Static_assert(sizeof(Header) == 80u, "private IPC header size");

typedef struct { pid_t pid; uint64_t token; } Child;
static Child children[MAX_CHILDREN];
#ifdef KA_WITH_ENCODEC
static uint64_t next_token = 1u;
#endif

struct KaEncodec {
    int channel;
    size_t slot;
    uint64_t token, generation, next_position, requested_position;
    KaEncodecInfo info;
    float pcm[PCM_FRAMES * 2u];
    size_t buffered, consumed;
    uint32_t error;
    size_t wire_samples;
    KaEncodecKind kind;
};

void ka_encodec_reap(void)
{
    for (size_t i = 0u; i < MAX_CHILDREN; ++i) {
        if (children[i].pid <= 0) { continue; }
        pid_t result = waitpid(children[i].pid, NULL, WNOHANG);
        if (result == children[i].pid || (result < 0 && errno == ECHILD)) {
            children[i].pid = 0;
            children[i].token = 0u;
        }
    }
}

static void fail(KaEncodec *source, uint32_t result)
{
    source->error = result;
    source->info.failed = true;
    source->info.seeking = false;
    source->buffered = source->consumed = 0u;
    if (source->channel >= 0) { close(source->channel); source->channel = -1; }
    if (source->slot < MAX_CHILDREN && children[source->slot].token == source->token
        && children[source->slot].pid > 0) {
        /* The PID remains our unreaped child, so it cannot have been reused. */
        (void)kill(children[source->slot].pid, SIGKILL);
    }
    ka_encodec_reap();
}

void ka_encodec_close(KaEncodec *source)
{
    if (source == NULL) { return; }
    fail(source, 0u);
    free(source);
}

static int send_message(int channel, const Header *header, const void *payload)
{
    struct iovec parts[2] = {{(void *)header, sizeof(*header)}, {(void *)payload, header->bytes}};
    struct msghdr message = {0};
    message.msg_iov = parts; message.msg_iovlen = header->bytes == 0u ? 1u : 2u;
    ssize_t written = sendmsg(channel, &message, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) { return 0; }
    return written == (ssize_t)(sizeof(*header) + header->bytes) ? 1 : -1;
}

KaEncodec *ka_encodec_open(const char *path, const char *mono_assets,
    const char *stereo_assets, unsigned int threads)
{
    return ka_encodec_open_source(KA_ENCODEC_FILE, path, -1, mono_assets, stereo_assets, threads);
}

KaEncodec *ka_encodec_open_source(KaEncodecKind kind, const char *path, int input_fd,
    const char *mono_assets, const char *stereo_assets, unsigned int threads)
{
    KaEncodec *source = calloc(1u, sizeof(*source));
    if (source == NULL) { return NULL; }
    source->channel = -1; source->slot = MAX_CHILDREN; source->generation = 1u;
    source->kind = kind;
    source->info.live = kind == KA_ENCODEC_STDIN || kind == KA_ENCODEC_SOCKET;
#ifndef KA_WITH_ENCODEC
    (void)path; (void)input_fd; (void)mono_assets; (void)stereo_assets; (void)threads;
    fail(source, 2u);
    return source;
#else
    char payload[PATH_PAYLOAD]; size_t bytes = 0u;
    const char *paths[] = {path, mono_assets == NULL ? "" : mono_assets, stereo_assets == NULL ? "" : stereo_assets};
    if (path == NULL || path[0] == '\0' || (threads != 1u && threads != 2u)
        || (kind != KA_ENCODEC_FILE && kind != KA_ENCODEC_STDIN && kind != KA_ENCODEC_SOCKET)
        || (kind == KA_ENCODEC_STDIN && input_fd < 0)) { fail(source, 1u); return source; }
    for (size_t i = 0u; i < 3u; ++i) {
        size_t count = strnlen(paths[i], PATH_MAX);
        if (count >= PATH_MAX) { fail(source, 1u); return source; }
        memcpy(payload + bytes, paths[i], count + 1u); bytes += count + 1u;
    }
    ka_encodec_reap();
    for (size_t i = 0u; i < MAX_CHILDREN; ++i) {
        if (children[i].pid == 0) { source->slot = i; break; }
    }
    if (source->slot == MAX_CHILDREN || next_token == UINT64_MAX) { fail(source, 3u); return source; }
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, pair) != 0) { fail(source, 3u); return source; }
    int bound = 65536;
    if (setsockopt(pair[0], SOL_SOCKET, SO_RCVBUF, &bound, sizeof(bound)) != 0
        || setsockopt(pair[1], SOL_SOCKET, SO_SNDBUF, &bound, sizeof(bound)) != 0) {
        close(pair[0]); close(pair[1]); fail(source, 3u); return source;
    }
    posix_spawn_file_actions_t actions;
    int inherited = kind == KA_ENCODEC_STDIN ? fcntl(input_fd, F_DUPFD_CLOEXEC, 5) : -1;
    if (kind == KA_ENCODEC_STDIN && inherited < 0) {
        close(pair[0]); close(pair[1]); fail(source, 1u); return source;
    }
    int result = posix_spawn_file_actions_init(&actions);
    if (result != 0) { if (inherited >= 0) close(inherited); close(pair[0]); close(pair[1]); fail(source, 3u); return source; }
    result = posix_spawn_file_actions_addclose(&actions, pair[0]);
    if (result == 0) { result = posix_spawn_file_actions_adddup2(&actions, pair[1], WORKER_FD); }
    if (result == 0 && inherited >= 0) { result = posix_spawn_file_actions_adddup2(&actions, inherited, 4); }
    if (result == 0) { result = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0); }
    if (result == 0) { result = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0); }
    if (result == 0) { result = posix_spawn_file_actions_addclosefrom_np(&actions, inherited >= 0 ? 5 : 4); }
    pid_t pid = -1;
    char *arguments[] = {"kilix-amp", "--encodec-worker", NULL};
    if (result == 0) { result = posix_spawn(&pid, "/proc/self/exe", &actions, NULL, arguments, environ); }
    posix_spawn_file_actions_destroy(&actions);
    if (inherited >= 0) { close(inherited); }
    close(pair[1]);
    if (result != 0) { close(pair[0]); fail(source, 3u); return source; }
    source->token = next_token++;
    children[source->slot] = (Child){pid, source->token};
    source->channel = pair[0];
    Header load = {0};
    load.magic = MAGIC; load.version = WIRE_VERSION; load.kind = REQUEST_LOAD;
    load.generation = source->generation; load.bytes = (uint32_t)bytes; load.result = threads;
    load.reserved = (uint32_t)kind;
    if (send_message(source->channel, &load, payload) != 1) { fail(source, 3u); }
    return source;
#endif
}

static bool valid_info(const Header *header, bool live)
{
    bool metadata = live
        ? header->samples == 0u && header->position <= MAX_LIVE_SAMPLES
            && (header->reserved & META_LIVE) != 0u && (header->reserved & ~7u) == 0u
            && header->profile == 1u
            && ((header->reserved & META_WIRE) != 0u || (header->wire_pts_ms == 0u && header->wire_epoch == 0u))
        : header->samples > 0u && header->samples <= (uint64_t)header->rate * 86400u
            && header->position <= header->samples && header->reserved == 0u
            && header->wire_pts_ms == 0u && header->wire_epoch == 0u;
    return metadata && header->result == 0u
        && ((header->profile == 1u && header->rate == 24000u && header->channels == 1u
             && (header->codebooks == 4u || header->codebooks == 8u || header->codebooks == 16u))
            || (header->profile == 2u && header->rate == 48000u && header->channels == 2u
                && (header->codebooks == 2u || header->codebooks == 4u || header->codebooks == 8u || header->codebooks == 16u)));
}

void ka_encodec_poll(KaEncodec *source)
{
    ka_encodec_reap();
    if (source == NULL || source->info.failed || source->channel < 0 || source->buffered > source->consumed
        || (source->info.ended && !source->info.seeking)) { return; }
    for (size_t iteration = 0u; iteration < 64u; ++iteration) {
        uint8_t packet[sizeof(Header) + MAX_PAYLOAD];
        struct iovec part = {packet, sizeof(packet)};
        struct msghdr message = {0}; message.msg_iov = &part; message.msg_iovlen = 1u;
        ssize_t got = recvmsg(source->channel, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) { return; }
        if (got < (ssize_t)sizeof(Header) || message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) { fail(source, 5u); return; }
        Header header; memcpy(&header, packet, sizeof(header));
        if (header.magic != MAGIC || header.version != WIRE_VERSION || header.bytes != (size_t)got - sizeof(header)) { fail(source, 5u); return; }
        if (header.generation < source->generation) { continue; }
        if (header.generation != source->generation) { fail(source, 5u); return; }
        if (header.kind == REPLY_ERROR && header.bytes == 0u && header.result >= 1u
            && header.result <= (source->info.live ? LIVE_ERR_ENDPOINT : 6u)
            && header.position == 0u && header.samples == 0u && header.rate == 0u && header.channels == 0u
            && header.profile == 0u && header.codebooks == 0u && header.reserved == 0u
            && header.wire_pts_ms == 0u && header.wire_epoch == 0u) {
            fail(source, header.result); return;
        }
        if (!valid_info(&header, source->info.live)) { fail(source, 5u); return; }
        if (header.kind == REPLY_READY && header.bytes == 0u && (!source->info.ready || source->info.seeking)) {
            if (header.position != source->requested_position
                || (source->info.ready && (header.rate != source->info.sample_rate
                    || header.channels != source->info.channels || header.profile != source->info.profile
                    || header.codebooks != source->info.codebooks || header.samples != source->info.samples))) {
                fail(source, 5u); return;
            }
            if (source->info.live && header.reserved != META_LIVE) { fail(source, 5u); return; }
            source->info.sample_rate = header.rate; source->info.channels = header.channels;
            source->info.codebooks = header.codebooks; source->info.profile = header.profile;
            source->info.samples = header.samples; source->info.position = header.position;
            source->info.ready = true; source->info.ended = source->info.seeking = false;
            source->next_position = header.position;
            continue;
        }
        if (!source->info.ready || source->info.seeking || header.rate != source->info.sample_rate
            || header.channels != source->info.channels || header.profile != source->info.profile
            || header.codebooks != source->info.codebooks || header.samples != source->info.samples
            || header.position != source->next_position) { fail(source, 5u); return; }
        bool degraded = (header.reserved & META_DEGRADED) != 0u;
        bool wire_valid = (header.reserved & META_WIRE) != 0u;
        if (source->info.live && header.kind != REPLY_PCM
            && (wire_valid != source->info.wire_valid || header.wire_pts_ms != source->info.wire_pts_ms
                || header.wire_epoch != source->info.wire_epoch)) { fail(source, 5u); return; }
        if (header.kind == REPLY_STATE && source->info.live && header.bytes == 0u
            && degraded) { source->info.degraded = true; continue; }
        if (header.kind == REPLY_END && header.bytes == 0u && (source->info.live || header.position == header.samples)) {
            source->info.ended = true;
            source->info.degraded = degraded;
            return;
        }
        if (header.kind != REPLY_PCM || header.bytes == 0u || header.bytes > MAX_PAYLOAD
            || header.bytes % (header.channels * sizeof(float)) != 0u) { fail(source, 5u); return; }
        size_t count = header.bytes / (header.channels * sizeof(float));
        if (count > PCM_FRAMES || count > (source->info.live ? MAX_LIVE_SAMPLES : header.samples) - header.position
            || (source->info.live && (!wire_valid || degraded || (count != 320u && count != 640u && count != 960u)))) {
            fail(source, 5u); return;
        }
        if (source->info.live && source->info.wire_valid
            && (header.wire_epoch < source->info.wire_epoch
                || (header.wire_epoch == source->info.wire_epoch
                    && (source->wire_samples != 960u || source->info.wire_pts_ms > UINT64_MAX - 40u
                        || header.wire_pts_ms != source->info.wire_pts_ms + 40u)))) { fail(source, 5u); return; }
        memcpy(source->pcm, packet + sizeof(header), header.bytes);
        for (size_t i = 0u; i < count * header.channels; ++i) {
            if (!isfinite(source->pcm[i])) { fail(source, 5u); return; }
        }
        source->info.degraded = degraded; source->info.wire_valid = wire_valid;
        source->info.wire_pts_ms = header.wire_pts_ms; source->info.wire_epoch = header.wire_epoch;
        source->wire_samples = count;
        source->buffered = count; source->consumed = 0u;
        source->info.position = header.position;
        source->next_position += count;
        return;
    }
}

int ka_encodec_read(KaEncodec *source, float *pcm, size_t scalar_capacity, uint64_t *position)
{
    if (source == NULL || pcm == NULL || position == NULL) { return -2; }
    ka_encodec_poll(source);
    if (source->info.failed) { return -2; }
    if (!source->info.ready || source->info.seeking) { return 0; }
    if (source->buffered == source->consumed) { return source->info.ended ? -1 : 0; }
    size_t count = source->buffered - source->consumed;
    if (count > scalar_capacity / source->info.channels) { count = scalar_capacity / source->info.channels; }
    if (count == 0u) { return -2; }
    memcpy(pcm, source->pcm + source->consumed * source->info.channels,
        count * source->info.channels * sizeof(*pcm));
    *position = source->info.position + source->consumed;
    source->consumed += count;
    return (int)count;
}

bool ka_encodec_seek(KaEncodec *source, uint64_t sample)
{
    if (source == NULL || source->info.failed || source->info.live || !source->info.ready || sample >= source->info.samples
        || source->generation == UINT64_MAX) { return false; }
    Header header = {0};
    header.magic = MAGIC; header.version = WIRE_VERSION; header.kind = REQUEST_SEEK;
    header.generation = source->generation + 1u; header.position = sample;
    if (send_message(source->channel, &header, NULL) != 1) { return false; }
    source->generation = header.generation;
    uint64_t span = source->info.profile == 1u ? 24000u : 47520u;
    source->requested_position = sample / span * span;
    source->buffered = source->consumed = 0u;
    source->info.seeking = true; source->info.ended = false;
    return true;
}

KaEncodecInfo ka_encodec_info(const KaEncodec *source)
{
    return source == NULL ? (KaEncodecInfo){0} : source->info;
}

const char *ka_encodec_error(const KaEncodec *source)
{
    if (source == NULL) { return "Cannot allocate EnCodec source"; }
    if (!source->info.failed) { return ""; }
    switch (source->error) {
    case 1u: return "Invalid EnCodec source request";
    case 2u: return "EnCodec model unavailable or incompatible";
    case 4u: return "Truncated EnCodec source";
    case 5u: return "Malformed EnCodec source or worker response";
    case 6u: return "Insufficient memory for EnCodec source";
    case LIVE_ERR_TIMEOUT: return "EnCodec live source stalled; reconnect required";
    case LIVE_ERR_DISCONNECTED: return "EnCodec live source disconnected; reconnect required";
    case LIVE_ERR_ENDPOINT: return "EnCodec live endpoint is unavailable or not private";
    default: return "EnCodec worker failed";
    }
}

unsigned int ka_encodec_error_code(const KaEncodec *source)
{
    return source == NULL ? 6u : source->error;
}

#ifdef KA_WITH_ENCODEC
static int limit_resource(int resource, rlim_t bound)
{
    struct rlimit limit;
    if (getrlimit(resource, &limit) != 0) { return -1; }
    if (limit.rlim_max > bound) { limit.rlim_max = bound; }
    if (limit.rlim_cur > limit.rlim_max) { limit.rlim_cur = limit.rlim_max; }
    return setrlimit(resource, &limit);
}

static int worker_receive(Header *header, uint8_t *payload, size_t capacity)
{
    struct iovec parts[2] = {{header, sizeof(*header)}, {payload, capacity}};
    struct msghdr message = {0}; message.msg_iov = parts; message.msg_iovlen = capacity > 0u ? 2u : 1u;
    ssize_t got = recvmsg(WORKER_FD, &message, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) { return 0; }
    if (got < (ssize_t)sizeof(*header) || message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)
        || header->magic != MAGIC || header->version != WIRE_VERSION
        || header->bytes != (size_t)got - sizeof(*header)) { return -1; }
    return 1;
}

static int await_io(short events)
{
    struct pollfd item = {WORKER_FD, events, 0};
    int result;
    do { result = poll(&item, 1u, -1); } while (result < 0 && errno == EINTR);
    return result > 0 && !(item.revents & (POLLERR | POLLHUP | POLLNVAL)) ? 0 : -1;
}

static void worker_error(uint64_t generation, uint32_t result)
{
    Header response = {0};
    response.magic = MAGIC; response.version = WIRE_VERSION; response.kind = REPLY_ERROR;
    response.generation = generation; response.result = (uint32_t)result;
    while (send_message(WORKER_FD, &response, NULL) == 0) {
        if (await_io(POLLOUT) != 0) { break; }
    }
}

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) { return UINT64_MAX; }
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

/* One absolute deadline covers a whole framed record, including its length.
 * No caller length causes allocation. The control channel stays observable
 * during a stalled producer, and parent closure ends this owned worker. */
static uint32_t live_read(int input, bool is_socket, uint8_t *bytes, size_t count,
                         uint64_t deadline, bool record_started)
{
    size_t done = 0u;
    while (done < count) {
        uint64_t now = monotonic_ms();
        if (now >= deadline) { return LIVE_ERR_TIMEOUT; }
        struct pollfd fds[] = {{WORKER_FD, POLLIN, 0}, {input, POLLIN, 0}};
        int ready = poll(fds, 2u, (int)(deadline - now));
        if (ready < 0 && errno == EINTR) { continue; }
        if (ready < 0 || fds[0].revents || (fds[1].revents & (POLLERR | POLLNVAL))) {
            return LIVE_ERR_DISCONNECTED;
        }
        if (ready == 0) { return LIVE_ERR_TIMEOUT; }
        ssize_t got = is_socket ? recv(input, bytes + done, count - done, MSG_DONTWAIT)
            : read(input, bytes + done, count - done);
        if (got < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) { continue; }
        if (got <= 0) { return done > 0u || record_started ? KENC_ERR_TRUNCATED : LIVE_ERR_DISCONNECTED; }
        done += (size_t)got;
    }
    return KENC_OK;
}

/* Retain the private directory object and connect through it. Replacing an
 * ancestor cannot redirect the endpoint after its ownership checks. */
static int live_socket(const char *path)
{
    if (path[0] != '/' || strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) { return -1; }
    char parts[sizeof(((struct sockaddr_un *)0)->sun_path)];
    memcpy(parts, path + 1u, strlen(path));
    int parent = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (parent < 0) { return -1; }
    char *part = parts;
    struct stat directory, before, after;
    int channel = -1;
    for (;;) {
        char *slash = strchr(part, '/');
        if (slash != NULL) { *slash = '\0'; }
        if (part[0] == '\0' || !strcmp(part, ".") || !strcmp(part, "..")) { goto done; }
        if (slash == NULL) { break; }
        int child = openat(parent, part, O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (child < 0) { goto done; }
        close(parent); parent = child;
        if (fstat(parent, &directory) != 0
            || (directory.st_uid != geteuid() && directory.st_uid != 0u)
            || ((directory.st_mode & 0022u) != 0u
                && !(directory.st_uid == 0u && (directory.st_mode & S_ISVTX) != 0u))) { goto done; }
        part = slash + 1u;
    }
    if (fstat(parent, &directory) != 0 || directory.st_uid != geteuid()
        || (directory.st_mode & 0077u) != 0u
        || fstatat(parent, part, &before, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISSOCK(before.st_mode) || before.st_uid != geteuid()
        || (before.st_mode & 0777u) != 0600u) { goto done; }
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    int size = snprintf(address.sun_path, sizeof(address.sun_path), "/proc/self/fd/%d/%s", parent, part);
    if (size < 0 || (size_t)size >= sizeof(address.sun_path)) { goto done; }
    channel = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (channel < 0) { goto done; }
    int bound = 4096;
    if (setsockopt(channel, SOL_SOCKET, SO_RCVBUF, &bound, sizeof(bound)) != 0) { goto refuse; }
    /* AF_UNIX nonblocking connect either completes or refuses immediately.
     * A full listen queue is a bounded refusal, not an unbounded retry. */
    if (connect(channel, (struct sockaddr *)&address, sizeof(address)) != 0) { goto refuse; }
    struct ucred peer; socklen_t peer_size = sizeof(peer);
    if (getsockopt(channel, SOL_SOCKET, SO_PEERCRED, &peer, &peer_size) != 0
        || peer_size != sizeof(peer) || peer.uid != geteuid()
        || fstatat(parent, part, &after, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISSOCK(after.st_mode) || after.st_dev != before.st_dev || after.st_ino != before.st_ino
        || after.st_uid != before.st_uid || (after.st_mode & 0777u) != 0600u) { goto refuse; }
    goto done;
refuse:
    close(channel); channel = -1;
done:
    close(parent);
    return channel;
}

static int live_stdin(bool *is_socket)
{
    struct stat before, after;
    if (fstat(4, &before) != 0) { return -1; }
    *is_socket = S_ISSOCK(before.st_mode);
    if (*is_socket) {
        int kind = 0; socklen_t length = sizeof(kind);
        if (getsockopt(4, SOL_SOCKET, SO_TYPE, &kind, &length) != 0 || kind != SOCK_STREAM) { return -1; }
        return 4; /* recv(MSG_DONTWAIT) leaves the caller's file flags unchanged. */
    }
    if (!S_ISFIFO(before.st_mode) && !S_ISREG(before.st_mode)) { return -1; }
    int input = open("/proc/self/fd/4", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (input < 0) { return -1; }
    if (fstat(input, &after) != 0 || before.st_dev != after.st_dev || before.st_ino != after.st_ino
        || (before.st_mode & S_IFMT) != (after.st_mode & S_IFMT)) { close(input); return -1; }
    if (S_ISREG(before.st_mode)) {
        off_t offset = lseek(4, 0, SEEK_CUR);
        if (offset < 0 || lseek(input, offset, SEEK_SET) != offset) { close(input); return -1; }
    }
    close(4);
    return input;
}

static int live_send(const Header *header, const void *payload)
{
    int result;
    while ((result = send_message(WORKER_FD, header, payload)) == 0) {
        if (await_io(POLLIN | POLLOUT) != 0) { return -1; }
        /* Live playback has no seek command. Any unexpected parent input is
         * refused instead of letting a malicious sender spin this loop. */
        struct pollfd request = {WORKER_FD, POLLIN, 0};
        if (poll(&request, 1u, 0) != 0) { return -1; }
    }
    return result;
}

static int worker_live(const Header *request, char *const paths[3])
{
    bool is_socket = request->reserved == KA_ENCODEC_SOCKET;
    int input = is_socket ? live_socket(paths[0]) : live_stdin(&is_socket);
    uint32_t result = input < 0 ? LIVE_ERR_ENDPOINT : KENC_OK;
    uint8_t bytes[KENC_FILE_HEADER_BYTES]; kenc_file_info info;
    kenc_model *model = NULL; kenc_decoder *decoder = NULL;
    if (result == KENC_OK) { result = live_read(input, is_socket, bytes, sizeof(bytes), monotonic_ms() + LIVE_TIMEOUT_MS, false); }
    if (result == KENC_OK) { result = kenc_file_header_read(&info, bytes, sizeof(bytes)); }
    if (result == KENC_OK && (info.flags != KENC_FILE_LIVE || info.profile != KENC_FILE_PROFILE_MONO)) {
        result = KENC_ERR_PROTOCOL;
    }
    kenc_options options = kenc_options_default();
    options.threads = (uint8_t)request->result;
    if (result == KENC_OK) { options.codebooks = info.codebooks; result = kenc_model_load(&model, paths[1]); }
    if (result == KENC_OK) { result = kenc_decoder_create(&decoder, model, &options); }
    kenc_model_free(model);
    if (result != KENC_OK) { goto done; }
    Header response = {.magic = MAGIC, .version = WIRE_VERSION, .kind = REPLY_READY,
        .generation = request->generation, .rate = 24000u, .channels = 1u,
        .profile = KENC_FILE_PROFILE_MONO, .codebooks = info.codebooks, .reserved = META_LIVE};
    if (live_send(&response, NULL) != 1) { result = LIVE_ERR_DISCONNECTED; goto done; }
    unsigned int discarded = 0u;
    uint64_t recovery_deadline = 0u;
    for (;;) {
        uint8_t prefix[4], packet[KENC_MAX_PACKET_BYTES];
        uint64_t deadline = monotonic_ms() + LIVE_TIMEOUT_MS;
        if (recovery_deadline != 0u && recovery_deadline < deadline) { deadline = recovery_deadline; }
        result = live_read(input, is_socket, prefix, sizeof(prefix), deadline, false);
        if (result != KENC_OK) { break; }
        uint32_t length = (uint32_t)prefix[0] | (uint32_t)prefix[1] << 8u
            | (uint32_t)prefix[2] << 16u | (uint32_t)prefix[3] << 24u;
        if (length == 0u) {
            response.kind = REPLY_END; response.bytes = 0u;
            if (live_send(&response, NULL) != 1) { result = LIVE_ERR_DISCONNECTED; }
            break;
        }
        if (length > sizeof(packet)) { result = KENC_ERR_PROTOCOL; break; }
        result = live_read(input, is_socket, packet, length, deadline, true);
        if (result != KENC_OK) { break; }
        kenc_packet_metadata metadata;
        result = kenc_packet_metadata_read(&metadata, packet, length, &options);
        if (result != KENC_OK) { break; } /* Malformed bytes are not epoch loss. */
        int16_t decoded[KENC_PACKET_SAMPLES]; size_t count = 0u;
        result = kenc_decoder_pull_s16(decoder, packet, length, decoded, KENC_PACKET_SAMPLES, &count, NULL);
        if (result == KENC_ERR_PROTOCOL) {
            if (++discarded > 50u) { break; }
            if (recovery_deadline == 0u) { recovery_deadline = monotonic_ms() + LIVE_TIMEOUT_MS; }
            if (discarded == 1u) {
                response.reserved |= META_DEGRADED;
                response.kind = REPLY_STATE; response.bytes = 0u;
                if (live_send(&response, NULL) != 1) { result = LIVE_ERR_DISCONNECTED; break; }
            }
            continue;
        }
        if (result != KENC_OK) { break; }
        if (count > MAX_LIVE_SAMPLES - response.position) { result = KENC_ERR_PROTOCOL; break; }
        discarded = 0u; recovery_deadline = 0u;
        float pcm[KENC_PACKET_SAMPLES];
        for (size_t i = 0u; i < count; ++i) { pcm[i] = (float)decoded[i] / 32768.0f; }
        response.wire_pts_ms = metadata.packet.pts_ms; response.wire_epoch = metadata.epoch;
        response.reserved = META_LIVE | META_WIRE;
        response.kind = REPLY_PCM; response.bytes = (uint32_t)(count * sizeof(*pcm));
        if (live_send(&response, pcm) != 1) { result = LIVE_ERR_DISCONNECTED; break; }
        response.position += count;
    }
done:
    if (input >= 0) { close(input); }
    kenc_decoder_free(decoder);
    if (result != KENC_OK) { worker_error(request->generation, result); }
    return result == KENC_OK ? 0 : 1;
}
#endif

int ka_encodec_worker_main(void)
{
#ifndef KA_WITH_ENCODEC
    return 69;
#else
    pid_t parent = getppid();
    int kind = 0; socklen_t length = sizeof(kind);
    if (parent <= 1 || prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parent
        || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0
        || getsockopt(WORKER_FD, SOL_SOCKET, SO_TYPE, &kind, &length) != 0 || kind != SOCK_SEQPACKET
        || limit_resource(RLIMIT_CORE, 0u) != 0
        || limit_resource(RLIMIT_AS, (rlim_t)2u * 1024u * 1024u * 1024u) != 0
        || limit_resource(RLIMIT_FSIZE, (rlim_t)512u * 1024u * 1024u) != 0
        || limit_resource(RLIMIT_NOFILE, 64u) != 0) { return 69; }
    Header request;
    uint8_t payload[PATH_PAYLOAD];
    int received;
    while ((received = worker_receive(&request, payload, sizeof(payload))) == 0) {
        if (await_io(POLLIN) != 0) { return 69; }
    }
    if (received < 0 || request.kind != REQUEST_LOAD || request.generation != 1u
        || request.position != 0u || request.samples != 0u || request.rate != 0u
        || request.channels != 0u || request.profile != 0u || request.codebooks != 0u || request.reserved > KA_ENCODEC_SOCKET
        || request.wire_pts_ms != 0u || request.wire_epoch != 0u
        || (request.result != 1u && request.result != 2u)) { return 69; }
    char *paths[3]; size_t offset = 0u;
    for (size_t i = 0u; i < 3u; ++i) {
        if (offset >= request.bytes) { return 69; }
        uint8_t *end = memchr(payload + offset, 0, request.bytes - offset);
        if (end == NULL || (size_t)(end - payload - offset) >= PATH_MAX) { return 69; }
        paths[i] = (char *)payload + offset;
        offset = (size_t)(end - payload) + 1u;
    }
    if (offset != request.bytes || paths[0][0] == '\0') { return 69; }
    if (request.reserved != KA_ENCODEC_FILE) { return worker_live(&request, paths); }
    int input = open(paths[0], O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    kenc_file_source *source = NULL; kenc_file_info info;
    kenc_result result = input < 0 ? KENC_ERR_INVALID : kenc_file_source_create(&source, input,
        paths[1], paths[2], (uint8_t)request.result, &info);
    if (input >= 0) { close(input); }
    if (result != KENC_OK) { worker_error(request.generation, result); return 1; }
    float *pcm = malloc(96000u * sizeof(*pcm));
    if (pcm == NULL) { kenc_file_source_free(source); worker_error(request.generation, KENC_ERR_MEMORY); return 1; }
    Header response = {0};
    response.magic = MAGIC; response.version = WIRE_VERSION; response.generation = request.generation;
    response.samples = info.samples; response.profile = info.profile; response.codebooks = info.codebooks;
    response.rate = info.profile == 1u ? 24000u : 48000u;
    response.channels = info.profile == 1u ? 1u : 2u;
    size_t count = 0u, consumed = 0u;
    uint64_t position = 0u;
    bool ready = true, ended = false, end_sent = false;
    int exit_code = 0;
    for (;;) {
        received = worker_receive(&request, NULL, 0u);
        if (received < 0) { break; }
        if (received > 0) {
            if (request.kind != REQUEST_SEEK || request.bytes != 0u || request.generation <= response.generation
                || request.samples != 0u || request.rate != 0u || request.channels != 0u || request.profile != 0u
                || request.codebooks != 0u || request.result != 0u || request.reserved != 0u
                || request.wire_pts_ms != 0u || request.wire_epoch != 0u) { exit_code = 1; break; }
            response.generation = request.generation;
            result = kenc_file_source_seek(source, request.position, &position);
            if (result != KENC_OK) { worker_error(response.generation, result); exit_code = 1; break; }
            consumed = count = 0u; ready = true; ended = end_sent = false;
        }
        if (!ready && consumed == count && !ended) {
            result = kenc_file_source_pull_f32(source, pcm, 96000u, &count, &position);
            consumed = 0u;
            if (result != KENC_OK) { worker_error(response.generation, result); exit_code = 1; break; }
            ended = count == 0u;
            continue; /* Process a pending seek before sending decoded output. */
        }
        if (end_sent) { if (await_io(POLLIN) != 0) { break; } continue; }
        response.position = position + consumed;
        response.kind = ready ? REPLY_READY : ended ? REPLY_END : REPLY_PCM;
        size_t frames = count - consumed;
        if (frames > PCM_FRAMES) { frames = PCM_FRAMES; }
        response.bytes = response.kind == REPLY_PCM ? (uint32_t)(frames * response.channels * sizeof(*pcm)) : 0u;
        int sent = send_message(WORKER_FD, &response, pcm + consumed * response.channels);
        if (sent < 0) { break; }
        if (sent == 0) { if (await_io(POLLIN | POLLOUT) != 0) { break; } continue; }
        if (ready) { ready = false; }
        else if (ended) { end_sent = true; }
        else { consumed += frames; }
    }
    free(pcm); kenc_file_source_free(source);
    return exit_code;
#endif
}
