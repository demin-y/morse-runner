// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_CALLLIST_H
#define MR_CALLLIST_H

#include "settings.h"

#include <glib.h>

#include <stdbool.h>
#include <stddef.h>

typedef char CallStr[CALL_LEN];

// Source of callsigns for the simulated stations.
//
// Calls are loaded from a Super Check Partial "Master.dta" file or from a plain
// text file (one call per line). Without a file, realistic random calls are
// generated from a weighted table of active contest prefixes.
typedef struct {
    GArray* all;        // CallStr, sorted, unique
    GArray* pool;       // CallStr: calls not yet used in this run (HST mode)
    GHashTable* used;   // generated calls already used in this run (HST mode)
} CallList;

void calls_init(CallList* l);
void calls_free(CallList* l);

// parse the contents of a file the caller has read;
// return the number of calls loaded, 0 on failure (the list is unchanged)
int calls_parse_master_dta(CallList* l, const char* data, size_t size);
int calls_parse_text(CallList* l, const char* text);   // one call per line

// restores the calls removed by calls_pick(.., true)
void calls_restart(CallList* l);
// remove = true guarantees unique calls (HST mode)
void calls_pick(CallList* l, bool remove, char* out);
void calls_generate(char* out);

bool is_valid_call(const char* call);

#endif
