#include "effects.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Convert user-supplied durations before narrowing to AudioBuf's int frame
 * count.  Invalid requests are represented by a valid empty mono buffer by
 * generators; effects use the false result as a no-op. */
static bool duration_frames(int sr, double duration_sec, int channels,
                            int *frames)
{
    if (sr <= 0 || !isfinite(duration_sec) || duration_sec < 0.0)
        return false;
    double count = (double)sr * duration_sec;
    if (!isfinite(count) || count > INT_MAX)
        return false;
    int n = (int)count;
    if (!abuf_dimensions_valid(n, channels))
        return false;
    *frames = n;
    return true;
}

static AudioBuf empty_mono(void)
{
    return abuf_alloc(0, 1);
}

static bool generator_level_valid(double amplitude)
{
    return isfinite(amplitude) && amplitude >= 0.0 && amplitude <= 1.0;
}

static bool generator_frequency_valid(int sr, double frequency)
{
    return isfinite(frequency) && frequency >= 0.0 &&
           frequency <= (double)sr / 2.0;
}

/* Per-frame peak across channels. */
static double frame_peak(const AudioBuf *b, int f)
{
    double peak = 0.0;
    for (int ch = 0; ch < b->channels; ch++) {
        double v = fabs(b->data[f * b->channels + ch]);
        if (v > peak)
            peak = v;
    }
    return peak;
}

static double frame_mean(const AudioBuf *b, int f)
{
    double sum = 0.0;
    for (int ch = 0; ch < b->channels; ch++)
        sum += b->data[f * b->channels + ch];
    return sum / b->channels;
}

static AudioBuf scaled_clipped(const AudioBuf *in, double factor)
{
    AudioBuf out = abuf_alloc(in->frames, in->channels);
    int n = abuf_samples(in);
    for (int i = 0; i < n; i++)
        out.data[i] = (float)KA_CLAMP(in->data[i] * factor, -1.0, 1.0);
    return out;
}

AudioBuf fx_amplify(const AudioBuf *in, int sr, double gain_db)
{
    (void)sr;
    if (!isfinite(gain_db))
        return abuf_copy(in);
    double factor = pow(10.0, gain_db / 20.0);
    return isfinite(factor) ? scaled_clipped(in, factor) : abuf_copy(in);
}

AudioBuf fx_normalize(const AudioBuf *in, int sr, double peak_level_db)
{
    (void)sr;
    if (in->frames == 0 || !isfinite(peak_level_db))
        return abuf_copy(in);
    double peak = 0.0;
    int n = abuf_samples(in);
    for (int i = 0; i < n; i++)
        peak = KA_MAX(peak, fabs(in->data[i]));
    if (peak < 1e-10)
        return abuf_copy(in);
    double target = pow(10.0, peak_level_db / 20.0);
    return isfinite(target) ? scaled_clipped(in, target / peak)
                            : abuf_copy(in);
}

AudioBuf fx_loudness_normalize(const AudioBuf *in, int sr,
                               double target_lufs)
{
    (void)sr;
    if (!isfinite(target_lufs))
        return abuf_copy(in);
    int n = abuf_samples(in);
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum += (double)in->data[i] * in->data[i];
    double rms = n > 0 ? sqrt(sum / n) : 0.0;
    if (rms < 1e-10)
        return abuf_copy(in);
    double current_db = 20.0 * log10(rms);
    /* Approximate: LUFS ~ RMS dB - 0.691 (simplified) */
    double current_lufs = current_db - 0.691;
    double gain_db = target_lufs - current_lufs;
    double factor = pow(10.0, gain_db / 20.0);
    return isfinite(factor) ? scaled_clipped(in, factor) : abuf_copy(in);
}

AudioBuf fx_fade_in(const AudioBuf *in, int sr, double duration_sec)
{
    AudioBuf out = abuf_copy(in);
    if (sr <= 0 || !isfinite(duration_sec) || duration_sec <= 0.0)
        return out;
    double requested = duration_sec * (double)sr;
    if (!isfinite(requested))
        return out;
    int n = requested >= in->frames ? in->frames : (int)requested;
    if (n <= 0)
        return out;
    for (int f = 0; f < n; f++) {
        double ramp = n > 1 ? (double)f / (n - 1) : 0.0;
        for (int ch = 0; ch < out.channels; ch++)
            out.data[f * out.channels + ch] *= (float)ramp;
    }
    return out;
}

AudioBuf fx_fade_out(const AudioBuf *in, int sr, double duration_sec)
{
    AudioBuf out = abuf_copy(in);
    if (sr <= 0 || !isfinite(duration_sec) || duration_sec <= 0.0)
        return out;
    double requested = duration_sec * (double)sr;
    if (!isfinite(requested))
        return out;
    int n = requested >= in->frames ? in->frames : (int)requested;
    if (n <= 0)
        return out;
    int start = in->frames - n;
    for (int f = 0; f < n; f++) {
        double ramp = n > 1 ? 1.0 - (double)f / (n - 1) : 1.0;
        for (int ch = 0; ch < out.channels; ch++)
            out.data[(start + f) * out.channels + ch] *= (float)ramp;
    }
    return out;
}

AudioBuf fx_invert(const AudioBuf *in, int sr)
{
    (void)sr;
    AudioBuf out = abuf_copy(in);
    int n = abuf_samples(in);
    for (int i = 0; i < n; i++)
        out.data[i] = -out.data[i];
    return out;
}

AudioBuf fx_reverse(const AudioBuf *in, int sr)
{
    (void)sr;
    AudioBuf out = abuf_alloc(in->frames, in->channels);
    for (int f = 0; f < in->frames; f++)
        for (int ch = 0; ch < in->channels; ch++)
            out.data[f * in->channels + ch] =
                in->data[(in->frames - 1 - f) * in->channels + ch];
    return out;
}

AudioBuf fx_truncate_silence(const AudioBuf *in, int sr, double threshold_db,
                             int min_silence_ms)
{
    if (!isfinite(threshold_db))
        return abuf_copy(in);
    double threshold = pow(10.0, threshold_db / 20.0);
    if (!isfinite(threshold))
        return abuf_copy(in);

    /* The kept run can never exceed the input, so cap in double precision
     * before converting.  Negative values mean keep no internal silence. */
    int min_silence = 0;
    if (min_silence_ms > 0 && sr > 0) {
        double requested = (double)min_silence_ms * (double)sr / 1000.0;
        min_silence = requested >= in->frames ? in->frames : (int)requested;
    }

    /* Find first and last loud frame */
    int start = -1, end = -1;
    for (int f = 0; f < in->frames; f++) {
        if (frame_peak(in, f) > threshold) {
            if (start < 0)
                start = f;
            end = f + 1;
        }
    }
    if (start < 0)
        return abuf_alloc(0, in->channels); /* all silence */

    /* Collapse internal silent runs longer than min_silence */
    int n = end - start;
    float *tmp = malloc(sizeof(float) * (size_t)n * in->channels);
    if (!tmp)
        abort();
    int out_frames = 0;
    int i = 0;
    while (i < n) {
        bool loud = frame_peak(in, start + i) > threshold;
        int j = i;
        while (j < n && (frame_peak(in, start + j) > threshold) == loud)
            j++;
        int run = j - i;
        int keep = (!loud && run > min_silence) ? min_silence : run;
        memcpy(tmp + (size_t)out_frames * in->channels,
               in->data + (size_t)(start + i) * in->channels,
               sizeof(float) * (size_t)keep * in->channels);
        out_frames += keep;
        i = j;
    }

    AudioBuf out = abuf_alloc(out_frames, in->channels);
    memcpy(out.data, tmp, sizeof(float) * (size_t)out_frames * in->channels);
    free(tmp);
    return out;
}

AudioBuf fx_compressor(const AudioBuf *in, int sr, double threshold_db,
                       double ratio, double attack_ms, double makeup_db)
{
    if (in->frames == 0 || sr <= 0 || !isfinite(threshold_db) ||
        !isfinite(ratio) || !isfinite(attack_ms) || !isfinite(makeup_db) ||
        ratio <= 0.0 || attack_ms <= 0.0)
        return abuf_copy(in);
    double threshold = pow(10.0, threshold_db / 20.0);
    double attack_coeff = exp(-1.0 / (attack_ms / 1000.0 * sr));
    double release_coeff = exp(-1.0 / (0.1 * sr)); /* 100ms release */
    double makeup = pow(10.0, makeup_db / 20.0);
    if (!isfinite(threshold) || !isfinite(attack_coeff) ||
        !isfinite(release_coeff) || !isfinite(makeup))
        return abuf_copy(in);

    AudioBuf out = abuf_alloc(in->frames, in->channels);
    double env = fabs(frame_mean(in, 0));
    for (int f = 0; f < in->frames; f++) {
        double amp = fabs(frame_mean(in, f));
        if (f > 0) {
            double coeff = amp > env ? attack_coeff : release_coeff;
            env = coeff * env + (1.0 - coeff) * amp;
        }
        double gain = 1.0;
        if (env > threshold)
            gain = pow(threshold / env, 1.0 - 1.0 / ratio);
        gain *= makeup;
        for (int ch = 0; ch < in->channels; ch++)
            out.data[f * in->channels + ch] = (float)KA_CLAMP(
                in->data[f * in->channels + ch] * gain, -1.0, 1.0);
    }
    return out;
}

AudioBuf fx_limiter(const AudioBuf *in, int sr, double threshold_db,
                    double release_ms)
{
    if (in->frames == 0 || sr <= 0 || !isfinite(threshold_db) ||
        !isfinite(release_ms))
        return abuf_copy(in);
    double threshold = KA_MAX(pow(10.0, threshold_db / 20.0), 1e-10);
    double release_coeff =
        exp(-1.0 / KA_MAX(1.0, release_ms / 1000.0 * sr));
    if (!isfinite(threshold) || !isfinite(release_coeff))
        return abuf_copy(in);

    AudioBuf out = abuf_alloc(in->frames, in->channels);
    double g = 1.0;
    for (int f = 0; f < in->frames; f++) {
        double peak = frame_peak(in, f);
        double target = peak > threshold ? threshold / peak : 1.0;
        if (target < g)
            g = target; /* instant attack */
        else
            g = release_coeff * g + (1.0 - release_coeff) * target;
        for (int ch = 0; ch < in->channels; ch++)
            out.data[f * in->channels + ch] = (float)KA_CLAMP(
                in->data[f * in->channels + ch] * g, -1.0, 1.0);
    }
    return out;
}

AudioBuf fx_change_pitch(const AudioBuf *in, int sr, double semitones)
{
    (void)sr;
    if (in->frames == 0 || !isfinite(semitones) || fabs(semitones) < 0.01)
        return abuf_copy(in);
    double factor = pow(2.0, -semitones / 12.0);
    double scaled = (double)in->frames * factor;
    if (!isfinite(factor) || factor <= 0.0 || !isfinite(scaled) ||
        scaled > INT_MAX)
        return abuf_copy(in);
    int new_len = KA_MAX(1, (int)scaled);
    if (!abuf_dimensions_valid(new_len, in->channels))
        return abuf_copy(in);
    AudioBuf out = abuf_alloc(new_len, in->channels);
    /* Linear-interpolation resample (scipy.signal.resample equivalent role) */
    for (int f = 0; f < new_len; f++) {
        double src = new_len > 1
                         ? (double)f * (in->frames - 1) / (new_len - 1)
                         : 0.0;
        int i0 = (int)src;
        int i1 = KA_MIN(i0 + 1, in->frames - 1);
        double t = src - i0;
        for (int ch = 0; ch < in->channels; ch++) {
            double v = in->data[i0 * in->channels + ch] * (1.0 - t) +
                       in->data[i1 * in->channels + ch] * t;
            out.data[f * in->channels + ch] = (float)KA_CLAMP(v, -1.0, 1.0);
        }
    }
    return out;
}

AudioBuf fx_graphic_eq(const AudioBuf *in, int sr,
                       const double gains_db[10])
{
    static const double freqs[10] = {60,   170,  310,  600,   1000,
                                     3000, 6000, 12000, 14000, 16000};
    AudioBuf out = abuf_copy(in);
    for (int band = 0; band < 10; band++) {
        if (fabs(gains_db[band]) < 0.1)
            continue;
        for (int ch = 0; ch < in->channels; ch++) {
            Biquad bq = {0};
            biquad_design_peaking(&bq, sr, freqs[band], gains_db[band], 1.0);
            for (int f = 0; f < out.frames; f++) {
                float *s = &out.data[f * out.channels + ch];
                *s = biquad_process(&bq, *s);
            }
        }
    }
    int n = abuf_samples(&out);
    for (int i = 0; i < n; i++)
        out.data[i] = (float)KA_CLAMP(out.data[i], -1.0, 1.0);
    return out;
}

static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

/* Detect clicks via derivative spikes and interpolate across them. */
static void remove_clicks_1d(float *sig, int n, double threshold, int width)
{
    if (n < 2)
        return;
    double *diff = malloc(sizeof(double) * (size_t)(n - 1));
    double *sorted = malloc(sizeof(double) * (size_t)(n - 1));
    if (!diff || !sorted)
        abort();
    for (int i = 0; i < n - 1; i++)
        diff[i] = fabs((double)sig[i + 1] - sig[i]);
    memcpy(sorted, diff, sizeof(double) * (size_t)(n - 1));
    qsort(sorted, (size_t)(n - 1), sizeof(double), cmp_double);
    double median = (n - 1) % 2 ? sorted[(n - 1) / 2]
                                : (sorted[(n - 1) / 2 - 1] +
                                   sorted[(n - 1) / 2]) / 2.0;
    if (median < 1e-10)
        median = 1e-10;

    for (int idx = 0; idx < n - 1; idx++) {
        if (diff[idx] <= threshold * median)
            continue;
        int start = width > idx ? 0 : idx - width;
        int end = width >= n - idx - 1 ? n : idx + width + 1;
        if (start > 0 && end < n) {
            double y0 = sig[start - 1];
            double y1 = sig[end];
            int len = end - start;
            for (int k = 0; k < len; k++)
                sig[start + k] = (float)(y0 + (y1 - y0) * k /
                                         KA_MAX(1, len - 1));
        }
    }
    free(diff);
    free(sorted);
}

AudioBuf fx_click_removal(const AudioBuf *in, int sr, double threshold,
                          double width_ms)
{
    AudioBuf out = abuf_copy(in);
    if (sr <= 0 || !isfinite(threshold) || !isfinite(width_ms) ||
        width_ms < 0.0)
        return out;
    double requested = width_ms / 1000.0 * (double)sr;
    if (!isfinite(requested))
        return out;
    int width = requested >= in->frames ? KA_MAX(in->frames, 1)
                                        : KA_MAX(1, (int)requested);
    float *chan = malloc(sizeof(float) * (size_t)KA_MAX(in->frames, 1));
    if (!chan)
        abort();
    for (int ch = 0; ch < in->channels; ch++) {
        for (int f = 0; f < in->frames; f++)
            chan[f] = out.data[f * out.channels + ch];
        remove_clicks_1d(chan, in->frames, threshold, width);
        for (int f = 0; f < in->frames; f++)
            out.data[f * out.channels + ch] = chan[f];
    }
    free(chan);
    return out;
}

/* Spectral subtraction noise reduction. Uses first 0.5s as noise profile. */
static void noise_reduce_1d(float *sig, int n, int sr, double reduction)
{
    const int fft_size = 2048;
    const int hop = fft_size / 4;
    if (n < fft_size)
        return;
    int noise_frames = (int)(0.5 * sr / hop);

    double *window = malloc(sizeof(double) * fft_size);
    double *output = calloc((size_t)n, sizeof(double));
    double *wsum = calloc((size_t)n, sizeof(double));
    double *noise = calloc((size_t)(fft_size / 2 + 1), sizeof(double));
    double *re = malloc(sizeof(double) * fft_size);
    double *im = malloc(sizeof(double) * fft_size);
    if (!window || !output || !wsum || !noise || !re || !im)
        abort();
    for (int i = 0; i < fft_size; i++)
        window[i] = 0.5 * (1.0 - cos(2.0 * M_PI * i / (fft_size - 1)));

    /* Collect noise profile from first 0.5s */
    int noise_count = 0;
    for (int i = 0; i < KA_MIN(noise_frames * hop, n - fft_size); i += hop) {
        for (int k = 0; k < fft_size; k++) {
            re[k] = sig[i + k] * window[k];
            im[k] = 0.0;
        }
        fft_complex(re, im, fft_size, false);
        for (int k = 0; k <= fft_size / 2; k++)
            noise[k] += sqrt(re[k] * re[k] + im[k] * im[k]);
        noise_count++;
    }
    if (noise_count > 0)
        for (int k = 0; k <= fft_size / 2; k++)
            noise[k] /= noise_count;

    /* Process all frames with overlap-add */
    for (int i = 0; i + fft_size < n; i += hop) {
        for (int k = 0; k < fft_size; k++) {
            re[k] = sig[i + k] * window[k];
            im[k] = 0.0;
        }
        fft_complex(re, im, fft_size, false);
        /* Subtract noise magnitude, keep phase; enforce a noise floor.
         * Apply to the full symmetric spectrum via bin mirroring. */
        for (int k = 0; k < fft_size; k++) {
            int bin = k <= fft_size / 2 ? k : fft_size - k;
            double mag = sqrt(re[k] * re[k] + im[k] * im[k]);
            double clean = mag - noise[bin] * reduction;
            clean = KA_MAX(clean, mag * 0.01);
            if (mag > 1e-30) {
                double scale = clean / mag;
                re[k] *= scale;
                im[k] *= scale;
            }
        }
        fft_complex(re, im, fft_size, true);
        for (int k = 0; k < fft_size; k++) {
            output[i + k] += re[k] * window[k];
            wsum[i + k] += window[k] * window[k];
        }
    }

    for (int i = 0; i < n; i++)
        if (wsum[i] > 1e-10)
            sig[i] = (float)KA_CLAMP(output[i] / wsum[i], -1.0, 1.0);
    /* Where we didn't process, the original remains. */

    free(window);
    free(output);
    free(wsum);
    free(noise);
    free(re);
    free(im);
}

AudioBuf fx_noise_reduction(const AudioBuf *in, int sr, double reduction_db)
{
    if (sr <= 0 || !isfinite(reduction_db))
        return abuf_copy(in);
    double reduction = pow(10.0, reduction_db / 20.0);
    if (!isfinite(reduction))
        return abuf_copy(in);
    AudioBuf out = abuf_copy(in);
    float *chan = malloc(sizeof(float) * (size_t)KA_MAX(in->frames, 1));
    if (!chan)
        abort();
    for (int ch = 0; ch < in->channels; ch++) {
        for (int f = 0; f < in->frames; f++)
            chan[f] = out.data[f * out.channels + ch];
        noise_reduce_1d(chan, in->frames, sr, reduction);
        for (int f = 0; f < in->frames; f++)
            out.data[f * out.channels + ch] = chan[f];
    }
    free(chan);
    return out;
}

/* Natural cubic spline through (x_known, y_known), evaluated at integer x.
 * Replaces scipy CubicSpline for the repair effect. */
static void cubic_spline_eval(const double *xs, const double *ys, int m,
                              float *out, int n)
{
    /* Solve for second derivatives with natural boundaries (tridiagonal). */
    double *h = malloc(sizeof(double) * (size_t)m);
    double *alpha = malloc(sizeof(double) * (size_t)m);
    double *l = malloc(sizeof(double) * (size_t)m);
    double *mu = malloc(sizeof(double) * (size_t)m);
    double *z = malloc(sizeof(double) * (size_t)m);
    double *c = calloc((size_t)m, sizeof(double));
    double *b = malloc(sizeof(double) * (size_t)m);
    double *d = malloc(sizeof(double) * (size_t)m);
    if (!h || !alpha || !l || !mu || !z || !c || !b || !d)
        abort();

    for (int i = 0; i < m - 1; i++)
        h[i] = xs[i + 1] - xs[i];
    for (int i = 1; i < m - 1; i++)
        alpha[i] = 3.0 * ((ys[i + 1] - ys[i]) / h[i] -
                          (ys[i] - ys[i - 1]) / h[i - 1]);
    l[0] = 1.0;
    mu[0] = z[0] = 0.0;
    for (int i = 1; i < m - 1; i++) {
        l[i] = 2.0 * (xs[i + 1] - xs[i - 1]) - h[i - 1] * mu[i - 1];
        mu[i] = h[i] / l[i];
        z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
    }
    l[m - 1] = 1.0;
    z[m - 1] = c[m - 1] = 0.0;
    for (int j = m - 2; j >= 0; j--) {
        c[j] = z[j] - mu[j] * c[j + 1];
        b[j] = (ys[j + 1] - ys[j]) / h[j] -
               h[j] * (c[j + 1] + 2.0 * c[j]) / 3.0;
        d[j] = (c[j + 1] - c[j]) / (3.0 * h[j]);
    }

    int seg = 0;
    for (int x = 0; x < n; x++) {
        while (seg < m - 2 && x > xs[seg + 1])
            seg++;
        double dx = x - xs[seg];
        double y = ys[seg] + b[seg] * dx + c[seg] * dx * dx +
                   d[seg] * dx * dx * dx;
        out[x] = (float)y;
    }

    free(h); free(alpha); free(l); free(mu); free(z);
    free(c); free(b); free(d);
}

AudioBuf fx_repair(const AudioBuf *in, int sr)
{
    (void)sr;
    int n = in->frames;
    if (n < 4)
        return abuf_copy(in);
    int ctx = KA_MIN(32, n / 4);
    if (ctx < 2)
        return abuf_copy(in);

    AudioBuf out = abuf_copy(in);
    int m = ctx * 2;
    double *xs = malloc(sizeof(double) * (size_t)m);
    double *ys = malloc(sizeof(double) * (size_t)m);
    float *chan = malloc(sizeof(float) * (size_t)n);
    if (!xs || !ys || !chan)
        abort();
    for (int i = 0; i < ctx; i++) {
        xs[i] = i;
        xs[ctx + i] = n - ctx + i;
    }
    for (int ch = 0; ch < in->channels; ch++) {
        for (int i = 0; i < m; i++)
            ys[i] = in->data[(int)xs[i] * in->channels + ch];
        cubic_spline_eval(xs, ys, m, chan, n);
        for (int f = 0; f < n; f++)
            out.data[f * out.channels + ch] =
                (float)KA_CLAMP(chan[f], -1.0, 1.0);
    }
    free(xs);
    free(ys);
    free(chan);
    return out;
}

/* Schroeder reverb: 4 comb filters + 2 allpass filters. */
static void comb_filter(const double *sig, double *out, int n, int delay,
                        double feedback)
{
    for (int i = 0; i < n; i++) {
        out[i] = sig[i];
        if (i >= delay)
            out[i] += out[i - delay] * feedback;
    }
}

static void allpass_filter(double *sig, int n, int delay, double feedback)
{
    double *buf = calloc((size_t)delay, sizeof(double));
    if (!buf)
        abort();
    int idx = 0;
    for (int i = 0; i < n; i++) {
        double delayed = buf[idx];
        double x = sig[i];
        sig[i] = -x * feedback + delayed;
        buf[idx] = x + delayed * feedback;
        idx = (idx + 1) % delay;
    }
    free(buf);
}

AudioBuf fx_reverb(const AudioBuf *in, int sr, double room_size,
                   double damping, double wet, double dry)
{
    if (sr <= 0 || !isfinite(room_size) || !isfinite(damping) ||
        !isfinite(wet) || !isfinite(dry))
        return abuf_copy(in);
    room_size = KA_CLAMP(room_size, 0.0, 1.0);
    damping = KA_CLAMP(damping, 0.0, 1.0);
    wet = KA_CLAMP(wet, 0.0, 1.0);
    dry = KA_CLAMP(dry, 0.0, 1.0);
    static const int base_delays[4] = {1557, 1617, 1491, 1422};
    int comb_delays[4];
    for (int i = 0; i < 4; i++)
        comb_delays[i] = KA_MAX(1, (int)(base_delays[i] * room_size));
    double comb_feedback = 0.84 * (1.0 - damping * 0.4);
    static const int ap_delays[2] = {225, 556};

    int n = in->frames;
    AudioBuf out = abuf_alloc(n, in->channels);
    double *sig = malloc(sizeof(double) * (size_t)KA_MAX(n, 1));
    double *reverbed = calloc((size_t)KA_MAX(n, 1), sizeof(double));
    double *tmp = malloc(sizeof(double) * (size_t)KA_MAX(n, 1));
    if (!sig || !reverbed || !tmp)
        abort();

    for (int ch = 0; ch < in->channels; ch++) {
        for (int f = 0; f < n; f++)
            sig[f] = in->data[f * in->channels + ch];
        memset(reverbed, 0, sizeof(double) * (size_t)KA_MAX(n, 1));
        for (int c = 0; c < 4; c++) {
            comb_filter(sig, tmp, n, comb_delays[c], comb_feedback);
            for (int f = 0; f < n; f++)
                reverbed[f] += tmp[f];
        }
        for (int f = 0; f < n; f++)
            reverbed[f] /= 4.0;
        for (int a = 0; a < 2; a++)
            allpass_filter(reverbed, n, ap_delays[a], 0.5);
        for (int f = 0; f < n; f++)
            out.data[f * out.channels + ch] = (float)KA_CLAMP(
                sig[f] * dry + reverbed[f] * wet, -1.0, 1.0);
    }
    free(sig);
    free(reverbed);
    free(tmp);
    return out;
}

/* --- Generators --- */

AudioBuf gen_tone(int sr, double duration_sec, double frequency,
                  double amplitude, WaveForm waveform)
{
    int n;
    if (!duration_frames(sr, duration_sec, 1, &n) ||
        !generator_frequency_valid(sr, frequency) ||
        !generator_level_valid(amplitude))
        return empty_mono();
    AudioBuf out = abuf_alloc(n, 1);
    for (int i = 0; i < n; i++) {
        double t = (double)i / sr;
        double v;
        switch (waveform) {
        case WAVE_SQUARE:
            v = sin(2.0 * M_PI * frequency * t) >= 0 ? 1.0 : -1.0;
            break;
        case WAVE_SAWTOOTH:
            v = 2.0 * fmod(frequency * t, 1.0) - 1.0;
            break;
        case WAVE_TRIANGLE:
            v = 2.0 * fabs(2.0 * fmod(frequency * t, 1.0) - 1.0) - 1.0;
            break;
        case WAVE_SINE:
        default:
            v = sin(2.0 * M_PI * frequency * t);
            break;
        }
        out.data[i] = (float)(v * amplitude);
    }
    return out;
}

AudioBuf gen_chirp(int sr, double duration_sec, double f0, double f1,
                   double amplitude)
{
    int n;
    if (!duration_frames(sr, duration_sec, 1, &n) ||
        !generator_frequency_valid(sr, f0) ||
        !generator_frequency_valid(sr, f1) ||
        !generator_level_valid(amplitude))
        return empty_mono();
    AudioBuf out = abuf_alloc(n, 1);
    /* Linear frequency sweep: phase = 2*pi*(f0*t + (f1-f0)/(2T)*t^2) */
    double T = KA_MAX(duration_sec, 1e-9);
    for (int i = 0; i < n; i++) {
        double t = (double)i / sr;
        double phase = 2.0 * M_PI * (f0 * t + (f1 - f0) / (2.0 * T) * t * t);
        out.data[i] = (float)(sin(phase) * amplitude);
    }
    return out;
}

AudioBuf gen_silence(int sr, double duration_sec, int channels)
{
    channels = KA_MAX(1, channels);
    int n;
    if (!duration_frames(sr, duration_sec, channels, &n))
        return empty_mono();
    return abuf_alloc(n, channels);
}

/* Box-Muller gaussian */
static double randn(void)
{
    double u1 = (rand() + 1.0) / ((double)RAND_MAX + 2.0);
    double u2 = (rand() + 1.0) / ((double)RAND_MAX + 2.0);
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

AudioBuf gen_noise(int sr, double duration_sec, double amplitude,
                   NoiseType type)
{
    int n;
    if (!duration_frames(sr, duration_sec, 1, &n) ||
        !generator_level_valid(amplitude))
        return empty_mono();
    AudioBuf out = abuf_alloc(n, 1);
    double *samples = malloc(sizeof(double) * (size_t)KA_MAX(n, 1));
    if (!samples)
        abort();

    if (type == NOISE_PINK) {
        /* Pink noise via IIR approximation (same coefficients as the
         * Python source). */
        static const double b[4] = {0.049922035, -0.095993537, 0.050612699,
                                    -0.004709510};
        static const double a[4] = {1.0, -2.494956002, 2.017265875,
                                    -0.522189400};
        double xh[4] = {0}, yh[4] = {0};
        for (int i = 0; i < n; i++) {
            double x = randn();
            double y = b[0] * x + b[1] * xh[1] + b[2] * xh[2] +
                       b[3] * xh[3] - a[1] * yh[1] - a[2] * yh[2] -
                       a[3] * yh[3];
            xh[3] = xh[2]; xh[2] = xh[1]; xh[1] = x;
            yh[3] = yh[2]; yh[2] = yh[1]; yh[1] = y;
            samples[i] = y;
        }
    } else if (type == NOISE_BROWN) {
        double acc = 0.0;
        for (int i = 0; i < n; i++) {
            acc += randn() * 0.02;
            samples[i] = acc;
        }
    } else {
        for (int i = 0; i < n; i++)
            samples[i] = randn();
    }

    double peak = 0.0;
    for (int i = 0; i < n; i++)
        peak = KA_MAX(peak, fabs(samples[i]));
    if (peak > 1e-10)
        for (int i = 0; i < n; i++)
            samples[i] /= peak;
    for (int i = 0; i < n; i++)
        out.data[i] = (float)(samples[i] * amplitude);
    free(samples);
    return out;
}

AudioBuf gen_dtmf(int sr, const char *sequence, double tone_duration,
                  double gap_duration, double amplitude)
{
    static const struct {
        char ch;
        int f1, f2;
    } freqs[] = {
        {'1', 697, 1209}, {'2', 697, 1336}, {'3', 697, 1477},
        {'A', 697, 1633}, {'4', 770, 1209}, {'5', 770, 1336},
        {'6', 770, 1477}, {'B', 770, 1633}, {'7', 852, 1209},
        {'8', 852, 1336}, {'9', 852, 1477}, {'C', 852, 1633},
        {'*', 941, 1209}, {'0', 941, 1336}, {'#', 941, 1477},
        {'D', 941, 1633},
    };
    int tone_n, gap_n;
    if (!sequence || !duration_frames(sr, tone_duration, 1, &tone_n) ||
        !duration_frames(sr, gap_duration, 1, &gap_n) ||
        !generator_level_valid(amplitude))
        return empty_mono();

    /* Count valid characters to size the output. */
    int64_t valid = 0;
    for (const char *p = sequence; *p; p++)
        for (size_t k = 0; k < KA_LEN(freqs); k++)
            if (freqs[k].ch == *p) {
                if (valid == INT64_MAX)
                    return empty_mono();
                valid++;
                break;
            }
    if (valid == 0)
        return empty_mono();

    int64_t per_tone = (int64_t)tone_n + gap_n;
    if (per_tone > 0 && valid > INT64_MAX / per_tone)
        return empty_mono();
    int64_t total = valid * per_tone;
    if (!abuf_dimensions_valid(total, 1))
        return empty_mono();

    AudioBuf out = abuf_alloc((int)total, 1);
    int pos = 0;
    for (const char *p = sequence; *p; p++) {
        int f1 = 0, f2 = 0;
        bool found = false;
        for (size_t k = 0; k < KA_LEN(freqs); k++)
            if (freqs[k].ch == *p) {
                f1 = freqs[k].f1;
                f2 = freqs[k].f2;
                found = true;
                break;
            }
        if (!found)
            continue;
        for (int i = 0; i < tone_n; i++) {
            double t = (double)i / sr;
            out.data[pos++] = (float)((sin(2.0 * M_PI * f1 * t) +
                                       sin(2.0 * M_PI * f2 * t)) /
                                      2.0 * amplitude);
        }
        pos += gap_n; /* silence gap (already zeroed) */
    }
    return out;
}
