// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "keyer.h"
#include "settings.h"

#include <glib.h>

#include <math.h>
#include <string.h>

// code of each character followed by the character gap
static const struct { char ch; const char* code; } MORSE_TABLE[] = {
    {'1', ".---- "}, {'2', "..--- "}, {'3', "...-- "}, {'4', "....- "}, {'5', "..... "},
    {'6', "-.... "}, {'7', "--... "}, {'8', "---.. "}, {'9', "----. "}, {'0', "----- "},
    {'A', ".- "},    {'B', "-... "},  {'C', "-.-. "},  {'D', "-.. "},   {'E', ". "},
    {'F', "..-. "},  {'G', "--. "},   {'H', ".... "},  {'I', ".. "},    {'J', ".--- "},
    {'K', "-.- "},   {'L', ".-.. "},  {'M', "-- "},    {'N', "-. "},    {'O', "--- "},
    {'P', ".--. "},  {'Q', "--.- "},  {'R', ".-. "},   {'S', "... "},   {'T', "- "},
    {'U', "..- "},   {'V', "...- "},  {'W', ".-- "},   {'X', "-..- "},  {'Y', "-.-- "},
    {'Z', "--.. "},
    {'/', "-..-. "}, {'.', ".-.-.- "}, {',', "--..-- "}, {'?', "..--.. "}, {'=', "-...- "},
    {'\\', "...-. "},
};

static double blackman_harris(double x)
{
    const double a0 = 0.35875, a1 = 0.48829, a2 = 0.14128, a3 = 0.01168;
    return a0 - a1 * cos(2 * G_PI * x) + a2 * cos(4 * G_PI * x) - a3 * cos(6 * G_PI * x);
}

void keyer_init(Keyer* k, int rate)
{
    memset(k, 0, sizeof(*k));
    k->rate = rate;
    for (size_t i = 0; i < sizeof(MORSE_TABLE) / sizeof(MORSE_TABLE[0]); ++i)
        k->morse[(unsigned char)MORSE_TABLE[i].ch] = MORSE_TABLE[i].code;

    // integrated (and normalized) Blackman-Harris kernel = smooth step, 5 ms rise time
    int len = (int)lround(2.7 * 0.005 * rate);
    k->rampLen = len;
    k->rampOn = g_new(float, len);
    k->rampOff = g_new(float, len);
    double* acc = g_new(double, len);
    double sum = 0;
    for (int i = 0; i < len; ++i) {
        sum += blackman_harris((double)i / len);
        acc[i] = sum;
    }
    for (int i = 0; i < len; ++i) {
        k->rampOn[i] = (float)(acc[i] / sum);
        k->rampOff[len - 1 - i] = k->rampOn[i];
    }
    g_free(acc);
}

void keyer_free(Keyer* k)
{
    g_clear_pointer(&k->rampOn, g_free);
    g_clear_pointer(&k->rampOff, g_free);
}

void keyer_encode(const Keyer* k, const char* text, char* out, size_t outSize)
{
    if (!outSize) return;
    out[0] = '\0';
    for (const char* p = text; *p; ++p) {
        const char* code = *p == ' ' || *p == '_' ? " " : k->morse[(unsigned char)*p];
        if (code && g_strlcat(out, code, outSize) >= outSize) break;   // truncated
    }
    size_t n = strlen(out);
    if (n) out[n - 1] = '~';
}

static void put(FloatBuf* r, int pos, float v)
{
    if (pos >= 0 && pos < r->len) r->data[pos] = v;
}

// a key-down element: ramp up, steady carrier, ramp down
static void element(const Keyer* k, FloatBuf* r, int* p, int units, int spu)
{
    const int rl = k->rampLen;
    for (int i = 0; i < rl; ++i) put(r, *p + i, k->rampOn[i]);
    *p += rl;
    for (int i = 0; i < units * spu - rl; ++i) put(r, *p + i, 1.f);
    *p += units * spu - rl;
    for (int i = 0; i < rl; ++i) put(r, *p + i, k->rampOff[i]);
    *p += rl;
}

void keyer_envelope(const Keyer* k, const char* morse, int wpm, int bufSize, FloatBuf* out)
{
    int unitCnt = 0;
    for (const char* c = morse; *c; ++c) {
        switch (*c) {
        case '.': unitCnt += 2; break;
        case '-': unitCnt += 4; break;
        case ' ': unitCnt += 2; break;
        case '~': unitCnt += 1; break;
        }
    }

    const int rampLen = k->rampLen;
    const int spu = (int)lround(0.1 * k->rate * 12 / (wpm > 0 ? wpm : 1));
    const int trueLen = unitCnt * spu + rampLen;
    const int bs = bufSize > 0 ? bufSize : 1;
    fb_zero(out, bs * ((trueLen + bs - 1) / bs));

    int p = 0;
    for (const char* c = morse; *c; ++c) {
        switch (*c) {
        case '.': element(k, out, &p, 1, spu); p += spu - rampLen; break;
        case '-': element(k, out, &p, 3, spu); p += spu - rampLen; break;
        case ' ': p += 2 * spu - rampLen; break;
        case '~': p += spu - rampLen; break;
        }
    }
}
