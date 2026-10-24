// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "config.h"
#include "buf.h"
#include "keys.h"
#include "theme.h"

#include <glib.h>
#include <glib/gstdio.h>

#ifdef G_OS_WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#include <stdlib.h>
#endif

#include <stdio.h>
#include <string.h>

static const char* SEC_STN = "Station";
static const char* SEC_BND = "Band";
static const char* SEC_TST = "Contest";
static const char* SEC_SYS = "System";

static const char* APP_DIR = "morserunner";

// moves the files of an older version's folder, named "MorseRunner", to the new one
static void migrate(const char* base, const char* to)
{
#ifdef G_OS_WIN32
    // the file names are case-insensitive: it is the same folder
    (void)base; (void)to;
#else
    gchar* from = g_build_filename(base, "MorseRunner", NULL);
    if (g_file_test(from, G_FILE_TEST_IS_DIR)) {
        g_mkdir_with_parents(to, 0755);
        GDir* dir = g_dir_open(from, 0, NULL);
        for (const char* name; dir && (name = g_dir_read_name(dir));) {
            gchar* src = g_build_filename(from, name, NULL);
            gchar* dst = g_build_filename(to, name, NULL);
            if (!g_file_test(dst, G_FILE_TEST_EXISTS)) g_rename(src, dst);
            g_free(src);
            g_free(dst);
        }
        if (dir) g_dir_close(dir);
        g_rmdir(from);   // only if it is empty now
    }
    g_free(from);
#endif
}

// the folders follow the platform's conventions: XDG on Linux, %LOCALAPPDATA% on
// Windows, ~/Library/Application Support on macOS for both settings and data
static const char* app_dir(bool config)
{
    static gchar* dirs[2];
    gchar** p = &dirs[config];
    if (!*p) {
        const char* base = config ? g_get_user_config_dir() : g_get_user_data_dir();
#ifdef __APPLE__
        *p = g_build_filename(g_get_home_dir(), "Library", "Application Support", APP_DIR, NULL);
#else
        *p = g_build_filename(base, APP_DIR, NULL);
#endif
        migrate(base, *p);
        g_mkdir_with_parents(*p, 0755);
    }
    return *p;
}

static const char* in_dir(char** cache, bool config, const char* name)
{
    if (!*cache) *cache = g_build_filename(app_dir(config), name, NULL);
    return *cache;
}

const char* path_data_dir(void) { return app_dir(false); }
const char* path_ini_file(void) { static char* p; return in_dir(&p, true, "MorseRunner.ini"); }
const char* path_wav_file(void) { static char* p; return in_dir(&p, false, "MorseRunner.wav"); }
const char* path_score_file(void) { static char* p; return in_dir(&p, false, "ScoreList.txt"); }
const char* path_hst_file(void) { static char* p; return in_dir(&p, false, "HstResults.txt"); }

// the call list an older version used: Master.dta or calls.txt in the data folder
static const char* const LEGACY_LISTS[] = { "Master.dta", "MASTER.DTA", "calls.txt" };

const char* path_calls_dir(void)
{
    static char* p;
    if (p) return p;
    in_dir(&p, false, "calls");
    g_mkdir_with_parents(p, 0755);
    for (size_t i = 0; i < G_N_ELEMENTS(LEGACY_LISTS); ++i) {
        gchar* src = g_build_filename(path_data_dir(), LEGACY_LISTS[i], NULL);
        gchar* dst = g_build_filename(p, LEGACY_LISTS[i], NULL);
        if (g_file_test(src, G_FILE_TEST_IS_REGULAR) && !g_file_test(dst, G_FILE_TEST_EXISTS)) g_rename(src, dst);
        g_free(src);
        g_free(dst);
    }
    return p;
}

const char* path_exe_dir(void)
{
    static gchar* dir;
    if (dir) return dir;
    gchar* exe = NULL;
#ifdef G_OS_WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) exe = g_utf16_to_utf8((const gunichar2*)buf, (glong)n, NULL, NULL, NULL);
#elif defined(__APPLE__)
    char buf[PATH_MAX], real[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0 && realpath(buf, real)) exe = g_strdup(real);
#else
    exe = g_file_read_link("/proc/self/exe", NULL);
#endif
    dir = exe ? g_path_get_dirname(exe) : g_get_current_dir();
    g_free(exe);
    return dir;
}

//------------------------------------------------------------------------------
//                                call lists
//------------------------------------------------------------------------------
static bool is_call_list(const char* name, bool textToo)
{
    return g_str_has_suffix(name, ".dta") || g_str_has_suffix(name, ".DTA") ||
           (textToo && (g_str_has_suffix(name, ".txt") || g_str_has_suffix(name, ".TXT")));
}

static void add_names(GPtrArray* names, const char* folder, bool textToo)
{
    GDir* dir = g_dir_open(folder, 0, NULL);
    if (!dir) return;
    for (const char* name; (name = g_dir_read_name(dir));) {
        if (!is_call_list(name, textToo)) continue;
        bool dupe = false;
        for (guint i = 0; i < names->len && !dupe; ++i) dupe = strcmp(g_ptr_array_index(names, i), name) == 0;
        if (!dupe) g_ptr_array_add(names, g_strdup(name));
    }
    g_dir_close(dir);
}

static gint cmp_names(gconstpointer a, gconstpointer b)
{
    return g_utf8_collate(*(const char* const*)a, *(const char* const*)b);
}

gchar** call_list_names(void)
{
    GPtrArray* names = g_ptr_array_new();
    add_names(names, path_calls_dir(), true);
    add_names(names, path_exe_dir(), false);   // e.g. Master.dta next to the .exe
    g_ptr_array_sort(names, cmp_names);
    g_ptr_array_add(names, NULL);
    return (gchar**)g_ptr_array_free(names, FALSE);
}

gchar* call_list_path(const char* name)
{
    if (!name || !name[0] || strchr(name, '/') || strchr(name, '\\')) return NULL;
    const char* dirs[] = { path_calls_dir(), path_exe_dir() };
    for (size_t i = 0; i < G_N_ELEMENTS(dirs); ++i) {
        if (i == 1 && !is_call_list(name, false)) break;
        gchar* path = g_build_filename(dirs[i], name, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) return path;
        g_free(path);
    }
    return NULL;
}

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
        if (g_key_file_has_key(kf, SEC_STN, "CallList", NULL)) {
            read_string(kf, SEC_STN, "CallList", s->callList, sizeof(s->callList));
        } else {
            // older versions used the list found in the data folder, now moved to the calls folder
            for (size_t i = 0; i < G_N_ELEMENTS(LEGACY_LISTS) && !s->callList[0]; ++i) {
                gchar* path = call_list_path(LEGACY_LISTS[i]);
                if (path) g_strlcpy(s->callList, LEGACY_LISTS[i], sizeof(s->callList));
                g_free(path);
            }
        }

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

        keys_load(kf);
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
    g_key_file_set_string(kf, SEC_STN, "CallList", s->callList);

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
    keys_save(kf);

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
