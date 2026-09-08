/* Synthetic peers live only in this test executable. The production program's
 * worker entry always runs the actual decoder. No environment switch exists. */
#include "ktest.h"
#include "encodec_source.h"
#include <SDL.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef KA_WITH_ENCODEC
typedef struct {
    uint32_t magic, version, kind, bytes;
    uint64_t generation, position, samples;
    uint32_t rate, channels, profile, codebooks, result, reserved;
    uint64_t wire_pts_ms, wire_epoch;
} TestHeader;

static void transmit(TestHeader *header, const void *payload, bool with_fd)
{
    struct iovec parts[2] = {{header, sizeof(*header)}, {(void *)payload, header->bytes}};
    struct msghdr message = {0}; message.msg_iov = parts;
    message.msg_iovlen = header->bytes ? 2u : 1u;
    char control[CMSG_SPACE(sizeof(int))]; int fd = -1;
    if (with_fd) {
        memset(control, 0, sizeof(control));
        message.msg_control = control; message.msg_controllen = sizeof(control);
        struct cmsghdr *item = CMSG_FIRSTHDR(&message);
        item->cmsg_level = SOL_SOCKET; item->cmsg_type = SCM_RIGHTS; item->cmsg_len = CMSG_LEN(sizeof(int));
        fd = open("/dev/null", O_RDONLY); memcpy(CMSG_DATA(item), &fd, sizeof(fd));
    }
    if (sendmsg(3, &message, MSG_NOSIGNAL) != (ssize_t)(sizeof(*header) + header->bytes)) _exit(2);
    if (fd >= 0) close(fd);
}

static int fixture_peer(void)
{
    uint8_t packet[13000];
    struct pollfd channel = {3, POLLIN, 0};
    if (poll(&channel, 1u, 5000) <= 0) return 2;
    ssize_t length = recv(3, packet, sizeof(packet), 0);
    if (length < (ssize_t)sizeof(TestHeader) + 1) return 2;
    const char *mode = (const char *)packet + sizeof(TestHeader);
    TestHeader response = {.magic = 0x4b414543, .version = 2u, .kind = 10u,
        .generation = 1u, .samples = 50003u, .rate = 24000u, .channels = 1u, .profile = 1u, .codebooks = 4u};
    float pcm[4097] = {0};
    TestHeader load;
    memcpy(&load, packet, sizeof(load));
    bool installed = !strncmp(mode, "installed-", 10u);
    bool live = !strncmp(mode, "live-", 5u) || (installed && (load.reserved & 0xffu) != KA_ENCODEC_FILE);
    if (installed) {
        const char *root = mode + strlen(mode) + 1u;
        char expected[PATH_MAX], storage[16384]; struct passwd account, *found = NULL;
        if (!strcmp(mode, "installed-default")) {
            if (getpwuid_r(geteuid(), &account, storage, sizeof(storage), &found) || !found) return 2;
            int size = snprintf(expected, sizeof(expected), "%s/.local/gpu_terminal/kilix/data/desktop-apps", account.pw_dir);
            if (size <= 0 || (size_t)size >= sizeof(expected)) return 2;
        } else strcpy(expected, "/var/lib/kenc-fixture");
        if ((load.reserved & 0x100u) == 0u || (load.reserved & ~0x103u) != 0u
            || load.result != 2u || strcmp(root, expected) || root[strlen(root) + 1u] != '\0'
            || length != (ssize_t)(sizeof(load) + strlen(mode) + strlen(root) + 3u)) return 2;
    }
    if (live) { response.samples = 0u; response.reserved = 1u; }
    if (!strcmp(mode, "live-duration")) response.samples = 1u;
    if (!strcmp(mode, "live-profile")) { response.profile = 2u; response.rate = 48000u; response.channels = 2u; }
    if (!strcmp(mode, "magic")) response.magic++;
    if (!strcmp(mode, "version")) response.version++;
    if (!strcmp(mode, "future")) response.generation++;
    if (!strcmp(mode, "reserved")) response.reserved++;
    if (!strcmp(mode, "rate")) response.rate++;
    if (!strcmp(mode, "duration")) response.samples = UINT64_MAX;
    if (!strcmp(mode, "initial-position")) response.position = 960u;
    if (!strcmp(mode, "uncanonical-error")) { response.kind = 13u; response.result = 2u; }
    if (!strcmp(mode, "fd-privacy")) {
        for (int fd = 4; fd < 128; ++fd) if (fcntl(fd, F_GETFD) >= 0) response.reserved = 1u;
    }
    transmit(&response, NULL, !strcmp(mode, "surplus-fd"));
    if (!strncmp(mode, "seek-", 5)) {
        if (poll(&channel, 1u, 5000) <= 0) return 2;
        TestHeader request;
        if (recv(3, &request, sizeof(request), 0) != sizeof(request)) return 2;
        response.generation = request.generation; response.position = 24000u;
        if (!strcmp(mode, "seek-position")) response.position = 0u;
        if (!strcmp(mode, "seek-metadata")) response.samples++;
        transmit(&response, NULL, false);
    } else if (!strcmp(mode, "duplicate-ready")) {
        transmit(&response, NULL, false);
    } else {
        response.kind = 11u; response.bytes = 960u * sizeof(float);
        if (live) {
            response.reserved = 5u; response.wire_epoch = 1u; response.wire_pts_ms = 1000u;
            if (!strcmp(mode, "live-wire")) { response.reserved = 1u; response.wire_epoch = response.wire_pts_ms = 0u; }
            if (!strcmp(mode, "live-size")) response.bytes = 959u * sizeof(float);
            if (!strcmp(mode, "live-state")) { response.kind = 14u; response.reserved = 7u; response.bytes = 0u; }
            if (!strcmp(mode, "live-end")) { response.kind = 12u; response.bytes = 0u; }
            if (!strcmp(mode, "live-nonfinite")) pcm[0] = NAN;
            if (!strcmp(mode, "live-pts") || !strcmp(mode, "live-epoch")) {
                transmit(&response, pcm, false);
                response.position = 960u;
                if (!strcmp(mode, "live-pts")) response.wire_pts_ms += 80u;
                else response.wire_epoch = 0u;
            }
        }
        if (!strcmp(mode, "nonfinite")) pcm[0] = NAN;
        if (!strcmp(mode, "discontinuous")) response.position = 1u;
        if (!strcmp(mode, "odd-pcm")) response.bytes--;
        if (!strcmp(mode, "oversize")) response.bytes = sizeof(pcm);
        if (!strcmp(mode, "early-end")) { response.kind = 12u; response.bytes = 0u; }
        transmit(&response, pcm, false);
    }
    for (;;) pause();
}

static bool await_response(KaEncodec *source, bool failure)
{
    uint32_t deadline = SDL_GetTicks() + 5000u;
    while (SDL_GetTicks() < deadline) {
        ka_encodec_poll(source);
        KaEncodecInfo info = ka_encodec_info(source);
        if (info.failed) return failure;
        if (info.ready && !info.seeking && !failure) return true;
        SDL_Delay(1u);
    }
    return false;
}

static void test_refusals(void)
{
    const char *modes[] = {"magic", "version", "future", "reserved", "rate", "duration",
        "initial-position", "uncanonical-error", "surplus-fd", "duplicate-ready", "nonfinite",
        "discontinuous", "odd-pcm", "oversize", "early-end", "seek-position", "seek-metadata"};
    for (size_t i = 0u; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        KaEncodec *source = ka_encodec_open(modes[i], "", "", 1u);
        ASSERT_TRUE(source && !ka_encodec_info(source).failed);
        if (!strncmp(modes[i], "seek-", 5)) {
            ASSERT_TRUE(await_response(source, false));
            ASSERT_TRUE(ka_encodec_seek(source, 24999u));
        }
        if (!await_response(source, true)) KT_FAIL("peer %s was not refused", modes[i]);
        ASSERT_TRUE(ka_encodec_info(source).failed);
        ASSERT_STR_EQ(ka_encodec_error(source), "Malformed EnCodec source or worker response");
        float pcm = 27.0f; uint64_t position = 42u;
        ASSERT_EQ_INT(ka_encodec_read(source, &pcm, 1u, &position), -2);
        ASSERT_TRUE(pcm == 27.0f && position == 42u);
        ka_encodec_close(source); SDL_Delay(2u); ka_encodec_reap();
    }
}

static void test_private_descriptors(void)
{
    int raw = open("/dev/null", O_RDONLY);
    int sentinel = fcntl(raw, F_DUPFD, 60); close(raw);
    ASSERT_TRUE(sentinel >= 60);
    KaEncodec *source = ka_encodec_open("fd-privacy", "", "", 1u);
    ASSERT_TRUE(await_response(source, false));
    float pcm[960]; uint64_t position = UINT64_MAX;
    uint32_t deadline = SDL_GetTicks() + 5000u;
    int count = 0;
    while (!count && SDL_GetTicks() < deadline) {
        count = ka_encodec_read(source, pcm, 960u, &position); SDL_Delay(1u);
    }
    ASSERT_EQ_INT(count, 960u);
    ASSERT_EQ_INT(position, 0u);
    ka_encodec_close(source); close(sentinel); SDL_Delay(2u); ka_encodec_reap();
}

static void test_live_metadata_refusals(void)
{
    const char *modes[] = {"live-duration", "live-profile", "live-wire", "live-size",
        "live-state", "live-end", "live-nonfinite", "live-pts", "live-epoch"};
    for (size_t i = 0u; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        KaEncodec *source = ka_encodec_open_source(KA_ENCODEC_SOCKET, modes[i], -1, "", "", 2u);
        ASSERT_TRUE(source != NULL);
        uint32_t start = SDL_GetTicks(); float pcm[960]; uint64_t position = 0u;
        while (SDL_GetTicks() - start < 5000u && !ka_encodec_info(source).failed) {
            (void)ka_encodec_read(source, pcm, 960u, &position); SDL_Delay(1u);
        }
        KaEncodecInfo info = ka_encodec_info(source);
        ASSERT_TRUE(info.failed && info.live);
        ASSERT_EQ_INT(ka_encodec_error_code(source), 5u);
        if (!strcmp(modes[i], "live-pts") || !strcmp(modes[i], "live-epoch")) {
            ASSERT_TRUE(info.wire_valid && info.wire_epoch == 1u && info.wire_pts_ms == 1000u);
        } else ASSERT_FALSE(info.wire_valid);
        ka_encodec_close(source); SDL_Delay(2u); ka_encodec_reap();
    }
}

static void test_installed_request_binding(void)
{
    int input = open("/dev/null", O_RDONLY | O_CLOEXEC);
    ASSERT_TRUE(input >= 0);
    for (unsigned int kind = KA_ENCODEC_FILE; kind <= KA_ENCODEC_SOCKET; ++kind) {
        KaEncodec *source = ka_encodec_open_installed_source((KaEncodecKind)kind, "installed-root", input,
            "/var/lib/kenc-fixture", 2u);
        ASSERT_TRUE(source != NULL && await_response(source, false));
        ASSERT_FALSE(ka_encodec_info(source).failed);
        ASSERT_EQ_INT(ka_encodec_info(source).live, kind != KA_ENCODEC_FILE);
        ka_encodec_close(source); SDL_Delay(2u); ka_encodec_reap();
    }
    const char *old_home = getenv("HOME"); char *saved_home = old_home ? strdup(old_home) : NULL;
    setenv("HOME", "/not-the-account-home", 1);
    for (unsigned int i = 0u; i < 2u; ++i) {
        KaEncodec *source = ka_encodec_open_installed_source(KA_ENCODEC_FILE, "installed-default", -1,
            i ? "" : NULL, 2u);
        ASSERT_TRUE(source != NULL && await_response(source, false));
        ka_encodec_close(source); SDL_Delay(2u); ka_encodec_reap();
    }
    if (saved_home) { setenv("HOME", saved_home, 1); free(saved_home); } else unsetenv("HOME");
    char oversized[PATH_MAX + 1u]; memset(oversized, 'x', sizeof(oversized));
    oversized[0] = '/'; oversized[PATH_MAX] = '\0';
    const char *bad_roots[] = {"relative/root", oversized};
    for (unsigned int i = 0u; i < 2u; ++i) {
        KaEncodec *source = ka_encodec_open_installed_source(KA_ENCODEC_FILE, "installed-root", -1, bad_roots[i], 2u);
        ASSERT_TRUE(source != NULL && ka_encodec_info(source).failed);
        ASSERT_EQ_INT(ka_encodec_error_code(source), 1u);
        ka_encodec_close(source);
    }
    close(input);
}
#endif

int main(int argc, char **argv)
{
#ifdef KA_WITH_ENCODEC
    if (argc == 2 && !strcmp(argv[1], "--encodec-worker")) return fixture_peer();
    RUN(test_refusals);
    RUN(test_private_descriptors);
    RUN(test_live_metadata_refusals);
    RUN(test_installed_request_binding);
#else
    (void)argc; (void)argv;
    KaEncodec *source = ka_encodec_open("missing.kenc", "", "", 1u);
    ASSERT_TRUE(source && ka_encodec_info(source).failed);
    ASSERT_STR_EQ(ka_encodec_error(source), "EnCodec model unavailable or incompatible");
    ka_encodec_close(source);
    puts("Native IPC cases require ENCODEC=1; default build refusal checked.");
#endif
    return kt_summary("EnCodec IPC");
}
