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
#include <unistd.h>

#ifdef KA_WITH_ENCODEC
#include <kilix_encodec_file.h>
#endif

extern char **environ;

#define WORKER_FD 3
#define MAGIC UINT32_C(0x4b414543)
#define WIRE_VERSION 1u
#define PCM_FRAMES 2048u
#define MAX_PAYLOAD (PCM_FRAMES * 2u * sizeof(float))
#define PATH_PAYLOAD (3u * PATH_MAX)
#define MAX_CHILDREN 8u

enum { REQUEST_LOAD = 1, REQUEST_SEEK = 2, REPLY_READY = 10,
    REPLY_PCM = 11, REPLY_END = 12, REPLY_ERROR = 13 };

/* Private same-executable IPC, not a network or persisted media format. */
typedef struct {
    uint32_t magic, version, kind, bytes;
    uint64_t generation, position, samples;
    uint32_t rate, channels, profile, codebooks, result, reserved;
} Header;
_Static_assert(sizeof(Header) == 64u, "private IPC header size");

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
    KaEncodec *source = calloc(1u, sizeof(*source));
    if (source == NULL) { return NULL; }
    source->channel = -1; source->slot = MAX_CHILDREN; source->generation = 1u;
#ifndef KA_WITH_ENCODEC
    (void)path; (void)mono_assets; (void)stereo_assets; (void)threads;
    fail(source, 2u);
    return source;
#else
    char payload[PATH_PAYLOAD]; size_t bytes = 0u;
    const char *paths[] = {path, mono_assets == NULL ? "" : mono_assets, stereo_assets == NULL ? "" : stereo_assets};
    if (path == NULL || path[0] == '\0' || (threads != 1u && threads != 2u)) { fail(source, 1u); return source; }
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
    int result = posix_spawn_file_actions_init(&actions);
    if (result != 0) { close(pair[0]); close(pair[1]); fail(source, 3u); return source; }
    result = posix_spawn_file_actions_addclose(&actions, pair[0]);
    if (result == 0) { result = posix_spawn_file_actions_adddup2(&actions, pair[1], WORKER_FD); }
    if (result == 0) { result = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0); }
    if (result == 0) { result = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0); }
    if (result == 0) { result = posix_spawn_file_actions_addclosefrom_np(&actions, WORKER_FD + 1); }
    pid_t pid = -1;
    char *arguments[] = {"kilix-amp", "--encodec-worker", NULL};
    if (result == 0) { result = posix_spawn(&pid, "/proc/self/exe", &actions, NULL, arguments, environ); }
    posix_spawn_file_actions_destroy(&actions);
    close(pair[1]);
    if (result != 0) { close(pair[0]); fail(source, 3u); return source; }
    source->token = next_token++;
    children[source->slot] = (Child){pid, source->token};
    source->channel = pair[0];
    Header load = {0};
    load.magic = MAGIC; load.version = WIRE_VERSION; load.kind = REQUEST_LOAD;
    load.generation = source->generation; load.bytes = (uint32_t)bytes; load.result = threads;
    if (send_message(source->channel, &load, payload) != 1) { fail(source, 3u); }
    return source;
#endif
}

static bool valid_info(const Header *header)
{
    return header->samples > 0u && header->samples <= (uint64_t)header->rate * 86400u
        && header->position <= header->samples && header->reserved == 0u && header->result == 0u
        && ((header->profile == 1u && header->rate == 24000u && header->channels == 1u
             && (header->codebooks == 4u || header->codebooks == 8u || header->codebooks == 16u))
            || (header->profile == 2u && header->rate == 48000u && header->channels == 2u
                && (header->codebooks == 2u || header->codebooks == 4u || header->codebooks == 8u || header->codebooks == 16u)));
}

void ka_encodec_poll(KaEncodec *source)
{
    ka_encodec_reap();
    if (source == NULL || source->info.failed || source->channel < 0 || source->buffered > source->consumed) { return; }
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
        if (header.kind == REPLY_ERROR && header.bytes == 0u && header.result >= 1u && header.result <= 6u
            && header.position == 0u && header.samples == 0u && header.rate == 0u && header.channels == 0u
            && header.profile == 0u && header.codebooks == 0u && header.reserved == 0u) {
            fail(source, header.result); return;
        }
        if (!valid_info(&header)) { fail(source, 5u); return; }
        if (header.kind == REPLY_READY && header.bytes == 0u && (!source->info.ready || source->info.seeking)) {
            if (header.position != source->requested_position
                || (source->info.ready && (header.rate != source->info.sample_rate
                    || header.channels != source->info.channels || header.profile != source->info.profile
                    || header.codebooks != source->info.codebooks || header.samples != source->info.samples))) {
                fail(source, 5u); return;
            }
            source->info = (KaEncodecInfo){header.rate, header.channels, header.codebooks, header.profile,
                header.samples, header.position, true, false, false, false};
            source->next_position = header.position;
            continue;
        }
        if (!source->info.ready || source->info.seeking || header.rate != source->info.sample_rate
            || header.channels != source->info.channels || header.profile != source->info.profile
            || header.codebooks != source->info.codebooks || header.samples != source->info.samples
            || header.position != source->next_position) { fail(source, 5u); return; }
        if (header.kind == REPLY_END && header.bytes == 0u && header.position == header.samples) {
            source->info.ended = true;
            return;
        }
        if (header.kind != REPLY_PCM || header.bytes == 0u || header.bytes > MAX_PAYLOAD
            || header.bytes % (header.channels * sizeof(float)) != 0u) { fail(source, 5u); return; }
        size_t count = header.bytes / (header.channels * sizeof(float));
        if (count > PCM_FRAMES || count > header.samples - header.position) { fail(source, 5u); return; }
        memcpy(source->pcm, packet + sizeof(header), header.bytes);
        for (size_t i = 0u; i < count * header.channels; ++i) {
            if (!isfinite(source->pcm[i])) { fail(source, 5u); return; }
        }
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
    if (source == NULL || source->info.failed || !source->info.ready || sample >= source->info.samples
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
    default: return "EnCodec worker failed";
    }
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

static void worker_error(uint64_t generation, kenc_result result)
{
    Header response = {0};
    response.magic = MAGIC; response.version = WIRE_VERSION; response.kind = REPLY_ERROR;
    response.generation = generation; response.result = (uint32_t)result;
    while (send_message(WORKER_FD, &response, NULL) == 0) {
        if (await_io(POLLOUT) != 0) { break; }
    }
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
        || request.channels != 0u || request.profile != 0u || request.codebooks != 0u || request.reserved != 0u
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
                || request.codebooks != 0u || request.result != 0u || request.reserved != 0u) { exit_code = 1; break; }
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
