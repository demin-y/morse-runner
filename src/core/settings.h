// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_SETTINGS_H
#define MR_SETTINGS_H

#include <stdbool.h>

#define SAMPLE_RATE 11025

#define CALL_LEN 16     // callsign buffers
#define NAME_LEN 64

typedef enum { RM_STOP, RM_PILEUP, RM_SINGLE, RM_WPX, RM_HST } RunMode;

// the competition modes run with fixed conditions and duration
static inline bool run_mode_is_competition(RunMode m) { return m == RM_WPX || m == RM_HST; }

enum {
    MIN_WPM = 10, MAX_WPM = 120,
    MIN_PITCH = 300, MAX_PITCH = 900,
    MIN_BANDWIDTH = 100, MAX_BANDWIDTH = 600,
    MAX_RIT = 500,
    MIN_ACTIVITY = 1, MAX_ACTIVITY = 9,
    MIN_DURATION = 1, MAX_DURATION = 240,
    MIN_COMP_DURATION = 1, MAX_COMP_DURATION = 60,
    MIN_MONITOR_DB = -60, MAX_MONITOR_DB = 20,
    MAX_CALL_CHARS = 12,
    HST_BANDWIDTH = 600,
    HST_ACTIVITY = 4
};

typedef struct {
    // station
    char call[CALL_LEN];
    char operatorName[NAME_LEN];   // shown in HST results
    int wpm;
    int pitch;          // Hz, 300..900 step 50
    int bandwidth;      // Hz, 100..600 step 50
    bool qsk;
    int rit;            // Hz, -500..500
    int monitorDb;      // self-monitor level, -60..+20 dB
    bool callsFromKeyer;

    // band conditions
    int activity;       // 1..9
    bool qrn, qrm, qsb, flutter, lids;

    // session
    int duration;       // minutes, Pile-Up and Single Calls modes
    int compDuration;   // minutes, WPX and HST modes
    int hiScore;
    bool saveWav;
    int lastMode;       // UI: 0 Pile-Up, 1 Single Calls, 2 WPX, 3 HST

    // system
    int bufSize;        // samples per audio block
    int theme;          // UI: 0 as in the system, 1 light, 2 dark

    RunMode runMode;
} Settings;

void settings_defaults(Settings* s);
// the session length in the given mode
static inline int settings_duration(const Settings* s, RunMode m)
{
    return run_mode_is_competition(m) ? s->compDuration : s->duration;
}

#endif
