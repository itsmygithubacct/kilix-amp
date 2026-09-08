#ifndef KA_ENCODEC_SOURCE_H
#define KA_ENCODEC_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct KaEncodec KaEncodec;

typedef enum { KA_ENCODEC_FILE = 0, KA_ENCODEC_STDIN = 1, KA_ENCODEC_SOCKET = 2 } KaEncodecKind;

typedef struct {
    uint32_t sample_rate, channels, codebooks, profile;
    uint64_t samples, position;
    bool ready, ended, failed, seeking;
    bool live, degraded, wire_valid;
    uint64_t wire_pts_ms, wire_epoch;
} KaEncodecInfo;

/* Main-loop-owned adapter. The worker owns only decoding; Amp remains the
 * sole owner of playback devices and DSP. All parent I/O and child reaping
 * are nonblocking. Rapid source changes have a fixed eight-child ceiling. */
/* Development-only byte/path interfaces for explicit native fixtures. Normal
 * application playback must use the installed admission interface below. */
KaEncodec *ka_encodec_open(const char *path, const char *mono_assets,
    const char *stereo_assets, unsigned int threads);
/* Live stdin duplicates the caller's descriptor, without changing its flags.
 * Unix endpoints require a private owned parent and a mode-0600 same-UID peer.
 * Live sources are bounded framed mono streams, never seekable. Reconnection
 * is an explicit new open, with no automatic retry loop. */
KaEncodec *ka_encodec_open_source(KaEncodecKind kind, const char *path, int input_fd,
    const char *mono_assets, const char *stereo_assets, unsigned int threads);
/* Installed application path: fresh packaged catalog/receipt admission for
 * every model load, with no fallback to graph directories. A NULL storage root
 * uses the NSS home and Kilix's default desktop-apps directory. Host launchers
 * should pass their actual resolved root when storage is relocated. */
KaEncodec *ka_encodec_open_installed_source(KaEncodecKind kind, const char *path, int input_fd,
    const char *content_root, unsigned int threads);
void ka_encodec_poll(KaEncodec *source);
/* >0 frames, 0 buffering, -1 EOF, -2 error. PCM is interleaved float. */
int ka_encodec_read(KaEncodec *source, float *pcm, size_t scalar_capacity,
    uint64_t *position);
bool ka_encodec_seek(KaEncodec *source, uint64_t sample);
KaEncodecInfo ka_encodec_info(const KaEncodec *source);
const char *ka_encodec_error(const KaEncodec *source);
unsigned int ka_encodec_error_code(const KaEncodec *source);
void ka_encodec_close(KaEncodec *source);
void ka_encodec_reap(void);
/* Internal same-executable worker entry; descriptor 3 is a private socketpair. */
int ka_encodec_worker_main(void);

#endif
