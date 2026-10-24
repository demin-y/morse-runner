// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_CONFIG_H
#define MR_CONFIG_H

#include "settings.h"

#include <glib.h>

#include <stdbool.h>

// Locations of the user files; the folders are created on demand.
// The strings are owned by the module.
const char* path_data_dir(void);     // recordings, score history
const char* path_calls_dir(void);    // the user's call lists
const char* path_exe_dir(void);      // the folder of the program
const char* path_ini_file(void);
const char* path_wav_file(void);
const char* path_score_file(void);   // WPX score strings
const char* path_hst_file(void);     // HST results

// Call lists: *.dta and *.txt files in the calls folder, and *.dta files next to
// the program. The names, sorted; g_strfreev() the result.
gchar** call_list_names(void);
// the full path of a call list, NULL if there is none with that name; g_free() it
gchar* call_list_path(const char* name);

void load_settings(Settings* s);
void save_settings(const Settings* s);

bool append_line(const char* fileName, const char* line);

#endif
