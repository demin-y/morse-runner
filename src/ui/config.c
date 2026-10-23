// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "config.h"
#include "buf.h"
#include "theme.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <stdio.h>
#include <string.h>

static const char* SEC_STN = "Station";
static const char* SEC_BND = "Band";
static const char* SEC_TST = "Contest";
static const char* SEC_SYS = "System";

static const char* in_dir(char** cache, const char* base, const char* name)
{
    if (!*cache) {
        gchar* dir = g_build_filename(base, "MorseRunner", NULL);
        g_mkdir_with_parents(dir, 0755);
        *cache = name ? g_build_filename(dir, name, NULL) : g_strdup(dir);
        g_free(dir);
    }
    return *cache;
}

const char* path_data_dir(void) { static char* p; return in_dir(&p, g_get_user_data_dir(), NULL); }
const char* path_ini_file(void) { static char* p; return in_dir(&p, g_get_user_config_dir(), "MorseRunner.ini"); }
const char* path_wav_file(void) { static char* p; return in_dir(&p, g_get_user_data_dir(), "MorseRunner.wav"); }
const char* path_score_file(void) { static char* p; return in_dir(&p, g_get_user_data_dir(), "ScoreList.txt"); }
const char* path_hst_file(void) { static char* p; return in_dir(&p, g_get_user_data_dir(), "HstResults.txt"); }

static int read_int(GKeyFile* kf, const char* group, const char* key, int def, int lo, int hi)
{
    GError* err = NULL;
    int v = g_key_file_get_integer(kf, group, key, &err);
    if (err) { g_error_free(err); return def; }
    return CLAMP(v, lo, hi);
}

static bool read_bool(GKeyFile* kf, const char* group, const char* key, bool def)
{
    GError* err = NULL;
    gboolean v = g_key_file_get_boolean(kf, group, key, &err);
    if (err) { g_error_free(err); return def; }
    return v != FALSE;
}

static void read_string(GKeyFile* kf, const char* group, const char* key, char* out, size_t size)
{
    gchar* v = g_key_file_get_string(kf, group, key, NULL);
    if (!v) return;
    g_strlcpy(out, v, size);
    g_free(v);
}

static int round_to(int v, int step) { return (v + step / 2) / step * step; }

void load_settings(Settings* s)
{
    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, path_ini_file(), G_KEY_FILE_NONE, NULL)) {
        read_string(kf, SEC_STN, "Call", s->call, sizeof(s->call));
        read_string(kf, SEC_STN, "Name", s->operatorName, sizeof(s->operatorName));
        s->wpm = read_int(kf, SEC_STN, "Wpm", s->wpm, MIN_WPM, MAX_WPM);
        s->pitch = round_to(read_int(kf, SEC_STN, "PitchHz", s->pitch, MIN_PITCH, MAX_PITCH), 50);
        s->bandwidth = round_to(read_int(kf, SEC_STN, "BandwidthHz", s->bandwidth, MIN_BANDWIDTH, MAX_BANDWIDTH), 50);
        s->qsk = read_bool(kf, SEC_STN, "Qsk", s->qsk);
        s->monitorDb = read_int(kf, SEC_STN, "SelfMonVolume", s->monitorDb, MIN_MONITOR_DB, MAX_MONITOR_DB);
        s->callsFromKeyer = read_bool(kf, SEC_STN, "CallsFromKeyer", s->callsFromKeyer);
        s->saveWav = read_bool(kf, SEC_STN, "SaveWav", s->saveWav);

        s->activity = read_int(kf, SEC_BND, "Activity", s->activity, MIN_ACTIVITY, MAX_ACTIVITY);
        s->qrn = read_bool(kf, SEC_BND, "Qrn", s->qrn);
        s->qrm = read_bool(kf, SEC_BND, "Qrm", s->qrm);
        s->qsb = read_bool(kf, SEC_BND, "Qsb", s->qsb);
        s->flutter = read_bool(kf, SEC_BND, "Flutter", s->flutter);
        s->lids = read_bool(kf, SEC_BND, "Lids", s->lids);

        s->duration = read_int(kf, SEC_TST, "Duration", s->duration, MIN_DURATION, MAX_DURATION);
        s->hiScore = read_int(kf, SEC_TST, "HiScore", s->hiScore, 0, 1 << 30);
        s->compDuration = read_int(kf, SEC_TST, "CompetitionDuration", s->compDuration, MIN_COMP_DURATION, MAX_COMP_DURATION);
        s->lastMode = read_int(kf, SEC_TST, "Mode", s->lastMode, 0, 3);

        // audio block = 64 << BufSize samples, 1..5, smaller = faster response
        s->bufSize = 64 << read_int(kf, SEC_SYS, "BufSize", 3, 1, 5);
        gchar* theme = g_key_file_get_string(kf, SEC_SYS, "Theme", NULL);
        if (theme) s->theme = (int)theme_mode_from_id(theme);
        else if (read_bool(kf, SEC_SYS, "DarkTheme", false)) s->theme = THEME_DARK;   // older versions
        g_free(theme);
    }
    g_key_file_free(kf);
}

void save_settings(const Settings* s)
{
    GKeyFile* kf = g_key_file_new();
    g_key_file_load_from_file(kf, path_ini_file(), G_KEY_FILE_KEEP_COMMENTS, NULL);

    g_key_file_set_string(kf, SEC_STN, "Call", s->call);
    g_key_file_set_string(kf, SEC_STN, "Name", s->operatorName);
    g_key_file_set_integer(kf, SEC_STN, "Wpm", s->wpm);
    g_key_file_set_integer(kf, SEC_STN, "PitchHz", s->pitch);
    g_key_file_set_integer(kf, SEC_STN, "BandwidthHz", s->bandwidth);
    g_key_file_set_boolean(kf, SEC_STN, "Qsk", s->qsk);
    g_key_file_set_integer(kf, SEC_STN, "SelfMonVolume", s->monitorDb);
    g_key_file_set_boolean(kf, SEC_STN, "CallsFromKeyer", s->callsFromKeyer);
    g_key_file_set_boolean(kf, SEC_STN, "SaveWav", s->saveWav);

    g_key_file_set_integer(kf, SEC_BND, "Activity", s->activity);
    g_key_file_set_boolean(kf, SEC_BND, "Qrn", s->qrn);
    g_key_file_set_boolean(kf, SEC_BND, "Qrm", s->qrm);
    g_key_file_set_boolean(kf, SEC_BND, "Qsb", s->qsb);
    g_key_file_set_boolean(kf, SEC_BND, "Flutter", s->flutter);
    g_key_file_set_boolean(kf, SEC_BND, "Lids", s->lids);

    g_key_file_set_integer(kf, SEC_TST, "Duration", s->duration);
    g_key_file_set_integer(kf, SEC_TST, "HiScore", s->hiScore);
    g_key_file_set_integer(kf, SEC_TST, "CompetitionDuration", s->compDuration);
    g_key_file_set_integer(kf, SEC_TST, "Mode", s->lastMode);

    int v = 1;
    while (v < 5 && (64 << v) < s->bufSize) ++v;
    g_key_file_set_integer(kf, SEC_SYS, "BufSize", v);
    g_key_file_set_string(kf, SEC_SYS, "Theme", theme_mode_id((ThemeMode)s->theme));
    g_key_file_remove_key(kf, SEC_SYS, "DarkTheme", NULL);

    g_key_file_save_to_file(kf, path_ini_file(), NULL);
    g_key_file_free(kf);
}

bool append_line(const char* fileName, const char* line)
{
    FILE* f = g_fopen(fileName, "a");
    if (!f) return false;
    fprintf(f, "%s\n", line);
    fclose(f);
    return true;
}
