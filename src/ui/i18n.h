// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_I18N_H
#define MR_I18N_H

#include <glib.h>

// Translations of the interface: lang/<code>.ini files with a [Language] section
// (Name=, Code=) and a [Strings] section of id=text. They are looked for in the
// lang folder of the user's data folder, next to the program and in
// <prefix>/share/morserunner; users add or delete them as they like.

// loads the language, "" = as in the system; English needs no file
void i18n_init(const char* code);
// the code of the language in use, "en" without a translation
const char* i18n_language(void);

// the text in the current language, the English one if there is no translation
// (or if the translation does not keep the % conversions of a format string)
const char* tr(const char* id, const char* english) G_GNUC_FORMAT(2);

typedef struct {
    gchar* code;
    gchar* name;
} Language;

// English and the translations found, sorted by name; g_ptr_array_unref() it
GPtrArray* i18n_languages(void);

#endif
