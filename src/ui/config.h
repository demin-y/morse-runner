// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_CONFIG_H
#define MR_CONFIG_H

#include "settings.h"

#include <stdbool.h>

// Locations of the user files; the folders are created on demand.
// The strings are owned by the module.
const char* path_data_dir(void);     // recordings, score history, call lists
const char* path_ini_file(void);
const char* path_wav_file(void);
const char* path_score_file(void);   // WPX score strings
const char* path_hst_file(void);     // HST results

void load_settings(Settings* s);
void save_settings(const Settings* s);

bool append_line(const char* fileName, const char* line);

#endif
