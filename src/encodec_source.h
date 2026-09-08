#ifndef KA_ENCODEC_SOURCE_H
#define KA_ENCODEC_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct KaEncodec KaEncodec;

typedef struct {
    uint32_t sample_rate, channels, codebooks, profile;
    uint64_t samples, position;
    bool ready, ended, failed, seeking;
} KaEncodecInfo;

/* Main-loop-owned adapter. The worker owns only decoding; Amp remains the
 * sole owner of playback devices and DSP. All parent I/O and child reaping
 * are nonblocking. Rapid source changes have a fixed eight-child ceiling. */
KaEncodec *ka_encodec_open(const char *path, const char *mono_assets,
    const char *stereo_assets, unsigned int threads);
void ka_encodec_poll(KaEncodec *source);
/* >0 frames, 0 buffering, -1 EOF, -2 error. PCM is interleaved float. */
int ka_encodec_read(KaEncodec *source, float *pcm, size_t scalar_capacity,
    uint64_t *position);
bool ka_encodec_seek(KaEncodec *source, uint64_t sample);
KaEncodecInfo ka_encodec_info(const KaEncodec *source);
const char *ka_encodec_error(const KaEncodec *source);
void ka_encodec_close(KaEncodec *source);
void ka_encodec_reap(void);
/* Internal same-executable worker entry; descriptor 3 is a private socketpair. */
int ka_encodec_worker_main(void);

#endif
