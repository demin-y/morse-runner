// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "i18n.h"
#include "config.h"

#include <string.h>

static GHashTable* strings;   // id -> text in the current language
static gchar* current;

// the lang folders, the user's own first
static void lang_dirs(const char* dirs[3], gchar** owned)
{
    *owned = g_build_filename(path_exe_dir(), "..", "share", "morserunner", "lang", NULL);
    static gchar* user;
    static gchar* exe;
    if (!user) user = g_build_filename(path_data_dir(), "lang", NULL);
    if (!exe) exe = g_build_filename(path_exe_dir(), "lang", NULL);
    dirs[0] = user;
    dirs[1] = exe;
    dirs[2] = *owned;
}

// the file of a language, NULL if there is none; g_free() it
static gchar* find_file(const char* code)
{
    const char* dirs[3];
    gchar* owned;
    lang_dirs(dirs, &owned);
    gchar* name = g_strconcat(code, ".ini", NULL);
    gchar* result = NULL;
    for (int i = 0; i < 3 && !result; ++i) {
        gchar* path = g_build_filename(dirs[i], name, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) result = path;
        else g_free(path);
    }
    g_free(name);
    g_free(owned);
    return result;
}

static bool load(const char* code)
{
    gchar* path = find_file(code);
    if (!path) return false;
    GKeyFile* kf = g_key_file_new();
    bool ok = g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);
    gchar** keys = ok ? g_key_file_get_keys(kf, "Strings", NULL, NULL) : NULL;
    for (gchar** k = keys; k && *k; ++k) {
        gchar* text = g_key_file_get_string(kf, "Strings", *k, NULL);
        if (text && text[0]) g_hash_table_replace(strings, g_strdup(*k), text);
        else g_free(text);
    }
    g_strfreev(keys);
    g_key_file_free(kf);
    g_free(path);
    return ok;
}

void i18n_init(const char* code)
{
    if (strings) g_hash_table_destroy(strings);
    strings = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_free(current);
    current = NULL;

    if (code && code[0]) {
        if (strcmp(code, "en") != 0 && load(code)) current = g_strdup(code);
    } else {
        // as in the system: ru_RU.UTF-8, ru_RU, ru...
        for (const gchar* const* n = g_get_language_names(); *n && !current; ++n) {
            if (!strcmp(*n, "C") || !strcmp(*n, "POSIX") || g_str_has_prefix(*n, "en")) break;
            if (!strchr(*n, '.') && !strchr(*n, '@') && load(*n)) current = g_strdup(*n);
        }
    }
    if (!current) current = g_strdup("en");
}

const char* i18n_language(void)
{
    return current ? current : "en";
}

// the conversions of a format string, "%d %s" -> "ds"
static void conversions(const char* s, char* out, size_t size)
{
    size_t n = 0;
    for (const char* p = strchr(s, '%'); p; p = strchr(p, '%')) {
        ++p;
        if (*p == '%') { ++p; continue; }
        p += strspn(p, "-+ #0123456789.*l");
        if (*p && n + 1 < size) out[n++] = *p;
    }
    out[n] = '\0';
}

const char* tr(const char* id, const char* english)
{
    const char* text = strings ? g_hash_table_lookup(strings, id) : NULL;
    if (!text) return english;
    if (strchr(english, '%') || strchr(text, '%')) {
        char a[32], b[32];
        conversions(english, a, sizeof(a));
        conversions(text, b, sizeof(b));
        if (strcmp(a, b) != 0) return english;   // a broken translation must not crash printf
    }
    return text;
}

//------------------------------------------------------------------------------
//                                the list
//------------------------------------------------------------------------------
static void free_language(gpointer p)
{
    Language* l = p;
    g_free(l->code);
    g_free(l->name);
    g_free(l);
}

static gint cmp_languages(gconstpointer a, gconstpointer b)
{
    const Language* x = *(const Language* const*)a;
    const Language* y = *(const Language* const*)b;
    return g_utf8_collate(x->name, y->name);
}

static void add_language(GPtrArray* list, const char* code, const char* name)
{
    for (guint i = 0; i < list->len; ++i)
        if (!strcmp(((Language*)g_ptr_array_index(list, i))->code, code)) return;
    Language* l = g_new(Language, 1);
    l->code = g_strdup(code);
    l->name = g_strdup(name);
    g_ptr_array_add(list, l);
}

GPtrArray* i18n_languages(void)
{
    GPtrArray* list = g_ptr_array_new_with_free_func(free_language);
    add_language(list, "en", "English");
    const char* dirs[3];
    gchar* owned;
    lang_dirs(dirs, &owned);
    for (int i = 0; i < 3; ++i) {
        GDir* dir = g_dir_open(dirs[i], 0, NULL);
        for (const char* f; dir && (f = g_dir_read_name(dir));) {
            if (!g_str_has_suffix(f, ".ini")) continue;
            gchar* code = g_strndup(f, strlen(f) - 4);
            gchar* path = g_build_filename(dirs[i], f, NULL);
            GKeyFile* kf = g_key_file_new();
            if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
                gchar* name = g_key_file_get_string(kf, "Language", "Name", NULL);
                add_language(list, code, name && name[0] ? name : code);
                g_free(name);
            }
            g_key_file_free(kf);
            g_free(path);
            g_free(code);
        }
        if (dir) g_dir_close(dir);
    }
    g_free(owned);
    g_ptr_array_sort(list, cmp_languages);
    return list;
}
