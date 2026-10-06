/* Real live producer/worker/shared-decoder controls. Synthetic source audio is
 * supplied explicitly as a previously encoded mono file; nothing downloads. */
#include "ktest.h"
#include "audio.h"
#include "encodec_source.h"
#include <SDL.h>
#include <kilix_encodec_file.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <limits.h>
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
/* OD-AT: a C5-R4 stream encoded here from the same audio, and the PCM a C5-R4
 * decoder produces from it at the worker's thread count. The active_* pointers
 * select which stream the producer sends and drain() compares against. */
static uint8_t c5_records[50][KENC_MAX_PACKET_BYTES];
static size_t c5_sizes[50];
static float c5_reference[48000];
static float c5_reference_one_thread[48000];
static unsigned int worker_threads = 2u;
static kenc_epoch_start stream_profile = KENC_EPOCH_START_C0;
static uint8_t (*active_records)[KENC_MAX_PACKET_BYTES] = records;
static size_t *active_sizes = sizes;
static float *active_reference = reference;
/* Streams whose RESET packets carry DISCONTINUITY: explicit encoder resets
 * before packets 0 and DISC_RESET_AT (C0 flags 5, C5-R4 flags 13), with a
 * PTS jump at the second, then the ordinary cadence RESET 25 packets later.
 * Indexed by profile, with the PCM each decodes to at two worker threads. */
#define DISC_RESET_AT 13u
#define DISC_PTS_JUMP 400u
static uint8_t disc_records[2][50][KENC_MAX_PACKET_BYTES];
static size_t disc_sizes[2][50];
static float disc_reference[2][48000];
/* END (2) on a stream's final record, in the combinations the library decoder
 * accepts: alone on an ordinary record (C0 2, C5-R4 2), and on an explicit RESET
 * with DISCONTINUITY (C0 7, C5-R4 15) or without it (C0 3, C5-R4 11). The RESET
 * forms need a stream whose last packet follows an explicit encoder reset. A
 * record's PCM does not depend on its flags, so one reference serves each stream. */
#define END_AT 49u
static uint8_t tail_records[2][50][KENC_MAX_PACKET_BYTES];
static size_t tail_sizes[2][50];
static float tail_reference[2][48000];
/* The last packet's wire metadata a completed stream reports. */
static uint64_t expected_wire_pts = 1960u, expected_wire_epoch = 1u;
/* Set: sources open through installed admission from this content root. */
static const char *installed_root;

/* Offset of a KMA2 record's flags byte: after the magic and four varints
 * (profile, epoch, index, PTS). */
static size_t flags_offset(const uint8_t *record, size_t size)
{
    size_t position = 4u;
    for (unsigned int field = 0u; field < 4u; ++field) {
        while (position < size && (record[position] & 128u) != 0u) ++position;
        ++position;
    }
    if (position >= size) _exit(2);
    return position;
}

static const char *temp_root(void)
{
    const char *root = getenv("TMPDIR");
    return root != NULL && root[0] != '\0' ? root : "/tmp";
}

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

static void decode_c5(kenc_model *model, uint8_t threads, float *out)
{
    kenc_decoder *decoder = NULL;
    kenc_options options = kenc_options_default();
    options.codebooks = file_info.codebooks; options.threads = threads;
    ASSERT_EQ_INT(kenc_decoder_create(&decoder, model, &options), KENC_OK);
    ASSERT_EQ_INT(kenc_decoder_set_epoch_start(decoder, KENC_EPOCH_START_C5_R4), KENC_OK);
    for (size_t i = 0u; i < 50u; ++i) {
        int16_t decoded[KENC_PACKET_SAMPLES]; size_t count = 0u;
        ASSERT_EQ_INT(kenc_decoder_pull_s16(decoder, c5_records[i], c5_sizes[i], decoded,
            KENC_PACKET_SAMPLES, &count, NULL), KENC_OK);
        ASSERT_EQ_INT(count, KENC_PACKET_SAMPLES);
        for (size_t j = 0u; j < KENC_PACKET_SAMPLES; ++j) {
            out[i * KENC_PACKET_SAMPLES + j] = (float)decoded[j] / 32768.0f;
        }
    }
    kenc_decoder_free(decoder);
}

static void build_discontinuity(kenc_model *model, kenc_epoch_start profile)
{
    kenc_encoder *encoder = NULL; kenc_decoder *decoder = NULL;
    kenc_options options = kenc_options_default();
    options.codebooks = file_info.codebooks; options.threads = 2u;
    uint8_t reset = profile == KENC_EPOCH_START_C5_R4
        ? KENC_PACKET_FLAG_RESET | KENC_PACKET_FLAG_EPOCH_PREROLL : KENC_PACKET_FLAG_RESET;
    ASSERT_EQ_INT(kenc_encoder_create(&encoder, model, &options), KENC_OK);
    ASSERT_EQ_INT(kenc_encoder_set_epoch_start(encoder, profile), KENC_OK);
    ASSERT_EQ_INT(kenc_decoder_create(&decoder, model, &options), KENC_OK);
    ASSERT_EQ_INT(kenc_decoder_set_epoch_start(decoder, profile), KENC_OK);
    uint64_t pts = 0u;
    for (size_t i = 0u; i < 50u; ++i) {
        int16_t pcm[KENC_PACKET_SAMPLES], decoded[KENC_PACKET_SAMPLES]; size_t count = 0u;
        if (i == 0u || i == DISC_RESET_AT) kenc_encoder_reset(encoder);
        if (i == DISC_RESET_AT) pts += DISC_PTS_JUMP;
        for (size_t j = 0u; j < KENC_PACKET_SAMPLES; ++j) {
            float value = reference[i * KENC_PACKET_SAMPLES + j] * 32768.0f;
            pcm[j] = (int16_t)(value > 32767.0f ? 32767 : value < -32768.0f ? -32768 : (int)lrintf(value));
        }
        ASSERT_EQ_INT(kenc_encoder_push_s16(encoder, pcm, KENC_PACKET_SAMPLES, pts,
            disc_records[profile][i], sizeof(disc_records[profile][i]), &disc_sizes[profile][i]), KENC_OK);
        /* The fixture carries exactly the flags it claims to. */
        kenc_packet_metadata metadata = {0};
        ASSERT_EQ_INT(kenc_packet_metadata_read(&metadata, disc_records[profile][i], disc_sizes[profile][i], &options), KENC_OK);
        ASSERT_EQ_INT(metadata.packet.flags, i == 0u || i == DISC_RESET_AT
            ? reset | KENC_PACKET_FLAG_DISCONTINUITY : i == DISC_RESET_AT + 25u ? reset : 0u);
        ASSERT_EQ_INT(kenc_decoder_pull_s16(decoder, disc_records[profile][i], disc_sizes[profile][i], decoded,
            KENC_PACKET_SAMPLES, &count, NULL), KENC_OK);
        ASSERT_EQ_INT(count, KENC_PACKET_SAMPLES);
        for (size_t j = 0u; j < KENC_PACKET_SAMPLES; ++j) {
            disc_reference[profile][i * KENC_PACKET_SAMPLES + j] = (float)decoded[j] / 32768.0f;
        }
        pts += 40u;
    }
    kenc_decoder_free(decoder); kenc_encoder_free(encoder);
}

static uint8_t reset_flags(kenc_epoch_start profile)
{
    return profile == KENC_EPOCH_START_C5_R4
        ? KENC_PACKET_FLAG_RESET | KENC_PACKET_FLAG_EPOCH_PREROLL : KENC_PACKET_FLAG_RESET;
}

static bool is_end_mode(const char *mode)
{
    return !strcmp(mode, "end") || !strcmp(mode, "end-reset") || !strcmp(mode, "end-reset-discontinuity");
}

/* The flags an END mode finds on the final record, and the ones it writes. */
static void end_flags(const char *mode, kenc_epoch_start profile, uint8_t *found, uint8_t *set)
{
    uint8_t reset = reset_flags(profile);
    *found = !strcmp(mode, "end") ? 0u : reset | KENC_PACKET_FLAG_DISCONTINUITY;
    *set = !strcmp(mode, "end") ? KENC_PACKET_FLAG_END
        : !strcmp(mode, "end-reset") ? reset | KENC_PACKET_FLAG_END
        : reset | KENC_PACKET_FLAG_DISCONTINUITY | KENC_PACKET_FLAG_END;
}

/* The library decoder is the oracle: after the first 49 records it accepts the
 * final one carrying the mode's END flags and decodes it to `expected`. */
static void check_end_accepted(kenc_model *model, kenc_epoch_start profile, const char *mode,
    uint8_t (*stream)[KENC_MAX_PACKET_BYTES], const size_t *stream_sizes, const float *expected)
{
    kenc_decoder *decoder = NULL;
    kenc_options options = kenc_options_default();
    options.codebooks = file_info.codebooks; options.threads = 2u;
    ASSERT_EQ_INT(kenc_decoder_create(&decoder, model, &options), KENC_OK);
    ASSERT_EQ_INT(kenc_decoder_set_epoch_start(decoder, profile), KENC_OK);
    int16_t decoded[KENC_PACKET_SAMPLES]; size_t count = 0u; kenc_packet_info info;
    for (size_t i = 0u; i < END_AT; ++i) {
        ASSERT_EQ_INT(kenc_decoder_pull_s16(decoder, stream[i], stream_sizes[i], decoded,
            KENC_PACKET_SAMPLES, &count, NULL), KENC_OK);
    }
    uint8_t copy[KENC_MAX_PACKET_BYTES], found, set;
    end_flags(mode, profile, &found, &set);
    memcpy(copy, stream[END_AT], stream_sizes[END_AT]);
    size_t at = flags_offset(copy, stream_sizes[END_AT]);
    ASSERT_EQ_INT(copy[at], found);
    copy[at] = set;
    ASSERT_EQ_INT(kenc_decoder_pull_s16(decoder, copy, stream_sizes[END_AT], decoded,
        KENC_PACKET_SAMPLES, &count, &info), KENC_OK);
    ASSERT_EQ_INT(count, KENC_PACKET_SAMPLES);
    ASSERT_EQ_INT(info.flags, set);
    float last[KENC_PACKET_SAMPLES];
    for (size_t j = 0u; j < KENC_PACKET_SAMPLES; ++j) last[j] = (float)decoded[j] / 32768.0f;
    ASSERT_TRUE(memcmp(last, expected + END_AT * KENC_PACKET_SAMPLES, sizeof(last)) == 0);
    kenc_decoder_free(decoder);
}

/* A stream whose last packet follows an explicit reset: RESET|DISCONTINUITY,
 * at packet index 0 of a new epoch, with the pre-roll bit under C5-R4. */
static void build_tail(kenc_model *model, kenc_epoch_start profile)
{
    kenc_encoder *encoder = NULL; kenc_decoder *decoder = NULL;
    kenc_options options = kenc_options_default();
    options.codebooks = file_info.codebooks; options.threads = 2u;
    ASSERT_EQ_INT(kenc_encoder_create(&encoder, model, &options), KENC_OK);
    ASSERT_EQ_INT(kenc_encoder_set_epoch_start(encoder, profile), KENC_OK);
    ASSERT_EQ_INT(kenc_decoder_create(&decoder, model, &options), KENC_OK);
    ASSERT_EQ_INT(kenc_decoder_set_epoch_start(decoder, profile), KENC_OK);
    for (size_t i = 0u; i < 50u; ++i) {
        int16_t pcm[KENC_PACKET_SAMPLES], decoded[KENC_PACKET_SAMPLES]; size_t count = 0u;
        if (i == END_AT) kenc_encoder_reset(encoder);
        for (size_t j = 0u; j < KENC_PACKET_SAMPLES; ++j) {
            float value = reference[i * KENC_PACKET_SAMPLES + j] * 32768.0f;
            pcm[j] = (int16_t)(value > 32767.0f ? 32767 : value < -32768.0f ? -32768 : (int)lrintf(value));
        }
        ASSERT_EQ_INT(kenc_encoder_push_s16(encoder, pcm, KENC_PACKET_SAMPLES, (uint64_t)i * 40u,
            tail_records[profile][i], sizeof(tail_records[profile][i]), &tail_sizes[profile][i]), KENC_OK);
        kenc_packet_metadata metadata = {0};
        ASSERT_EQ_INT(kenc_packet_metadata_read(&metadata, tail_records[profile][i], tail_sizes[profile][i], &options), KENC_OK);
        ASSERT_EQ_INT(metadata.packet.flags, i == END_AT ? reset_flags(profile) | KENC_PACKET_FLAG_DISCONTINUITY
            : i == 0u || i == 25u ? reset_flags(profile) : 0u);
        ASSERT_EQ_INT(kenc_decoder_pull_s16(decoder, tail_records[profile][i], tail_sizes[profile][i], decoded,
            KENC_PACKET_SAMPLES, &count, NULL), KENC_OK);
        ASSERT_EQ_INT(count, KENC_PACKET_SAMPLES);
        for (size_t j = 0u; j < KENC_PACKET_SAMPLES; ++j) {
            tail_reference[profile][i * KENC_PACKET_SAMPLES + j] = (float)decoded[j] / 32768.0f;
        }
    }
    kenc_decoder_free(decoder); kenc_encoder_free(encoder);
    check_end_accepted(model, profile, "end-reset", tail_records[profile], tail_sizes[profile], tail_reference[profile]);
    check_end_accepted(model, profile, "end-reset-discontinuity", tail_records[profile], tail_sizes[profile], tail_reference[profile]);
}

static void build_c5(void)
{
    kenc_model *model = NULL; kenc_encoder *encoder = NULL; kenc_decoder *decoder = NULL;
    kenc_options options = kenc_options_default();
    options.codebooks = file_info.codebooks; options.threads = 2u;
    ASSERT_EQ_INT(kenc_model_load(&model, assets), KENC_OK);
    ASSERT_EQ_INT(kenc_encoder_create(&encoder, model, &options), KENC_OK);
    ASSERT_EQ_INT(kenc_encoder_set_epoch_start(encoder, KENC_EPOCH_START_C5_R4), KENC_OK);
    ASSERT_EQ_INT(kenc_decoder_create(&decoder, model, &options), KENC_OK);
    ASSERT_EQ_INT(kenc_decoder_set_epoch_start(decoder, KENC_EPOCH_START_C5_R4), KENC_OK);
    for (size_t i = 0u; i < 50u; ++i) {
        int16_t pcm[KENC_PACKET_SAMPLES], decoded[KENC_PACKET_SAMPLES]; size_t count = 0u;
        for (size_t j = 0u; j < KENC_PACKET_SAMPLES; ++j) {
            float value = reference[i * KENC_PACKET_SAMPLES + j] * 32768.0f;
            pcm[j] = (int16_t)(value > 32767.0f ? 32767 : value < -32768.0f ? -32768 : (int)lrintf(value));
        }
        ASSERT_EQ_INT(kenc_encoder_push_s16(encoder, pcm, KENC_PACKET_SAMPLES, (uint64_t)i * 40u,
            c5_records[i], sizeof(c5_records[i]), &c5_sizes[i]), KENC_OK);
        ASSERT_EQ_INT(kenc_decoder_pull_s16(decoder, c5_records[i], c5_sizes[i], decoded,
            KENC_PACKET_SAMPLES, &count, NULL), KENC_OK);
        ASSERT_EQ_INT(count, KENC_PACKET_SAMPLES);
        for (size_t j = 0u; j < KENC_PACKET_SAMPLES; ++j) {
            c5_reference[i * KENC_PACKET_SAMPLES + j] = (float)decoded[j] / 32768.0f;
        }
    }
    kenc_decoder_free(decoder); kenc_encoder_free(encoder);
    /* The worker decodes at its selected thread count; references match it. */
    decode_c5(model, 1u, c5_reference_one_thread);
    build_discontinuity(model, KENC_EPOCH_START_C0);
    build_discontinuity(model, KENC_EPOCH_START_C5_R4);
    build_tail(model, KENC_EPOCH_START_C0);
    build_tail(model, KENC_EPOCH_START_C5_R4);
    check_end_accepted(model, KENC_EPOCH_START_C0, "end", records, sizes, reference);
    check_end_accepted(model, KENC_EPOCH_START_C5_R4, "end", c5_records, c5_sizes, c5_reference);
    kenc_model_free(model);
}

static void select_stream(kenc_epoch_start profile)
{
    stream_profile = profile;
    bool c5 = profile == KENC_EPOCH_START_C5_R4;
    active_records = c5 ? c5_records : records;
    active_sizes = c5 ? c5_sizes : sizes;
    active_reference = c5 ? c5_reference : reference;
    expected_wire_pts = 1960u; expected_wire_epoch = 1u;
}

static void select_discontinuity(kenc_epoch_start profile)
{
    select_stream(profile);
    active_records = disc_records[profile]; active_sizes = disc_sizes[profile];
    active_reference = disc_reference[profile];
    expected_wire_pts = DISC_PTS_JUMP + 49u * 40u; expected_wire_epoch = 2u;
}

static void select_tail(kenc_epoch_start profile)
{
    select_stream(profile);
    active_records = tail_records[profile]; active_sizes = tail_sizes[profile];
    active_reference = tail_reference[profile];
    expected_wire_epoch = 2u;
}

static KaEncodec *open_live(KaEncodecKind kind, const char *path, int fd, unsigned int threads)
{
    return installed_root != NULL
        ? ka_encodec_open_installed_source(kind, path, fd, installed_root, threads)
        : ka_encodec_open_source(kind, path, fd, assets, "", threads);
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
    /* A C0 header for C5-R4 records is the marker mismatch a decoder must refuse. */
    kenc_epoch_start header_profile = !strcmp(mode, "c5-records-c0-header") ? KENC_EPOCH_START_C0
        : !strcmp(mode, "c0-records-c5-header") ? KENC_EPOCH_START_C5_R4 : stream_profile;
    if (kenc_file_header_write_epoch_start(&live, header_profile, header, sizeof(header)) != KENC_OK) _exit(2);
    if (!strcmp(mode, "header")) header[0]++;
    if (!strcmp(mode, "marker-unknown")) header[42] = 2u;          /* version 2, unknown marker */
    if (!strcmp(mode, "marker-v2-c0")) { header[4] = 2u; header[42] = 0u; } /* version 2 naming C0 */
    if (!strcmp(mode, "stereo")) header[8] = 2u;
    write_all(fd, header, sizeof(header));
    if (!strcmp(mode, "timeout")) { for (;;) pause(); }
    if (!strcmp(mode, "disconnect") || !strcmp(mode, "header") || !strcmp(mode, "stereo")
        || !strcmp(mode, "marker-unknown") || !strcmp(mode, "marker-v2-c0")) return;
    uint8_t prefix[4] = {1u, 0u, 0u, 0u};
    if (!strcmp(mode, "prefix")) { write_all(fd, prefix, 2u); return; }
    if (!strcmp(mode, "oversize")) { memset(prefix, 255, sizeof(prefix)); write_all(fd, prefix, 4u); return; }
    if (!strcmp(mode, "trickle")) {
        for (size_t i = 0u; i < sizeof(prefix); ++i) { write_all(fd, prefix + i, 1u); usleep(1700000u); }
        for (;;) pause();
    }
    /* The other profile's records: C0 records under a C5-R4 header and back. */
    bool c5 = stream_profile == KENC_EPOCH_START_C5_R4;
    uint8_t (*other_records)[KENC_MAX_PACKET_BYTES] = c5 ? records : c5_records;
    size_t *other_sizes = c5 ? sizes : c5_sizes;
    for (size_t i = 0u; i < 50u; ++i) {
        if ((!strcmp(mode, "join") && i < 7u)
            || ((!strcmp(mode, "loss") || !strcmp(mode, "loss-then-switch")) && i == 10u)) continue;
        /* Records from the other profile: from the start, or from the second
         * epoch on, either after an accepted epoch or while the decoder is
         * still discarding after the loss of packet 10. */
        bool other = !strcmp(mode, "c0-records-c5-header")
            || ((!strcmp(mode, "switch-to-c0") || !strcmp(mode, "switch-to-c5")
                || !strcmp(mode, "loss-then-switch")) && i >= 25u);
        const uint8_t *record = other ? other_records[i] : active_records[i];
        size_t record_size = other ? other_sizes[i] : active_sizes[i];
        memset(prefix, 0, sizeof(prefix)); prefix[0] = (uint8_t)record_size;
        write_all(fd, prefix, sizeof(prefix));
        if (!strcmp(mode, "packet-empty")) return;
        if (!strcmp(mode, "packet-truncated")) { write_all(fd, active_records[i], active_sizes[i] - 1u); return; }
        uint8_t copy[KENC_MAX_PACKET_BYTES]; memcpy(copy, record, record_size);
        if (!strcmp(mode, "packet-malformed")) copy[0]++;
        /* DISCONTINUITY without RESET (flags 4) is malformed in both profiles. */
        if (!strcmp(mode, "discontinuity-unreset") && i == 30u) {
            size_t at = flags_offset(copy, record_size);
            if (copy[at] != 0u) _exit(2);
            copy[at] = KENC_PACKET_FLAG_DISCONTINUITY;
        }
        /* END on the last record, which a decoder accepts and decodes. */
        if (is_end_mode(mode) && i == END_AT) {
            uint8_t found, set; end_flags(mode, stream_profile, &found, &set);
            size_t at = flags_offset(copy, record_size);
            if (copy[at] != found) _exit(2);
            copy[at] = set;
        }
        /* Irregular physical fragments must not change packet PCM or metadata. */
        write_all(fd, copy, 3u); write_all(fd, copy + 3u, record_size - 3u);
    }
    memset(prefix, 0, sizeof(prefix)); write_all(fd, prefix, sizeof(prefix));
}

static void finish_child(pid_t child)
{
    int status;
    pid_t result = waitpid(child, &status, WNOHANG);
    if (result == 0) { kill(child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} }
}

/* PCM a refused stream emits before its refusal: nothing, except a later
 * profile switch, which keeps exactly the epoch decoded before the mismatched
 * RESET; a switch while discarding after loss, which keeps the ten packets
 * before the loss; and a malformed packet 30, which keeps those before it. */
static size_t refused_total(const char *mode)
{
    if (!strcmp(mode, "switch-to-c0") || !strcmp(mode, "switch-to-c5")) return 24000u;
    if (!strcmp(mode, "loss-then-switch")) return 9600u;
    if (!strcmp(mode, "discontinuity-unreset")) return 28800u;
    return 0u;
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
        if (memcmp(pcm, active_reference + offset, (size_t)count * sizeof(*pcm))) exact = false;
        total += (size_t)count;
    }
    KaEncodecInfo info = ka_encodec_info(source);
    ASSERT_TRUE(info.live);
    ASSERT_FALSE(ka_encodec_seek(source, 0u));
    if (expected_error) {
        ASSERT_TRUE(info.failed);
        ASSERT_EQ_INT(total, refused_total(mode));
        ASSERT_TRUE(exact);
        ASSERT_EQ_INT(ka_encodec_error_code(source), expected_error);
        ASSERT_TRUE(SDL_GetTicks() - start < 11000u);
        ASSERT_FALSE(ended);
    } else {
        if (info.failed) printf("live error: %s\n", ka_encodec_error(source));
        ASSERT_TRUE(ended && exact && !info.failed);
        ASSERT_EQ_INT(total, expected);
        ASSERT_EQ_INT(info.samples, 0u);
        ASSERT_EQ_INT(info.wire_pts_ms, expected_wire_pts);
        ASSERT_EQ_INT(info.wire_epoch, expected_wire_epoch);
        ASSERT_TRUE(info.wire_valid && !info.degraded);
        uint64_t position = 123u;
        ASSERT_EQ_INT(ka_encodec_read(source, pcm, 257u, &position), -1);
        ASSERT_EQ_INT(position, 123u);
    }
}

static void pipe_case(const char *mode, unsigned int error)
{
    printf("%spipe %s\n", installed_root != NULL ? "installed " : "", mode); fflush(stdout);
    int fds[2];
    if ((!strcmp(mode, "socket-stdin")
        ? socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds)
        : pipe2(fds, O_CLOEXEC)) != 0) exit(2);
    int flags = fcntl(fds[0], F_GETFL);
    pid_t child = fork();
    if (child < 0) exit(2);
    if (child == 0) { close(fds[0]); producer(fds[1], mode); close(fds[1]); _exit(0); }
    close(fds[1]);
    KaEncodec *source = open_live(KA_ENCODEC_STDIN, "stdin", fds[0], worker_threads);
    drain(source, mode, error);
    ASSERT_EQ_INT(fcntl(fds[0], F_GETFL), flags);
    close(fds[0]); ka_encodec_close(source); finish_child(child);
    SDL_Delay(20u); ka_encodec_reap();
}

static void regular_stdin_case(void)
{
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/ka-in.XXXXXX", temp_root()) >= (int)sizeof(path)) exit(2);
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
    printf("%ssocket %s\n", installed_root != NULL ? "installed " : "", private ? "private" : "public");
    fflush(stdout);
    char directory[PATH_MAX];
    if (snprintf(directory, sizeof(directory), "%s/ka.XXXXXX", temp_root()) >= (int)sizeof(directory)
        || !mkdtemp(directory)) exit(2);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    int length = snprintf(address.sun_path, sizeof(address.sun_path), "%s/s", directory);
    if (length < 0 || (size_t)length >= sizeof(address.sun_path)) {
        fprintf(stderr, "TMPDIR is too long for a Unix socket path: %s\n", directory);
        rmdir(directory); exit(2);
    }
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
    KaEncodec *source = open_live(KA_ENCODEC_SOCKET, address.sun_path, -1, worker_threads);
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
    if (memcmp(pcm, active_reference + tapped, frames * sizeof(*pcm))) tap_exact = false;
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
    /* OD-AT: the decoder follows the live header's epoch-start marker. */
    build_c5();
    ASSERT_TRUE(memcmp(c5_reference, reference, sizeof(reference)) != 0);
    select_stream(KENC_EPOCH_START_C5_R4);
    pipe_case("normal", 0u); pipe_case("join", 0u); pipe_case("loss", 0u);
    socket_case(true); socket_case(false);
    /* One worker thread decodes against its own one-thread reference. */
    worker_threads = 1u; active_reference = c5_reference_one_thread;
    pipe_case("normal", 0u);
    worker_threads = 2u; active_reference = c5_reference;
    pipe_case("c5-records-c0-header", 5u); pipe_case("c0-records-c5-header", 5u);
    pipe_case("switch-to-c0", 5u);
    /* A mismatched RESET is refused even while discarding after loss. */
    pipe_case("loss-then-switch", 5u);
    pipe_case("marker-unknown", 5u); pipe_case("marker-v2-c0", 5u);
    select_stream(KENC_EPOCH_START_C0);
    /* Neither direction switches after an accepted epoch, or during loss. */
    pipe_case("switch-to-c5", 5u); pipe_case("loss-then-switch", 5u);
    /* RESET|DISCONTINUITY records (C0 5, C5-R4 13) decode exactly, across a
     * PTS jump; DISCONTINUITY without RESET (4) is refused in both profiles. */
    puts("discontinuity C0"); select_discontinuity(KENC_EPOCH_START_C0); pipe_case("normal", 0u);
    puts("discontinuity C5-R4"); select_discontinuity(KENC_EPOCH_START_C5_R4); pipe_case("normal", 0u);
    select_stream(KENC_EPOCH_START_C0); pipe_case("discontinuity-unreset", 5u);
    select_stream(KENC_EPOCH_START_C5_R4); pipe_case("discontinuity-unreset", 5u);
    /* END (2) on the last record decodes exactly, alone and with RESET, with
     * and without DISCONTINUITY; an Amp-side flag list must not refuse it. */
    static const struct { const char *label; kenc_epoch_start profile; } profiles[] = {
        {"C0", KENC_EPOCH_START_C0}, {"C5-R4", KENC_EPOCH_START_C5_R4}};
    for (size_t i = 0u; i < sizeof(profiles) / sizeof(profiles[0]); ++i) {
        printf("END %s\n", profiles[i].label);
        select_stream(profiles[i].profile); pipe_case("end", 0u);
        select_tail(profiles[i].profile);
        pipe_case("end-reset", 0u); pipe_case("end-reset-discontinuity", 0u);
    }
    select_stream(KENC_EPOCH_START_C0);
    if (strcmp(argv[3], "--development-only")) {
        setenv("KILIX_CONTENT_ROOT", argv[3], 1);
        shared_audio_case();
        /* The installed admission path selects the header's profile too. */
        select_stream(KENC_EPOCH_START_C5_R4); shared_audio_case(); select_stream(KENC_EPOCH_START_C0);
        /* Installed C5-R4 decoding with one worker thread, and over a private
         * Unix socket, selects the header's profile too (review r2). */
        installed_root = argv[3]; select_stream(KENC_EPOCH_START_C5_R4);
        worker_threads = 1u; active_reference = c5_reference_one_thread;
        pipe_case("normal", 0u);
        worker_threads = 2u; active_reference = c5_reference;
        socket_case(true);
        installed_root = NULL; select_stream(KENC_EPOCH_START_C0);
    } else puts("Explicit development live-byte checks: installed shared audio path not exercised.");
    thread_selection_case();
    return kt_summary("native EnCodec live");
}
