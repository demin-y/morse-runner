// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "contest.h"
#include "rnd.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void post_event(Contest* t, EngineEventType type, int index, RunMode mode, bool completed);

void contest_init(Contest* t)
{
    memset(t, 0, sizeof(*t));
    settings_defaults(&t->cfg);
    keyer_init(&t->keyer, SAMPLE_RATE);
    calls_init(&t->calls);
    t->stations = g_ptr_array_new_with_free_func((GDestroyNotify)station_delete);
    t->qsos = g_array_new(FALSE, FALSE, sizeof(Qso));
    t->events = g_array_new(FALSE, FALSE, sizeof(EngineEvent));
    mavg_init(&t->filters[0]);
    mavg_init(&t->filters[1]);
    modul_init(&t->modul);
    agc_init(&t->agc, 76, 76, 155, 155);   // AGC attack 5 ms
    t->lastMode = RM_PILEUP;

    t->me = station_new_my(t);
    contest_apply_bandwidth(t);
    contest_apply_pitch(t);
}

void contest_free(Contest* t)
{
    wav_close(&t->wav);
    g_ptr_array_unref(t->stations);
    station_delete(t->me);
    g_array_unref(t->qsos);
    g_array_unref(t->events);
    keyer_free(&t->keyer);
    calls_free(&t->calls);
    mavg_free(&t->filters[0]);
    mavg_free(&t->filters[1]);
    modul_free(&t->modul);
    agc_free(&t->agc);
    reim_free(&t->reIm);
    reim_free(&t->filtered);
    fb_free(&t->blk);
    memset(t, 0, sizeof(*t));
}

//------------------------------------------------------------------------------
//                          effective conditions
//------------------------------------------------------------------------------
static bool condition(const Contest* t, bool user)
{
    return t->cfg.runMode == RM_WPX || (t->cfg.runMode != RM_HST && user);
}

bool contest_qrn(const Contest* t) { return condition(t, t->cfg.qrn); }
bool contest_qrm(const Contest* t) { return condition(t, t->cfg.qrm); }
bool contest_qsb(const Contest* t) { return condition(t, t->cfg.qsb); }
bool contest_flutter(const Contest* t) { return condition(t, t->cfg.flutter); }
bool contest_lids(const Contest* t) { return condition(t, t->cfg.lids); }

int contest_activity(const Contest* t)
{
    return t->cfg.runMode == RM_HST ? HST_ACTIVITY : t->cfg.activity;
}

int contest_bandwidth(const Contest* t)
{
    return t->cfg.runMode == RM_HST ? HST_BANDWIDTH : t->cfg.bandwidth;
}

int contest_duration(const Contest* t)
{
    return settings_duration(&t->cfg, t->cfg.runMode);
}

//------------------------------------------------------------------------------
//                               settings
//------------------------------------------------------------------------------
static int round_to_50(int hz) { return (hz + 25) / 50 * 50; }

void contest_apply_pitch(Contest* t)
{
    modul_set_carrier_freq(&t->modul, t->cfg.pitch);
    station_set_pitch(t->me, t->cfg.pitch);
}

void contest_apply_bandwidth(Contest* t)
{
    const int bw = contest_bandwidth(t);
    for (int i = 0; i < 2; ++i)
        mavg_configure(&t->filters[i], (int)lround(0.7 * SAMPLE_RATE / bw), t->cfg.bufSize, 10 * log10(500.0 / bw));
}

void contest_set_pitch(Contest* t, int hz)
{
    t->cfg.pitch = CLAMP(round_to_50(hz), MIN_PITCH, MAX_PITCH);
    contest_apply_pitch(t);
}

bool contest_set_bandwidth(Contest* t, int hz)
{
    hz = CLAMP(round_to_50(hz), MIN_BANDWIDTH, MAX_BANDWIDTH);
    if (t->cfg.runMode == RM_HST || hz == t->cfg.bandwidth) return false;   // fixed in HST
    t->cfg.bandwidth = hz;
    contest_apply_bandwidth(t);
    return true;
}

void contest_set_wpm(Contest* t, int wpm)
{
    t->cfg.wpm = CLAMP(wpm, MIN_WPM, MAX_WPM);
    t->me->wpm = t->cfg.wpm;
}

void contest_set_my_call(Contest* t, const char* call)
{
    // call may be cfg.call itself, and g_strlcpy must not copy onto its source
    char c[CALL_LEN];
    g_strlcpy(c, call, sizeof(c));
    memcpy(t->cfg.call, c, sizeof(c));
    memcpy(t->me->myCall, c, sizeof(c));
}

void contest_set_rit(Contest* t, int hz)
{
    t->cfg.rit = CLAMP(hz, -MAX_RIT, MAX_RIT);
}

void contest_set_his_call(Contest* t, const char* call)
{
    char c[CALL_LEN];   // call may be hisCall itself
    g_strlcpy(c, call, sizeof(c));
    memcpy(t->me->hisCall, c, sizeof(c));
}

//------------------------------------------------------------------------------
//                              run control
//------------------------------------------------------------------------------
bool contest_start(Contest* t, RunMode mode, FILE* wav)
{
    if (mode == RM_STOP || contest_running(t) || !is_valid_call(t->cfg.call)) {
        if (wav) fclose(wav);
        return false;
    }
    t->cfg.runMode = mode;
    t->lastMode = mode;

    my_init(t->me);
    my_abort_send(t->me);
    t->blockNumber = 0;
    t->ritPhase = 0;
    t->me->nr = 1;
    g_array_set_size(t->qsos, 0);
    ++t->logVersion;
    g_ptr_array_set_size(t->stations, 0);
    calls_restart(&t->calls);
    t->cfg.rit = 0;
    contest_apply_bandwidth(t);
    contest_apply_pitch(t);
    agc_reset(&t->agc);
    g_array_set_size(t->events, 0);

    wav_open(&t->wav, wav, SAMPLE_RATE);
    return true;
}

void contest_stop(Contest* t, bool userStopped)
{
    if (!contest_running(t)) return;
    RunMode mode = t->cfg.runMode;
    t->cfg.runMode = RM_STOP;
    wav_close(&t->wav);
    contest_apply_bandwidth(t);
    post_event(t, EE_CONTEST_OVER, 0, mode, !userStopped);
}

//------------------------------------------------------------------------------
//                                stations
//------------------------------------------------------------------------------
static Station* add_station(Contest* t, Station* s)
{
    g_ptr_array_add(t->stations, s);
    return s;
}

Station* contest_add_caller(Contest* t)
{
    return add_station(t, station_new_dx(t));
}

// deletes the stations that have left the band
static void sweep(Contest* t)
{
    for (int i = contest_station_count(t) - 1; i >= 0; --i)
        if (contest_station(t, i)->removed) g_ptr_array_remove_index(t->stations, i);
}

int contest_dx_count(const Contest* t)
{
    int result = 0;
    for (int i = 0; i < contest_station_count(t); ++i) {
        const Station* s = contest_station(t, i);
        if (s->kind == ST_DX && !s->removed && s->oper.state != OS_DONE) ++result;
    }
    return result;
}

void contest_on_me_finished_sending(Contest* t)
{
    if (!contest_running(t)) return;
    const RunMode mode = t->cfg.runMode;
    const MsgSet m = t->me->msg;

    // the stations heard my CQ and want to call
    if (mode != RM_SINGLE && mode != RM_HST) {
        if (has_msg(m, MSG_CQ) || (contest_qso_count(t) && has_msg(m, MSG_TU) && has_msg(m, MSG_MY_CALL))) {
            int n = rnd_poisson(contest_activity(t) / 2.0);
            for (int i = 0; i < n; ++i) contest_add_caller(t);
        }
    }

    // tell the callers that I finished sending
    for (int i = contest_station_count(t) - 1; i >= 0; --i)
        if (!contest_station(t, i)->removed) station_process_event(contest_station(t, i), EV_ME_FINISHED);
}

void contest_on_me_started_sending(Contest* t)
{
    for (int i = contest_station_count(t) - 1; i >= 0; --i)
        if (!contest_station(t, i)->removed) station_process_event(contest_station(t, i), EV_ME_STARTED);
}

// the DX station has received my TU: write his data to the log and let him go
static void finish_done_stations(Contest* t)
{
    for (int i = contest_station_count(t) - 1; i >= 0; --i) {
        Station* s = contest_station(t, i);
        if (s->kind != ST_DX || s->removed) continue;
        Qso* last = contest_last_qso(t);
        if (s->oper.state == OS_DONE && last && strcmp(s->myCall, last->call) == 0) {
            dx_data_to_last_qso(s);
            check_err(last);
            contest_post_event(t, EE_QSO_UPDATED, contest_qso_count(t) - 1);
        }
    }
}

//------------------------------------------------------------------------------
//                                 audio
//------------------------------------------------------------------------------
void contest_render_block(Contest* t, FloatBuf* out)
{
    const int n = t->cfg.bufSize;
    if (!contest_running(t)) {
        fb_zero(out, n);
        return;
    }
    ++t->blockNumber;

    const float NOISEAMP = 6000;
    ReIm* r = &t->reIm;
    reim_zero(r, n);

    // complex noise
    for (int i = 0; i < n; ++i) {
        r->re.data[i] = (float)(3 * NOISEAMP * (rnd_random() - 0.5));
        r->im.data[i] = (float)(3 * NOISEAMP * (rnd_random() - 0.5));
    }

    // QRN
    if (contest_qrn(t)) {
        // background
        for (int i = 0; i < n; ++i)
            if (rnd_random() < 0.01) r->re.data[i] = (float)(60 * NOISEAMP * (rnd_random() - 0.5));
        // burst
        if (rnd_random() < 0.01) add_station(t, station_new_qrn(t));
    }

    // QRM
    if (contest_qrm(t) && rnd_random() < 0.0002) add_station(t, station_new_qrm(t));

    // audio from the stations
    const double ritStep = 2 * G_PI * t->cfg.rit / SAMPLE_RATE;
    for (int s = 0; s < contest_station_count(t); ++s) {
        Station* st = contest_station(t, s);
        if (st->removed || st->state != SS_SENDING) continue;
        station_get_block(st, &t->blk);
        for (int i = 0; i < n && i < t->blk.len; ++i) {
            double bfo = station_next_bfo(st) - t->ritPhase - i * ritStep;
            r->re.data[i] += (float)(t->blk.data[i] * cos(bfo));
            r->im.data[i] -= (float)(t->blk.data[i] * sin(bfo));
        }
    }

    // RIT
    t->ritPhase += n * ritStep;
    while (t->ritPhase > 2 * G_PI) t->ritPhase -= 2 * G_PI;
    while (t->ritPhase < -2 * G_PI) t->ritPhase += 2 * G_PI;

    // my audio
    Station* me = t->me;
    if (me->state == SS_SENDING) {
        station_get_block(me, &t->blk);
        // self-monitoring gain
        const float smg = (float)pow(10.0, t->cfg.monitorDb / 20.0);
        float rfg = 1;
        for (int i = 0; i < n && i < t->blk.len; ++i) {
            const float b = t->blk.data[i];
            if (t->cfg.qsk) {
                // the receiver recovers between the elements
                float k = 1 - b / me->amplitude;
                if (rfg > k) rfg = k;
                else rfg = rfg * 0.997f + 0.003f;
                r->re.data[i] = smg * b + rfg * r->re.data[i];
                r->im.data[i] = smg * b + rfg * r->im.data[i];
            } else {
                r->re.data[i] = smg * b;
                r->im.data[i] = smg * b;
            }
        }
    }

    // LPF: the second filter warms up and replaces the first one every
    // 10 blocks, this keeps the recursive sums from drifting
    mavg_filter(&t->filters[1 - t->filt], r, NULL);
    mavg_filter(&t->filters[t->filt], r, &t->filtered);
    if (t->blockNumber % 10 == 0) {
        t->filt = 1 - t->filt;
        mavg_reset(&t->filters[1 - t->filt]);
    }

    // mix up to the pitch frequency
    modul_modulate(&t->modul, &t->filtered, out);
    // AGC
    agc_process(&t->agc, out);
    for (int i = 0; i < n; ++i) out->data[i] /= 32768.f;
    // save
    wav_write(&t->wav, out);

    // timer tick
    station_tick(me);
    for (int i = contest_station_count(t) - 1; i >= 0; --i)
        if (!contest_station(t, i)->removed) station_tick(contest_station(t, i));

    // if the DX is done, write to the log and kill
    finish_done_stations(t);

    const int dx = contest_dx_count(t);
    if (t->cfg.runMode == RM_SINGLE && dx == 0) {
        // no need to send CQ in this mode
        me->msg = msg_bit(MSG_CQ);
        station_process_event(contest_add_caller(t), EV_ME_FINISHED);
    } else if (t->cfg.runMode == RM_HST && dx < contest_activity(t)) {
        me->msg = msg_bit(MSG_CQ);
        for (int i = dx + 1; i <= contest_activity(t); ++i)
            station_process_event(contest_add_caller(t), EV_ME_FINISHED);
    }
    sweep(t);

    if (contest_elapsed(t) >= contest_duration(t) * 60.0) contest_stop(t, false);
}

//------------------------------------------------------------------------------
//                            operator actions
//------------------------------------------------------------------------------
void contest_send_msg(Contest* t, Msg m)
{
    if (!contest_running(t)) return;
    station_send_msg(t->me, m);
    sweep(t);
}

void contest_abort_send(Contest* t)
{
    my_abort_send(t->me);
    sweep(t);
}

bool contest_update_call_in_message(Contest* t, const char* call)
{
    return my_update_call_in_message(t->me, call);
}

bool contest_save_qso(Contest* t, const char* call, const char* rst, const char* nr)
{
    if (!contest_running(t)) return false;
    size_t nrLen = strlen(nr);
    if (strlen(call) < 3 || strlen(rst) != 3 || nrLen == 0 || nrLen > 6) return false;

    Qso q = { 0 };
    q.t = contest_elapsed(t);
    size_t n = 0;
    for (const char* c = call; *c && n + 1 < sizeof(q.call); ++c)
        if (*c != '?') q.call[n++] = *c;
    q.call[n] = '\0';
    q.rst = atoi(rst);
    q.nr = atoi(nr);
    extract_prefix(q.call, q.pfx, sizeof(q.pfx));
    if (t->cfg.runMode == RM_HST) snprintf(q.pfx, sizeof(q.pfx), "%d", call_to_score(&t->keyer, q.call));
    q.sentRst = t->me->rst;
    q.sentNr = t->me->nr;

    // mark if dupe
    for (int i = 0; i < contest_qso_count(t); ++i) {
        const Qso* x = contest_qso(t, i);
        if (strcmp(x->call, q.call) == 0 && qso_ok(x)) q.dupe = true;
    }
    g_array_append_val(t->qsos, q);

    // what's in the DX's log?
    for (int i = contest_station_count(t) - 1; i >= 0; --i) {
        Station* s = contest_station(t, i);
        if (s->kind == ST_DX && !s->removed && s->oper.state == OS_DONE && strcmp(s->myCall, q.call) == 0) {
            dx_data_to_last_qso(s);   // the DX station leaves the band
            break;
        }
    }
    check_err(contest_last_qso(t));
    contest_post_event(t, EE_QSO_ADDED, contest_qso_count(t) - 1);

    ++t->me->nr;
    sweep(t);
    return true;
}

//------------------------------------------------------------------------------
//                                 state
//------------------------------------------------------------------------------
int contest_seconds_to_blocks(const Contest* t, double sec)
{
    return (int)lround((double)SAMPLE_RATE / t->cfg.bufSize * sec);
}

double contest_blocks_to_seconds(const Contest* t, double blocks)
{
    return blocks * t->cfg.bufSize / SAMPLE_RATE;
}

double contest_elapsed(const Contest* t)
{
    return contest_blocks_to_seconds(t, (double)t->blockNumber);
}

int contest_rate(const Contest* t)
{
    const double now = contest_elapsed(t);
    if (now <= 0) return 0;
    const double d = now < 300 ? now : 300;
    int cnt = 0;
    for (int i = contest_qso_count(t) - 1; i >= 0 && contest_qso(t, i)->t > now - d; --i) ++cnt;
    return (int)lround(cnt / d * 3600);
}

Stats contest_stats(const Contest* t)
{
    const Qso* qsos = (const Qso*)t->qsos->data;
    const int n = contest_qso_count(t);
    return t->lastMode == RM_HST ? hst_stats(&t->keyer, qsos, n) : wpx_stats(qsos, n);
}

static void post_event(Contest* t, EngineEventType type, int index, RunMode mode, bool completed)
{
    EngineEvent e = { .type = type, .index = index, .mode = mode, .completed = completed };
    g_array_append_val(t->events, e);
    if (type == EE_QSO_ADDED || type == EE_QSO_UPDATED) ++t->logVersion;
}

void contest_post_event(Contest* t, EngineEventType type, int index)
{
    post_event(t, type, index, t->cfg.runMode, false);
}

int contest_take_events(Contest* t, EngineEvent** events)
{
    // hand the buffer over, the next event allocates a new one
    gsize n;
    *events = g_array_steal(t->events, &n);
    return (int)n;
}
