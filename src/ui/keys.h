// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_KEYS_H
#define MR_KEYS_H

#include <gtk/gtk.h>

#include <stdbool.h>

// The keyboard shortcuts: every action has a few key combinations,
// the defaults or the user's own, saved in the [Keys] section of the INI file.
typedef enum {
    KA_NONE = -1,
    // QSO
    KA_ESM, KA_LOG, KA_CQ, KA_NR, KA_TU, KA_MY, KA_HIS, KA_B4, KA_QM, KA_NIL,
    KA_CALL_NR, KA_TU_LOG, KA_STOP_SENDING,
    // fields
    KA_NEXT_FIELD, KA_WIPE,
    // radio
    KA_RIT_UP, KA_RIT_DOWN, KA_BW_UP, KA_BW_DOWN, KA_SPEED_UP, KA_SPEED_DOWN,
    // program
    KA_START, KA_START_HST, KA_RUN_STOP, KA_MODE_PILEUP, KA_MODE_SINGLE, KA_MODE_WPX, KA_MODE_HST,
    KA_SETTINGS, KA_SHORTCUTS,
    KA_COUNT
} KeyAction;

typedef enum { KG_QSO, KG_FIELDS, KG_RADIO, KG_PROGRAM, KG_COUNT } KeyGroup;

enum { MAX_COMBOS = 6 };

typedef struct {
    guint key;              // lower case, keypad keys as the main ones
    GdkModifierType mods;   // Shift only where it matters
} KeyCombo;

const char* keys_title(KeyAction a);
KeyGroup keys_group(KeyAction a);
const char* keys_group_title(KeyGroup g);
// holding the key repeats the action (tuning), otherwise one press = one action
bool keys_repeats(KeyAction a);

int keys_count(KeyAction a);
KeyCombo keys_get(KeyAction a, int i);
// a GTK accelerator label, e.g. "Ctrl+F9"; g_free() it
gchar* keys_label(KeyCombo c);
// the labels of all the combinations, "F1, \\"; g_free() it
gchar* keys_labels(KeyAction a);
// GTK accelerator names for gtk_application_set_accels_for_action; g_strfreev() it
gchar** keys_accels(KeyAction a);

// the key combination of an event, with the Latin layout of the key
KeyCombo keys_from_event(const GdkEventKey* ev, GdkKeymap* keymap);
KeyAction keys_lookup(KeyCombo c);
// keys that type a call or edit a field cannot be shortcuts
bool keys_allowed(KeyCombo c);

// replaces combination i, or adds one with i = keys_count(); the combination is
// taken away from any other action, which is returned (KA_NONE if none)
KeyAction keys_set(KeyAction a, int i, KeyCombo c);
void keys_remove(KeyAction a, int i);
void keys_reset(KeyAction a);
void keys_reset_all(void);
bool keys_is_default(KeyAction a);

// the [Keys] section: only the changed actions are written
void keys_load(GKeyFile* kf);
void keys_save(GKeyFile* kf);

#endif
