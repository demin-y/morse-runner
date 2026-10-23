// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "rnd.h"

#include <glib.h>

#include <math.h>

// GRand (Mersenne Twister) gives the same sequence on every platform
static GRand* rng;

static GRand* gen(void)
{
    if (!rng) rng = g_rand_new();   // seeded from the system
    return rng;
}

void rnd_seed(guint32 value)
{
    g_rand_set_seed(gen(), value);
}

double rnd_random(void)
{
    return g_rand_double(gen());
}

int rnd_int(int n)
{
    if (n <= 1) return 0;
    return g_rand_int_range(gen(), 0, n);
}

// uniform in (0, 1], safe for log()
static double positive(void)
{
    return 1.0 - rnd_random();
}

double rnd_normal(void)
{
    return sqrt(-2.0 * log(positive())) * cos(2 * G_PI * rnd_random());
}

double rnd_gauss_lim(double mean, double lim)
{
    double r = mean + rnd_normal() * 0.5 * lim;
    if (r < mean - lim) r = mean - lim;
    if (r > mean + lim) r = mean + lim;
    return r;
}

double rnd_rayleigh(double mean)
{
    return mean * sqrt(-log(positive()) - log(positive()));
}

double rnd_uniform(void)
{
    return 2 * rnd_random() - 1;
}

double rnd_ushaped(void)
{
    return sin(G_PI * (rnd_random() - 0.5));
}

// Numerical Recipes, ch. 7.3
int rnd_poisson(double mean)
{
    double g = exp(-mean), t = 1;
    int result = 0;
    for (; result < 30; ++result) {
        t *= rnd_random();
        if (t <= g) break;
    }
    return result;
}
