// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "calllist.h"
#include "rnd.h"

#include <stdio.h>
#include <string.h>

// Prefix families with a rough weight reflecting CW contest activity.
// digits: allowed call area digits ("" = any, the 0 being rare);
// sfxMin/sfxMax: suffix length range.
typedef struct {
    const char* const* prefixes;   // alternatives, NULL-terminated
    int weight;
    const char* digits;
    int sfxMin, sfxMax;
} PrefixInfo;

static const PrefixInfo PREFIXES[] = {
    // North America
    {(const char* const[]){"K", "W", "N", NULL}, 90, "", 1, 3},
    {(const char* const[]){"AA", "AB", "AC", "AD", "AE", "AF", "AG", "AI", "AJ", "AK", "KA", "KB", "KC", "KD",
        "KE", "KF", "KG", "KI", "KJ", "KK", "KN", "KO", "KQ", "KR", "KS", "KT", "KU", "KV", "KW", "KX", "KY",
        "KZ", "NA", "NB", "NC", "ND", "NE", "NF", "NG", "NI", "NJ", "NK", "NN", "NO", "NQ", "NR", "NS", "NT",
        "NU", "NV", "NW", "NX", "NY", "NZ", "WA", "WB", "WC", "WD", "WE", "WF", "WG", "WI", "WJ", "WK", "WN",
        "WO", "WQ", "WR", "WS", "WT", "WU", "WV", "WW", "WX", "WY", "WZ", NULL}, 60, "", 1, 3},
    {(const char* const[]){"VE", "VA", NULL}, 18, "1234567", 2, 3},
    {(const char* const[]){"VY2", "VO1", "VO2", NULL}, 2, "", 2, 3},
    {(const char* const[]){"XE", NULL}, 3, "123", 2, 3},
    {(const char* const[]){"KP4", "KH6", "KL7", "NP4", "WP4", "NH6", "WH6", "AL7", NULL}, 3, "", 2, 3},
    // Europe
    {(const char* const[]){"DL", "DK", "DJ", "DF", "DG", "DH", "DM", "DO", "DB", "DC", "DD", NULL}, 40, "", 2, 3},
    {(const char* const[]){"G", "M", NULL}, 12, "034", 2, 3},
    {(const char* const[]){"2E0", NULL}, 2, "", 3, 3},
    {(const char* const[]){"GM", "MM", "GW", "MW", "GI", "MI", NULL}, 5, "034", 2, 3},
    {(const char* const[]){"EI", NULL}, 3, "2345789", 2, 3},
    {(const char* const[]){"F", NULL}, 12, "123456845", 2, 3},
    {(const char* const[]){"ON", "OT", NULL}, 6, "4567", 2, 3},
    {(const char* const[]){"PA", "PD", "PE", "PH", NULL}, 10, "0123459", 2, 3},
    {(const char* const[]){"I", "IK", "IZ", "IW", "IV", NULL}, 18, "012345678", 2, 3},
    {(const char* const[]){"EA", "EB", "EC", NULL}, 12, "1234567", 2, 3},
    {(const char* const[]){"CT", "CS", NULL}, 4, "12", 2, 3},
    {(const char* const[]){"HB9", NULL}, 5, "", 2, 3},
    {(const char* const[]){"OE", NULL}, 6, "123456789", 2, 3},
    {(const char* const[]){"OK", "OL", NULL}, 12, "12", 2, 3},
    {(const char* const[]){"OM", NULL}, 6, "1234567", 2, 3},
    {(const char* const[]){"SP", "SQ", "SN", "SO", NULL}, 14, "123456789", 2, 3},
    {(const char* const[]){"HA", "HG", NULL}, 7, "135678", 2, 3},
    {(const char* const[]){"YO", NULL}, 6, "23456789", 2, 3},
    {(const char* const[]){"LZ", NULL}, 6, "12345", 1, 3},
    {(const char* const[]){"YU", "YT", NULL}, 6, "1234789", 2, 3},
    {(const char* const[]){"9A", NULL}, 6, "1234567", 2, 3},
    {(const char* const[]){"S5", NULL}, 6, "0123579", 2, 3},
    {(const char* const[]){"E7", NULL}, 3, "", 2, 3},
    {(const char* const[]){"Z3", NULL}, 1, "", 2, 3},
    {(const char* const[]){"SV", "SW", NULL}, 4, "123456789", 2, 3},
    {(const char* const[]){"OZ", "OU", NULL}, 6, "1234567", 2, 3},
    {(const char* const[]){"SM", "SA", "SE", "SK", "SL", NULL}, 10, "01234567", 2, 3},
    {(const char* const[]){"LA", "LB", NULL}, 5, "12345679", 2, 3},
    {(const char* const[]){"OH", "OG", NULL}, 10, "0123456789", 2, 3},
    {(const char* const[]){"ES", NULL}, 5, "12345678", 1, 3},
    {(const char* const[]){"YL", NULL}, 5, "2", 2, 3},
    {(const char* const[]){"LY", NULL}, 5, "12345", 2, 3},
    {(const char* const[]){"EU", "EW", NULL}, 6, "1234678", 2, 3},
    {(const char* const[]){"UR", "UT", "UX", "UY", "US", "UW", NULL}, 14, "0123456789", 2, 3},
    {(const char* const[]){"UA", "RA", "RN", "RU", "RV", "RW", "RX", "RZ", "RK", "UB", NULL}, 30, "13469", 2, 3},
    {(const char* const[]){"UA9", "RA9", "RU9", "RW9", "RV9", "UA0", "RA0", NULL}, 8, "", 2, 3},
    {(const char* const[]){"ER", NULL}, 2, "123", 2, 3},
    {(const char* const[]){"4X", "4Z", NULL}, 3, "12456", 2, 3},
    {(const char* const[]){"TA", NULL}, 2, "1234", 2, 3},
    {(const char* const[]){"UN", NULL}, 2, "5679", 2, 3},
    {(const char* const[]){"4L", NULL}, 1, "", 2, 3},
    // Asia / Oceania
    {(const char* const[]){"JA", "JE", "JF", "JG", "JH", "JI", "JJ", "JK", "JL", "JM", "JN", "JO", "JP", "JQ",
        "JR", "JS", "7K", "7L", "7M", "7N", NULL}, 30, "0123456789", 2, 3},
    {(const char* const[]){"HL", "DS", NULL}, 4, "12345", 2, 3},
    {(const char* const[]){"BA", "BD", "BG", "BH", "BY", NULL}, 5, "1234567890", 2, 3},
    {(const char* const[]){"BV", NULL}, 1, "", 2, 3},
    {(const char* const[]){"VU2", NULL}, 2, "", 2, 3},
    {(const char* const[]){"YB", "YC", "YD", NULL}, 4, "0123456789", 2, 3},
    {(const char* const[]){"HS", NULL}, 1, "0", 2, 3},
    {(const char* const[]){"9M2", "9V1", NULL}, 1, "", 2, 3},
    {(const char* const[]){"VK", NULL}, 8, "1234567", 2, 3},
    {(const char* const[]){"ZL", NULL}, 3, "1234", 2, 3},
    // South America / Africa
    {(const char* const[]){"PY", "PU", "PP", "PR", "PT", NULL}, 10, "1234567", 2, 3},
    {(const char* const[]){"LU", NULL}, 5, "1234567", 2, 3},
    {(const char* const[]){"CE", "CA", NULL}, 3, "123", 2, 3},
    {(const char* const[]){"CX", NULL}, 2, "12345", 2, 3},
    {(const char* const[]){"HK", NULL}, 1, "34", 2, 3},
    {(const char* const[]){"YV", NULL}, 1, "15", 2, 3},
    {(const char* const[]){"OA", NULL}, 1, "4", 2, 3},
    {(const char* const[]){"ZS", NULL}, 4, "1256", 2, 3},
    {(const char* const[]){"CN8", "EA8", "EA9", "CT3", "D4C", "5B4", "9H1", NULL}, 3, "", 2, 3},
};
#define PREFIX_COUNT ((int)G_N_ELEMENTS(PREFIXES))

bool is_valid_call(const char* call)
{
    size_t len = strlen(call);
    if (len < 3 || len > 12) return false;
    bool digit = false, letter = false;
    for (const char* c = call; *c; ++c) {
        if (g_ascii_isdigit(*c)) digit = true;
        else if (g_ascii_isupper(*c)) letter = true;
        else if (*c != '/') return false;
    }
    return digit && letter;
}

void calls_generate(char* out)
{
    static int totalWeight = 0;
    if (!totalWeight)
        for (int i = 0; i < PREFIX_COUNT; ++i) totalWeight += PREFIXES[i].weight;

    int r = rnd_int(totalWeight);
    int f = 0;
    for (; f + 1 < PREFIX_COUNT; ++f) {
        if (r < PREFIXES[f].weight) break;
        r -= PREFIXES[f].weight;
    }
    const PrefixInfo* info = &PREFIXES[f];

    int alternatives = 0;
    while (info->prefixes[alternatives]) ++alternatives;
    char call[CALL_LEN];
    g_strlcpy(call, info->prefixes[rnd_int(alternatives)], sizeof(call));
    size_t len = strlen(call);

    if (!len || !g_ascii_isdigit(call[len - 1])) {
        const char* digits = info->digits[0] ? info->digits : "1234567890123456789";
        call[len++] = digits[rnd_int((int)strlen(digits))];
        call[len] = '\0';
    }

    int sfx = info->sfxMin + rnd_int(info->sfxMax - info->sfxMin + 1);
    // short prefixes (K, W, N, G, F, I) mostly go with longer suffixes
    if (len == 2 && sfx == 1) sfx = 2 + rnd_int(2);
    for (int i = 0; i < sfx; ++i) call[len++] = (char)('A' + rnd_int(26));
    call[len] = '\0';

    // occasional portable / QRP stations
    double p = rnd_random();
    snprintf(out, CALL_LEN, "%s%s", call, p < 0.01 ? "/P" : p < 0.015 ? "/QRP" : "");
}

void calls_init(CallList* l)
{
    l->all = g_array_new(FALSE, FALSE, sizeof(CallStr));
    l->pool = g_array_new(FALSE, FALSE, sizeof(CallStr));
    l->used = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
}

void calls_free(CallList* l)
{
    g_clear_pointer(&l->all, g_array_unref);
    g_clear_pointer(&l->pool, g_array_unref);
    g_clear_pointer(&l->used, g_hash_table_destroy);
}

static gint cmp_calls(gconstpointer a, gconstpointer b)
{
    return strcmp(a, b);
}

// takes ownership of the array
static int finish(CallList* l, GArray* calls)
{
    if (!calls->len) {
        g_array_unref(calls);
        return 0;
    }
    g_array_sort(calls, cmp_calls);
    CallStr* c = (CallStr*)calls->data;
    guint n = 0;
    for (guint i = 0; i < calls->len; ++i)
        if (!n || strcmp(c[i], c[n - 1]) != 0) memmove(c[n++], c[i], sizeof(CallStr));
    g_array_set_size(calls, n);
    g_array_unref(l->all);
    l->all = calls;
    calls_restart(l);
    return (int)n;
}

// normalizes a call: no spaces, upper case; ASCII only, whatever the locale
static void normalize(const char* src, size_t len, char* out)
{
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < CALL_LEN; ++i) {
        char c = src[i];
        if (c == ' ' || c == '\t') continue;
        out[n++] = g_ascii_toupper(c);
    }
    out[n] = '\0';
}

static guint32 le32(const char* p)
{
    guint32 v;
    memcpy(&v, p, sizeof(v));
    return GUINT32_FROM_LE(v);
}

int calls_parse_master_dta(CallList* l, const char* data, size_t size)
{
    enum { CHRCOUNT = 37, INDEXSIZE = CHRCOUNT * CHRCOUNT + 1, INDEXBYTES = INDEXSIZE * 4 };   // A..Z 0..9 /
    if (!data || size < INDEXBYTES || le32(data) != INDEXBYTES || le32(data + (INDEXSIZE - 1) * 4) != size)
        return 0;

    GArray* calls = g_array_new(FALSE, FALSE, sizeof(CallStr));
    for (size_t p = INDEXBYTES; p < size;) {
        const char* z = memchr(data + p, '\0', size - p);
        size_t len = z ? (size_t)(z - (data + p)) : size - p;
        char c[CALL_LEN];
        normalize(data + p, len, c);
        if (is_valid_call(c)) g_array_append_vals(calls, c, 1);
        p += len + 1;
    }
    return finish(l, calls);
}

int calls_parse_text(CallList* l, const char* text)
{
    if (!text) return 0;
    GArray* calls = g_array_new(FALSE, FALSE, sizeof(CallStr));
    for (const char* line = text; *line;) {
        size_t len = strcspn(line, "\r\n");
        size_t indent = strspn(line, " \t");
        size_t callLen = indent < len ? strcspn(line + indent, "#,;\t \r\n") : 0;
        char c[CALL_LEN];
        normalize(line + indent, callLen, c);
        if (is_valid_call(c)) g_array_append_vals(calls, c, 1);
        line += len;
        while (*line == '\r' || *line == '\n') ++line;
    }
    return finish(l, calls);
}

void calls_restart(CallList* l)
{
    g_array_set_size(l->pool, 0);
    g_array_append_vals(l->pool, l->all->data, l->all->len);
    g_hash_table_remove_all(l->used);
}

void calls_pick(CallList* l, bool remove, char* out)
{
    if (!l->all->len) {
        calls_generate(out);
        for (int i = 0; remove && g_hash_table_contains(l->used, out) && i < 50; ++i) calls_generate(out);
        if (remove) g_hash_table_add(l->used, g_strdup(out));
        return;
    }
    if (!l->pool->len) calls_restart(l);
    guint idx = (guint)rnd_int((int)l->pool->len);
    g_strlcpy(out, g_array_index(l->pool, CallStr, idx), CALL_LEN);
    // the last call takes the place of the removed one
    if (remove) g_array_remove_index_fast(l->pool, idx);
}
