// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "dsp.h"
#include "settings.h"

#include <glib.h>

#include <math.h>
#include <string.h>

static double** alloc2d(int rows, int cols)
{
    double** p = g_new(double*, rows);
    for (int i = 0; i < rows; ++i) p[i] = g_new0(double, cols);
    return p;
}

static void free2d(double** p, int rows)
{
    if (!p) return;
    for (int i = 0; i < rows; ++i) g_free(p[i]);
    g_free(p);
}

//------------------------------------------------------------------------------
//                              MovingAverage
//------------------------------------------------------------------------------
static void mavg_free_bufs(MovingAverage* f)
{
    free2d(f->bufRe, f->passes + 1);
    free2d(f->bufIm, f->passes + 1);
    f->bufRe = f->bufIm = NULL;
    f->bufLen = 0;
}

void mavg_init(MovingAverage* f)
{
    memset(f, 0, sizeof(*f));
    f->passes = 3;
    mavg_configure(f, 129, 512, 0);
}

void mavg_free(MovingAverage* f)
{
    mavg_free_bufs(f);
}

void mavg_configure(MovingAverage* f, int points, int samplesInInput, double gainDb)
{
    f->points = points < 1 ? 1 : points;
    f->samplesInInput = samplesInInput < 1 ? 1 : samplesInInput;
    // (gain, dB to linear) * (averaging factor)
    f->norm = pow(10.0, 0.05 * gainDb) * pow((double)f->points, -f->passes);
    mavg_reset(f);
}

void mavg_reset(MovingAverage* f)
{
    // one buffer for the input data and one for each pass
    const int len = f->samplesInInput + f->points;
    if (len != f->bufLen) {
        mavg_free_bufs(f);
        f->bufRe = alloc2d(f->passes + 1, len);
        f->bufIm = alloc2d(f->passes + 1, len);
        f->bufLen = len;
        return;
    }
    for (int i = 0; i <= f->passes; ++i) {
        memset(f->bufRe[i], 0, (size_t)len * sizeof(double));
        memset(f->bufIm[i], 0, (size_t)len * sizeof(double));
    }
}

static void mavg_do_filter(MovingAverage* f, const FloatBuf* in, double** bufs, FloatBuf* out)
{
    const int n = f->samplesInInput;
    const int len = n + f->points;

    // shift the input buffer to the left and append the new data
    double* b0 = bufs[0];
    memmove(b0, b0 + n, (size_t)(len - n) * sizeof(double));
    for (int i = 0; i < n; ++i) b0[len - n + i] = i < in->len ? in->data[i] : 0.0;

    // calculate the moving average recursively, pass after pass
    for (int p = 1; p <= f->passes; ++p) {
        const double* src = bufs[p - 1];
        double* dst = bufs[p];
        memmove(dst, dst + n, (size_t)(len - n) * sizeof(double));
        for (int i = f->points; i < len; ++i) dst[i] = dst[i - 1] - src[i - f->points] + src[i];
    }

    if (!out) return;
    const double* res = bufs[f->passes];
    fb_resize(out, n);
    for (int i = 0; i < n; ++i) out->data[i] = (float)(res[f->points + i] * f->norm);
}

void mavg_filter(MovingAverage* f, const ReIm* in, ReIm* out)
{
    mavg_do_filter(f, &in->re, f->bufRe, out ? &out->re : NULL);
    mavg_do_filter(f, &in->im, f->bufIm, out ? &out->im : NULL);
}

//------------------------------------------------------------------------------
//                              QuickAverage
//------------------------------------------------------------------------------
void qavg_init(QuickAverage* f, int passes, int points)
{
    memset(f, 0, sizeof(*f));
    f->passes = passes < 1 ? 1 : passes > 8 ? 8 : passes;
    qavg_set_points(f, points);
}

void qavg_free(QuickAverage* f)
{
    free2d(f->reBufs, f->passes + 1);
    free2d(f->imBufs, f->passes + 1);
    f->reBufs = f->imBufs = NULL;
    f->bufLen = 0;
}

void qavg_set_points(QuickAverage* f, int points)
{
    f->points = points < 1 ? 1 : points;
    if (f->points != f->bufLen) {
        qavg_free(f);
        f->reBufs = alloc2d(f->passes + 1, f->points);
        f->imBufs = alloc2d(f->passes + 1, f->points);
        f->bufLen = f->points;
    } else {
        for (int i = 0; i <= f->passes; ++i) {
            memset(f->reBufs[i], 0, (size_t)f->points * sizeof(double));
            memset(f->imBufs[i], 0, (size_t)f->points * sizeof(double));
        }
    }
    f->scale = pow((double)f->points, -f->passes);
    f->idx = 0;
    f->prevIdx = f->points - 1;
}

static double qavg_do_filter(QuickAverage* f, double v, double** bufs)
{
    double result = v;
    for (int p = 1; p <= f->passes; ++p) {
        v = result;
        result = bufs[p][f->prevIdx] - bufs[p - 1][f->idx] + v;
        bufs[p - 1][f->idx] = v;
    }
    bufs[f->passes][f->idx] = result;
    return result * f->scale;
}

void qavg_filter(QuickAverage* f, double re, double im, double* outRe, double* outIm)
{
    *outRe = qavg_do_filter(f, re, f->reBufs);
    *outIm = qavg_do_filter(f, im, f->imBufs);
    f->prevIdx = f->idx;
    f->idx = (f->idx + 1) % f->points;
}

//------------------------------------------------------------------------------
//                               Modulator
//------------------------------------------------------------------------------
void modul_init(Modulator* m)
{
    memset(m, 0, sizeof(*m));
    modul_set_carrier_freq(m, 600);
}

void modul_free(Modulator* m)
{
    g_clear_pointer(&m->sn, g_free);
    g_clear_pointer(&m->cs, g_free);
}

void modul_set_carrier_freq(Modulator* m, double value)
{
    // an integer number of samples per period makes a short lookup table possible
    int cnt = (int)lround(SAMPLE_RATE / value);
    if (cnt < 2) cnt = 2;
    m->carrierFreq = (double)SAMPLE_RATE / cnt;
    double dFi = 2 * G_PI / cnt;
    modul_free(m);
    m->sn = g_new(float, cnt);
    m->cs = g_new(float, cnt);
    for (int i = 0; i < cnt; ++i) {
        m->sn[i] = (float)sin(i * dFi);
        m->cs[i] = (float)cos(i * dFi);
    }
    m->count = cnt;
    m->sampleNo = 0;
}

void modul_modulate(Modulator* m, const ReIm* in, FloatBuf* out)
{
    fb_resize(out, in->re.len);
    for (int i = 0; i < in->re.len; ++i) {
        out->data[i] = in->re.data[i] * m->sn[m->sampleNo] - in->im.data[i] * m->cs[m->sampleNo];
        m->sampleNo = (m->sampleNo + 1) % m->count;
    }
}

//------------------------------------------------------------------------------
//                             VolumeControl
//------------------------------------------------------------------------------
void agc_init(VolumeControl* a, double noiseInDb, double noiseOutDb, int attackSamples, int holdSamples)
{
    memset(a, 0, sizeof(*a));
    a->maxOut = 20000;

    // Amplitude characteristic of the AGC:  Out = MaxOut * (1 - Exp(-In / Beta)).
    // Beta maps NoiseIn to NoiseOut.
    double noiseIn = pow(10.0, 0.05 * noiseInDb);
    double noiseOut = fmin(0.25 * a->maxOut, pow(10.0, 0.05 * noiseOutDb));
    a->beta = noiseIn / log(a->maxOut / (a->maxOut - noiseOut));

    // look-ahead window: attack ramp, hold, hold, attack ramp
    if (attackSamples < 1) attackSamples = 1;
    if (holdSamples < 1) holdSamples = 1;
    a->len = 2 * (attackSamples + holdSamples) + 1;
    a->attackShape = g_new0(double, a->len);
    a->realBuf = g_new0(float, a->len);
    a->magBuf = g_new0(double, a->len);
    for (int i = 0; i < attackSamples; ++i) {
        a->attackShape[i] = log(0.5 - 0.5 * cos((i + 1) * G_PI / (attackSamples + 1)));
        a->attackShape[a->len - 1 - i] = a->attackShape[i];
    }
}

void agc_free(VolumeControl* a)
{
    g_clear_pointer(&a->attackShape, g_free);
    g_clear_pointer(&a->realBuf, g_free);
    g_clear_pointer(&a->magBuf, g_free);
}

void agc_reset(VolumeControl* a)
{
    memset(a->realBuf, 0, (size_t)a->len * sizeof(float));
    memset(a->magBuf, 0, (size_t)a->len * sizeof(double));
    a->bufIdx = 0;
}

static double agc_gain(VolumeControl* a)
{
    // look at both sides of the sample and find the max. magnitude,
    // weighed by the attack shape
    double envel = 1e-10;
    int di = a->bufIdx;
    for (int wi = 0; wi < a->len; ++wi) {
        double sample = a->magBuf[di] + a->attackShape[wi];
        if (sample > envel) envel = sample;
        if (++di == a->len) di = 0;
    }
    envel = exp(envel);
    return a->maxOut * (1 - exp(-envel / a->beta)) / envel;
}

void agc_process(VolumeControl* a, FloatBuf* data)
{
    for (int i = 0; i < data->len; ++i) {
        a->realBuf[a->bufIdx] = data->data[i];
        a->magBuf[a->bufIdx] = log(fabs(data->data[i] + 1e-10));
        a->bufIdx = (a->bufIdx + 1) % a->len;
        data->data[i] = (float)(a->realBuf[(a->bufIdx + a->len / 2) % a->len] * agc_gain(a));
    }
}
