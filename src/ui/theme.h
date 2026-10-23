// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_THEME_H
#define MR_THEME_H

typedef enum { THEME_SYSTEM, THEME_LIGHT, THEME_DARK } ThemeMode;

// Call before gtk_init: takes over the GTK_THEME environment variable,
// which would otherwise lock the theme and make the light/dark switch impossible.
void theme_preinit(void);

// Call once after gtk_init, before theme_apply: remembers the system theme.
void theme_init(void);

// THEME_SYSTEM follows the desktop (GTK_THEME, GTK settings, the GNOME
// color scheme, the Windows and macOS dark mode) and its changes.
void theme_apply(ThemeMode mode);

const char* theme_mode_id(ThemeMode mode);       // "system", "light", "dark"
ThemeMode theme_mode_from_id(const char* id);

#endif
