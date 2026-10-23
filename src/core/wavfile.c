// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "wavfile.h"

#include <glib.h>

#include <math.h>

// the format is little-endian
static void put16(FILE* f, guint16 v)
{
    v = GUINT16_TO_LE(v);
    fwrite(&v, sizeof(v), 1, f);
}

static void put32(FILE* f, guint32 v)
{
    v = GUINT32_TO_LE(v);
    fwrite(&v, sizeof(v), 1, f);
}

static void write_header(WavWriter* w)
{
    const uint32_t dataBytes = w->samples * 2;
    fwrite("RIFF", 1, 4, w->file);
    put32(w->file, 36 + dataBytes);
    fwrite("WAVEfmt ", 1, 8, w->file);
    put32(w->file, 16);                    // fmt chunk size
    put16(w->file, 1);                     // PCM
    put16(w->file, 1);                     // mono
    put32(w->file, (uint32_t)w->rate);
    put32(w->file, (uint32_t)w->rate * 2); // bytes per second
    put16(w->file, 2);                     // block align
    put16(w->file, 16);                    // bits per sample
    fwrite("data", 1, 4, w->file);
    put32(w->file, dataBytes);
}

bool wav_open(WavWriter* w, FILE* file, int rate)
{
    wav_close(w);
    w->file = file;
    if (!w->file) return false;
    w->rate = rate;
    w->samples = 0;
    write_header(w);
    return true;
}

void wav_write(WavWriter* w, const FloatBuf* samples)
{
    if (!w->file) return;
    gint16 buf[512];
    size_t n = 0;
    for (int i = 0; i < samples->len; ++i) {
        buf[n++] = GINT16_TO_LE((gint16)lroundf(CLAMP(samples->data[i], -1.f, 1.f) * 32767));
        if (n == G_N_ELEMENTS(buf)) {
            fwrite(buf, sizeof(buf[0]), n, w->file);
            n = 0;
        }
    }
    if (n) fwrite(buf, sizeof(buf[0]), n, w->file);
    w->samples += (uint32_t)samples->len;
}

void wav_close(WavWriter* w)
{
    if (!w->file) return;
    fseek(w->file, 0, SEEK_SET);
    write_header(w);
    fclose(w->file);
    w->file = NULL;
}
