// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_WAVFILE_H
#define MR_WAVFILE_H

#include "buf.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// Writes 16-bit mono PCM .wav files.
typedef struct {
    FILE* file;
    uint32_t samples;
    int rate;
} WavWriter;

// takes over a file opened for writing in binary mode; wav_close closes it
bool wav_open(WavWriter* w, FILE* file, int rate);
// samples are in the -1..+1 range
void wav_write(WavWriter* w, const FloatBuf* samples);
void wav_close(WavWriter* w);

#endif
