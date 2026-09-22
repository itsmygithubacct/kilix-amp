/* Observation-only LD_PRELOAD probe for tests/headless_admission_stereo.py.
 *
 * On a host with no model runtime every admitted model still ends in
 * KENC_ERR_MODEL, the same code a refused admission gives, so the player's
 * state alone cannot tell "admitted, then stopped at the runtime" from
 * "refused". This probe records, per call, what the installed admission
 * interface returned and what the native loaders then returned, as one JSON
 * line appended to $KA_ADMISSION_PROBE_LOG. It forwards every call unchanged
 * and never alters a result. Build: make ENCODEC=1 build-encodec/admission_probe.so
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "kilix_encodec.h"
#include "kilix_encodec_content.h"
#include "kilix_encodec_file.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void note(const char *format, ...)
{
    const char *path = getenv("KA_ADMISSION_PROBE_LOG");
    if (path == NULL || path[0] != '/') { return; }
    char line[4096];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof(line) - 1u, format, arguments);
    va_end(arguments);
    if (length <= 0 || (size_t)length >= sizeof(line) - 1u) { return; }
    line[length++] = '\n';
    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) { return; }
    (void)!write(fd, line, (size_t)length);
    (void)close(fd);
}

static void *next(const char *name)
{
    void *symbol = dlsym(RTLD_NEXT, name);
    if (symbol == NULL) { abort(); }
    return symbol;
}

kenc_result kenc_installed_assets_open(kenc_installed_assets **out,
    uint8_t profile, const char *root, uint32_t timeout_ms,
    kenc_admission_cancelled cancelled, void *context)
{
    kenc_result (*real)(kenc_installed_assets **, uint8_t, const char *, uint32_t,
        kenc_admission_cancelled, void *) = next("kenc_installed_assets_open");
    kenc_result result = real(out, profile, root, timeout_ms, cancelled, context);
    char files[3072] = "";
    size_t used = 0u, count = 0u;
    const kenc_asset_set *set = result == KENC_OK && out != NULL && *out != NULL
        ? kenc_installed_assets_files(*out) : NULL;
    if (set != NULL) {
        count = set->count;
        for (size_t i = 0u; i < set->count && used < sizeof(files) - 256u; ++i) {
            struct stat info;
            long long bytes = fstat(set->files[i].descriptor, &info) == 0 ? (long long)info.st_size : -1;
            int seals = fcntl(set->files[i].descriptor, F_GET_SEALS);
            int wrote = snprintf(files + used, sizeof(files) - used, "%s{\"name\":\"%s\",\"bytes\":%lld,\"seals\":%d}",
                i ? "," : "", set->files[i].name, bytes, seals);
            if (wrote < 0) { break; }
            used += (size_t)wrote;
        }
    }
    note("{\"call\":\"kenc_installed_assets_open\",\"pid\":%ld,\"profile\":%u,\"result\":%d,\"count\":%zu,\"files\":[%s]}",
        (long)getpid(), (unsigned int)profile, (int)result, count, files);
    return result;
}

kenc_result kenc_file_source_create_fds(kenc_file_source **out, int descriptor,
    const kenc_asset_set *mono_assets, const kenc_asset_set *stereo_assets,
    uint8_t threads, kenc_file_info *info)
{
    kenc_result (*real)(kenc_file_source **, int, const kenc_asset_set *, const kenc_asset_set *,
        uint8_t, kenc_file_info *) = next("kenc_file_source_create_fds");
    kenc_result result = real(out, descriptor, mono_assets, stereo_assets, threads, info);
    note("{\"call\":\"kenc_file_source_create_fds\",\"pid\":%ld,\"mono\":%zu,\"stereo\":%zu,\"result\":%d}",
        (long)getpid(), mono_assets ? mono_assets->count : 0u, stereo_assets ? stereo_assets->count : 0u, (int)result);
    return result;
}

kenc_result kenc_stereo_create_fds(kenc_stereo **out, const kenc_asset_set *assets,
    uint8_t codebooks, uint8_t threads)
{
    kenc_result (*real)(kenc_stereo **, const kenc_asset_set *, uint8_t, uint8_t) = next("kenc_stereo_create_fds");
    kenc_result result = real(out, assets, codebooks, threads);
    note("{\"call\":\"kenc_stereo_create_fds\",\"pid\":%ld,\"count\":%zu,\"result\":%d}",
        (long)getpid(), assets ? assets->count : 0u, (int)result);
    return result;
}

kenc_result kenc_model_load_fds(kenc_model **out, const kenc_asset_set *assets)
{
    kenc_result (*real)(kenc_model **, const kenc_asset_set *) = next("kenc_model_load_fds");
    kenc_result result = real(out, assets);
    note("{\"call\":\"kenc_model_load_fds\",\"pid\":%ld,\"count\":%zu,\"result\":%d}",
        (long)getpid(), assets ? assets->count : 0u, (int)result);
    return result;
}

/* The path-based development loaders. Installed playback must never reach them. */
kenc_result kenc_file_source_create(kenc_file_source **out, int descriptor,
    const char *mono_assets, const char *stereo_assets, uint8_t threads, kenc_file_info *info)
{
    kenc_result (*real)(kenc_file_source **, int, const char *, const char *, uint8_t,
        kenc_file_info *) = next("kenc_file_source_create");
    kenc_result result = real(out, descriptor, mono_assets, stereo_assets, threads, info);
    note("{\"call\":\"kenc_file_source_create\",\"pid\":%ld,\"result\":%d}", (long)getpid(), (int)result);
    return result;
}

kenc_result kenc_stereo_create(kenc_stereo **out, const char *asset_dir, uint8_t codebooks, uint8_t threads)
{
    kenc_result (*real)(kenc_stereo **, const char *, uint8_t, uint8_t) = next("kenc_stereo_create");
    kenc_result result = real(out, asset_dir, codebooks, threads);
    note("{\"call\":\"kenc_stereo_create\",\"pid\":%ld,\"result\":%d}", (long)getpid(), (int)result);
    return result;
}

kenc_result kenc_model_load(kenc_model **out, const char *asset_dir)
{
    kenc_result (*real)(kenc_model **, const char *) = next("kenc_model_load");
    kenc_result result = real(out, asset_dir);
    note("{\"call\":\"kenc_model_load\",\"pid\":%ld,\"result\":%d}", (long)getpid(), (int)result);
    return result;
}
