// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_KEYER_H
#define MR_KEYER_H

#include "buf.h"

#include <stddef.h>

// Converts text to Morse code and Morse code to a keying envelope
// with Blackman-Harris shaped rise and fall.
typedef struct {
    int rate;
    const char* morse[256];
    float* rampOn;
    float* rampOff;
    int rampLen;
} Keyer;

void keyer_init(Keyer* k, int rate);
void keyer_free(Keyer* k);

// "CQ" -> "-.-. --.-~"   (dots, dashes, ' ' between chars and words, '~' at the end)
void keyer_encode(const Keyer* k, const char* text, char* out, size_t outSize);
// the length of the result is a multiple of bufSize
void keyer_envelope(const Keyer* k, const char* morse, int wpm, int bufSize, FloatBuf* out);

#endif
