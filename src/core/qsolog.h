// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_QSOLOG_H
#define MR_QSOLOG_H

#include "keyer.h"
#include "settings.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    double t;                      // seconds since the start
    char call[CALL_LEN];           // what I logged
    char trueCall[CALL_LEN];       // what the DX station really was
    int rst, trueRst;
    int nr, trueNr;
    int sentRst, sentNr;
    char pfx[CALL_LEN];            // WPX prefix, or the call score in HST mode
    bool dupe;
    char err[4];                   // "", "NIL", "DUP", "RST", "NR"
} Qso;

static inline bool qso_ok(const Qso* q) { return q->err[0] == '\0'; }

typedef struct {
    int rawPts, rawMul, rawScore;
    int verPts, verMul, verScore;
} Stats;

// WPX prefix of a callsign, e.g. "DL1ABC" -> "DL1", "VE3/G4ABC" -> "VE3"
void extract_prefix(const char* call, char* out, size_t outSize);
// HST score of a callsign: its length in Morse units
int call_to_score(const Keyer* keyer, const char* call);
// sets qso->err from the true data
void check_err(Qso* q);
// WPX scoring: points * prefixes, raw and verified
Stats wpx_stats(const Qso* qsos, int count);
Stats hst_stats(const Keyer* keyer, const Qso* qsos, int count);
// QSO count in 5-minute bins
void rate_histogram(const Qso* qsos, int count, int* bins, int binCount);

// "2026-10-22 UA3ABC 123 98"
void wpx_score_string(const char* date, const char* call, int pts, int mul, char* out, size_t outSize);

#endif
