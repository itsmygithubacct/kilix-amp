/* Real live producer/worker/shared-decoder controls. Synthetic source audio is
 * supplied explicitly as a previously encoded mono file; nothing downloads. */
#include "ktest.h"
#include "audio.h"
#include "encodec_source.h"
#include <SDL.h>
#include <kilix_encodec_file.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *assets;
static kenc_file_info file_info;
static uint8_t records[50][KENC_MAX_PACKET_BYTES];
static size_t sizes[50];
static float reference[48000];

static void load_reference(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    kenc_file_reader *reader = NULL;
    ASSERT_TRUE(fd >= 0);
    ASSERT_EQ_INT(kenc_file_reader_create(&reader, fd, &file_info), KENC_OK);
    if (!reader || file_info.profile != 1u || file_info.samples < 48000u) exit(2);
    for (size_t i = 0u; i < 50u; ++i) {
        uint64_t position = 0u;
        ASSERT_EQ_INT(kenc_file_reader_next(reader, records[i], sizeof(records[i]), &sizes[i], &position), KENC_OK);
        ASSERT_EQ_INT(position, i * 960u);
    }
    kenc_file_reader_free(reader);
    kenc_file_source *source = NULL;
    ASSERT_EQ_INT(kenc_file_source_create(&source, fd, assets, "", 2u, &file_info), KENC_OK);
    close(fd);
    if (!source) exit(2);
    for (size_t i = 0u; i < 50u; ++i) {
        uint64_t position = 0u; size_t count = 0u;
        ASSERT_EQ_INT(kenc_file_source_pull_f32(source, reference + i * 960u, 960u, &count, &position), KENC_OK);
        ASSERT_EQ_INT(position, i * 960u); ASSERT_EQ_INT(count, 960u);
    }
    kenc_file_source_free(source);
}

static void write_all(int fd, const uint8_t *bytes, size_t size)
{
    while (size > 0u) {
        ssize_t written = write(fd, bytes, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) _exit(3);
        bytes += written; size -= (size_t)written;
    }
}

static void producer(int fd, const char *mode)
{
    uint8_t header[KENC_FILE_HEADER_BYTES];
    kenc_file_info live = {1u, file_info.codebooks, KENC_FILE_LIVE, 0u};
    if (kenc_file_header_write(&live, header, sizeof(header)) != KENC_OK) _exit(2);
    if (!strcmp(mode, "header")) header[0]++;
    if (!strcmp(mode, "stereo")) header[8] = 2u;
    write_all(fd, header, sizeof(header));
    if (!strcmp(mode, "timeout")) { for (;;) pause(); }
    if (!strcmp(mode, "disconnect") || !strcmp(mode, "header") || !strcmp(mode, "stereo")) return;
    uint8_t prefix[4] = {1u, 0u, 0u, 0u};
    if (!strcmp(mode, "prefix")) { write_all(fd, prefix, 2u); return; }
    if (!strcmp(mode, "oversize")) { memset(prefix, 255, sizeof(prefix)); write_all(fd, prefix, 4u); return; }
    if (!strcmp(mode, "trickle")) {
        for (size_t i = 0u; i < sizeof(prefix); ++i) { write_all(fd, prefix + i, 1u); usleep(1700000u); }
        for (;;) pause();
    }
    for (size_t i = 0u; i < 50u; ++i) {
        if ((!strcmp(mode, "join") && i < 7u) || (!strcmp(mode, "loss") && i == 10u)) continue;
        memset(prefix, 0, sizeof(prefix)); prefix[0] = (uint8_t)sizes[i];
        write_all(fd, prefix, sizeof(prefix));
        if (!strcmp(mode, "packet-empty")) return;
        if (!strcmp(mode, "packet-truncated")) { write_all(fd, records[i], sizes[i] - 1u); return; }
        uint8_t copy[KENC_MAX_PACKET_BYTES]; memcpy(copy, records[i], sizes[i]);
        if (!strcmp(mode, "packet-malformed")) copy[0]++;
        /* Irregular physical fragments must not change packet PCM or metadata. */
        write_all(fd, copy, 3u); write_all(fd, copy + 3u, sizes[i] - 3u);
    }
    memset(prefix, 0, sizeof(prefix)); write_all(fd, prefix, sizeof(prefix));
}

static void finish_child(pid_t child)
{
    int status;
    pid_t result = waitpid(child, &status, WNOHANG);
    if (result == 0) { kill(child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} }
}

static void drain(KaEncodec *source, const char *mode, unsigned int expected_error)
{
    uint32_t start = SDL_GetTicks(); bool ended = false, exact = true;
    size_t total = 0u; float pcm[257];
    size_t expected = !strcmp(mode, "join") ? 24000u : !strcmp(mode, "loss") ? 33600u : 48000u;
    while (SDL_GetTicks() - start < 12000u) {
        uint64_t position = UINT64_MAX;
        int count = ka_encodec_read(source, pcm, 257u, &position);
        if (count == -1) { ended = true; break; }
        if (count == -2) break;
        if (count == 0) { SDL_Delay(1u); continue; }
        size_t offset = !strcmp(mode, "join") ? total + 24000u
            : !strcmp(mode, "loss") && total >= 9600u ? total + 14400u : total;
        if (position != total || (size_t)count > expected - total || offset + (size_t)count > 48000u) { exact = false; break; }
        if (memcmp(pcm, reference + offset, (size_t)count * sizeof(*pcm))) exact = false;
        total += (size_t)count;
    }
    KaEncodecInfo info = ka_encodec_info(source);
    ASSERT_TRUE(info.live);
    ASSERT_FALSE(ka_encodec_seek(source, 0u));
    if (expected_error) {
        ASSERT_TRUE(info.failed);
        ASSERT_EQ_INT(ka_encodec_error_code(source), expected_error);
        ASSERT_TRUE(SDL_GetTicks() - start < 11000u);
        ASSERT_FALSE(ended);
    } else {
        if (info.failed) printf("live error: %s\n", ka_encodec_error(source));
        ASSERT_TRUE(ended && exact && !info.failed);
        ASSERT_EQ_INT(total, expected);
        ASSERT_EQ_INT(info.samples, 0u);
        ASSERT_EQ_INT(info.wire_pts_ms, 1960u);
        ASSERT_EQ_INT(info.wire_epoch, 1u);
        ASSERT_TRUE(info.wire_valid && !info.degraded);
        uint64_t position = 123u;
        ASSERT_EQ_INT(ka_encodec_read(source, pcm, 257u, &position), -1);
        ASSERT_EQ_INT(position, 123u);
    }
}

static void pipe_case(const char *mode, unsigned int error)
{
    printf("pipe %s\n", mode); fflush(stdout);
    int fds[2];
    if ((!strcmp(mode, "socket-stdin")
        ? socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds)
        : pipe2(fds, O_CLOEXEC)) != 0) exit(2);
    int flags = fcntl(fds[0], F_GETFL);
    pid_t child = fork();
    if (child < 0) exit(2);
    if (child == 0) { close(fds[0]); producer(fds[1], mode); close(fds[1]); _exit(0); }
    close(fds[1]);
    KaEncodec *source = ka_encodec_open_source(KA_ENCODEC_STDIN, "stdin", fds[0], assets, "", 2u);
    drain(source, mode, error);
    ASSERT_EQ_INT(fcntl(fds[0], F_GETFL), flags);
    close(fds[0]); ka_encodec_close(source); finish_child(child);
    SDL_Delay(20u); ka_encodec_reap();
}

static void regular_stdin_case(void)
{
    char path[] = "/tmp/kalive-input-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0 || unlink(path) != 0) exit(2);
    static const uint8_t ignored[] = "prefix to skip";
    write_all(fd, ignored, sizeof(ignored)); producer(fd, "normal");
    if (lseek(fd, sizeof(ignored), SEEK_SET) != (off_t)sizeof(ignored)) exit(2);
    int flags = fcntl(fd, F_GETFL);
    KaEncodec *source = ka_encodec_open_source(KA_ENCODEC_STDIN, "stdin", fd, assets, "", 2u);
    drain(source, "normal", 0u);
    ASSERT_EQ_INT(lseek(fd, 0, SEEK_CUR), sizeof(ignored));
    ASSERT_EQ_INT(fcntl(fd, F_GETFL), flags);
    close(fd); ka_encodec_close(source); SDL_Delay(20u); ka_encodec_reap();
}

static void thread_selection_case(void)
{
    const char *values[] = {"1", "2", "0", "1.0", "3"};
    unsigned int selected[] = {1u, 2u, 0u, 0u, 0u};
    for (size_t i = 0u; i < sizeof(selected) / sizeof(selected[0]); ++i) {
        int fds[2]; if (pipe2(fds, O_CLOEXEC) != 0) exit(2);
        setenv("KILIX_ENCODEC_THREADS", values[i], 1);
        AudioEngine *audio = audio_new();
        ASSERT_EQ_INT(audio_load_live(audio, KA_ENCODEC_STDIN, "stdin", fds[0]), selected[i] != 0u);
        AudioSourceInfo info = audio_source_info(audio);
        ASSERT_EQ_INT(info.threads, selected[i]);
        ASSERT_EQ_INT(info.error_code, selected[i] ? 0u : 1u);
        ASSERT_EQ_INT(info.sample_rate, 0u);
        ASSERT_EQ_INT(info.channels, 0u);
        ASSERT_FALSE(info.ready);
        audio_cleanup(audio); close(fds[0]); close(fds[1]);
        SDL_Delay(20u); ka_encodec_reap();
    }
    unsetenv("KILIX_ENCODEC_THREADS");
}

static void socket_case(bool private)
{
    char directory[] = "/tmp/kalive-XXXXXX";
    if (!mkdtemp(directory)) exit(2);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    snprintf(address.sun_path, sizeof(address.sun_path), "%s/source", directory);
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    mode_t old = umask(0177);
    int bound = bind(server, (struct sockaddr *)&address, sizeof(address)); umask(old);
    if (server < 0 || bound != 0 || listen(server, 1) != 0) exit(2);
    if (!private) chmod(address.sun_path, 0666);
    pid_t child = fork();
    if (child < 0) exit(2);
    if (child == 0) {
        int connection = accept4(server, NULL, NULL, SOCK_CLOEXEC);
        if (connection < 0) _exit(2);
        producer(connection, "normal"); close(connection); _exit(0);
    }
    close(server);
    KaEncodec *source = ka_encodec_open_source(KA_ENCODEC_SOCKET, address.sun_path, -1, assets, "", 2u);
    drain(source, "normal", private ? 0u : 9u);
    ka_encodec_close(source); finish_child(child);
    struct stat preserved; ASSERT_EQ_INT(lstat(address.sun_path, &preserved), 0);
    unlink(address.sun_path); rmdir(directory);
    SDL_Delay(20u); ka_encodec_reap();
}

static size_t tapped;
static bool tap_exact, audio_failed;
static void pcm_tap(void *unused, const float *pcm, size_t frames,
    unsigned int channels, unsigned int rate, uint64_t position)
{
    (void)unused;
    if (channels != 1u || rate != 24000u || position != tapped || frames > 48000u - tapped) {
        tap_exact = false; return;
    }
    if (memcmp(pcm, reference + tapped, frames * sizeof(*pcm))) tap_exact = false;
    tapped += frames;
}
static void audio_failure(void *unused, const char *error)
{
    (void)unused; audio_failed = true; printf("shared audio error: %s\n", error);
}
static void shared_audio_case(void)
{
    int fds[2]; if (pipe2(fds, O_CLOEXEC) != 0) exit(2);
    pid_t child = fork();
    if (child < 0) exit(2);
    if (child == 0) { close(fds[0]); producer(fds[1], "normal"); close(fds[1]); _exit(0); }
    close(fds[1]);
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    unsetenv("KILIX_ENCODEC_THREADS"); /* Exercise the measured two-thread default. */
    tapped = 0u; tap_exact = true; audio_failed = false;
    AudioEngine *audio = audio_new();
    AudioCallbacks callbacks = {.decoded_pcm = pcm_tap, .error = audio_failure};
    audio_set_callbacks(audio, &callbacks);
    audio_set_preamp(audio, -4.0); audio_set_eq_band(audio, 2, 3.0);
    audio_set_volume(audio, 65); audio_set_balance(audio, -35);
    ASSERT_TRUE(audio_load_live(audio, KA_ENCODEC_STDIN, "stdin", fds[0]));
    close(fds[0]); audio_play(audio);
    uint32_t start = SDL_GetTicks(); bool paused = false, played = false;
    while (SDL_GetTicks() - start < 15000u) {
        audio_poll(audio);
        if (!strcmp(audio_state(audio), "playing")) {
            played = true;
            if (!paused) {
                audio_pause(audio); int position = audio_get_position_ms(audio);
                for (unsigned int i = 0u; i < 20u; ++i) { audio_poll(audio); SDL_Delay(2u); }
                ASSERT_STR_EQ(audio_state(audio), "paused");
                ASSERT_EQ_INT(audio_get_position_ms(audio), position);
                audio_seek(audio, 1500); ASSERT_EQ_INT(audio_get_position_ms(audio), position);
                AudioSourceInfo info = audio_source_info(audio);
                ASSERT_TRUE(info.live && info.ready && !info.seekable);
                ASSERT_EQ_INT(info.threads, 2u);
                ASSERT_EQ_INT(audio_get_duration_ms(audio), 0);
                ASSERT_EQ_INT(info.samples, 0u);
                audio_pause(audio); paused = true;
            }
        }
        if (played && !strcmp(audio_state(audio), "stopped")) break;
        if (audio_failed) break;
        SDL_Delay(2u);
    }
    ASSERT_TRUE(played && paused && !audio_failed);
    ASSERT_TRUE(tap_exact);
    ASSERT_EQ_INT(tapped, 48000u);
    ASSERT_TRUE(audio_source_info(audio).ended);
    ASSERT_EQ_INT(audio_get_position_ms(audio), 2000);
    ASSERT_FALSE(audio_load_live(audio, KA_ENCODEC_STDIN, "stdin", STDIN_FILENO));
    audio_cleanup(audio); finish_child(child);
    SDL_Delay(20u); ka_encodec_reap(); SDL_Quit();
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--encodec-worker")) return ka_encodec_worker_main();
    if (argc != 4) return 2;
    signal(SIGPIPE, SIG_IGN);
    assets = argv[1]; load_reference(argv[2]);
    pipe_case("normal", 0u); pipe_case("join", 0u); pipe_case("loss", 0u);
    pipe_case("socket-stdin", 0u); regular_stdin_case();
    socket_case(true); socket_case(false);
    pipe_case("header", 5u); pipe_case("stereo", 5u); pipe_case("prefix", 4u);
    pipe_case("disconnect", 8u); pipe_case("oversize", 5u);
    pipe_case("packet-truncated", 4u); pipe_case("packet-malformed", 5u);
    pipe_case("packet-empty", 4u);
    pipe_case("timeout", 7u); pipe_case("trickle", 7u);
    if (strcmp(argv[3], "--development-only")) {
        setenv("KILIX_CONTENT_ROOT", argv[3], 1);
        shared_audio_case();
    } else puts("Explicit development live-byte checks: installed shared audio path not exercised.");
    thread_selection_case();
    return kt_summary("native EnCodec live");
}
