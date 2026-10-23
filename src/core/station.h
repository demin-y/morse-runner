// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_STATION_H
#define MR_STATION_H

#include "buf.h"
#include "dsp.h"
#include "settings.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

struct Contest;

typedef enum {
    MSG_NONE, MSG_CQ, MSG_NR, MSG_TU, MSG_MY_CALL, MSG_HIS_CALL, MSG_B4, MSG_QM, MSG_NIL,
    MSG_GARBAGE, MSG_R_NR, MSG_R_NR2, MSG_DE_MY_CALL1, MSG_DE_MY_CALL2, MSG_DE_MY_CALL_NR1,
    MSG_DE_MY_CALL_NR2, MSG_NR_QM, MSG_LONG_CQ, MSG_MY_CALL_NR2, MSG_QRL, MSG_QRL2, MSG_QSY, MSG_AGN
} Msg;

typedef uint32_t MsgSet;
static inline MsgSet msg_bit(Msg m) { return (MsgSet)1 << m; }
static inline bool has_msg(MsgSet set, Msg m) { return (set & msg_bit(m)) != 0; }

#define NEVER INT_MAX
#define MSG_LEN 256
#define MAX_PIECES 16

typedef enum { ST_MY, ST_DX, ST_QRM, ST_QRN } StationKind;
typedef enum { SS_LISTENING, SS_COPYING, SS_PREPARING_TO_SEND, SS_SENDING } StationState;
typedef enum { EV_TIMEOUT, EV_MSG_SENT, EV_ME_STARTED, EV_ME_FINISHED } StationEvent;

//------------------------------------------------------------------------------
// Rayleigh fading: a slowly varying random gain
typedef struct {
    QuickAverage filt;
    int bufSize;
    double gain, qsbLevel;
} Qsb;


//------------------------------------------------------------------------------
// The operator of a calling station: a state machine that reacts to my messages
typedef enum {
    OS_NEED_PREV_END, OS_NEED_QSO, OS_NEED_NR, OS_NEED_CALL, OS_NEED_CALL_NR, OS_NEED_END, OS_DONE, OS_FAILED
} OperatorState;

typedef enum { MC_NO, MC_YES, MC_ALMOST } CallCheck;

typedef struct {
    struct Contest* tst;
    char call[CALL_LEN];
    int skills;
    int patience;
    int repeatCnt;
    OperatorState state;
} DxOperator;

void oper_init(DxOperator* o, struct Contest* tst);
// compares the call I sent with his true call
CallCheck oper_is_my_call(const DxOperator* o);

//------------------------------------------------------------------------------
// A station on the band: my station, a calling (DX) station, QRM or QRN
typedef struct Station {
    StationKind kind;
    struct Contest* tst;

    float amplitude;
    int wpm;
    FloatBuf envelope;
    StationState state;
    int nr, rst;
    char myCall[CALL_LEN], hisCall[CALL_LEN];
    MsgSet msg;
    char msgText[MSG_LEN];

    int sendPos;
    int timeOut;
    bool nrWithError;
    double bfo, dPhi;
    bool removed;   // left the band, the contest deletes it

    // DX station
    DxOperator oper;
    Qsb qsb;
    // QRM station
    int patience;
    // my station: the message split into pieces, "@" is his callsign
    char pieces[MAX_PIECES][MSG_LEN];
    int pieceCount;
} Station;

Station* station_new_my(struct Contest* tst);
Station* station_new_dx(struct Contest* tst);
Station* station_new_qrm(struct Contest* tst);
Station* station_new_qrn(struct Contest* tst);
void station_delete(Station* s);

void station_tick(Station* s);
void station_get_block(Station* s, FloatBuf* out);
void station_process_event(Station* s, StationEvent e);
void station_send_msg(Station* s, Msg m);
void station_set_pitch(Station* s, int value);
// returns the current BFO phase and advances it by one sample
double station_next_bfo(Station* s);

// my station
void my_init(Station* s);
void my_abort_send(Station* s);
// tries to replace the callsign being sent with a corrected one
bool my_update_call_in_message(Station* s, const char* call);

// DX station: copies the true data to the last QSO in my log and leaves the band
void dx_data_to_last_qso(Station* s);

#endif
