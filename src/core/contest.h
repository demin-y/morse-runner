// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_CONTEST_H
#define MR_CONTEST_H

#include "calllist.h"
#include "dsp.h"
#include "keyer.h"
#include "qsolog.h"
#include "settings.h"
#include "station.h"
#include "wavfile.h"

#include <glib.h>

typedef enum {
    EE_QSO_ADDED,     // index = QSO number
    EE_QSO_UPDATED,   // the DX station confirmed the QSO, index = QSO number
    EE_PIECE_SENT,    // a part of my message was sent, move the cursor
    EE_CONTEST_OVER   // mode = the mode that was running
} EngineEventType;

typedef struct {
    EngineEventType type;
    int index;
    RunMode mode;
    bool completed;   // EE_CONTEST_OVER: the full duration has been run
} EngineEvent;

// The simulation engine: the band, the stations on it, my station and my log.
//
// The engine is not thread-safe: the audio thread and the UI thread
// must serialize the calls with a mutex.
typedef struct Contest {
    Settings cfg;
    Keyer keyer;
    CallList calls;

    Station* me;
    GPtrArray* stations;   // Station*, owned
    GArray* qsos;          // Qso
    GArray* events;        // EngineEvent, pending for the UI

    MovingAverage filters[2];
    int filt;          // index of the active filter, the other one warms up
    Modulator modul;
    VolumeControl agc;
    WavWriter wav;
    double ritPhase;
    long blockNumber;
    RunMode lastMode;
    int logVersion;    // changes whenever the log changes

    ReIm reIm, filtered;
    FloatBuf blk;
} Contest;

void contest_init(Contest* t);
void contest_free(Contest* t);

// -- run control
// returns false without starting when my callsign is not valid;
// wav: the audio recording, or NULL; the contest closes it
bool contest_start(Contest* t, RunMode mode, FILE* wav);
void contest_stop(Contest* t, bool userStopped);
static inline bool contest_running(const Contest* t) { return t->cfg.runMode != RM_STOP; }

// -- effective conditions: the competition modes override the user settings
bool contest_qrn(const Contest* t);
bool contest_qrm(const Contest* t);
bool contest_qsb(const Contest* t);
bool contest_flutter(const Contest* t);
bool contest_lids(const Contest* t);
int contest_activity(const Contest* t);
int contest_bandwidth(const Contest* t);
int contest_duration(const Contest* t);

// -- settings that need to reconfigure the DSP chain
void contest_apply_pitch(Contest* t);
void contest_set_pitch(Contest* t, int hz);
// returns false if nothing changed (the bandwidth is fixed in HST mode)
bool contest_set_bandwidth(Contest* t, int hz);
void contest_apply_bandwidth(Contest* t);
void contest_set_wpm(Contest* t, int wpm);
void contest_set_my_call(Contest* t, const char* call);
void contest_set_rit(Contest* t, int hz);

// -- audio: renders one block of cfg.bufSize samples, scaled to +-1
void contest_render_block(Contest* t, FloatBuf* out);

// -- operator actions
void contest_send_msg(Contest* t, Msg m);
void contest_abort_send(Contest* t);
bool contest_update_call_in_message(Contest* t, const char* call);
// returns false if the data are incomplete
bool contest_save_qso(Contest* t, const char* call, const char* rst, const char* nr);
void contest_set_his_call(Contest* t, const char* call);

// -- state for the UI
double contest_elapsed(const Contest* t);
static inline double contest_minute(const Contest* t) { return contest_elapsed(t) / 60; }
int contest_dx_count(const Contest* t);
int contest_rate(const Contest* t);
Stats contest_stats(const Contest* t);
// moves the pending events to the caller's array (g_free() it), returns the count
int contest_take_events(Contest* t, EngineEvent** events);

// -- used by the stations
int contest_seconds_to_blocks(const Contest* t, double sec);
double contest_blocks_to_seconds(const Contest* t, double blocks);
void contest_on_me_finished_sending(Contest* t);
void contest_on_me_started_sending(Contest* t);
void contest_post_event(Contest* t, EngineEventType type, int index);
Station* contest_add_caller(Contest* t);
static inline int contest_qso_count(const Contest* t) { return (int)t->qsos->len; }
static inline Qso* contest_qso(const Contest* t, int i)
{
    return i >= 0 && i < contest_qso_count(t) ? &g_array_index(t->qsos, Qso, i) : NULL;
}
static inline Qso* contest_last_qso(Contest* t) { return contest_qso(t, contest_qso_count(t) - 1); }
static inline int contest_station_count(const Contest* t) { return (int)t->stations->len; }
static inline Station* contest_station(const Contest* t, int i) { return g_ptr_array_index(t->stations, i); }

#endif
