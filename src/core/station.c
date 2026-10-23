// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "station.h"
#include "contest.h"
#include "rnd.h"

#include <math.h>
#include <string.h>

static void send_text(Station* s, const char* text);
static void qsb_free(Qsb* q);
static void qsb_set_bandwidth(Qsb* q, double value);

//------------------------------------------------------------------------------
//                                 Station
//------------------------------------------------------------------------------
static Station* station_alloc(Contest* tst, StationKind kind)
{
    Station* s = g_new0(Station, 1);
    s->kind = kind;
    s->tst = tst;
    s->wpm = 30;
    s->rst = 599;
    s->state = SS_LISTENING;
    s->timeOut = NEVER;
    return s;
}

void station_delete(Station* s)
{
    if (!s) return;
    fb_free(&s->envelope);
    if (s->kind == ST_DX) qsb_free(&s->qsb);
    g_free(s);
}

void station_set_pitch(Station* s, int value)
{
    s->dPhi = 2 * G_PI * value / SAMPLE_RATE;
}

double station_next_bfo(Station* s)
{
    double result = s->bfo;
    s->bfo += s->dPhi;
    if (s->bfo > 2 * G_PI) s->bfo -= 2 * G_PI;
    return result;
}

static void base_get_block(Station* s, FloatBuf* out)
{
    const int n = s->tst->cfg.bufSize;
    fb_zero(out, n);
    for (int i = 0; i < n && s->sendPos + i < s->envelope.len; ++i)
        out->data[i] = s->envelope.data[s->sendPos + i];

    // advance the TX buffer
    s->sendPos += n;
    if (s->sendPos >= s->envelope.len) fb_clear(&s->envelope);
}

static void send_morse(Station* s, const char* morse)
{
    if (!s->envelope.len) {
        s->sendPos = 0;
        s->bfo = 0;
    }
    keyer_envelope(&s->tst->keyer, morse, s->wpm, s->tst->cfg.bufSize, &s->envelope);
    for (int i = 0; i < s->envelope.len; ++i) s->envelope.data[i] *= s->amplitude;
    s->state = SS_SENDING;
    s->timeOut = NEVER;
}

// the exchange as the station sends it, g_free() the result
static gchar* nr_as_text(Station* s)
{
    GString* r = g_string_new(NULL);
    g_string_printf(r, "%d%03d", s->rst, s->nr);

    if (s->nrWithError) {
        int idx = (int)r->len - 1;
        if (!(r->str[idx] >= '2' && r->str[idx] <= '7')) --idx;
        if (idx >= 0 && r->str[idx] >= '2' && r->str[idx] <= '7') {
            if (rnd_random() < 0.5) --r->str[idx]; else ++r->str[idx];
            g_string_append_printf(r, "EEEEE %03d", s->nr);
        }
        s->nrWithError = false;
    }

    g_string_replace(r, "599", "5NN", 0);

    if (s->tst->cfg.runMode != RM_HST) {
        // cut numbers
        g_string_replace(r, "000", "TTT", 0);
        g_string_replace(r, "00", "TT", 0);
        if (rnd_random() < 0.4) g_string_replace(r, "0", "O", 0);
        else if (rnd_random() < 0.97) g_string_replace(r, "0", "T", 0);
        if (rnd_random() < 0.97) g_string_replace(r, "9", "N", 0);
    }
    return g_string_free(r, FALSE);
}

static void base_send_text(Station* s, const char* text)
{
    GString* msg = g_string_new(text);
    if (strstr(msg->str, "<#>")) {
        // the first number may be sent with an error...
        gchar* nr = nr_as_text(s);
        g_string_replace(msg, "<#>", nr, 1);
        g_free(nr);
        // ...and is then repeated correctly
        nr = nr_as_text(s);
        g_string_replace(msg, "<#>", nr, 0);
        g_free(nr);
    }
    g_string_replace(msg, "<my>", s->myCall, 0);
    g_string_replace(msg, "<his>", s->hisCall, 0);

    if (s->msgText[0]) g_strlcat(s->msgText, " ", sizeof(s->msgText));
    g_strlcat(s->msgText, msg->str, sizeof(s->msgText));
    g_string_free(msg, TRUE);

    char morse[MSG_LEN * 8];
    keyer_encode(&s->tst->keyer, s->msgText, morse, sizeof(morse));
    send_morse(s, morse);
}

// <my> = my call, <his> = his call, <#> = RST + exchange number
static const char* const MSG_TEXT[] = {
    [MSG_CQ] = "CQ <my> TEST",
    [MSG_NR] = "<#>",
    [MSG_TU] = "TU",
    [MSG_MY_CALL] = "<my>",
    [MSG_HIS_CALL] = "<his>",
    [MSG_B4] = "QSO B4",
    [MSG_QM] = "?",
    [MSG_NIL] = "NIL",
    [MSG_R_NR] = "R <#>",
    [MSG_R_NR2] = "R <#> <#>",
    [MSG_DE_MY_CALL1] = "DE <my>",
    [MSG_DE_MY_CALL2] = "DE <my> <my>",
    [MSG_DE_MY_CALL_NR1] = "DE <my> <#>",
    [MSG_DE_MY_CALL_NR2] = "DE <my> <my> <#>",
    [MSG_MY_CALL_NR2] = "<my> <my> <#>",
    [MSG_NR_QM] = "NR?",
    [MSG_LONG_CQ] = "CQ CQ TEST <my> <my> TEST",
    [MSG_QRL] = "QRL?",
    [MSG_QRL2] = "QRL?   QRL?",
    [MSG_QSY] = "<his>  QSY QSY",
    [MSG_AGN] = "AGN",
};

void station_send_msg(Station* s, Msg m)
{
    if (!s->envelope.len) s->msg = 0;
    if (m == MSG_NONE) {
        s->state = SS_LISTENING;
        return;
    }
    s->msg |= msg_bit(m);

    const char* text = MSG_TEXT[m];
    if (text) send_text(s, text);
}

void station_tick(Station* s)
{
    if (s->state == SS_SENDING && !s->envelope.len) {
        // just finished sending
        s->msgText[0] = '\0';
        s->state = SS_LISTENING;
        station_process_event(s, EV_MSG_SENT);
    } else if (s->state != SS_SENDING) {
        // check the timeout
        if (s->timeOut > -1 && s->timeOut != NEVER) --s->timeOut;
        if (s->timeOut == 0) station_process_event(s, EV_TIMEOUT);
    }
}

//------------------------------------------------------------------------------
//                                   Qsb
//------------------------------------------------------------------------------
static double qsb_new_gain(Qsb* q)
{
    double re, im;
    qavg_filter(&q->filt, rnd_uniform(), rnd_uniform(), &re, &im);
    double result = sqrt((re * re + im * im) * 3 * q->filt.points);
    return result * q->qsbLevel + (1 - q->qsbLevel);
}

static void qsb_init(Qsb* q, int bufSize)
{
    qavg_init(&q->filt, 3, 128);
    q->bufSize = bufSize < 4 ? 4 : bufSize;
    q->gain = 1;
    q->qsbLevel = 1;
    qsb_set_bandwidth(q, 0.1);
}

static void qsb_free(Qsb* q)
{
    qavg_free(&q->filt);
}

static void qsb_set_bandwidth(Qsb* q, double value)
{
    qavg_set_points(&q->filt, (int)ceil(0.37 * SAMPLE_RATE / ((q->bufSize / 4) * value)));
    for (int i = 0; i <= q->filt.points * 3; ++i) q->gain = qsb_new_gain(q);
}

static void qsb_apply(Qsb* q, FloatBuf* data)
{
    const int quarter = q->bufSize / 4;
    const int blkCnt = data->len / quarter;
    for (int b = 0; b < blkCnt; ++b) {
        double dG = (qsb_new_gain(q) - q->gain) / quarter;
        for (int i = 0; i < quarter; ++i) {
            data->data[b * quarter + i] = (float)(data->data[b * quarter + i] * q->gain);
            q->gain += dG;
        }
    }
}

//------------------------------------------------------------------------------
//                               DxOperator
//------------------------------------------------------------------------------
#define FULL_PATIENCE 5

void oper_init(DxOperator* o, Contest* tst)
{
    memset(o, 0, sizeof(*o));
    o->tst = tst;
    o->skills = 1;
    o->patience = FULL_PATIENCE;
    o->repeatCnt = 1;
    o->state = OS_NEED_PREV_END;
}

// The delay before the reply, the keying speed and the exchange number
// depend on the skills of the operator

// a timeout must be at least one block: station_tick counts down before it checks
static int at_least_one(int blocks) { return blocks < 1 ? 1 : blocks; }

static int oper_send_delay(const DxOperator* o)
{
    if (o->state == OS_NEED_PREV_END) return NEVER;
    if (o->tst->cfg.runMode == RM_HST)
        return at_least_one(contest_seconds_to_blocks(o->tst, 0.05 + 0.5 * rnd_random() * 10.0 / o->tst->cfg.wpm));
    return at_least_one(contest_seconds_to_blocks(o->tst, 0.1 + 0.5 * rnd_random()));
}

static int oper_wpm(const DxOperator* o)
{
    if (o->tst->cfg.runMode == RM_HST) return o->tst->cfg.wpm;
    return (int)lround(o->tst->cfg.wpm * 0.5 * (1 + rnd_random()));
}

static int oper_nr(const DxOperator* o)
{
    return 1 + (int)lround(rnd_random() * contest_minute(o->tst) * o->skills);
}

static int oper_reply_timeout(const DxOperator* o)
{
    int result;
    if (o->tst->cfg.runMode == RM_HST) result = contest_seconds_to_blocks(o->tst, 60.0 / o->tst->cfg.wpm);
    else result = contest_seconds_to_blocks(o->tst, 6 - o->skills);
    return at_least_one((int)lround(rnd_gauss_lim(result, result / 2.0)));
}

static void dec_patience(DxOperator* o)
{
    if (o->state == OS_DONE) return;
    if (--o->patience < 1) o->state = OS_FAILED;
}

static void oper_set_state(DxOperator* o, OperatorState s)
{
    o->state = s;
    if (s == OS_NEED_QSO) o->patience = (int)lround(rnd_rayleigh(4));
    else o->patience = FULL_PATIENCE;

    RunMode mode = o->tst->cfg.runMode;
    if (s == OS_NEED_QSO && mode != RM_SINGLE && mode != RM_HST && rnd_random() < 0.1)
        o->repeatCnt = 2;
    else
        o->repeatCnt = 1;
}

static int min3(int a, int b, int c)
{
    int m = a < b ? a : b;
    return m < c ? m : c;
}

CallCheck oper_is_my_call(const DxOperator* o)
{
    enum { W_X = 2, W_Y = 2, W_D = 2 };
    const char* c0 = o->call;                // his true call
    const char* c = o->tst->me->hisCall;     // the call I sent
    const int nx = (int)strlen(c), ny = (int)strlen(c0);
    int m[CALL_LEN + 1][CALL_LEN + 1];

    // dynamic programming: weighted edit distance with '?' wildcards
    for (int y = 0; y <= ny; ++y) m[0][y] = 0;
    for (int x = 1; x <= nx; ++x) m[x][0] = m[x - 1][0] + W_X;

    for (int x = 1; x <= nx; ++x)
        for (int y = 1; y <= ny; ++y) {
            const char cx = c[x - 1];
            int t = m[x][y - 1];
            // '?' can match more than one char, the end may be missing
            if (x < nx && cx != '?') t += W_Y;

            int l = m[x - 1][y];
            // '?' can match no chars
            if (cx != '?') l += W_X;

            int d = m[x - 1][y - 1];
            // '?' matches any char
            if (cx != c0[y - 1] && cx != '?') d += W_D;

            m[x][y] = min3(t, d, l);
        }

    // classify by penalty
    int penalty = m[nx][ny];
    CallCheck result = penalty == 0 ? MC_YES : penalty <= 2 ? MC_ALMOST : MC_NO;

    // callsign-specific corrections
    const bool lids = contest_lids(o->tst);
    if (!lids && nx == 2 && result == MC_ALMOST) result = MC_NO;

    // partial and wildcard matches result in 0 penalty but are not exact matches
    if (result == MC_YES && (nx != ny || strchr(c, '?'))) result = MC_ALMOST;

    // partial match too short
    int letters = 0;
    for (int i = 0; i < nx; ++i) if (c[i] != '?') ++letters;
    if (letters < 2) result = MC_NO;

    // accept a wrong call, or reject the correct one
    if (lids && nx > 3) {
        if (result == MC_YES && rnd_random() < 0.01) result = MC_ALMOST;
        else if (result == MC_ALMOST && rnd_random() < 0.04) result = MC_YES;
    }
    return result;
}

static void oper_msg_received(DxOperator* o, MsgSet m)
{
    // if CQ is received, we can call no matter what else was sent
    if (has_msg(m, MSG_CQ)) {
        switch (o->state) {
        case OS_NEED_PREV_END: oper_set_state(o, OS_NEED_QSO); break;
        case OS_NEED_QSO: dec_patience(o); break;
        case OS_NEED_NR: case OS_NEED_CALL: case OS_NEED_CALL_NR: o->state = OS_FAILED; break;
        case OS_NEED_END: o->state = OS_DONE; break;
        default: break;
        }
        return;
    }

    if (has_msg(m, MSG_NIL)) {
        switch (o->state) {
        case OS_NEED_PREV_END: oper_set_state(o, OS_NEED_QSO); break;
        case OS_NEED_QSO: dec_patience(o); break;
        case OS_NEED_NR: case OS_NEED_CALL: case OS_NEED_CALL_NR: case OS_NEED_END: o->state = OS_FAILED; break;
        default: break;
        }
        return;
    }

    OperatorState st = o->state;
    if (has_msg(m, MSG_HIS_CALL)) {
        switch (oper_is_my_call(o)) {
        case MC_YES:
            if (st == OS_NEED_PREV_END || st == OS_NEED_QSO) oper_set_state(o, OS_NEED_NR);
            else if (st == OS_NEED_CALL_NR) oper_set_state(o, OS_NEED_NR);
            else if (st == OS_NEED_CALL) oper_set_state(o, OS_NEED_END);
            break;
        case MC_ALMOST:
            if (st == OS_NEED_PREV_END || st == OS_NEED_QSO) oper_set_state(o, OS_NEED_CALL_NR);
            else if (st == OS_NEED_NR) oper_set_state(o, OS_NEED_CALL_NR);
            else if (st == OS_NEED_END) oper_set_state(o, OS_NEED_CALL);
            break;
        case MC_NO:
            if (st == OS_NEED_QSO) o->state = OS_NEED_PREV_END;
            else if (st == OS_NEED_NR || st == OS_NEED_CALL || st == OS_NEED_CALL_NR) o->state = OS_FAILED;
            else if (st == OS_NEED_END) o->state = OS_DONE;
            break;
        }
    }

    if (has_msg(m, MSG_B4)) {
        switch (o->state) {
        case OS_NEED_PREV_END: case OS_NEED_QSO: oper_set_state(o, OS_NEED_QSO); break;
        case OS_NEED_NR: case OS_NEED_END: o->state = OS_FAILED; break;
        default: break;   // NeedCall, NeedCallNr: same state, correct the call
        }
    }

    const bool hst = o->tst->cfg.runMode == RM_HST;
    if (has_msg(m, MSG_NR)) {
        switch (o->state) {
        case OS_NEED_QSO: o->state = OS_NEED_PREV_END; break;
        case OS_NEED_NR: if (rnd_random() < 0.9 || hst) oper_set_state(o, OS_NEED_END); break;
        case OS_NEED_CALL_NR: if (rnd_random() < 0.9 || hst) oper_set_state(o, OS_NEED_CALL); break;
        default: break;
        }
    }

    if (has_msg(m, MSG_TU)) {
        switch (o->state) {
        case OS_NEED_PREV_END: oper_set_state(o, OS_NEED_QSO); break;
        case OS_NEED_END: o->state = OS_DONE; break;
        default: break;
        }
    }

    if (!contest_lids(o->tst) && m == msg_bit(MSG_GARBAGE)) o->state = OS_NEED_PREV_END;

    if (o->state != OS_NEED_PREV_END) dec_patience(o);
}

static Msg oper_reply(const DxOperator* o)
{
    const bool hst = o->tst->cfg.runMode == RM_HST;
    switch (o->state) {
    case OS_NEED_PREV_END: case OS_DONE: case OS_FAILED:
        return MSG_NONE;
    case OS_NEED_QSO:
        return MSG_MY_CALL;
    case OS_NEED_NR:
        if (o->patience == FULL_PATIENCE - 1 || rnd_random() < 0.3) return MSG_NR_QM;
        return MSG_AGN;
    case OS_NEED_CALL:
        if (hst || rnd_random() > 0.5) return MSG_DE_MY_CALL_NR1;
        if (rnd_random() > 0.25) return MSG_DE_MY_CALL_NR2;
        return MSG_MY_CALL_NR2;
    case OS_NEED_CALL_NR:
        if (hst || rnd_random() > 0.5) return MSG_DE_MY_CALL1;
        return MSG_DE_MY_CALL2;
    case OS_NEED_END:
    default:
        if (o->patience < FULL_PATIENCE - 1) return MSG_NR;
        if (hst || rnd_random() < 0.9) return MSG_R_NR;
        return MSG_R_NR2;
    }
}

//------------------------------------------------------------------------------
//                                DxStation
//------------------------------------------------------------------------------
Station* station_new_dx(Contest* tst)
{
    Station* s = station_alloc(tst, ST_DX);
    g_strlcpy(s->hisCall, tst->cfg.call, sizeof(s->hisCall));
    calls_pick(&tst->calls, tst->cfg.runMode == RM_HST, s->myCall);

    oper_init(&s->oper, tst);
    g_strlcpy(s->oper.call, s->myCall, sizeof(s->oper.call));
    s->oper.skills = 1 + rnd_int(3);   // 1..3
    oper_set_state(&s->oper, OS_NEED_PREV_END);
    s->nrWithError = contest_lids(tst) && rnd_random() < 0.1;

    s->wpm = oper_wpm(&s->oper);
    s->nr = oper_nr(&s->oper);
    if (contest_lids(tst) && rnd_random() < 0.03) s->rst = 559 + 10 * rnd_int(4);
    else s->rst = 599;

    qsb_init(&s->qsb, tst->cfg.bufSize);
    qsb_set_bandwidth(&s->qsb, 0.1 + rnd_random() / 2);
    if (contest_flutter(tst) && rnd_random() < 0.3) qsb_set_bandwidth(&s->qsb, 3 + rnd_random() * 30);

    s->amplitude = (float)(9000 + 18000 * (1 + rnd_ushaped()));
    station_set_pitch(s, (int)lround(rnd_gauss_lim(0, 300)));

    // the MeFinished event will follow immediately
    s->timeOut = NEVER;
    s->state = SS_COPYING;
    return s;
}

static void dx_process_event(Station* s, StationEvent e)
{
    DxOperator* o = &s->oper;
    if (o->state == OS_DONE) return;

    switch (e) {
    case EV_MSG_SENT:
        // we finished sending and started listening
        if (s->tst->me->state == SS_SENDING) s->timeOut = NEVER;
        else s->timeOut = oper_reply_timeout(o);
        break;

    case EV_TIMEOUT:
        // he did not reply, quit or try again
        if (s->state == SS_LISTENING) {
            oper_msg_received(o, msg_bit(MSG_NONE));
            if (o->state == OS_FAILED) { s->removed = true; return; }
            s->state = SS_PREPARING_TO_SEND;
        }
        // preparations to send are done, now send
        if (s->state == SS_PREPARING_TO_SEND)
            for (int i = 0; i < o->repeatCnt; ++i) station_send_msg(s, oper_reply(o));
        break;

    case EV_ME_FINISHED:
        // we notice the message only if we are not sending ourselves
        if (s->state != SS_SENDING) {
            MsgSet m = s->tst->me->msg;
            if (s->state == SS_COPYING)
                oper_msg_received(o, m);
            // these messages can be copied even if partially received
            else if (has_msg(m, MSG_CQ) || has_msg(m, MSG_TU) || has_msg(m, MSG_NIL))
                oper_msg_received(o, m);
            else
                oper_msg_received(o, msg_bit(MSG_GARBAGE));

            // react to the message
            if (o->state == OS_FAILED) { s->removed = true; return; }   // give up
            s->timeOut = oper_send_delay(o);   // reply or switch to standby
            s->state = SS_PREPARING_TO_SEND;
        }
        break;

    case EV_ME_STARTED:
        // if we are not sending, we can start copying;
        // cancel the timeout, he is replying
        if (s->state != SS_SENDING) s->state = SS_COPYING;
        s->timeOut = NEVER;
        break;
    }
}

void dx_data_to_last_qso(Station* s)
{
    Qso* q = contest_last_qso(s->tst);
    if (q) {
        g_strlcpy(q->trueCall, s->myCall, sizeof(q->trueCall));
        q->trueRst = s->rst;
        q->trueNr = s->nr;
    }
    s->removed = true;
}

//------------------------------------------------------------------------------
//                               QrmStation
//------------------------------------------------------------------------------
Station* station_new_qrm(Contest* tst)
{
    Station* s = station_alloc(tst, ST_QRM);
    s->patience = 1 + rnd_int(5);
    calls_pick(&tst->calls, false, s->myCall);
    g_strlcpy(s->hisCall, tst->cfg.call, sizeof(s->hisCall));
    s->amplitude = (float)(5000 + 25000 * rnd_random());
    station_set_pitch(s, (int)lround(rnd_gauss_lim(0, 300)));
    s->wpm = 30 + rnd_int(20);

    switch (rnd_int(7)) {
    case 0: station_send_msg(s, MSG_QRL); break;
    case 1: case 2: station_send_msg(s, MSG_QRL2); break;
    case 3: case 4: case 5: station_send_msg(s, MSG_LONG_CQ); break;
    default: station_send_msg(s, MSG_QSY); break;
    }
    return s;
}

static void qrm_process_event(Station* s, StationEvent e)
{
    switch (e) {
    case EV_MSG_SENT:
        if (--s->patience <= 0) s->removed = true;
        else s->timeOut = at_least_one((int)lround(rnd_gauss_lim(contest_seconds_to_blocks(s->tst, 4), 2)));
        break;
    case EV_TIMEOUT:
        station_send_msg(s, MSG_LONG_CQ);
        break;
    default:
        break;
    }
}

//------------------------------------------------------------------------------
//                               QrnStation
//------------------------------------------------------------------------------
Station* station_new_qrn(Contest* tst)
{
    Station* s = station_alloc(tst, ST_QRN);
    int dur = contest_seconds_to_blocks(tst, rnd_random()) * tst->cfg.bufSize;
    if (dur < tst->cfg.bufSize) dur = tst->cfg.bufSize;
    fb_zero(&s->envelope, dur);
    s->amplitude = (float)(1e5 * pow(10.0, 2 * rnd_random()));
    for (int i = 0; i < dur; ++i)
        if (rnd_random() < 0.01) s->envelope.data[i] = (float)((rnd_random() - 0.5) * s->amplitude);
    s->state = SS_SENDING;
    return s;
}

//------------------------------------------------------------------------------
//                               MyStation
//------------------------------------------------------------------------------
Station* station_new_my(Contest* tst)
{
    Station* s = station_alloc(tst, ST_MY);
    my_init(s);
    return s;
}

void my_init(Station* s)
{
    g_strlcpy(s->myCall, s->tst->cfg.call, sizeof(s->myCall));
    s->nr = 1;
    s->rst = 599;
    station_set_pitch(s, s->tst->cfg.pitch);
    s->wpm = s->tst->cfg.wpm;
    s->amplitude = 300000;
}

void my_abort_send(Station* s)
{
    fb_clear(&s->envelope);
    s->msg = msg_bit(MSG_GARBAGE);
    s->msgText[0] = '\0';
    s->pieceCount = 0;
    s->state = SS_LISTENING;
    station_process_event(s, EV_MSG_SENT);
}

static void add_piece(Station* s, const char* text, size_t len)
{
    if (s->pieceCount < MAX_PIECES) g_strlcpy(s->pieces[s->pieceCount++], text, MIN(len + 1, MSG_LEN));
}

static void pop_piece(Station* s)
{
    if (!s->pieceCount) return;
    --s->pieceCount;
    memmove(s->pieces[0], s->pieces[1], (size_t)s->pieceCount * MSG_LEN);
}

// splits into pieces, "@" stands for his callsign which may be corrected
// while it is being sent
static void add_to_pieces(Station* s, const char* text)
{
    const char* p = text;
    const char* his;
    while ((his = strstr(p, "<his>")) != NULL) {
        if (his > p) add_piece(s, p, (size_t)(his - p));
        add_piece(s, "@", 1);
        p = his + 5;
    }
    if (*p) add_piece(s, p, strlen(p));
}

static void send_next_piece(Station* s)
{
    s->msgText[0] = '\0';
    if (strcmp(s->pieces[0], "@") != 0) base_send_text(s, s->pieces[0]);
    else if (s->tst->cfg.callsFromKeyer && !run_mode_is_competition(s->tst->cfg.runMode)) base_send_text(s, " ");
    else base_send_text(s, s->hisCall);
}

static void my_send_text(Station* s, const char* text)
{
    add_to_pieces(s, text);
    if (s->state != SS_SENDING && s->pieceCount) {
        send_next_piece(s);
        contest_on_me_started_sending(s->tst);
    }
}

static void my_get_block(Station* s, FloatBuf* out)
{
    base_get_block(s, out);
    if (!s->envelope.len) {
        pop_piece(s);
        if (s->pieceCount) send_next_piece(s);
        // the cursor goes to the exchange field
        contest_post_event(s->tst, EE_PIECE_SENT, 0);
    }
}

bool my_update_call_in_message(Station* s, const char* call)
{
    char newCall[CALL_LEN];   // call may be hisCall itself
    g_strlcpy(newCall, call, sizeof(newCall));
    call = newCall;

    if (!call[0]) return false;

    // are we sending the call now?
    bool result = s->pieceCount && strcmp(s->pieces[0], "@") == 0;

    // is the already sent part of the call the same as in the new call?
    if (result) {
        char morse[MSG_LEN * 8];
        FloatBuf env = { 0 };
        keyer_encode(&s->tst->keyer, call, morse, sizeof(morse));
        keyer_envelope(&s->tst->keyer, morse, s->wpm, s->tst->cfg.bufSize, &env);
        for (int i = 0; i < env.len; ++i) env.data[i] *= s->amplitude;

        result = env.len >= s->sendPos && s->envelope.len >= s->sendPos;
        for (int i = 0; result && i < s->sendPos; ++i) result = s->envelope.data[i] == env.data[i];

        if (result) {
            fb_swap(&s->envelope, &env);
            g_strlcpy(s->hisCall, call, sizeof(s->hisCall));
        }
        fb_free(&env);
    }

    // could not correct the current message, but another call is scheduled for sending
    if (!result)
        for (int i = 1; i < s->pieceCount; ++i)
            if (strcmp(s->pieces[i], "@") == 0) {
                g_strlcpy(s->hisCall, call, sizeof(s->hisCall));
                return true;
            }
    return result;
}

//------------------------------------------------------------------------------
//                                dispatch
//------------------------------------------------------------------------------
static void send_text(Station* s, const char* text)
{
    if (s->kind == ST_MY) my_send_text(s, text);
    else base_send_text(s, text);
}

void station_get_block(Station* s, FloatBuf* out)
{
    switch (s->kind) {
    case ST_MY:
        my_get_block(s, out);
        break;
    case ST_DX:
        base_get_block(s, out);
        if (contest_qsb(s->tst)) qsb_apply(&s->qsb, out);
        break;
    default:
        base_get_block(s, out);
        break;
    }
}

void station_process_event(Station* s, StationEvent e)
{
    switch (s->kind) {
    case ST_MY:
        if (e == EV_MSG_SENT) contest_on_me_finished_sending(s->tst);
        break;
    case ST_DX:
        dx_process_event(s, e);
        break;
    case ST_QRM:
        qrm_process_event(s, e);
        break;
    case ST_QRN:
        if (e == EV_MSG_SENT) s->removed = true;
        break;
    }
}
