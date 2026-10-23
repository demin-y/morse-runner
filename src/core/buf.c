// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "buf.h"

#include <glib.h>

#include <string.h>

void fb_resize(FloatBuf* b, int len)
{
    len = MAX(len, 0);
    if (len > b->cap) {
        // the blocks keep the same size, so the buffers rarely grow
        b->data = g_renew(float, b->data, len);
        b->cap = len;
    }
    if (len > b->len) memset(b->data + b->len, 0, (size_t)(len - b->len) * sizeof(float));
    b->len = len;
}

void fb_zero(FloatBuf* b, int len)
{
    fb_resize(b, len);
    if (len) memset(b->data, 0, (size_t)len * sizeof(float));
}

void fb_clear(FloatBuf* b)
{
    b->len = 0;
}

void fb_free(FloatBuf* b)
{
    g_free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

void fb_swap(FloatBuf* a, FloatBuf* b)
{
    FloatBuf t = *a;
    *a = *b;
    *b = t;
}

void reim_zero(ReIm* r, int len)
{
    fb_zero(&r->re, len);
    fb_zero(&r->im, len);
}

void reim_free(ReIm* r)
{
    fb_free(&r->re);
    fb_free(&r->im);
}
