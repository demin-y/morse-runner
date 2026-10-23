// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_DSP_H
#define MR_DSP_H

#include "buf.h"

#include <stdbool.h>

// Multi-pass recursive moving average, used as the receiver's low-pass filter.
typedef struct {
    int passes, points, samplesInInput;
    double norm;
    double** bufRe;   // passes + 1 buffers of samplesInInput + points
    double** bufIm;
    int bufLen;       // the allocated length of each buffer
} MovingAverage;

void mavg_init(MovingAverage* f);
void mavg_free(MovingAverage* f);
void mavg_configure(MovingAverage* f, int points, int samplesInInput, double gainDb);
// clears the history; reuses the buffers when the size has not changed
void mavg_reset(MovingAverage* f);
// the input length must be samplesInInput; out may be NULL
void mavg_filter(MovingAverage* f, const ReIm* in, ReIm* out);

// Sample-by-sample multi-pass moving average (used by the QSB model).
typedef struct {
    int passes, points;
    double scale;
    double** reBufs;
    double** imBufs;
    int bufLen;
    int idx, prevIdx;
} QuickAverage;

void qavg_init(QuickAverage* f, int passes, int points);
void qavg_free(QuickAverage* f);
// sets the length and clears the history
void qavg_set_points(QuickAverage* f, int points);
void qavg_filter(QuickAverage* f, double re, double im, double* outRe, double* outIm);

// Shifts a complex baseband signal up to the audio pitch.
typedef struct {
    double carrierFreq;
    int sampleNo;
    int count;
    float* sn;
    float* cs;
} Modulator;

void modul_init(Modulator* m);
void modul_free(Modulator* m);
void modul_set_carrier_freq(Modulator* m, double value);
void modul_modulate(Modulator* m, const ReIm* in, FloatBuf* out);

// Look-ahead AGC.
typedef struct {
    double maxOut;
    double beta;
    int len, bufIdx;
    double* attackShape;
    float* realBuf;
    double* magBuf;
} VolumeControl;

// noise levels in dB, attack and hold in samples
void agc_init(VolumeControl* a, double noiseInDb, double noiseOutDb, int attackSamples, int holdSamples);
void agc_free(VolumeControl* a);
void agc_reset(VolumeControl* a);
void agc_process(VolumeControl* a, FloatBuf* data);

#endif
