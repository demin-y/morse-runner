// Unit tests of the simulation core. No audio device or GUI is needed.
#include "contest.h"
#include "rnd.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) \
    do { if (!(cond)) { printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

#define CHECK_INT(a, b) \
    do { long long va_ = (a), vb_ = (b); if (va_ != vb_) { \
        printf("%s:%d: %s == %lld, expected %lld\n", __FILE__, __LINE__, #a, va_, vb_); ++failures; } } while (0)

#define CHECK_STR(a, b) \
    do { const char* sa_ = (a); const char* sb_ = (b); if (strcmp(sa_, sb_) != 0) { \
        printf("%s:%d: %s == \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, sa_, sb_); ++failures; } } while (0)

static void test_keyer(void)
{
    Keyer k;
    keyer_init(&k, SAMPLE_RATE);
    char m[256];
    keyer_encode(&k, "CQ", m, sizeof(m));
    CHECK_STR(m, "-.-. --.-~");
    keyer_encode(&k, "E E", m, sizeof(m));
    CHECK_STR(m, ".  .~");
    // a short buffer: truncated, terminated, still ends with the end mark
    char small[6];
    keyer_encode(&k, "CQ CQ", small, sizeof(small));
    CHECK_STR(small, "-.-.~");
    keyer_encode(&k, "", small, sizeof(small));
    CHECK_STR(small, "");

    FloatBuf env = { 0 };
    keyer_encode(&k, "TEST", m, sizeof(m));
    keyer_envelope(&k, m, 30, 512, &env);
    CHECK(env.len > 0);
    CHECK_INT(env.len % 512, 0);
    float mx = 0;
    for (int i = 0; i < env.len; ++i) if (env.data[i] > mx) mx = env.data[i];
    CHECK(mx > 0.99f && mx <= 1.0f);
    // extreme speeds must not crash
    keyer_encode(&k, "5NN TU", m, sizeof(m));
    keyer_envelope(&k, m, 120, 512, &env);
    CHECK(env.len > 0);
    keyer_envelope(&k, m, 10, 512, &env);
    CHECK(env.len > 0);
    fb_free(&env);
    keyer_free(&k);
}

static void test_prefix(void)
{
    struct { const char* call; const char* pfx; } cases[] = {
        {"DL1ABC", "DL1"}, {"VE3/G4ABC", "VE3"}, {"K1ABC/4", "K4"}, {"W1AW/P", "W1"},
        {"HB9XYZ", "HB9"}, {"4X4ABC", "4X4"}, {"RA9ABC/QRP", "RA9"}, {"N8", "N8"}, {"OK", "OK0"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        char p[CALL_LEN];
        extract_prefix(cases[i].call, p, sizeof(p));
        CHECK_STR(p, cases[i].pfx);
    }
}

static void test_scoring(void)
{
    Keyer k;
    keyer_init(&k, SAMPLE_RATE);
    // E = '.', '~'  -> -1 + 2 = 1
    CHECK_INT(call_to_score(&k, "E"), 1);
    // T E: "- .~" -> -1 + 4 + 2 + 2 = 7
    CHECK_INT(call_to_score(&k, "TE"), 7);
    keyer_free(&k);

    Qso log[3];
    memset(log, 0, sizeof(log));
    strcpy(log[0].call, "DL1ABC"); strcpy(log[0].pfx, "DL1");
    strcpy(log[1].call, "DL2ABC"); strcpy(log[1].pfx, "DL2"); strcpy(log[1].err, "NIL");
    strcpy(log[2].call, "DL1XYZ"); strcpy(log[2].pfx, "DL1");
    Stats s = wpx_stats(log, 3);
    CHECK_INT(s.rawPts, 3);
    CHECK_INT(s.rawMul, 2);
    CHECK_INT(s.rawScore, 6);
    CHECK_INT(s.verPts, 2);
    CHECK_INT(s.verMul, 1);
    CHECK_INT(s.verScore, 2);

    char ss[64];
    wpx_score_string("2026-10-22", "UA3ABC", 100, 80, ss, sizeof(ss));
    CHECK(strcmp(ss, "2026-10-22 UA3ABC 100 80") == 0);
}

static void test_call_list(void)
{
    char c[CALL_LEN];
    for (int i = 0; i < 2000; ++i) {
        calls_generate(c);
        if (!is_valid_call(c)) { printf("bad call: %s\n", c); ++failures; break; }
    }
    CallList l;
    calls_init(&l);
    CHECK_INT(l.all->len, 0);
    calls_pick(&l, true, c);
    CHECK(is_valid_call(c));
    calls_free(&l);
}

static void test_call_match(void)
{
    Contest t;
    contest_init(&t);
    t.cfg.lids = false;
    t.cfg.runMode = RM_PILEUP;
    DxOperator op;
    oper_init(&op, &t);
    strcpy(op.call, "DL1ABC");

    struct { const char* sent; CallCheck expected; } cases[] = {
        {"DL1ABC", MC_YES}, {"DL1AB", MC_ALMOST}, {"DL?ABC", MC_ALMOST}, {"ABC", MC_ALMOST},
        {"W1XYZ", MC_NO}, {"D", MC_NO},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        contest_set_his_call(&t, cases[i].sent);
        CHECK_INT(oper_is_my_call(&op), cases[i].expected);
    }
    t.cfg.runMode = RM_STOP;
    contest_free(&t);
}

// renders up to `seconds` of audio until done(t) is true
typedef bool (*Pred)(Contest* t, void* ctx);

static bool run_until(Contest* t, double seconds, Pred done, void* ctx)
{
    FloatBuf out = { 0 };
    int blocks = contest_seconds_to_blocks(t, seconds);
    bool ok = false;
    for (int i = 0; i < blocks && !ok; ++i) {
        contest_render_block(t, &out);
        ok = done(t, ctx);
    }
    fb_free(&out);
    return ok;
}

static Station* find_dx(Contest* t, OperatorState st, const char* call)
{
    for (int i = 0; i < contest_station_count(t); ++i) {
        Station* s = contest_station(t, i);
        if (s->kind == ST_DX && !s->removed && s->oper.state == st && (!call || strcmp(s->myCall, call) == 0))
            return s;
    }
    return NULL;
}

// somebody has called me and stopped sending
static bool someone_called(Contest* t, void* ctx)
{
    Station* dx = find_dx(t, OS_NEED_QSO, NULL);
    *(Station**)ctx = dx;
    return dx && dx->state != SS_SENDING && t->me->state != SS_SENDING && dx->msg != 0;
}

// he has sent R + exchange and waits for my TU
static bool he_needs_end(Contest* t, void* ctx)
{
    Station* dx = find_dx(t, OS_NEED_END, (const char*)ctx);
    return dx && dx->state != SS_SENDING && t->me->state != SS_SENDING;
}

static bool confirmed(Contest* t, void* ctx)
{
    (void)ctx;
    return t->me->state != SS_SENDING && contest_last_qso(t)->trueCall[0];
}

// a complete QSO as a perfect operator would make it; returns the log entry
static const Qso* make_qso(Contest* t, int nrDelta)
{
    Station* dx = NULL;
    if (!run_until(t, 15, someone_called, &dx)) return NULL;

    char call[CALL_LEN], rst[8], nr[16];
    strcpy(call, dx->myCall);
    snprintf(rst, sizeof(rst), "%d", dx->rst);
    snprintf(nr, sizeof(nr), "%d", dx->nr + nrDelta);
    contest_set_his_call(t, call);
    contest_send_msg(t, MSG_HIS_CALL);
    contest_send_msg(t, MSG_NR);
    if (!run_until(t, 20, he_needs_end, call)) return NULL;

    contest_send_msg(t, MSG_TU);
    CHECK(contest_save_qso(t, call, rst, nr));
    // the QSO is confirmed when he receives the TU
    run_until(t, 5, confirmed, NULL);
    return contest_last_qso(t);
}

static void test_full_qso(void)
{
    rnd_seed(12345);
    Contest t;
    contest_init(&t);
    contest_set_my_call(&t, "UA3ABC");
    t.cfg.lids = false;
    t.cfg.qrm = false;
    t.cfg.qrn = false;
    t.cfg.activity = 5;
    t.cfg.duration = 10;
    CHECK(contest_start(&t, RM_PILEUP, NULL));
    CHECK(contest_running(&t));
    CHECK(!contest_start(&t, RM_PILEUP, NULL));   // already running

    int done = 0;
    for (int attempt = 0; attempt < 20 && done < 3; ++attempt) {
        contest_send_msg(&t, MSG_CQ);
        const Qso* q = make_qso(&t, 0);
        if (q && qso_ok(q)) ++done;
        else if (q) printf("QSO with %s: %s\n", q->call, q->err);
    }
    CHECK_INT(done, 3);
    CHECK_INT(contest_stats(&t).verPts, done);

    // incomplete data is rejected
    CHECK(!contest_save_qso(&t, "DL", "599", "1"));
    CHECK(!contest_save_qso(&t, "DL1ABC", "59", "1"));
    CHECK(!contest_save_qso(&t, "DL1ABC", "599", ""));

    contest_stop(&t, true);
    CHECK(!contest_running(&t));
    EngineEvent* ev;
    int n = contest_take_events(&t, &ev);
    CHECK(n > 0 && ev[n - 1].type == EE_CONTEST_OVER && !ev[n - 1].completed && ev[n - 1].mode == RM_PILEUP);
    g_free(ev);
    contest_free(&t);
}

// a wrong exchange in the log is detected
static void test_bad_nr(void)
{
    rnd_seed(777);
    Contest t;
    contest_init(&t);
    contest_set_my_call(&t, "UA3ABC");
    t.cfg.lids = false;
    t.cfg.qrm = false;
    t.cfg.activity = 1;
    contest_start(&t, RM_SINGLE, NULL);
    // the simulated operator does not always copy at the first try
    const Qso* q = NULL;
    for (int attempt = 0; attempt < 10 && !q; ++attempt) q = make_qso(&t, 1);
    CHECK(q != NULL);
    if (q) CHECK_STR(q->err, "NR");
    contest_free(&t);
}

// the contest ends by itself after the duration
static void test_duration(void)
{
    Contest t;
    contest_init(&t);
    contest_set_my_call(&t, "UA3ABC");
    t.cfg.duration = 1;
    contest_start(&t, RM_PILEUP, NULL);
    FloatBuf out = { 0 };
    float mx = 0;
    for (int i = 0; i < contest_seconds_to_blocks(&t, 61) && contest_running(&t); ++i) {
        contest_render_block(&t, &out);
        for (int j = 0; j < out.len; ++j) if (fabsf(out.data[j]) > mx) mx = fabsf(out.data[j]);
    }
    CHECK(!contest_running(&t));
    EngineEvent* ev;
    int n = contest_take_events(&t, &ev);
    CHECK(n > 0 && ev[n - 1].type == EE_CONTEST_OVER && ev[n - 1].completed);
    g_free(ev);
    // the audio is in range and not silent
    CHECK(mx <= 1.0f && mx > 0.05f);
    fb_free(&out);
    contest_free(&t);
}

// a contest does not start without a valid own callsign
static void test_no_call(void)
{
    Contest t;
    contest_init(&t);
    // setting the call from the settings themselves (done at startup) keeps it
    contest_set_my_call(&t, "UA3ABC");
    contest_set_my_call(&t, t.cfg.call);
    CHECK_STR(t.cfg.call, "UA3ABC");
    CHECK_STR(t.me->myCall, "UA3ABC");
    contest_set_my_call(&t, "");
    CHECK(!contest_start(&t, RM_PILEUP, NULL));
    contest_set_my_call(&t, "UA");
    CHECK(!contest_start(&t, RM_PILEUP, NULL));
    CHECK(!contest_running(&t));
    contest_set_my_call(&t, "UA3ABC");
    CHECK(contest_start(&t, RM_PILEUP, NULL));
    contest_free(&t);
}

// indented and commented lines of a text call list are read
static void test_text_call_list(void)
{
    CallList l;
    calls_init(&l);
    CHECK_INT(calls_parse_text(&l, " DL1ABC\n\tk1xyz  # a comment\r\nG4ABC,599\n\n   \n"), 3);
    CHECK_INT(calls_parse_text(&l, "not a call\n"), 0);
    CHECK_INT(l.all->len, 3);   // a failed load keeps the list
    calls_free(&l);
}

// Super Check Partial format: an index of little-endian offsets, then the calls
static void test_master_dta(void)
{
    enum { INDEXSIZE = 37 * 37 + 1, INDEXBYTES = INDEXSIZE * 4 };
    const char calls[] = "DL1ABC\0k1xyz\0DL1ABC\0bad\0";
    size_t size = INDEXBYTES + sizeof(calls) - 1;
    unsigned char* data = g_malloc0(size);
    const uint32_t first = INDEXBYTES, last = (uint32_t)size;
    for (int b = 0; b < 4; ++b) {
        data[b] = (unsigned char)(first >> (8 * b));
        data[(INDEXSIZE - 1) * 4 + b] = (unsigned char)(last >> (8 * b));
    }
    memcpy(data + INDEXBYTES, calls, sizeof(calls) - 1);
    CallList l;
    calls_init(&l);
    CHECK_INT(calls_parse_master_dta(&l, (const char*)data, size), 2);   // unique, valid only
    CHECK_INT(calls_parse_master_dta(&l, (const char*)data, size - 1), 0);   // the index does not match
    calls_free(&l);
    g_free(data);
}

// with long audio blocks a reply delay rounds to 0 blocks: it must still fire
static void test_hst_long_blocks(void)
{
    rnd_seed(99);
    Contest t;
    contest_init(&t);
    contest_set_my_call(&t, "UA3ABC");
    t.cfg.bufSize = 2048;
    contest_apply_bandwidth(&t);
    CHECK(contest_start(&t, RM_HST, NULL));
    FloatBuf out = { 0 };
    for (int b = 0; b < contest_seconds_to_blocks(&t, 30); ++b) {
        if (b % 40 == 5) contest_send_msg(&t, MSG_CQ);
        contest_render_block(&t, &out);
    }
    int stuck = 0;
    for (int i = 0; i < contest_station_count(&t); ++i) {
        const Station* s = contest_station(&t, i);
        if (s->kind == ST_DX && s->state == SS_PREPARING_TO_SEND && s->timeOut <= 0) ++stuck;
    }
    CHECK_INT(stuck, 0);
    fb_free(&out);
    contest_free(&t);
}

static void test_hst(void)
{
    Contest t;
    contest_init(&t);
    contest_set_my_call(&t, "UA3ABC");
    t.cfg.compDuration = 1;
    t.cfg.bandwidth = 200;
    contest_start(&t, RM_HST, NULL);
    CHECK(!contest_qrn(&t) && !contest_qsb(&t) && !contest_lids(&t));
    CHECK_INT(contest_bandwidth(&t), HST_BANDWIDTH);
    FloatBuf out = { 0 };
    contest_render_block(&t, &out);
    CHECK_INT(contest_dx_count(&t), HST_ACTIVITY);
    contest_stop(&t, true);
    CHECK_INT(contest_bandwidth(&t), 200);
    fb_free(&out);
    contest_free(&t);
}

int main(void)
{
    test_keyer();
    test_prefix();
    test_scoring();
    test_call_list();
    test_call_match();
    test_full_qso();
    test_bad_nr();
    test_duration();
    test_no_call();
    test_text_call_list();
    test_master_dta();
    test_hst_long_blocks();
    test_hst();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
