// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_BUF_H
#define MR_BUF_H

// Audio sample buffers. The DSP loops index them directly, which a GArray
// (with its untyped data pointer) would make clumsy.
typedef struct {
    float* data;
    int len;
    int cap;
} FloatBuf;

// sets the length; new elements are zero
void fb_resize(FloatBuf* b, int len);
// sets the length and fills the whole buffer with zeros
void fb_zero(FloatBuf* b, int len);
void fb_clear(FloatBuf* b);   // length 0, keeps the memory
void fb_free(FloatBuf* b);
void fb_swap(FloatBuf* a, FloatBuf* b);

// complex samples as two arrays
typedef struct {
    FloatBuf re, im;
} ReIm;

void reim_zero(ReIm* r, int len);
void reim_free(ReIm* r);

#endif
