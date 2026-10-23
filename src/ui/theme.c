// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "theme.h"

#include <gtk/gtk.h>

#include <stdbool.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

static gchar* envTheme;      // GTK_THEME as it was at startup, "Name[:variant]"
static gchar* systemName;    // the theme the desktop asks for
static bool systemDark;      // the desktop asks for the dark variant
static ThemeMode current = THEME_SYSTEM;
static GSettings* gnome;     // org.gnome.desktop.interface, when available

void theme_preinit(void)
{
    const char* env = g_getenv("GTK_THEME");
    if (env && *env) {
        envTheme = g_strdup(env);
        g_unsetenv("GTK_THEME");
    }
}

// the theme is installed with this file in its gtk-3.0 folder; the built-in
// themes have everything (their dark variant works through prefer-dark)
static bool theme_has_file(const char* name, const char* file)
{
    if (!name || !*name) return false;
    if (!strcmp(name, "Adwaita") || !strcmp(name, "HighContrast") || !strcmp(name, "HighContrastInverse"))
        return true;

    gchar* dirs[64];
    int n = 0;
    dirs[n++] = g_build_filename(g_get_user_data_dir(), "themes", NULL);
    dirs[n++] = g_build_filename(g_get_home_dir(), ".themes", NULL);
    for (const gchar* const* d = g_get_system_data_dirs(); *d && n < 64; ++d)
        dirs[n++] = g_build_filename(*d, "themes", NULL);

    bool found = false;
    for (int i = 0; i < n; ++i) {
        gchar* css = g_build_filename(dirs[i], name, "gtk-3.0", file, NULL);
        found = found || g_file_test(css, G_FILE_TEST_EXISTS);
        g_free(css);
        g_free(dirs[i]);
    }
    return found;
}

// "Adwaita-dark" -> "Adwaita", "Yaru-dark" -> "Yaru"; NULL if not a dark theme name
static gchar* strip_dark_suffix(const char* name)
{
    size_t len = strlen(name);
    if (len > 5 && g_ascii_strcasecmp(name + len - 5, "-dark") == 0) return g_strndup(name, len - 5);
    return NULL;
}

// the dark mode of the OS or the desktop, outside of the GTK theme
static bool desktop_prefers_dark(void)
{
#ifdef _WIN32
    DWORD value = 1, size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &value, &size) == ERROR_SUCCESS)
        return value == 0;
#endif
#ifdef __APPLE__
    bool dark = false;
    CFStringRef style = (CFStringRef)CFPreferencesCopyAppValue(CFSTR("AppleInterfaceStyle"),
                                                               kCFPreferencesAnyApplication);
    if (style) {
        dark = CFGetTypeID(style) == CFStringGetTypeID() &&
               CFStringCompare(style, CFSTR("Dark"), kCFCompareCaseInsensitive) == kCFCompareEqualTo;
        CFRelease(style);
    }
    if (dark) return true;
#endif
    if (gnome) {
        gchar* scheme = g_settings_get_string(gnome, "color-scheme");
        bool dark = scheme && !strcmp(scheme, "prefer-dark");
        g_free(scheme);
        return dark;
    }
    return false;
}

// reads what the desktop asks for: the GTK settings without our overrides
static void read_system(void)
{
    GtkSettings* s = gtk_settings_get_default();
    g_free(systemName);
    systemName = NULL;
    gboolean prefer = FALSE;

    if (envTheme) {
        // GTK_THEME=Name:variant takes precedence, as in GTK itself
        const char* colon = strchr(envTheme, ':');
        systemName = colon ? g_strndup(envTheme, (gsize)(colon - envTheme)) : g_strdup(envTheme);
        prefer = colon && !g_ascii_strcasecmp(colon + 1, "dark");
    } else {
        g_object_get(s, "gtk-theme-name", &systemName, "gtk-application-prefer-dark-theme", &prefer, NULL);
    }
    if (!systemName) systemName = g_strdup("Adwaita");

    gchar* base = strip_dark_suffix(systemName);
    systemDark = prefer || base != NULL || desktop_prefers_dark();
    g_free(base);
}

// the light (dark = false) or dark variant of the system theme, or of Adwaita
// when the system theme has no such variant or is not installed
static void set_variant(bool dark)
{
    gchar* base = strip_dark_suffix(systemName);
    const char* light = base ? base : systemName;
    if (!theme_has_file(light, "gtk.css")) light = "Adwaita";

    // themes ship the dark variant either as gtk-dark.css or as a separate "-dark" theme
    const char* name = light;
    gchar* darkName = g_strconcat(light, "-dark", NULL);
    if (dark && !theme_has_file(light, "gtk-dark.css")) {
        if (base && theme_has_file(systemName, "gtk.css")) name = systemName;
        else if (theme_has_file(darkName, "gtk.css")) name = darkName;
        else name = "Adwaita";
    }
    g_object_set(gtk_settings_get_default(), "gtk-theme-name", name,
                 "gtk-application-prefer-dark-theme", (gboolean)dark, NULL);
    g_free(darkName);
    g_free(base);
}

static void on_system_changed(void)
{
    if (current != THEME_SYSTEM) return;
    GtkSettings* s = gtk_settings_get_default();
    // let the desktop values come through again, then follow them
    gtk_settings_reset_property(s, "gtk-theme-name");
    gtk_settings_reset_property(s, "gtk-application-prefer-dark-theme");
    read_system();
    // a light desktop theme is used as is, so that its later changes come through;
    // GTK_THEME and a dark mode set outside of GTK need our help
    if (envTheme || systemDark) set_variant(systemDark);
}

static void on_gnome_changed(GSettings* settings, gchar* key, gpointer data)
{
    (void)settings; (void)data;
    if (!strcmp(key, "color-scheme") || !strcmp(key, "gtk-theme")) on_system_changed();
}

void theme_init(void)
{
    GSettingsSchemaSource* src = g_settings_schema_source_get_default();
    GSettingsSchema* schema = src ? g_settings_schema_source_lookup(src, "org.gnome.desktop.interface", TRUE) : NULL;
    if (schema) {
        if (g_settings_schema_has_key(schema, "color-scheme")) {
            gnome = g_settings_new("org.gnome.desktop.interface");
            g_signal_connect(gnome, "changed", G_CALLBACK(on_gnome_changed), NULL);
        }
        g_settings_schema_unref(schema);
    }
    read_system();
}

void theme_apply(ThemeMode mode)
{
    current = mode;
    switch (mode) {
    case THEME_LIGHT: set_variant(false); break;
    case THEME_DARK: set_variant(true); break;
    default: on_system_changed(); break;
    }
}

const char* theme_mode_id(ThemeMode mode)
{
    switch (mode) {
    case THEME_LIGHT: return "light";
    case THEME_DARK: return "dark";
    default: return "system";
    }
}

ThemeMode theme_mode_from_id(const char* id)
{
    if (id && !strcmp(id, "light")) return THEME_LIGHT;
    if (id && !strcmp(id, "dark")) return THEME_DARK;
    return THEME_SYSTEM;
}
