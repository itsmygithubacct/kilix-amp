#include "dsp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

AudioBuf abuf_alloc(int frames, int channels)
{
    AudioBuf b = {0};
    b.frames = frames;
    b.channels = channels;
    b.data = calloc((size_t)KA_MAX(frames * channels, 1), sizeof(float));
    if (!b.data)
        abort();
    return b;
}

AudioBuf abuf_copy(const AudioBuf *src)
{
    AudioBuf b = abuf_alloc(src->frames, src->channels);
    memcpy(b.data, src->data,
           (size_t)abuf_samples(src) * sizeof(float));
    return b;
}

void abuf_free(AudioBuf *b)
{
    free(b->data);
    b->data = NULL;
    b->frames = 0;
}

void fft_complex(double *re, double *im, int n, bool inverse)
{
    /* Bit-reversal permutation */
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    /* Butterflies */
    for (int len = 2; len <= n; len <<= 1) {
        double ang = 2.0 * M_PI / len * (inverse ? 1.0 : -1.0);
        double wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cwr = 1.0, cwi = 0.0;
            for (int k = 0; k < len / 2; k++) {
                int a = i + k, b = i + k + len / 2;
                double ur = re[a], ui = im[a];
                double vr = re[b] * cwr - im[b] * cwi;
                double vi = re[b] * cwi + im[b] * cwr;
                re[a] = ur + vr;
                im[a] = ui + vi;
                re[b] = ur - vr;
                im[b] = ui - vi;
                double nwr = cwr * wr - cwi * wi;
                cwi = cwr * wi + cwi * wr;
                cwr = nwr;
            }
        }
    }
    if (inverse) {
        for (int i = 0; i < n; i++) {
            re[i] /= n;
            im[i] /= n;
        }
    }
}

void fft_real_mag(const float *in, int n, double *mag)
{
    double *re = malloc(sizeof(double) * (size_t)n * 2);
    if (!re)
        abort();
    double *im = re + n;
    for (int i = 0; i < n; i++) {
        re[i] = in[i];
        im[i] = 0.0;
    }
    fft_complex(re, im, n, false);
    for (int k = 0; k <= n / 2; k++)
        mag[k] = sqrt(re[k] * re[k] + im[k] * im[k]);
    free(re);
}

void biquad_design_peaking(Biquad *bq, double fs, double f0, double gain_db,
                           double q)
{
    double a = pow(10.0, gain_db / 40.0);
    double w0 = 2.0 * M_PI * f0 / fs;
    double alpha = sin(w0) / (2.0 * q);
    double cosw = cos(w0);

    double b0 = 1.0 + alpha * a;
    double b1 = -2.0 * cosw;
    double b2 = 1.0 - alpha * a;
    double a0 = 1.0 + alpha / a;
    double a1 = -2.0 * cosw;
    double a2 = 1.0 - alpha / a;

    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

void biquad_reset(Biquad *bq)
{
    bq->x1 = bq->x2 = bq->y1 = bq->y2 = 0.0;
}
