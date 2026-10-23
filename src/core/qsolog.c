// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "qsolog.h"

#include <glib.h>

#include <stdio.h>
#include <string.h>

void extract_prefix(const char* src, char* out, size_t outSize)
{
    char result[CALL_LEN + 8] = "";
    char dig = '\0';
    out[0] = '\0';

    // kill modifiers
    GString* str = g_string_new(src);
    g_string_append_c(str, '|');
    const char* const MODIFIERS[] = { "/QRP|", "/MM|", "/M|", "/P|", "|" };
    for (size_t i = 0; i < G_N_ELEMENTS(MODIFIERS); ++i) g_string_replace(str, MODIFIERS[i], "", 1);
    while (g_string_replace(str, "//", "/", 0)) {}
    char* call = str->str;
    size_t len = str->len;

    // select the shorter piece
    char* slash = strchr(call, '/');
    if (len < 2) {
        g_string_free(str, TRUE);
        return;
    }
    if (!slash) {
        g_strlcpy(result, call, sizeof(result));
    } else if (slash == call) {
        g_strlcpy(result, call + 1, sizeof(result));
    } else if ((size_t)(slash - call) == len - 1) {
        *slash = '\0';
        g_strlcpy(result, call, sizeof(result));
    } else {
        *slash = '\0';
        const char* s1 = call;
        const char* s2 = slash + 1;
        size_t l1 = strlen(s1), l2 = strlen(s2);
        if (l1 == 1 && g_ascii_isdigit(s1[0])) { dig = s1[0]; g_strlcpy(result, s2, sizeof(result)); }
        else if (l2 == 1 && g_ascii_isdigit(s2[0])) { dig = s2[0]; g_strlcpy(result, s1, sizeof(result)); }
        else g_strlcpy(result, l1 <= l2 ? s1 : s2, sizeof(result));
    }
    g_string_free(str, TRUE);
    len = strlen(result);
    if (!len || strchr(result, '/')) return;

    // delete trailing letters, retain at least 2 chars
    while (len > 2 && !g_ascii_isdigit(result[len - 1])) result[--len] = '\0';
    // ensure digit
    if (!g_ascii_isdigit(result[len - 1])) { result[len++] = '0'; result[len] = '\0'; }
    // replace digit
    if (dig) result[len - 1] = dig;

    result[5] = '\0';
    g_strlcpy(out, result, outSize);
}

int call_to_score(const Keyer* keyer, const char* call)
{
    char s[256];
    keyer_encode(keyer, call, s, sizeof(s));
    int result = -1;
    for (const char* c = s; *c; ++c) {
        switch (*c) {
        case '.': result += 2; break;
        case '-': result += 4; break;
        case ' ': result += 2; break;
        }
    }
    return result;
}

void check_err(Qso* q)
{
    const char* e = "";
    if (!q->trueCall[0]) e = "NIL";
    else if (q->dupe) e = "DUP";
    else if (q->trueRst != q->rst) e = "RST";
    else if (q->trueNr != q->nr) e = "NR";
    g_strlcpy(q->err, e, sizeof(q->err));
}

Stats wpx_stats(const Qso* qsos, int count)
{
    Stats s = { 0 };
    // the multipliers: distinct prefixes
    GHashTable* raw = g_hash_table_new(g_str_hash, g_str_equal);
    GHashTable* ver = g_hash_table_new(g_str_hash, g_str_equal);
    for (int i = 0; i < count; ++i) {
        g_hash_table_add(raw, (gpointer)qsos[i].pfx);
        if (qso_ok(&qsos[i])) {
            g_hash_table_add(ver, (gpointer)qsos[i].pfx);
            ++s.verPts;
        }
    }
    s.rawPts = count;
    s.rawMul = (int)g_hash_table_size(raw);
    s.rawScore = s.rawPts * s.rawMul;
    s.verMul = (int)g_hash_table_size(ver);
    s.verScore = s.verPts * s.verMul;
    g_hash_table_destroy(raw);
    g_hash_table_destroy(ver);
    return s;
}

Stats hst_stats(const Keyer* keyer, const Qso* qsos, int count)
{
    Stats s = { 0 };
    for (int i = 0; i < count; ++i) {
        int score = call_to_score(keyer, qsos[i].call);
        s.rawScore += score;
        if (qso_ok(&qsos[i])) s.verScore += score;
    }
    s.rawPts = count;
    return s;
}

void rate_histogram(const Qso* qsos, int count, int* bins, int binCount)
{
    memset(bins, 0, (size_t)binCount * sizeof(int));
    for (int i = 0; i < count; ++i) {
        int x = (int)(qsos[i].t / 300);
        if (x >= 0 && x < binCount) ++bins[x];
    }
}

void wpx_score_string(const char* date, const char* call, int pts, int mul, char* out, size_t outSize)
{
    snprintf(out, outSize, "%s %s %d %d", date, call, pts, mul);
}
