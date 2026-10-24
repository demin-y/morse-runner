// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "keys.h"

#include <string.h>

static const char* SEC_KEYS = "Keys";

typedef struct {
    const char* id;         // the key in the INI file
    const char* title;
    KeyGroup group;
    bool repeats;
    const char* defaults;   // GTK accelerators separated by ';'
} ActionInfo;

static const ActionInfo ACTIONS[KA_COUNT] = {
    [KA_ESM] = { "Enter", "Send what the QSO needs next, log it when complete (ESM)", KG_QSO, false, "Return" },
    [KA_LOG] = { "Log", "Log the QSO", KG_QSO, false, "<Control>Return;<Shift>Return;<Alt>Return" },
    [KA_CQ] = { "CQ", "CQ", KG_QSO, false, "F1;backslash" },
    [KA_NR] = { "Exchange", "Exchange", KG_QSO, false, "F2" },
    [KA_TU] = { "TU", "TU", KG_QSO, false, "F3" },
    [KA_MY] = { "MyCall", "My call", KG_QSO, false, "F4" },
    [KA_HIS] = { "HisCall", "His call", KG_QSO, false, "F5" },
    [KA_B4] = { "B4", "QSO B4", KG_QSO, false, "F6" },
    [KA_QM] = { "Repeat", "Ask to repeat (?)", KG_QSO, false, "F7" },
    [KA_NIL] = { "NIL", "Not in log (NIL)", KG_QSO, false, "F8" },
    [KA_CALL_NR] = { "CallAndExchange", "His call + exchange", KG_QSO, false, "semicolon;Insert" },
    [KA_TU_LOG] = { "TuAndLog", "TU and log the QSO", KG_QSO, false, "period;plus;bracketleft;comma;KP_Decimal" },
    [KA_STOP_SENDING] = { "StopSending", "Stop sending", KG_QSO, false, "Escape" },
    [KA_NEXT_FIELD] = { "NextField", "Next field, fills in 599", KG_FIELDS, false, "space" },
    [KA_WIPE] = { "Wipe", "Clear the fields", KG_FIELDS, false, "<Control>w;<Alt>w;F11" },
    [KA_RIT_UP] = { "RitUp", "RIT +50 Hz", KG_RADIO, true, "Up" },
    [KA_RIT_DOWN] = { "RitDown", "RIT −50 Hz", KG_RADIO, true, "Down" },
    [KA_BW_UP] = { "BandwidthUp", "Bandwidth +50 Hz", KG_RADIO, true, "<Control>Up;<Alt>Up" },
    [KA_BW_DOWN] = { "BandwidthDown", "Bandwidth −50 Hz", KG_RADIO, true, "<Control>Down;<Alt>Down" },
    [KA_SPEED_UP] = { "SpeedUp", "Speed +5 WPM", KG_RADIO, true, "Page_Up" },
    [KA_SPEED_DOWN] = { "SpeedDown", "Speed −5 WPM", KG_RADIO, true, "Page_Down" },
    [KA_START] = { "Start", "Start the chosen mode (when stopped)", KG_PROGRAM, false, "F9" },
    [KA_START_HST] = { "StartHst", "Start the HST competition (when stopped)", KG_PROGRAM, false, "<Control>F9" },
    [KA_RUN_STOP] = { "StartStop", "Start / stop", KG_PROGRAM, false, "<Primary>r" },
    [KA_MODE_PILEUP] = { "ModePileUp", "Mode: Pile-Up", KG_PROGRAM, false, "<Control>1" },
    [KA_MODE_SINGLE] = { "ModeSingleCalls", "Mode: Single Calls", KG_PROGRAM, false, "<Control>2" },
    [KA_MODE_WPX] = { "ModeWpx", "Mode: WPX Competition", KG_PROGRAM, false, "<Control>3" },
    [KA_MODE_HST] = { "ModeHst", "Mode: HST Competition", KG_PROGRAM, false, "<Control>4" },
    [KA_SETTINGS] = { "Settings", "Settings", KG_PROGRAM, false, "<Primary>comma" },
    [KA_SHORTCUTS] = { "Shortcuts", "Keyboard shortcuts", KG_PROGRAM, false, "<Primary>question" },
};

static const char* GROUP_TITLES[KG_COUNT] = { "QSO", "Fields", "Radio", "Program" };

static KeyCombo combos[KA_COUNT][MAX_COMBOS];
static int counts[KA_COUNT];
static bool ready;

//------------------------------------------------------------------------------
//                              key combinations
//------------------------------------------------------------------------------
// the keypad keys work as the main ones
static guint main_key(guint key)
{
    switch (key) {
    case GDK_KEY_KP_Enter: case GDK_KEY_ISO_Enter: return GDK_KEY_Return;
    case GDK_KEY_KP_Up: return GDK_KEY_Up;
    case GDK_KEY_KP_Down: return GDK_KEY_Down;
    case GDK_KEY_KP_Left: return GDK_KEY_Left;
    case GDK_KEY_KP_Right: return GDK_KEY_Right;
    case GDK_KEY_KP_Home: return GDK_KEY_Home;
    case GDK_KEY_KP_End: return GDK_KEY_End;
    case GDK_KEY_KP_Page_Up: return GDK_KEY_Page_Up;
    case GDK_KEY_KP_Page_Down: return GDK_KEY_Page_Down;
    case GDK_KEY_KP_Insert: return GDK_KEY_Insert;
    case GDK_KEY_KP_Delete: return GDK_KEY_Delete;
    case GDK_KEY_KP_Tab: case GDK_KEY_ISO_Left_Tab: return GDK_KEY_Tab;
    case GDK_KEY_KP_Space: return GDK_KEY_space;
    case GDK_KEY_KP_Add: return GDK_KEY_plus;
    case GDK_KEY_KP_Separator: return GDK_KEY_KP_Decimal;   // the keypad comma of some layouts
    default: return gdk_keyval_to_lower(key);
    }
}

// a symbol such as + or ? needs Shift on some layouts and not on others
static bool is_symbol(guint key)
{
    return key >= 0x21 && key <= 0x7E && !g_ascii_isalnum((char)key);
}

static KeyCombo normalize(KeyCombo c)
{
    c.key = main_key(c.key);
    c.mods &= GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK | GDK_META_MASK;
    if (is_symbol(c.key)) c.mods &= ~GDK_SHIFT_MASK;
    return c;
}

static bool same(KeyCombo a, KeyCombo b)
{
    return a.key == b.key && a.mods == b.mods;
}

static bool parse(const char* accel, KeyCombo* out)
{
    guint key = 0;
    GdkModifierType mods = 0;
    gtk_accelerator_parse(accel, &key, &mods);
    if (!key) return false;
    *out = normalize((KeyCombo){ key, mods });
    return true;
}

static int parse_list(const char* list, KeyCombo* out)
{
    gchar** accels = g_strsplit(list, ";", -1);
    int n = 0;
    for (gchar** p = accels; *p && n < MAX_COMBOS; ++p)
        if (parse(g_strstrip(*p), &out[n])) ++n;
    g_strfreev(accels);
    return n;
}

static void ensure_ready(void)
{
    if (ready) return;
    ready = true;
    keys_reset_all();
}

KeyCombo keys_from_event(const GdkEventKey* ev, GdkKeymap* keymap)
{
    guint key = ev->keyval;
    // keys work the same with any keyboard layout: use the key of the first (Latin) layout
    if (ev->group != 0) {
        guint latin = 0;
        if (gdk_keymap_translate_keyboard_state(keymap, ev->hardware_keycode, (GdkModifierType)ev->state, 0,
                                                &latin, NULL, NULL, NULL))
            key = latin;
    }
    GdkModifierType mods = (GdkModifierType)ev->state;
    gdk_keymap_add_virtual_modifiers(keymap, &mods);   // Cmd on macOS
    return normalize((KeyCombo){ key, mods });
}

bool keys_allowed(KeyCombo c)
{
    if (!c.key) return false;
    if (c.mods & ~GDK_SHIFT_MASK) return true;
    // these type a call or edit the fields
    if (c.key < 0x80 && (g_ascii_isalnum((char)c.key) || c.key == '/' || c.key == '?')) return false;
    switch (c.key) {
    case GDK_KEY_Tab: case GDK_KEY_BackSpace: case GDK_KEY_Delete:
    case GDK_KEY_Left: case GDK_KEY_Right: case GDK_KEY_Home: case GDK_KEY_End:
        return false;
    default:
        return true;
    }
}

//------------------------------------------------------------------------------
//                                 actions
//------------------------------------------------------------------------------
const char* keys_title(KeyAction a) { return ACTIONS[a].title; }
KeyGroup keys_group(KeyAction a) { return ACTIONS[a].group; }
const char* keys_group_title(KeyGroup g) { return GROUP_TITLES[g]; }
bool keys_repeats(KeyAction a) { return ACTIONS[a].repeats; }

int keys_count(KeyAction a)
{
    ensure_ready();
    return counts[a];
}

KeyCombo keys_get(KeyAction a, int i)
{
    ensure_ready();
    return combos[a][i];
}

gchar* keys_label(KeyCombo c)
{
    return gtk_accelerator_get_label(c.key, c.mods);
}

gchar* keys_labels(KeyAction a)
{
    ensure_ready();
    GString* s = g_string_new(NULL);
    for (int i = 0; i < counts[a]; ++i) {
        gchar* l = keys_label(combos[a][i]);
        g_string_append_printf(s, "%s%s", i ? ", " : "", l);
        g_free(l);
    }
    return g_string_free(s, FALSE);
}

gchar** keys_accels(KeyAction a)
{
    ensure_ready();
    gchar** list = g_new0(gchar*, counts[a] + 1);
    for (int i = 0; i < counts[a]; ++i) list[i] = gtk_accelerator_name(combos[a][i].key, combos[a][i].mods);
    return list;
}

KeyAction keys_lookup(KeyCombo c)
{
    ensure_ready();
    c = normalize(c);
    for (int a = 0; a < KA_COUNT; ++a)
        for (int i = 0; i < counts[a]; ++i)
            if (same(combos[a][i], c)) return (KeyAction)a;
    return KA_NONE;
}

void keys_remove(KeyAction a, int i)
{
    ensure_ready();
    if (i < 0 || i >= counts[a]) return;
    memmove(&combos[a][i], &combos[a][i + 1], (size_t)(counts[a] - i - 1) * sizeof(KeyCombo));
    --counts[a];
}

KeyAction keys_set(KeyAction a, int i, KeyCombo c)
{
    ensure_ready();
    c = normalize(c);
    KeyAction owner = KA_NONE;
    for (int b = 0; b < KA_COUNT; ++b) {
        for (int j = 0; j < counts[b]; ++j) {
            if (!same(combos[b][j], c) || (b == (int)a && j == i)) continue;
            keys_remove((KeyAction)b, j);
            if (b == (int)a) {
                if (j < i) --i;
            } else {
                owner = (KeyAction)b;
            }
            break;
        }
    }
    if (i >= counts[a]) {
        i = counts[a] < MAX_COMBOS ? counts[a]++ : MAX_COMBOS - 1;
    }
    combos[a][i] = c;
    return owner;
}

void keys_reset(KeyAction a)
{
    ensure_ready();
    KeyCombo defaults[MAX_COMBOS];
    int n = parse_list(ACTIONS[a].defaults, defaults);
    counts[a] = 0;
    for (int i = 0; i < n; ++i) keys_set(a, i, defaults[i]);
}

void keys_reset_all(void)
{
    ready = true;
    for (int a = 0; a < KA_COUNT; ++a) counts[a] = parse_list(ACTIONS[a].defaults, combos[a]);
}

bool keys_is_default(KeyAction a)
{
    ensure_ready();
    KeyCombo defaults[MAX_COMBOS];
    int n = parse_list(ACTIONS[a].defaults, defaults);
    if (n != counts[a]) return false;
    for (int i = 0; i < n; ++i)
        if (!same(defaults[i], combos[a][i])) return false;
    return true;
}

//------------------------------------------------------------------------------
//                                 INI file
//------------------------------------------------------------------------------
void keys_load(GKeyFile* kf)
{
    keys_reset_all();
    for (int a = 0; a < KA_COUNT; ++a) {
        gchar* value = g_key_file_get_value(kf, SEC_KEYS, ACTIONS[a].id, NULL);
        if (!value) continue;
        KeyCombo list[MAX_COMBOS];
        int n = parse_list(value, list);
        g_free(value);
        counts[a] = 0;
        for (int i = 0; i < n; ++i)
            if (keys_allowed(list[i])) keys_set((KeyAction)a, counts[a], list[i]);
    }
}

void keys_save(GKeyFile* kf)
{
    for (int a = 0; a < KA_COUNT; ++a) {
        if (keys_is_default((KeyAction)a)) {
            g_key_file_remove_key(kf, SEC_KEYS, ACTIONS[a].id, NULL);
            continue;
        }
        gchar** accels = keys_accels((KeyAction)a);
        gchar* value = g_strjoinv(";", accels);
        g_key_file_set_value(kf, SEC_KEYS, ACTIONS[a].id, value);   // "" = no keys
        g_free(value);
        g_strfreev(accels);
    }
    gsize n = 0;
    gchar** left = g_key_file_get_keys(kf, SEC_KEYS, &n, NULL);
    if (left && n == 0) g_key_file_remove_group(kf, SEC_KEYS, NULL);
    g_strfreev(left);
}
