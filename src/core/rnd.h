// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_RND_H
#define MR_RND_H

#include <glib.h>

// for reproducible runs; unseeded, the generator starts from a random seed
void rnd_seed(guint32 value);

// uniform in [0, 1)
double rnd_random(void);
// uniform integer in [0, n)
int rnd_int(int n);

double rnd_normal(void);
double rnd_gauss_lim(double mean, double lim);
double rnd_rayleigh(double mean);
// uniform in [-1, 1)
double rnd_uniform(void);
// U-shaped distribution in [-1, 1]
double rnd_ushaped(void);
int rnd_poisson(double mean);

#endif
