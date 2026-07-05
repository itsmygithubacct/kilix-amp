/* In-house DSP primitives: complex FFT, biquad peaking filters, and the
 * interleaved float audio buffer type shared by the engine and editor. */
#ifndef KA_DSP_H
#define KA_DSP_H

#include "common.h"

/* Interleaved float32 audio. frames*channels samples in data. */
typedef struct {
    float *data;
    int frames;
    int channels;
} AudioBuf;

AudioBuf abuf_alloc(int frames, int channels); /* zeroed */
AudioBuf abuf_copy(const AudioBuf *src);
void abuf_free(AudioBuf *b);
static inline int abuf_samples(const AudioBuf *b)
{
    return b->frames * b->channels;
}

/* In-place iterative radix-2 complex FFT; n must be a power of two.
 * inverse=true applies 1/n scaling. */
void fft_complex(double *re, double *im, int n, bool inverse);
/* Real-input FFT: fills mag[0..n/2] with |X_k| (rfft magnitudes). */
void fft_real_mag(const float *in, int n, double *mag);

/* Audio EQ Cookbook peaking biquad (direct form I). */
typedef struct {
    double b0, b1, b2, a1, a2; /* normalized by a0 */
    double x1, x2, y1, y2;
} Biquad;

void biquad_design_peaking(Biquad *bq, double fs, double f0, double gain_db,
                           double q);
static inline float biquad_process(Biquad *bq, float x)
{
    double y = bq->b0 * x + bq->b1 * bq->x1 + bq->b2 * bq->x2 -
               bq->a1 * bq->y1 - bq->a2 * bq->y2;
    bq->x2 = bq->x1;
    bq->x1 = x;
    bq->y2 = bq->y1;
    bq->y1 = y;
    return (float)y;
}
void biquad_reset(Biquad *bq);

#endif
