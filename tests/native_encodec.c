/* Explicit native integration executable. No fake decoder or audio override is
 * present in the player: this test owns its SDL dummy output and worker entry. */
#include "ktest.h"
#include "audio.h"
#include "encodec_source.h"
#include <SDL.h>
#include <kilix_encodec_file.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *asset24, *asset48, *content_root;
static float *reference;
static kenc_file_info metadata;
static unsigned int channels, rate;

static bool wait_ready(KaEncodec *source)
{
    uint32_t deadline = SDL_GetTicks() + 30000u;
    while (SDL_GetTicks() < deadline) {
        ka_encodec_poll(source);
        KaEncodecInfo info = ka_encodec_info(source);
        if (info.failed) { printf("worker: %s\n", ka_encodec_error(source)); return false; }
        if (info.ready && !info.seeking) return true;
        SDL_Delay(1u);
    }
    return false;
}

static void load_reference(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    ASSERT_TRUE(fd >= 0);
    kenc_file_source *source = NULL;
    ASSERT_EQ_INT(kenc_file_source_create(&source, fd, asset24, asset48, 2u, &metadata), KENC_OK);
    close(fd);
    if (!source || metadata.samples > 480000u) exit(2);
    channels = metadata.profile == 1u ? 1u : 2u;
    rate = metadata.profile == 1u ? 24000u : 48000u;
    reference = calloc((size_t)metadata.samples * channels, sizeof(*reference));
    ASSERT_TRUE(reference != NULL);
    if (!reference) exit(2);
    uint64_t total = 0u;
    for (;;) {
        float pcm[96000]; size_t count = 0u; uint64_t position = 0u;
        ASSERT_EQ_INT(kenc_file_source_pull_f32(source, pcm, 96000u, &count, &position), KENC_OK);
        ASSERT_EQ_INT(position, total);
        if (count == 0u) break;
        if (count > metadata.samples - total) exit(2);
        memcpy(reference + total * channels, pcm, count * channels * sizeof(*pcm));
        total += count;
    }
    ASSERT_EQ_INT(total, metadata.samples);
    kenc_file_source_free(source);
}

static void drain_exact(KaEncodec *source, uint64_t start)
{
    uint32_t deadline = SDL_GetTicks() + 30000u;
    uint64_t total = start; bool ended = false, exact = true;
    float pcm[514];
    while (SDL_GetTicks() < deadline) {
        uint64_t position = UINT64_MAX;
        int count = ka_encodec_read(source, pcm, sizeof(pcm) / sizeof(pcm[0]), &position);
        if (count == -1) { ended = true; break; }
        if (count < 0) { printf("drain: %s\n", ka_encodec_error(source)); break; }
        if (count == 0) { SDL_Delay(1u); continue; }
        if (position != total || (uint64_t)count > metadata.samples - total) { exact = false; break; }
        if (memcmp(pcm, reference + total * channels, (size_t)count * channels * sizeof(*pcm)) != 0)
            exact = false;
        total += (unsigned int)count;
    }
    ASSERT_TRUE(ended);
    ASSERT_TRUE(exact);
    ASSERT_EQ_INT(total, metadata.samples);
}

static void test_adapter(const char *path)
{
    KaEncodec *source = content_root
        ? ka_encodec_open_installed_source(KA_ENCODEC_FILE, path, -1, content_root, 2u)
        : ka_encodec_open(path, asset24, asset48, 2u);
    ASSERT_TRUE(source != NULL);
    ASSERT_TRUE(wait_ready(source));
    KaEncodecInfo info = ka_encodec_info(source);
    ASSERT_EQ_INT(info.profile, metadata.profile);
    ASSERT_EQ_INT(info.codebooks, metadata.codebooks);
    ASSERT_EQ_INT(info.sample_rate, rate);
    ASSERT_EQ_INT(info.channels, channels);
    ASSERT_EQ_INT(info.samples, metadata.samples);
    float sentinel = 42.0f; uint64_t position = 87u;
    /* An empty destination must never consume or overwrite accepted PCM. */
    int small = ka_encodec_read(source, &sentinel, 0u, &position);
    ASSERT_TRUE(small == -2 || small == 0);
    ASSERT_TRUE(sentinel == 42.0f && position == 87u);
    drain_exact(source, 0u);
    ASSERT_FALSE(ka_encodec_seek(source, metadata.samples));
    uint64_t span = metadata.profile == 1u ? 24000u : 47520u;
    ASSERT_TRUE(ka_encodec_seek(source, span + 3u));
    ASSERT_TRUE(ka_encodec_seek(source, 0u));
    ASSERT_TRUE(ka_encodec_seek(source, span + 7u));
    ASSERT_TRUE(wait_ready(source));
    ASSERT_EQ_INT(ka_encodec_info(source).position, span);
    drain_exact(source, span);
    ka_encodec_close(source);
}

typedef struct { uint64_t next, received; unsigned int errors, eos, tags; bool exact, playing, buffering; } Capture;

static void on_pcm(void *ud, const float *pcm, size_t frames, unsigned int ch,
    unsigned int sample_rate, uint64_t position)
{
    Capture *capture = ud;
    if (ch != channels || sample_rate != rate || position != capture->next
        || position > metadata.samples || frames > metadata.samples - position) {
        capture->exact = false; return;
    }
    if (memcmp(pcm, reference + position * channels, frames * channels * sizeof(*pcm)) != 0)
        capture->exact = false;
    capture->next += frames;
    capture->received += frames;
}

static void on_error(void *ud, const char *message) { ((Capture *)ud)->errors++; printf("audio: %s\n", message); }
static void on_eos(void *ud) { ((Capture *)ud)->eos++; }
static void on_state(void *ud, const char *state)
{
    Capture *capture = ud;
    if (strcmp(state, "playing") == 0) capture->playing = true;
    if (strcmp(state, "buffering") == 0) capture->buffering = true;
}
static void on_tags(void *ud, const AudioTags *tags)
{
    ((Capture *)ud)->tags++;
    ASSERT_EQ_INT(tags->sample_rate, rate);
    ASSERT_EQ_INT(tags->channels, channels);
    ASSERT_EQ_INT(tags->bitrate, metadata.codebooks * (metadata.profile == 1u ? 750u : 1500u));
}

static void test_audio(const char *path)
{
    Capture capture = {.exact = true};
    AudioEngine *audio = audio_new();
    AudioCallbacks callbacks = {.ud = &capture, .decoded_pcm = on_pcm, .error = on_error,
        .eos = on_eos, .state_changed = on_state, .tag_found = on_tags};
    audio_set_callbacks(audio, &callbacks);
    /* Exercise the actual chain while the tap remains before DSP. */
    audio_set_volume(audio, 37); audio_set_balance(audio, -31);
    audio_set_preamp(audio, -3); audio_set_eq_band(audio, 2, 5);
    audio_load(audio, path);
    ASSERT_STR_EQ(audio_state(audio), "loading");
    audio_play(audio);
    uint32_t deadline = SDL_GetTicks() + 30000u;
    while (capture.received == 0u && !capture.errors && SDL_GetTicks() < deadline) {
        audio_poll(audio); SDL_Delay(2u);
    }
    ASSERT_TRUE(capture.received > 0u);
    AudioSourceInfo info = audio_source_info(audio);
    ASSERT_TRUE(info.encodec && info.ready && info.seekable && !info.live);
    ASSERT_EQ_INT(info.samples, metadata.samples);
    ASSERT_EQ_INT(info.sample_rate, rate);
    ASSERT_EQ_INT(audio_get_duration_ms(audio), metadata.samples * 1000u / rate);
    audio_pause(audio);
    ASSERT_STR_EQ(audio_state(audio), "paused");
    uint64_t retained = capture.received;
    for (unsigned int i = 0u; i < 20u; ++i) { audio_poll(audio); SDL_Delay(2u); }
    ASSERT_EQ_INT(capture.received, retained);
    uint64_t span = metadata.profile == 1u ? 24000u : 47520u;
    int previous_position = audio_get_position_ms(audio);
    audio_seek(audio, (int)((span + 480u) * 1000u / rate));
    ASSERT_EQ_INT(audio_get_position_ms(audio), previous_position);
    deadline = SDL_GetTicks() + 30000u;
    unsigned int previous_tags = capture.tags;
    while (capture.tags == previous_tags && !capture.errors && SDL_GetTicks() < deadline) {
        audio_poll(audio); SDL_Delay(2u);
    }
    ASSERT_TRUE(capture.tags > previous_tags);
    ASSERT_EQ_INT(capture.received, retained);
    ASSERT_STR_EQ(audio_state(audio), "paused");
    ASSERT_EQ_INT(audio_get_position_ms(audio), span * 1000u / rate);
    capture.next = span;
    audio_pause(audio);
    deadline = SDL_GetTicks() + 30000u;
    while (!capture.eos && !capture.errors && SDL_GetTicks() < deadline) {
        audio_poll(audio); SDL_Delay(2u);
    }
    ASSERT_EQ_INT(capture.errors, 0u);
    ASSERT_EQ_INT(capture.eos, 1u);
    ASSERT_EQ_INT(capture.next, metadata.samples);
    ASSERT_TRUE(capture.exact && capture.playing && capture.buffering);
    ASSERT_STR_EQ(audio_state(audio), "stopped");
    /* Stop/replay must own a fresh worker; cancel while it loads. */
    audio_play(audio);
    ASSERT_STR_EQ(audio_state(audio), "loading");
    audio_stop(audio);
    ASSERT_STR_EQ(audio_state(audio), "stopped");
    audio_cleanup(audio);
}

static size_t count_fds(void)
{
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) exit(2);
    size_t count = 0u; struct dirent *entry;
    while ((entry = readdir(directory))) if (entry->d_name[0] != '.') ++count;
    closedir(directory); return count;
}

static void test_cancellation(const char *path)
{
    pid_t unrelated = fork();
    if (unrelated == 0) { for (;;) pause(); }
    ASSERT_TRUE(unrelated > 0);
    if (unrelated <= 0) exit(2);
    size_t before = count_fds();
    for (unsigned int i = 0u; i < 12u; ++i) {
        KaEncodec *source = content_root
            ? ka_encodec_open_installed_source(KA_ENCODEC_FILE, path, -1, content_root, 2u)
            : ka_encodec_open(path, asset24, asset48, 2u);
        ASSERT_TRUE(source != NULL && !ka_encodec_info(source).failed);
        ka_encodec_close(source);
        for (unsigned int wait = 0u; wait < 10u; ++wait) { ka_encodec_reap(); SDL_Delay(2u); }
    }
    ASSERT_EQ_INT(waitpid(unrelated, NULL, WNOHANG), 0);
    ASSERT_EQ_INT(count_fds(), before);
    kill(unrelated, SIGKILL);
    ASSERT_EQ_INT(waitpid(unrelated, NULL, 0), unrelated);
    ka_encodec_reap();
    ASSERT_EQ_INT(waitpid(-1, NULL, WNOHANG), -1);
    ASSERT_EQ_INT(errno, ECHILD);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--encodec-worker") == 0) return ka_encodec_worker_main();
    if (argc != 5) { fprintf(stderr, "usage: native_encodec FILE.kenc MONO_ASSETS STEREO_ASSETS CONTENT_ROOT|--development-only\n"); return 2; }
    asset24 = argv[2]; asset48 = argv[3];
    content_root = strcmp(argv[4], "--development-only") ? argv[4] : NULL;
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (content_root) setenv("KILIX_CONTENT_ROOT", content_root, 1);
    setenv("KILIX_ENCODEC_THREADS", "2", 1);
    load_reference(argv[1]);
    test_adapter(argv[1]);
    if (content_root) test_audio(argv[1]);
    else puts("Explicit development byte/path checks: shared installed audio path not exercised.");
    test_cancellation(argv[1]);
    free(reference); SDL_Quit();
    return kt_summary("native EnCodec worker/audio");
}
