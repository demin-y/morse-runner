// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "main_window.h"
#include "audio/audio_output.h"
#include "config.h"
#include "contest.h"
#include "theme.h"
#include "widgets.h"

#include <glib/gstdio.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MR_VERSION
#define MR_VERSION "2.0"
#endif

#define MAX_BINS 48

enum { COL_NR, COL_TIME, COL_CALL, COL_RECV, COL_SENT, COL_PFX, COL_CHK, COL_CHK_COLOR, COL_COUNT };

// F1..F8 send these messages
static const Msg FKEY_MSG[8] = { MSG_CQ, MSG_NR, MSG_TU, MSG_MY_CALL, MSG_HIS_CALL, MSG_B4, MSG_QM, MSG_NIL };
static const char* FKEY_LABEL[8] = { "CQ", "NR", "TU", "My", "His", "B4", "?", "NIL" };
static const char* FKEY_TIP[8] = {
    "Call CQ", "Send the exchange", "Thank you (TU)", "Send my callsign",
    "Send his callsign", "Worked before (QSO B4)", "Ask to repeat (?)", "Not in log (NIL)"
};

// the names of RM_PILEUP..RM_HST, in the order of the mode list
static const char* MODE_NAMES[4] = { "Pile-Up", "Single Calls", "WPX Competition", "HST Competition" };

struct App {
    Contest tst;
    GMutex lock;          // guards tst: the audio thread renders the blocks
    AudioOutput* audio;
    FloatBuf block;       // audio thread only
    int blockPos;
    char audioError[256];

    // QSO state of the operator
    bool callSent, nrSent, mustAdvance;
    bool updating;        // widgets are being updated programmatically
    bool inserting;       // the filtered text is being inserted
    bool keyDown[256];    // hardware keys being held, to ignore their auto-repeat
    guint timer;

    GtkWidget* window;
    GtkWidget* header;
    GtkWidget* modeCombo;
    GtkWidget* runButton;
    GtkWidget* settingsPopover;

    GtkWidget* clockLabel;
    GtkWidget* clockSub;
    GtkWidget* progress;
    GtkWidget* scoreLabel;
    GtkWidget* scoreSub;
    GtkWidget* rateLabel;
    GtkWidget* histo;
    int histoBins[MAX_BINS];
    int histoCount;
    Stats stats;          // cached: recomputed when the log changes
    int statsVersion;
    RunMode statsMode;

    GtkWidget* logStack;
    GtkWidget* logView;
    GtkListStore* logStore;
    GtkWidget* placeholderTitle;
    GtkWidget* placeholderText;

    GtkWidget* callEntry;
    GtkWidget* rstEntry;
    GtkWidget* nrEntry;
    GtkWidget* fkeys[8];

    GtkWidget* ritView;
    GtkWidget* rxLabel;
    GtkWidget* monitorScale;

    // settings
    GtkWidget* sCall;
    GtkWidget* sName;
    GtkWidget* sWpm;
    GtkWidget* sPitch;
    GtkWidget* sBw;
    GtkWidget* sQsk;
    GtkWidget* sActivity;
    GtkWidget* sQrn;
    GtkWidget* sQrm;
    GtkWidget* sQsb;
    GtkWidget* sFlutter;
    GtkWidget* sLids;
    GtkWidget* sDuration;
    GtkWidget* sSaveWav;
    GSimpleAction* playAction;
};

#define LOCK(a) g_mutex_lock(&(a)->lock)
#define UNLOCK(a) g_mutex_unlock(&(a)->lock)

static void run(App* a, RunMode mode);
static void request_stop(App* a);
static void pump_events(App* a);
static void update_dashboard(App* a);
static void update_controls(App* a);
static void sync_settings_widgets(App* a);
static void update_title(App* a);
static void update_receiver(App* a);
static void show_placeholder(App* a);
static void focus_call(App* a);

//------------------------------------------------------------------------------
//                                  helpers
//------------------------------------------------------------------------------
static const char* mode_name(RunMode m)
{
    return m == RM_STOP ? "Ready" : MODE_NAMES[m - RM_PILEUP];
}

static void format_time(double seconds, char* out, size_t size)
{
    int s = (int)seconds;
    snprintf(out, size, "%02d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
}

// returns true if the text has changed
static bool set_label(GtkWidget* label, const char* text)
{
    if (strcmp(text, gtk_label_get_text(GTK_LABEL(label))) == 0) return false;
    gtk_label_set_text(GTK_LABEL(label), text);
    return true;
}

static void add_class(GtkWidget* w, const char* cls)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(w), cls);
}

static const char* text_of(GtkWidget* entry)
{
    return gtk_entry_get_text(GTK_ENTRY(entry));
}

static GtkWidget* card(GtkWidget* child)
{
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_class(box, "mr-card");
    gtk_box_pack_start(GTK_BOX(box), child, TRUE, TRUE, 0);
    return box;
}

static GtkWidget* setting_row(GtkWidget* grid, int row, const char* text, GtkWidget* w)
{
    GtkWidget* l = gtk_label_new(text);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_widget_set_hexpand(l, TRUE);
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_widget_set_halign(w, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1);
    return w;
}

static void setting_header(GtkWidget* grid, int row, const char* text)
{
    GtkWidget* l = make_caption(text);
    add_class(l, "mr-settings-header");
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 2, 1);
}

static GtkWidget* hz_combo(int from, int to)
{
    GtkWidget* c = gtk_combo_box_text_new();
    for (int f = from; f <= to; f += 50) {
        char id[16], text[32];
        snprintf(id, sizeof(id), "%d", f);
        snprintf(text, sizeof(text), "%d Hz", f);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(c), id, text);
    }
    return c;
}

static void set_combo_hz(GtkWidget* c, int hz)
{
    char id[16];
    snprintf(id, sizeof(id), "%d", hz);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(c), id);
}

static int combo_hz(GtkWidget* c)
{
    const char* id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(c));
    return id ? atoi(id) : 0;
}

static void show_uri(App* a, const char* path)
{
    gchar* uri = g_filename_to_uri(path, NULL, NULL);
    if (uri) gtk_show_uri_on_window(GTK_WINDOW(a->window), uri, GDK_CURRENT_TIME, NULL);
    g_free(uri);
}

static void show_error(App* a, const char* msg)
{
    GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(a->window), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
                                            GTK_BUTTONS_OK, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}


static RunMode current_mode(App* a)
{
    LOCK(a);
    RunMode m = a->tst.cfg.runMode;
    UNLOCK(a);
    return m;
}

static bool is_running(App* a)
{
    return current_mode(a) != RM_STOP;
}

static RunMode selected_mode(App* a)
{
    int i = gtk_combo_box_get_active(GTK_COMBO_BOX(a->modeCombo));
    return i >= 1 && i <= 3 ? (RunMode)(RM_PILEUP + i) : RM_PILEUP;
}

// the receiver passband and RIT, for drawing
static void rit_snapshot(App* a, int* bw, int* rit)
{
    LOCK(a);
    *bw = contest_bandwidth(&a->tst);
    *rit = a->tst.cfg.rit;
    UNLOCK(a);
}

//------------------------------------------------------------------------------
//                                   audio
//------------------------------------------------------------------------------
static void fill_audio(float* out, int frames, void* user)
{
    App* a = user;
    LOCK(a);
    int i = 0;
    while (i < frames) {
        if (a->blockPos >= a->block.len) {
            contest_render_block(&a->tst, &a->block);
            a->blockPos = 0;
        }
        int n = a->block.len - a->blockPos;
        if (n > frames - i) n = frames - i;
        memcpy(out + i, a->block.data + a->blockPos, (size_t)n * sizeof(float));
        a->blockPos += n;
        i += n;
    }
    UNLOCK(a);
}

//------------------------------------------------------------------------------
//                              operator actions
//------------------------------------------------------------------------------
static void send_msg(App* a, Msg m)
{
    LOCK(a);
    if (contest_running(&a->tst)) {
        if (m == MSG_HIS_CALL) {
            if (text_of(a->callEntry)[0]) contest_set_his_call(&a->tst, text_of(a->callEntry));
            a->callSent = true;
        }
        if (m == MSG_NR) a->nrSent = true;
        contest_send_msg(&a->tst, m);
    }
    UNLOCK(a);
}

static void focus_call(App* a) { gtk_widget_grab_focus(a->callEntry); }
static void focus_nr(App* a) { gtk_widget_grab_focus(a->nrEntry); }

static void wipe_boxes(App* a)
{
    gtk_entry_set_text(GTK_ENTRY(a->callEntry), "");
    gtk_entry_set_text(GTK_ENTRY(a->rstEntry), "");
    gtk_entry_set_text(GTK_ENTRY(a->nrEntry), "");
    focus_call(a);
    a->callSent = false;
    a->nrSent = false;
}

static void save_qso(App* a)
{
    LOCK(a);
    bool ok = contest_save_qso(&a->tst, text_of(a->callEntry), text_of(a->rstEntry), text_of(a->nrEntry));
    UNLOCK(a);
    if (!ok) {
        gtk_widget_error_bell(a->window);
        return;
    }
    wipe_boxes(a);
    pump_events(a);
}

// Enter sends the message that the state of the QSO calls for (ESM)
static void process_enter(App* a, bool save)
{
    a->mustAdvance = false;
    if (save) { save_qso(a); return; }

    // no QSO in progress, send CQ
    if (!text_of(a->callEntry)[0]) { send_msg(a, MSG_CQ); return; }

    const bool c = a->callSent, n = a->nrSent, r = text_of(a->nrEntry)[0] != '\0';

    // send his call if it was not sent before, or if the call has changed
    if (!c || (!n && !r)) send_msg(a, MSG_HIS_CALL);
    if (!n) send_msg(a, MSG_NR);
    if (n && !r) send_msg(a, MSG_QM);

    if (r && (c || n)) {
        send_msg(a, MSG_TU);
        save_qso(a);
    } else {
        a->mustAdvance = true;
    }
}

static void process_space(App* a)
{
    a->mustAdvance = false;
    GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(a->window));
    if (focus == a->callEntry || focus == a->rstEntry) {
        if (!text_of(a->rstEntry)[0]) gtk_entry_set_text(GTK_ENTRY(a->rstEntry), "599");
        focus_nr(a);
    } else {
        focus_call(a);
    }
}

static void abort_send(App* a)
{
    LOCK(a);
    MsgSet m = a->tst.me->msg;
    if (has_msg(m, MSG_HIS_CALL)) a->callSent = false;
    if (has_msg(m, MSG_NR)) a->nrSent = false;
    contest_abort_send(&a->tst);
    UNLOCK(a);
}

static void tu_and_save(App* a)
{
    if (!a->callSent) send_msg(a, MSG_HIS_CALL);
    send_msg(a, MSG_TU);
    save_qso(a);
}

static void call_and_nr(App* a)
{
    send_msg(a, MSG_HIS_CALL);
    send_msg(a, MSG_NR);
}

// a part of my message has been sent: move the cursor to the next field
static void advance(App* a)
{
    if (!a->mustAdvance) return;
    if (!text_of(a->rstEntry)[0]) gtk_entry_set_text(GTK_ENTRY(a->rstEntry), "599");
    if (!strchr(text_of(a->callEntry), '?')) focus_nr(a);
    else focus_call(a);
    a->mustAdvance = false;
}

static void inc_rit(App* a, int step)
{
    LOCK(a);
    contest_set_rit(&a->tst, step == 0 ? 0 : a->tst.cfg.rit + step);
    UNLOCK(a);
    update_receiver(a);
}

static void change_bandwidth(App* a, int step)
{
    LOCK(a);
    bool changed = contest_set_bandwidth(&a->tst, a->tst.cfg.bandwidth + step);
    UNLOCK(a);
    if (!changed) return;
    sync_settings_widgets(a);
    update_receiver(a);
}

// PgUp/PgDn: speed in 5 WPM steps
static void change_speed(App* a, int dir)
{
    LOCK(a);
    int wpm = a->tst.cfg.wpm;
    if (dir > 0) wpm = wpm / 5 * 5 + 5;
    else wpm = (wpm + 4) / 5 * 5 - 5;
    contest_set_wpm(&a->tst, wpm);
    UNLOCK(a);
    sync_settings_widgets(a);
    update_receiver(a);
}

// types a character into an entry at the cursor, replacing the selection
static void type_char(GtkWidget* entry, char c)
{
    GtkEditable* e = GTK_EDITABLE(entry);
    gint start, end;
    if (gtk_editable_get_selection_bounds(e, &start, &end)) gtk_editable_delete_text(e, start, end);
    gint pos = gtk_editable_get_position(e);
    gtk_editable_insert_text(e, &c, 1, &pos);
    gtk_editable_set_position(e, pos);
}

// keys that send a message or log a QSO: holding them must not repeat the action
static bool is_action_key(guint keyval)
{
    switch (keyval) {
    case GDK_KEY_F1: case GDK_KEY_F2: case GDK_KEY_F3: case GDK_KEY_F4:
    case GDK_KEY_F5: case GDK_KEY_F6: case GDK_KEY_F7: case GDK_KEY_F8: case GDK_KEY_F9:
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_ISO_Enter:
    case GDK_KEY_Insert: case GDK_KEY_KP_Insert: case GDK_KEY_semicolon:
    case GDK_KEY_period: case GDK_KEY_plus: case GDK_KEY_KP_Add: case GDK_KEY_bracketleft: case GDK_KEY_comma:
    case GDK_KEY_KP_Decimal: case GDK_KEY_KP_Separator: case GDK_KEY_backslash:
        return true;
    default:
        return false;
    }
}

static void select_mode(App* a, int index)
{
    if (is_running(a)) return;
    gtk_combo_box_set_active(GTK_COMBO_BOX(a->modeCombo), index);
}

static bool on_key_press(App* a, GdkEventKey* ev)
{
    // the settings popover gets its keys as usual
    if (gtk_widget_get_visible(a->settingsPopover)) return false;
    GtkWidget* focus = gtk_window_get_focus(GTK_WINDOW(a->window));
    const bool qsoField = focus == a->callEntry || focus == a->rstEntry || focus == a->nrEntry;
    if (focus && GTK_IS_ENTRY(focus) && !qsoField) return false;

    // keys work the same with any keyboard layout: use the key of the first (Latin) layout
    guint keyval = ev->keyval;
    if (ev->group != 0) {
        guint latin = 0;
        GdkKeymap* keymap = gdk_keymap_get_for_display(gtk_widget_get_display(a->window));
        if (gdk_keymap_translate_keyboard_state(keymap, ev->hardware_keycode, (GdkModifierType)ev->state, 0,
                                                &latin, NULL, NULL, NULL))
            keyval = latin;
    }

    const guint mods = ev->state & gtk_accelerator_get_default_mod_mask();
    const bool ctrl = (mods & GDK_CONTROL_MASK) != 0;
    const bool alt = (mods & GDK_MOD1_MASK) != 0;
    const bool shift = (mods & GDK_SHIFT_MASK) != 0;

    // auto-repeat of a held key: one press, one message
    const guint code = ev->hardware_keycode & 0xFF;
    if (is_action_key(keyval)) {
        if (a->keyDown[code]) return true;
        a->keyDown[code] = true;
    }

    // Ctrl+1..4 choose the mode
    if (ctrl && !alt && keyval >= GDK_KEY_1 && keyval <= GDK_KEY_4) {
        select_mode(a, (int)(keyval - GDK_KEY_1));
        return true;
    }

    switch (keyval) {
    case GDK_KEY_F1: case GDK_KEY_F2: case GDK_KEY_F3: case GDK_KEY_F4:
    case GDK_KEY_F5: case GDK_KEY_F6: case GDK_KEY_F7: case GDK_KEY_F8:
        if (ctrl || alt) return false;
        send_msg(a, FKEY_MSG[keyval - GDK_KEY_F1]);
        return true;
    case GDK_KEY_F9:
        if (ctrl || alt) change_speed(a, -1);
        else if (!is_running(a)) run(a, selected_mode(a));
        return true;
    case GDK_KEY_F10:
        if (ctrl || alt) change_speed(a, +1);
        return true;   // no menu bar
    case GDK_KEY_F11:
        wipe_boxes(a);
        return true;
    case GDK_KEY_Escape:
        abort_send(a);
        return true;
    case GDK_KEY_Insert: case GDK_KEY_KP_Insert: case GDK_KEY_semicolon:
        call_and_nr(a);
        return true;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_ISO_Enter:
        if (!is_running(a)) run(a, selected_mode(a));
        else process_enter(a, ctrl || shift || alt);
        return true;
    case GDK_KEY_Up: case GDK_KEY_KP_Up:
        if (ctrl) change_bandwidth(a, +50); else inc_rit(a, +50);
        return true;
    case GDK_KEY_Down: case GDK_KEY_KP_Down:
        if (ctrl) change_bandwidth(a, -50); else inc_rit(a, -50);
        return true;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up:
        change_speed(a, +1);
        return true;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down:
        change_speed(a, -1);
        return true;
    case GDK_KEY_w: case GDK_KEY_W:
        if (ctrl || alt) { wipe_boxes(a); return true; }
        break;
    case GDK_KEY_space:
        process_space(a);
        return true;
    case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab: case GDK_KEY_KP_Tab: {
        // Tab cycles through the QSO fields only
        GtkWidget* order[3] = { a->callEntry, a->rstEntry, a->nrEntry };
        int idx = focus == a->rstEntry ? 1 : focus == a->nrEntry ? 2 : 0;
        bool back = shift || keyval == GDK_KEY_ISO_Left_Tab;
        gtk_widget_grab_focus(order[(idx + (back ? 2 : 1)) % 3]);
        return true;
    }
    case GDK_KEY_period: case GDK_KEY_plus: case GDK_KEY_KP_Add: case GDK_KEY_bracketleft: case GDK_KEY_comma:
    case GDK_KEY_KP_Decimal: case GDK_KEY_KP_Separator:
        if (ctrl) return false;
        tu_and_save(a);
        return true;
    case GDK_KEY_backslash:
        send_msg(a, MSG_CQ);
        return true;
    default:
        break;
    }

    if (ctrl || alt || keyval < 0x21 || keyval >= 0x7F) return false;
    // the focus has left the QSO fields (a click elsewhere): typing goes to the call field
    if (!qsoField) {
        focus_call(a);
        type_char(a->callEntry, (char)keyval);
        return true;
    }
    // a non-Latin layout is active: type the Latin character of the key
    if (ev->group != 0) {
        type_char(focus, (char)keyval);
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
//                                  dialogs
//------------------------------------------------------------------------------
static void copy_icon_pressed(GtkEntry* e, GtkEntryIconPosition pos, GdkEvent* ev, gpointer data)
{
    (void)pos; (void)ev; (void)data;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), gtk_entry_get_text(e), -1);
}

static void popup_score_wpx(App* a)
{
    LOCK(a);
    Stats st = contest_stats(&a->tst);
    char call[CALL_LEN];
    g_strlcpy(call, a->tst.cfg.call, sizeof(call));
    bool record = st.verScore > a->tst.cfg.hiScore;
    if (record) a->tst.cfg.hiScore = st.verScore;
    UNLOCK(a);

    GDateTime* now = g_date_time_new_now_local();
    gchar* date = g_date_time_format(now, "%Y-%m-%d");
    char s[128];
    wpx_score_string(date, call, st.verPts, st.verMul, s, sizeof(s));
    g_free(date);
    g_date_time_unref(now);
    append_line(path_score_file(), s);

    GtkWidget* dlg = gtk_dialog_new_with_buttons("The contest is over", GTK_WINDOW(a->window),
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR,
                                                 "Close", GTK_RESPONSE_CLOSE, NULL);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_box_set_spacing(GTK_BOX(area), 8);
    gtk_container_set_border_width(GTK_CONTAINER(area), 18);
    if (record) {
        GtkWidget* l = gtk_label_new("Congratulations on a new record!");
        add_class(l, "mr-record");
        gtk_box_pack_start(GTK_BOX(area), l, FALSE, FALSE, 0);
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%d QSO  ×  %d prefixes  =  %d points", st.verPts, st.verMul, st.verScore);
    gtk_box_pack_start(GTK_BOX(area), gtk_label_new(buf), FALSE, FALSE, 0);
    GtkWidget* cap = make_caption("YOUR SCORE STRING");
    gtk_widget_set_margin_top(cap, 8);
    gtk_box_pack_start(GTK_BOX(area), cap, FALSE, FALSE, 0);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), s);
    gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
    gtk_entry_set_width_chars(GTK_ENTRY(entry), (int)strlen(s) + 2);
    gtk_entry_set_icon_from_icon_name(GTK_ENTRY(entry), GTK_ENTRY_ICON_SECONDARY, "edit-copy-symbolic");
    gtk_entry_set_icon_tooltip_text(GTK_ENTRY(entry), GTK_ENTRY_ICON_SECONDARY, "Copy");
    g_signal_connect(entry, "icon-press", G_CALLBACK(copy_icon_pressed), NULL);
    gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 0);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void popup_score_hst(App* a)
{
    LOCK(a);
    Stats st = contest_stats(&a->tst);
    char call[CALL_LEN], name[NAME_LEN];
    g_strlcpy(call, a->tst.cfg.call, sizeof(call));
    g_strlcpy(name, a->tst.cfg.operatorName, sizeof(name));
    UNLOCK(a);

    GDateTime* now = g_date_time_new_now_local();
    gchar* date = g_date_time_format(now, "%Y-%m-%d %H:%M");
    char buf[256];
    snprintf(buf, sizeof(buf), "%s\t%s\t%s\t%d", date, call, name, st.verScore);
    g_free(date);
    g_date_time_unref(now);
    append_line(path_hst_file(), buf);

    GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(a->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO,
                                            GTK_BUTTONS_CLOSE, "HST score: %d", st.verScore);
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%d QSOs logged, raw score %d.",
                                             st.rawPts, st.rawScore);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void show_history(App* a)
{
    GtkWidget* dlg = gtk_dialog_new_with_buttons("Score History", GTK_WINDOW(a->window),
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_USE_HEADER_BAR, NULL, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 560, 380);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));

    GtkWidget* stack = gtk_stack_new();
    const char* titles[2] = { "WPX", "HST" };
    const char* files[2] = { path_score_file(), path_hst_file() };
    for (int i = 0; i < 2; ++i) {
        gchar* content = NULL;
        g_file_get_contents(files[i], &content, NULL, NULL);
        GtkWidget* tv = gtk_text_view_new();
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(tv), TRUE);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(tv), FALSE);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 12);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(tv), 8);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv)),
                                 content && content[0] ? content : "No results yet.\n", -1);
        g_free(content);
        GtkWidget* sw = gtk_scrolled_window_new(NULL, NULL);
        gtk_container_add(GTK_CONTAINER(sw), tv);
        gtk_stack_add_titled(GTK_STACK(stack), sw, titles[i], titles[i]);
    }
    GtkWidget* switcher = gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(stack));
    GtkWidget* hb = gtk_dialog_get_header_bar(GTK_DIALOG(dlg));
    if (hb) gtk_header_bar_set_custom_title(GTK_HEADER_BAR(hb), switcher);
    gtk_box_pack_start(GTK_BOX(area), stack, TRUE, TRUE, 0);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

typedef struct { const char* accel; const char* title; } ShortcutItem;

static void add_shortcut_group(GtkWidget* section, const char* title, const ShortcutItem* items, int count)
{
    GtkWidget* group = GTK_WIDGET(g_object_new(GTK_TYPE_SHORTCUTS_GROUP, "title", title, "visible", TRUE, NULL));
    for (int i = 0; i < count; ++i) {
        GtkWidget* sc = GTK_WIDGET(g_object_new(GTK_TYPE_SHORTCUTS_SHORTCUT, "accelerator", items[i].accel,
                                                "title", items[i].title, "visible", TRUE, NULL));
        gtk_container_add(GTK_CONTAINER(group), sc);
    }
    gtk_container_add(GTK_CONTAINER(section), group);
}

static void show_shortcuts(App* a)
{
    static const ShortcutItem qso[] = {
        { "Return", "Send what the QSO needs next and log it (ESM)" },
        { "<Ctrl>Return <Shift>Return", "Log the QSO" },
        { "F1 backslash", "CQ" },
        { "F2", "Exchange" },
        { "F3", "TU" },
        { "F4", "My call" },
        { "F5", "His call" },
        { "F6", "QSO B4" },
        { "F7", "?" },
        { "F8", "NIL" },
        { "semicolon Insert", "His call + exchange" },
        { "period plus bracketleft comma KP_Decimal", "TU and log the QSO" },
        { "Escape", "Stop sending" },
    };
    static const ShortcutItem fields[] = {
        { "space", "Next field, fills in 599" },
        { "Tab <Shift>Tab", "Next / previous field" },
        { "<Ctrl>w <Alt>w F11", "Wipe the fields" },
    };
    static const ShortcutItem radio[] = {
        { "Up Down", "RIT ±50 Hz" },
        { "<Ctrl>Up <Ctrl>Down", "Bandwidth ±50 Hz" },
        { "Page_Up Page_Down", "Speed ±5 WPM" },
        { "F9 Return", "Start (when stopped)" },
        { "<Ctrl>r", "Start / stop" },
        { "<Ctrl>1 <Ctrl>2 <Ctrl>3 <Ctrl>4", "Mode: Pile-Up, Single Calls, WPX, HST" },
        { "<Ctrl>comma", "Settings" },
    };

    GtkWidget* win = GTK_WIDGET(g_object_new(GTK_TYPE_SHORTCUTS_WINDOW, "modal", TRUE,
                                             "transient-for", a->window, NULL));
    GtkWidget* section = GTK_WIDGET(g_object_new(GTK_TYPE_SHORTCUTS_SECTION, "section-name", "main",
                                                 "max-height", 14, "visible", TRUE, NULL));
    add_shortcut_group(section, "QSO", qso, (int)(sizeof(qso) / sizeof(qso[0])));
    add_shortcut_group(section, "Fields", fields, (int)(sizeof(fields) / sizeof(fields[0])));
    add_shortcut_group(section, "Radio", radio, (int)(sizeof(radio) / sizeof(radio[0])));
    gtk_container_add(GTK_CONTAINER(win), section);
    gtk_widget_show_all(win);
}

static void show_about(App* a)
{
    GdkPixbuf* logo = create_app_icon(96);
    gtk_show_about_dialog(GTK_WINDOW(a->window),
                          "program-name", "Morse Runner",
                          "version", MR_VERSION,
                          "comments", "CW contest simulator.\n"
                                      "Practice pile-ups, copying calls under QRM, QRN and QSB,\n"
                                      "and contest operating at your own speed.",
#if GTK_CHECK_VERSION(3, 24, 20)
                          "license-type", GTK_LICENSE_MPL_2_0,
#else
                          "license", "Mozilla Public License, version 2.0",
#endif
                          "website", "https://github.com/demin-y/morse-runner",
                          "logo", logo,
                          NULL);
    g_object_unref(logo);
}

//------------------------------------------------------------------------------
//                                run control
//------------------------------------------------------------------------------
static void run(App* a, RunMode mode)
{
    if (mode == RM_STOP) { request_stop(a); return; }
    // the previous run may have just ended on the audio thread: handle its events first
    pump_events(a);
    LOCK(a);
    bool already = contest_running(&a->tst);
    FILE* wav = !already && a->tst.cfg.saveWav ? g_fopen(path_wav_file(), "wb") : NULL;
    bool started = !already && contest_start(&a->tst, mode, wav);
    if (started) a->blockPos = a->block.len;
    UNLOCK(a);
    if (already) return;
    if (!started) {
        show_error(a, "Please enter your callsign in the settings first.");
        gtk_popover_popup(GTK_POPOVER(a->settingsPopover));
        return;
    }

    gtk_list_store_clear(a->logStore);
    wipe_boxes(a);
    update_controls(a);
    sync_settings_widgets(a);
    update_title(a);
    update_dashboard(a);
    show_placeholder(a);

    if (!audio_start(a->audio)) {
        LOCK(a);
        contest_stop(&a->tst, true);
        UNLOCK(a);
        if (!a->audioError[0]) g_strlcpy(a->audioError, "Failed to start the audio output.", sizeof(a->audioError));
    }
    if (a->audioError[0]) {
        show_error(a, a->audioError);
        a->audioError[0] = '\0';   // tell once
    }
    focus_call(a);
}

static void request_stop(App* a)
{
    LOCK(a);
    contest_stop(&a->tst, true);
    UNLOCK(a);
    pump_events(a);
}

static void on_contest_over(App* a, const EngineEvent* e)
{
    // never hold the engine lock here: stopping waits for the audio callback
    audio_stop(a->audio);
    update_controls(a);
    sync_settings_widgets(a);
    update_title(a);
    update_dashboard(a);
    show_placeholder(a);

    if (e->mode == RM_WPX && e->completed) popup_score_wpx(a);
    else if (e->mode == RM_HST && e->completed) popup_score_hst(a);
}

//------------------------------------------------------------------------------
//                                  display
//------------------------------------------------------------------------------
static void set_qso_row(App* a, int index, const Qso* q)
{
    GtkTreeModel* model = GTK_TREE_MODEL(a->logStore);
    GtkTreeIter it;
    int rows = gtk_tree_model_iter_n_children(model, NULL);
    while (rows <= index) {
        gtk_list_store_append(a->logStore, &it);
        ++rows;
    }
    gtk_tree_model_iter_nth_child(model, &it, NULL, index);

    char nr[16], time[32], recv[32], sent[32];
    snprintf(nr, sizeof(nr), "%d", index + 1);
    format_time(q->t, time, sizeof(time));
    snprintf(recv, sizeof(recv), "%03d %04d", q->rst, q->nr);
    snprintf(sent, sizeof(sent), "%03d %04d", q->sentRst, q->sentNr);
    bool ok = qso_ok(q);
    gtk_list_store_set(a->logStore, &it, COL_NR, nr, COL_TIME, time, COL_CALL, q->call, COL_RECV, recv,
                       COL_SENT, sent, COL_PFX, q->pfx, COL_CHK, ok ? "✓" : q->err,
                       COL_CHK_COLOR, ok ? MR_COLOR_OK : MR_COLOR_ERROR, -1);

    GtkTreePath* path = gtk_tree_model_get_path(model, &it);
    gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(a->logView), path, NULL, FALSE, 0, 0);
    gtk_tree_path_free(path);
    show_placeholder(a);
}

static void pump_events(App* a)
{
    LOCK(a);
    EngineEvent* events;
    int n = contest_take_events(&a->tst, &events);
    Qso* rows = g_new0(Qso, n);
    for (int i = 0; i < n; ++i) {
        bool row = events[i].type == EE_QSO_ADDED || events[i].type == EE_QSO_UPDATED;
        const Qso* q = row ? contest_qso(&a->tst, events[i].index) : NULL;
        if (q) rows[i] = *q;
    }
    UNLOCK(a);

    for (int i = 0; i < n; ++i) {
        switch (events[i].type) {
        case EE_QSO_ADDED:
        case EE_QSO_UPDATED:
            set_qso_row(a, events[i].index, &rows[i]);
            break;
        case EE_PIECE_SENT:
            advance(a);
            break;
        case EE_CONTEST_OVER:
            on_contest_over(a, &events[i]);
            break;
        }
    }
    g_free(rows);
    g_free(events);
}

static void show_placeholder(App* a)
{
    if (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(a->logStore), NULL) > 0) {
        gtk_stack_set_visible_child_name(GTK_STACK(a->logStack), "log");
        return;
    }
    RunMode mode = current_mode(a);
    if (mode == RM_STOP) {
        set_label(a->placeholderTitle, "Ready");
        set_label(a->placeholderText, "Choose a mode and press Enter or Start.\n"
                                      "Press Ctrl+? to see the keyboard shortcuts.");
    } else if (mode == RM_SINGLE || mode == RM_HST) {
        set_label(a->placeholderTitle, "Stations are calling");
        set_label(a->placeholderText, "Type the call you hear and press Enter.\n"
                                      "Enter again logs the QSO once you have his number.");
    } else {
        set_label(a->placeholderTitle, "On the air");
        set_label(a->placeholderText, "Press Enter or F1 to call CQ, then type a call you hear.\n"
                                      "Enter sends the call and the exchange, the next Enter logs the QSO.");
    }
    gtk_stack_set_visible_child_name(GTK_STACK(a->logStack), "placeholder");
}

static void update_dashboard(App* a)
{
    RunMode selected = selected_mode(a);
    LOCK(a);
    const Contest* t = &a->tst;
    RunMode mode = t->cfg.runMode, last = t->lastMode;
    double elapsed = contest_elapsed(t);
    int duration = settings_duration(&t->cfg, mode != RM_STOP ? mode : selected);
    int dx = contest_dx_count(t);
    int rate = mode == RM_STOP ? 0 : contest_rate(t);
    int binCount = CLAMP((duration + 4) / 5, 6, MAX_BINS);
    // the score and the histogram change only with the log
    bool logChanged = t->logVersion != a->statsVersion || last != a->statsMode || binCount != a->histoCount;
    if (logChanged) {
        a->stats = contest_stats(t);
        rate_histogram((const Qso*)t->qsos->data, contest_qso_count(t), a->histoBins, binCount);
        a->histoCount = binCount;
        a->statsVersion = t->logVersion;
        a->statsMode = last;
    }
    Stats st = a->stats;
    UNLOCK(a);

    char buf[160];
    format_time(elapsed, buf, sizeof(buf));
    set_label(a->clockLabel, buf);
    double total = duration * 60.0 < 60 ? 60 : duration * 60.0;
    double fraction = fmin(1.0, elapsed / total);
    if (fabs(fraction - gtk_progress_bar_get_fraction(GTK_PROGRESS_BAR(a->progress))) > 1e-4)
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(a->progress), fraction);

    if (mode == RM_STOP) snprintf(buf, sizeof(buf), "Stopped  ·  %d min session", duration);
    else if (mode == RM_PILEUP) snprintf(buf, sizeof(buf), "%s  ·  %d calling", mode_name(mode), dx);
    else snprintf(buf, sizeof(buf), "%s  ·  %d min", mode_name(mode), duration);
    set_label(a->clockSub, buf);
    GtkStyleContext* ctx = gtk_widget_get_style_context(a->clockSub);
    if (run_mode_is_competition(mode)) gtk_style_context_add_class(ctx, "mr-compet");
    else gtk_style_context_remove_class(ctx, "mr-compet");

    snprintf(buf, sizeof(buf), "%d", st.verScore);
    set_label(a->scoreLabel, buf);
    if (last == RM_HST) snprintf(buf, sizeof(buf), "%d QSO  ·  raw score %d", st.rawPts, st.rawScore);
    else snprintf(buf, sizeof(buf), "%d QSO × %d mult  ·  raw %d", st.verPts, st.verMul, st.rawScore);
    set_label(a->scoreSub, buf);

    snprintf(buf, sizeof(buf), "%d Q/hr", rate);
    set_label(a->rateLabel, buf);

    if (logChanged) gtk_widget_queue_draw(a->histo);
    update_receiver(a);
}

static void update_receiver(App* a)
{
    LOCK(a);
    int wpm = a->tst.cfg.wpm, pitch = a->tst.cfg.pitch, bw = contest_bandwidth(&a->tst), rit = a->tst.cfg.rit;
    bool qsk = a->tst.cfg.qsk;
    UNLOCK(a);
    char buf[160];
    snprintf(buf, sizeof(buf), "%d WPM  ·  %d Hz  ·  BW %d  ·  RIT %+d%s", wpm, pitch, bw, rit,
             qsk ? "  ·  QSK" : "");
    if (set_label(a->rxLabel, buf)) gtk_widget_queue_draw(a->ritView);
}

static void update_title(App* a)
{
    LOCK(a);
    const char* name = a->tst.cfg.operatorName;
    gchar* sub = g_strdup_printf("%s%s%s", a->tst.cfg.call[0] ? a->tst.cfg.call : "No callsign: Ctrl+, to set it",
                                 name[0] ? "  ·  " : "", name);
    UNLOCK(a);
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(a->header), sub);
    g_free(sub);
}

// enables the controls that may be changed in the current mode
static void update_controls(App* a)
{
    RunMode mode = current_mode(a);
    const bool stop = mode == RM_STOP;
    const bool compet = run_mode_is_competition(mode);

    gtk_widget_set_sensitive(a->modeCombo, stop);
    gtk_widget_set_sensitive(a->sCall, stop);
    gtk_widget_set_sensitive(a->sName, stop);
    gtk_widget_set_sensitive(a->sDuration, stop);
    gtk_widget_set_sensitive(a->sSaveWav, stop);
    GtkWidget* conditions[] = { a->sQrn, a->sQrm, a->sQsb, a->sFlutter, a->sLids };
    for (int i = 0; i < 5; ++i) gtk_widget_set_sensitive(conditions[i], !compet);
    gtk_widget_set_sensitive(a->sActivity, mode != RM_HST);
    gtk_widget_set_sensitive(a->sBw, mode != RM_HST);
    for (int i = 0; i < 8; ++i) gtk_widget_set_sensitive(a->fkeys[i], !stop);

    GtkWidget* label = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(label),
                       gtk_image_new_from_icon_name(stop ? "media-playback-start-symbolic"
                                                         : "media-playback-stop-symbolic", GTK_ICON_SIZE_BUTTON),
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(label), gtk_label_new(stop ? "Start" : "Stop"), FALSE, FALSE, 0);
    GtkWidget* old = gtk_bin_get_child(GTK_BIN(a->runButton));
    if (old) gtk_widget_destroy(old);
    gtk_container_add(GTK_CONTAINER(a->runButton), label);
    gtk_widget_show_all(a->runButton);
    gtk_widget_set_tooltip_text(a->runButton, stop ? "Start the contest  (Enter, F9, Ctrl+R)" : "Stop  (Ctrl+R)");
    GtkStyleContext* ctx = gtk_widget_get_style_context(a->runButton);
    gtk_style_context_remove_class(ctx, stop ? "destructive-action" : "suggested-action");
    gtk_style_context_add_class(ctx, stop ? "suggested-action" : "destructive-action");

    bool canPlay = stop && g_file_test(path_wav_file(), G_FILE_TEST_EXISTS);
    g_simple_action_set_enabled(a->playAction, canPlay);
}

// shows the effective settings: the competition modes override some of them
static void sync_settings_widgets(App* a)
{
    a->updating = true;
    LOCK(a);
    const Contest* t = &a->tst;
    Settings c = t->cfg;
    bool qrn = contest_qrn(t), qrm = contest_qrm(t), qsb = contest_qsb(t);
    bool flutter = contest_flutter(t), lids = contest_lids(t);
    int activity = contest_activity(t), bw = contest_bandwidth(t);
    int duration = c.runMode == RM_STOP ? c.duration : contest_duration(t);
    UNLOCK(a);

    if (strcmp(text_of(a->sCall), c.call) != 0) gtk_entry_set_text(GTK_ENTRY(a->sCall), c.call);
    if (strcmp(text_of(a->sName), c.operatorName) != 0) gtk_entry_set_text(GTK_ENTRY(a->sName), c.operatorName);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->sWpm), c.wpm);
    set_combo_hz(a->sPitch, c.pitch);
    set_combo_hz(a->sBw, bw);
    gtk_switch_set_active(GTK_SWITCH(a->sQsk), c.qsk);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->sActivity), activity);
    gtk_switch_set_active(GTK_SWITCH(a->sQrn), qrn);
    gtk_switch_set_active(GTK_SWITCH(a->sQrm), qrm);
    gtk_switch_set_active(GTK_SWITCH(a->sQsb), qsb);
    gtk_switch_set_active(GTK_SWITCH(a->sFlutter), flutter);
    gtk_switch_set_active(GTK_SWITCH(a->sLids), lids);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(a->sDuration), duration);
    gtk_switch_set_active(GTK_SWITCH(a->sSaveWav), c.saveWav);
    gtk_range_set_value(GTK_RANGE(a->monitorScale), c.monitorDb);
    a->updating = false;
}

//------------------------------------------------------------------------------
//                                 callbacks
//------------------------------------------------------------------------------
static gboolean cb_key_press(GtkWidget* w, GdkEventKey* ev, gpointer data)
{
    (void)w;
    return on_key_press(data, ev) ? TRUE : FALSE;
}

static gboolean cb_key_release(GtkWidget* w, GdkEventKey* ev, gpointer data)
{
    (void)w;
    App* a = data;
    a->keyDown[ev->hardware_keycode & 0xFF] = false;
    return FALSE;
}

// a key released in another window would stay "held"
static gboolean cb_focus_out(GtkWidget* w, GdkEvent* ev, gpointer data)
{
    (void)w; (void)ev;
    App* a = data;
    memset(a->keyDown, 0, sizeof(a->keyDown));
    return FALSE;
}

static gboolean cb_timer(gpointer data)
{
    pump_events(data);
    update_dashboard(data);
    return G_SOURCE_CONTINUE;
}

static gboolean cb_delete(GtkWidget* w, GdkEvent* ev, gpointer data)
{
    (void)w; (void)ev;
    App* a = data;
    audio_stop(a->audio);
    LOCK(a);
    contest_stop(&a->tst, true);
    Settings cfg = a->tst.cfg;
    UNLOCK(a);
    save_settings(&cfg);
    return FALSE;
}

static void cb_destroy(GtkWidget* w, gpointer data)
{
    (void)w;
    App* a = data;
    if (a->timer) g_source_remove(a->timer);
    audio_close(a->audio);
    contest_free(&a->tst);
    fb_free(&a->block);
    g_mutex_clear(&a->lock);
    g_free(a);
}

static void cb_run_clicked(GtkButton* b, gpointer data)
{
    (void)b;
    App* a = data;
    if (is_running(a)) request_stop(a);
    else run(a, selected_mode(a));
}

static void cb_fkey(GtkButton* b, gpointer data)
{
    App* a = data;
    send_msg(a, FKEY_MSG[GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "fkey"))]);
    focus_call(a);
}

static void cb_call_changed(GtkEditable* e, gpointer data)
{
    (void)e;
    App* a = data;
    const char* call = text_of(a->callEntry);
    if (!call[0]) a->nrSent = false;
    LOCK(a);
    if (!contest_update_call_in_message(&a->tst, call)) a->callSent = false;
    UNLOCK(a);
}

// filters the characters typed into the fields and converts them to upper case
static void cb_insert_text(GtkEditable* e, gchar* text, gint len, gint* pos, gpointer data)
{
    App* a = data;
    if (a->inserting) return;
    GtkWidget* w = GTK_WIDGET(e);
    const bool digits = w == a->rstEntry || w == a->nrEntry;
    const bool call = w == a->callEntry;
    if (len < 0) len = (gint)strlen(text);

    char* out = g_malloc((size_t)len + 1);
    int n = 0;
    for (int i = 0; i < len; ++i) {
        char c = text[i];
        c = g_ascii_toupper(c);
        bool ok;
        if (digits) ok = c >= '0' && c <= '9';
        else if (call) ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/' || c == '?';
        else ok = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/';
        if (ok) out[n++] = c;
    }
    out[n] = '\0';
    if (n != len || memcmp(out, text, (size_t)len) != 0) {
        a->inserting = true;
        if (n) gtk_editable_insert_text(e, out, n, pos);
        a->inserting = false;
        g_signal_stop_emission_by_name(e, "insert-text");
    }
    g_free(out);
}

// the cursor enters a field: select the first '?' of the call, or the middle digit of the RST
static void cb_grab_focus(GtkWidget* w, gpointer data)
{
    App* a = data;
    const char* t = text_of(w);
    if (w == a->callEntry) {
        const char* q = strchr(t, '?');
        if (q && q > t) gtk_editable_select_region(GTK_EDITABLE(w), (gint)(q - t), (gint)(q - t) + 1);
        else gtk_editable_set_position(GTK_EDITABLE(w), -1);
    } else if (w == a->rstEntry && strlen(t) == 3) {
        gtk_editable_select_region(GTK_EDITABLE(w), 1, 2);
    } else {
        // no select-all: a digit typed after returning to the field
        // must not wipe the number copied so far
        gtk_editable_set_position(GTK_EDITABLE(w), -1);
    }
}

static gboolean cb_histo_draw(GtkWidget* w, cairo_t* cr, gpointer data)
{
    App* a = data;
    draw_histogram(w, cr, a->histoBins, a->histoCount);
    return FALSE;
}

static gboolean cb_rit_draw(GtkWidget* w, cairo_t* cr, gpointer data)
{
    int bw, rit;
    rit_snapshot(data, &bw, &rit);
    draw_rit(w, cr, bw, rit);
    return FALSE;
}

static gboolean cb_rit_press(GtkWidget* w, GdkEventButton* ev, gpointer data)
{
    App* a = data;
    int bw, rit;
    rit_snapshot(a, &bw, &rit);
    RitGeometry g = rit_geometry(w, bw, rit);
    if (ev->x < g.left) inc_rit(a, -50);
    else if (ev->x > g.right) inc_rit(a, +50);
    else inc_rit(a, 0);
    return TRUE;
}

static gboolean cb_rit_scroll(GtkWidget* w, GdkEventScroll* ev, gpointer data)
{
    (void)w;
    App* a = data;
    double dx = 0, dy = 0;
    if (ev->direction == GDK_SCROLL_UP || ev->direction == GDK_SCROLL_RIGHT) inc_rit(a, +50);
    else if (ev->direction == GDK_SCROLL_DOWN || ev->direction == GDK_SCROLL_LEFT) inc_rit(a, -50);
    else if (gdk_event_get_scroll_deltas((GdkEvent*)ev, &dx, &dy) && dy != 0) inc_rit(a, dy < 0 ? 50 : -50);
    return TRUE;
}

static void cb_monitor_changed(GtkRange* r, gpointer data)
{
    App* a = data;
    if (a->updating) return;
    LOCK(a);
    a->tst.cfg.monitorDb = (int)lround(gtk_range_get_value(r));
    UNLOCK(a);
}

static gchar* cb_monitor_format(GtkScale* s, gdouble value, gpointer data)
{
    (void)s; (void)data;
    return g_strdup_printf("%+d dB", (int)lround(value));
}

static void cb_setting_changed(GtkWidget* w, gpointer data)
{
    App* a = data;
    if (a->updating) return;
    bool title = false;
    LOCK(a);
    Contest* t = &a->tst;
    if (w == a->sCall) {
        contest_set_my_call(t, text_of(w));
        title = true;
    } else if (w == a->sName) {
        g_strlcpy(t->cfg.operatorName, text_of(w), sizeof(t->cfg.operatorName));
        title = true;
    } else if (w == a->sWpm) {
        contest_set_wpm(t, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w)));
    } else if (w == a->sPitch) {
        if (combo_hz(w)) contest_set_pitch(t, combo_hz(w));
    } else if (w == a->sBw) {
        if (combo_hz(w)) contest_set_bandwidth(t, combo_hz(w));
    } else if (w == a->sActivity) {
        t->cfg.activity = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));
    } else if (w == a->sDuration) {
        t->cfg.duration = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));
    }
    UNLOCK(a);
    if (title) update_title(a);
    update_receiver(a);
}

static void cb_switch_changed(GObject* obj, GParamSpec* spec, gpointer data)
{
    (void)spec;
    App* a = data;
    if (a->updating) return;
    GtkWidget* w = GTK_WIDGET(obj);
    bool on = gtk_switch_get_active(GTK_SWITCH(w)) != FALSE;
    LOCK(a);
    Settings* c = &a->tst.cfg;
    if (w == a->sQsk) c->qsk = on;
    else if (w == a->sSaveWav) c->saveWav = on;
    else if (w == a->sQrn) c->qrn = on;
    else if (w == a->sQrm) c->qrm = on;
    else if (w == a->sQsb) c->qsb = on;
    else if (w == a->sFlutter) c->flutter = on;
    else if (w == a->sLids) c->lids = on;
    UNLOCK(a);
    update_receiver(a);
}

// the settings open with the cursor in the first field that can be changed now
static gboolean focus_settings(gpointer data)
{
    App* a = data;
    GtkWidget* first = gtk_widget_get_sensitive(a->sCall) ? a->sCall : a->sWpm;
    gtk_widget_grab_focus(first);
    return G_SOURCE_REMOVE;
}

static void cb_popover_shown(GtkWidget* p, gpointer data)
{
    (void)p;
    g_idle_add(focus_settings, data);
}

static void cb_popover_closed(GtkPopover* p, gpointer data)
{
    (void)p;
    App* a = data;
    // the disk write happens outside the lock the audio thread needs
    LOCK(a);
    Settings cfg = a->tst.cfg;
    UNLOCK(a);
    save_settings(&cfg);
    focus_call(a);
}

static void cb_mode_changed(GtkComboBox* c, gpointer data)
{
    App* a = data;
    int idx = gtk_combo_box_get_active(c);
    LOCK(a);
    a->tst.cfg.lastMode = idx < 0 ? 0 : idx;
    UNLOCK(a);
    focus_call(a);
}

static void cb_action(GSimpleAction* action, GVariant* param, gpointer data)
{
    (void)param;
    App* a = data;
    const char* name = g_action_get_name(G_ACTION(action));
    if (!strcmp(name, "run")) cb_run_clicked(NULL, a);
    else if (!strcmp(name, "history")) show_history(a);
    else if (!strcmp(name, "play")) {
        if (g_file_test(path_wav_file(), G_FILE_TEST_EXISTS)) show_uri(a, path_wav_file());
    } else if (!strcmp(name, "folder")) show_uri(a, path_data_dir());
    else if (!strcmp(name, "shortcuts")) show_shortcuts(a);
    else if (!strcmp(name, "about")) show_about(a);
    else if (!strcmp(name, "settings")) gtk_popover_popup(GTK_POPOVER(a->settingsPopover));
}

static void cb_theme_changed(GSimpleAction* action, GVariant* value, gpointer data)
{
    App* a = data;
    ThemeMode mode = theme_mode_from_id(g_variant_get_string(value, NULL));
    g_simple_action_set_state(action, value);
    theme_apply(mode);
    LOCK(a);
    a->tst.cfg.theme = (int)mode;
    UNLOCK(a);
}

//------------------------------------------------------------------------------
//                                construction
//------------------------------------------------------------------------------
static GtkWidget* build_settings(App* a)
{
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);

    GtkWidget* left = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(left), 8);
    gtk_grid_set_column_spacing(GTK_GRID(left), 16);
    int r = 0;
    setting_header(left, r++, "STATION");
    a->sCall = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(a->sCall), 10);
    gtk_entry_set_max_length(GTK_ENTRY(a->sCall), MAX_CALL_CHARS);
    g_signal_connect(a->sCall, "insert-text", G_CALLBACK(cb_insert_text), a);
    setting_row(left, r++, "Callsign", a->sCall);
    a->sName = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(a->sName), 10);
    gtk_entry_set_max_length(GTK_ENTRY(a->sName), NAME_LEN - 1);
    gtk_entry_set_placeholder_text(GTK_ENTRY(a->sName), "optional");
    gtk_widget_set_tooltip_text(a->sName, "Written to the HST results");
    setting_row(left, r++, "Operator", a->sName);
    a->sWpm = gtk_spin_button_new_with_range(MIN_WPM, MAX_WPM, 1);
    setting_row(left, r++, "Speed, WPM", a->sWpm);
    a->sPitch = hz_combo(MIN_PITCH, MAX_PITCH);
    setting_row(left, r++, "Pitch", a->sPitch);
    a->sBw = hz_combo(MIN_BANDWIDTH, MAX_BANDWIDTH);
    setting_row(left, r++, "RX bandwidth", a->sBw);
    a->sQsk = gtk_switch_new();
    gtk_widget_set_tooltip_text(a->sQsk, "Hear the band between your own dits and dahs (full break-in)");
    setting_row(left, r++, "QSK", a->sQsk);
    setting_header(left, r++, "SESSION");
    a->sDuration = gtk_spin_button_new_with_range(MIN_DURATION, MAX_DURATION, 5);
    gtk_widget_set_tooltip_text(a->sDuration, "Pile-Up and Single Calls modes. WPX and HST use the "
                                              "competition duration (60 min).");
    setting_row(left, r++, "Duration, min", a->sDuration);
    a->sSaveWav = gtk_switch_new();
    gtk_widget_set_tooltip_text(a->sSaveWav, "Save the audio of the next run to MorseRunner.wav");
    setting_row(left, r++, "Record audio", a->sSaveWav);

    GtkWidget* right = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(right), 8);
    gtk_grid_set_column_spacing(GTK_GRID(right), 16);
    r = 0;
    setting_header(right, r++, "BAND CONDITIONS");
    a->sActivity = gtk_spin_button_new_with_range(MIN_ACTIVITY, MAX_ACTIVITY, 1);
    gtk_widget_set_tooltip_text(a->sActivity, "How many stations answer a CQ on average");
    setting_row(right, r++, "Activity", a->sActivity);
    a->sQrn = setting_row(right, r++, "QRN — static", gtk_switch_new());
    a->sQrm = setting_row(right, r++, "QRM — interference", gtk_switch_new());
    a->sQsb = setting_row(right, r++, "QSB — fading", gtk_switch_new());
    a->sFlutter = setting_row(right, r++, "Flutter — auroral", gtk_switch_new());
    a->sLids = setting_row(right, r++, "LIDs — sloppy operators", gtk_switch_new());
    GtkWidget* note = gtk_label_new("Competition modes use fixed\nconditions while running.");
    add_class(note, "mr-placeholder");
    gtk_widget_set_halign(note, GTK_ALIGN_START);
    gtk_widget_set_margin_top(note, 8);
    gtk_grid_attach(GTK_GRID(right), note, 0, r++, 2, 1);

    GtkWidget* spins[] = { a->sWpm, a->sActivity, a->sDuration };
    for (int i = 0; i < 3; ++i) g_signal_connect(spins[i], "value-changed", G_CALLBACK(cb_setting_changed), a);
    GtkWidget* changed[] = { a->sCall, a->sName, a->sPitch, a->sBw };
    for (int i = 0; i < 4; ++i) g_signal_connect(changed[i], "changed", G_CALLBACK(cb_setting_changed), a);
    GtkWidget* switches[] = { a->sQsk, a->sSaveWav, a->sQrn, a->sQrm, a->sQsb, a->sFlutter, a->sLids };
    for (int i = 0; i < 7; ++i) g_signal_connect(switches[i], "notify::active", G_CALLBACK(cb_switch_changed), a);

    gtk_box_pack_start(GTK_BOX(box), left, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), right, FALSE, FALSE, 0);
    gtk_widget_show_all(box);
    return box;
}

static void build_header(App* a)
{
    a->header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(a->header), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(a->header), "Morse Runner");

    a->modeCombo = gtk_combo_box_text_new();
    for (int i = 0; i < 4; ++i)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a->modeCombo), MODE_NAMES[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(a->modeCombo), a->tst.cfg.lastMode);
    gtk_widget_set_tooltip_text(a->modeCombo, "Ctrl+1  Pile-Up: several stations answer each CQ\n"
                                              "Ctrl+2  Single Calls: one station at a time\n"
                                              "Ctrl+3  WPX: fixed band conditions, 60 minutes, scored\n"
                                              "Ctrl+4  HST: IARU High Speed Telegraphy rules");
    gtk_widget_set_can_focus(a->modeCombo, FALSE);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(a->header), a->modeCombo);

    a->runButton = gtk_button_new();
    gtk_widget_set_can_focus(a->runButton, FALSE);
    g_signal_connect(a->runButton, "clicked", G_CALLBACK(cb_run_clicked), a);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(a->header), a->runButton);

    // main menu
    GMenu* menu = g_menu_new();
    GMenu* s1 = g_menu_new();
    g_menu_append(s1, "Score History", "win.history");
    g_menu_append(s1, "Play Recording", "win.play");
    g_menu_append(s1, "Open Data Folder", "win.folder");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(s1));
    GMenu* s2 = g_menu_new();
    GMenu* themes = g_menu_new();
    g_menu_append(themes, "As in the System", "win.theme::system");
    g_menu_append(themes, "Light", "win.theme::light");
    g_menu_append(themes, "Dark", "win.theme::dark");
    g_menu_append_submenu(s2, "Theme", G_MENU_MODEL(themes));
    g_object_unref(themes);
    g_menu_append(s2, "Keyboard Shortcuts", "win.shortcuts");
    g_menu_append(s2, "About Morse Runner", "win.about");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(s2));

    GtkWidget* menuButton = gtk_menu_button_new();
    gtk_widget_set_can_focus(menuButton, FALSE);
    gtk_button_set_image(GTK_BUTTON(menuButton), gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(menuButton), G_MENU_MODEL(menu));
    gtk_menu_button_set_use_popover(GTK_MENU_BUTTON(menuButton), TRUE);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(a->header), menuButton);
    g_object_unref(s1);
    g_object_unref(s2);
    g_object_unref(menu);

    // settings
    GtkWidget* settingsButton = gtk_menu_button_new();
    gtk_widget_set_can_focus(settingsButton, FALSE);
    gtk_widget_set_tooltip_text(settingsButton, "Settings  (Ctrl+,)");
    gtk_button_set_image(GTK_BUTTON(settingsButton),
                         gtk_image_new_from_icon_name("emblem-system-symbolic", GTK_ICON_SIZE_BUTTON));
    a->settingsPopover = gtk_popover_new(settingsButton);
    gtk_container_add(GTK_CONTAINER(a->settingsPopover), build_settings(a));
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(settingsButton), a->settingsPopover);
    g_signal_connect(a->settingsPopover, "closed", G_CALLBACK(cb_popover_closed), a);
    g_signal_connect(a->settingsPopover, "map", G_CALLBACK(cb_popover_shown), a);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(a->header), settingsButton);

    gtk_window_set_titlebar(GTK_WINDOW(a->window), a->header);
}

static void build_actions(App* a, GtkApplication* gtkApp)
{
    const char* names[] = { "run", "history", "play", "folder", "shortcuts", "about", "settings" };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        GSimpleAction* act = g_simple_action_new(names[i], NULL);
        g_signal_connect(act, "activate", G_CALLBACK(cb_action), a);
        g_action_map_add_action(G_ACTION_MAP(a->window), G_ACTION(act));
        if (!strcmp(names[i], "play")) a->playAction = act;
        g_object_unref(act);
    }
    GSimpleAction* theme = g_simple_action_new_stateful("theme", G_VARIANT_TYPE_STRING,
                                                        g_variant_new_string(theme_mode_id((ThemeMode)a->tst.cfg.theme)));
    g_signal_connect(theme, "change-state", G_CALLBACK(cb_theme_changed), a);
    g_action_map_add_action(G_ACTION_MAP(a->window), G_ACTION(theme));
    g_object_unref(theme);

    const char* runAccels[] = { "<Primary>r", NULL };
    const char* settingsAccels[] = { "<Primary>comma", NULL };
    const char* shortcutAccels[] = { "<Primary>question", "<Primary>F1", NULL };
    gtk_application_set_accels_for_action(gtkApp, "win.run", runAccels);
    gtk_application_set_accels_for_action(gtkApp, "win.settings", settingsAccels);
    gtk_application_set_accels_for_action(gtkApp, "win.shortcuts", shortcutAccels);
}

static GtkWidget* value_label(const char* text, const char* tip)
{
    GtkWidget* l = gtk_label_new(text);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    add_class(l, "mr-value");
    if (tip) gtk_widget_set_tooltip_text(l, tip);
    return l;
}

static GtkWidget* build_dashboard(App* a)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);

    // time
    GtkWidget* t = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(t), make_caption("TIME"), FALSE, FALSE, 0);
    a->clockLabel = value_label("00:00:00", NULL);
    gtk_box_pack_start(GTK_BOX(t), a->clockLabel, FALSE, FALSE, 0);
    a->progress = gtk_progress_bar_new();
    gtk_widget_set_margin_top(a->progress, 4);
    gtk_box_pack_start(GTK_BOX(t), a->progress, FALSE, FALSE, 0);
    a->clockSub = gtk_label_new("");
    gtk_widget_set_halign(a->clockSub, GTK_ALIGN_START);
    gtk_widget_set_margin_top(a->clockSub, 4);
    add_class(a->clockSub, "mr-mode");
    gtk_box_pack_start(GTK_BOX(t), a->clockSub, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), card(t), TRUE, TRUE, 0);

    // score
    GtkWidget* s = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(s), make_caption("SCORE"), FALSE, FALSE, 0);
    a->scoreLabel = value_label("0", "Verified score: only the QSOs confirmed by the other station");
    gtk_box_pack_start(GTK_BOX(s), a->scoreLabel, FALSE, FALSE, 0);
    a->scoreSub = gtk_label_new("");
    gtk_widget_set_halign(a->scoreSub, GTK_ALIGN_START);
    add_class(a->scoreSub, "mr-subvalue");
    gtk_box_pack_start(GTK_BOX(s), a->scoreSub, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), card(s), TRUE, TRUE, 0);

    // rate
    GtkWidget* r = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(r), make_caption("RATE"), FALSE, FALSE, 0);
    a->rateLabel = value_label("0 Q/hr", "QSO rate over the last 5 minutes");
    gtk_box_pack_start(GTK_BOX(r), a->rateLabel, FALSE, FALSE, 0);
    a->histo = gtk_drawing_area_new();
    gtk_widget_set_size_request(a->histo, -1, 30);
    gtk_widget_set_tooltip_text(a->histo, "QSOs in 5-minute intervals");
    g_signal_connect(a->histo, "draw", G_CALLBACK(cb_histo_draw), a);
    gtk_box_pack_start(GTK_BOX(r), a->histo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), card(r), TRUE, TRUE, 0);
    return row;
}

static GtkWidget* build_log(App* a)
{
    a->logStack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(a->logStack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    // placeholder
    GtkWidget* ph = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_valign(ph, GTK_ALIGN_CENTER);
    a->placeholderTitle = gtk_label_new("");
    add_class(a->placeholderTitle, "mr-placeholder-title");
    a->placeholderText = gtk_label_new("");
    gtk_label_set_justify(GTK_LABEL(a->placeholderText), GTK_JUSTIFY_CENTER);
    add_class(a->placeholderText, "mr-placeholder");
    gtk_box_pack_start(GTK_BOX(ph), a->placeholderTitle, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ph), a->placeholderText, FALSE, FALSE, 0);
    gtk_stack_add_named(GTK_STACK(a->logStack), ph, "placeholder");

    // log
    a->logStore = gtk_list_store_new(COL_COUNT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                     G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    a->logView = gtk_tree_view_new_with_model(GTK_TREE_MODEL(a->logStore));
    g_object_unref(a->logStore);
    add_class(a->logView, "mr-log");
    gtk_widget_set_can_focus(a->logView, FALSE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(a->logView), FALSE);

    static const struct { const char* title; int col; float xalign; bool expand; } cols[] = {
        { "#", COL_NR, 1.f, false }, { "Time", COL_TIME, 0.f, false }, { "Call", COL_CALL, 0.f, true },
        { "Recv", COL_RECV, 0.f, false }, { "Sent", COL_SENT, 0.f, false }, { "Pfx", COL_PFX, 0.f, false },
        { "Check", COL_CHK, 0.f, false },
    };
    for (size_t i = 0; i < sizeof(cols) / sizeof(cols[0]); ++i) {
        GtkCellRenderer* cell = gtk_cell_renderer_text_new();
        g_object_set(cell, "xalign", cols[i].xalign, "xpad", 8, "ypad", 2, NULL);
        GtkTreeViewColumn* c = gtk_tree_view_column_new_with_attributes(cols[i].title, cell, "text", cols[i].col, NULL);
        if (cols[i].col == COL_CHK) {
            gtk_tree_view_column_add_attribute(c, cell, "foreground", COL_CHK_COLOR);
            g_object_set(cell, "weight", PANGO_WEIGHT_BOLD, NULL);
        }
        if (cols[i].col == COL_CALL) g_object_set(cell, "weight", PANGO_WEIGHT_BOLD, NULL);
        gtk_tree_view_column_set_expand(c, cols[i].expand);
        gtk_tree_view_append_column(GTK_TREE_VIEW(a->logView), c);
    }

    GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), a->logView);
    gtk_stack_add_named(GTK_STACK(a->logStack), scroll, "log");

    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    add_class(frame, "mr-log-frame");
    gtk_widget_set_size_request(frame, -1, 160);
    gtk_box_pack_start(GTK_BOX(frame), a->logStack, TRUE, TRUE, 0);
    return frame;
}

static GtkWidget* build_entry(App* a)
{
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 3);

    a->callEntry = gtk_entry_new();
    a->rstEntry = gtk_entry_new();
    a->nrEntry = gtk_entry_new();
    GtkWidget* entries[3] = { a->callEntry, a->rstEntry, a->nrEntry };
    const char* labels[3] = { "CALL", "RST", "NR" };
    const int maxLen[3] = { MAX_CALL_CHARS, 3, 6 };
    const int width[3] = { 12, 3, 5 };
    for (int i = 0; i < 3; ++i) {
        GtkWidget* e = entries[i];
        add_class(e, "mr-qso");
        gtk_entry_set_max_length(GTK_ENTRY(e), maxLen[i]);
        gtk_entry_set_width_chars(GTK_ENTRY(e), width[i]);
        g_signal_connect(e, "insert-text", G_CALLBACK(cb_insert_text), a);
        g_signal_connect_after(e, "grab-focus", G_CALLBACK(cb_grab_focus), a);
        gtk_grid_attach(GTK_GRID(grid), make_caption(labels[i]), i, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), e, i, 1, 1, 1);
    }
    gtk_widget_set_hexpand(a->callEntry, TRUE);
    gtk_entry_set_input_purpose(GTK_ENTRY(a->rstEntry), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_input_purpose(GTK_ENTRY(a->nrEntry), GTK_INPUT_PURPOSE_DIGITS);
    gtk_widget_set_tooltip_text(a->callEntry, "The call you hear. '?' marks a character you did not copy.");
    g_signal_connect(a->callEntry, "changed", G_CALLBACK(cb_call_changed), a);
    return grid;
}

static GtkWidget* build_keys(App* a)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_set_homogeneous(GTK_BOX(row), TRUE);
    for (int i = 0; i < 8; ++i) {
        char markup[128];
        snprintf(markup, sizeof(markup), "<span alpha=\"55%%\" size=\"small\">F%d</span>  %s", i + 1, FKEY_LABEL[i]);
        GtkWidget* label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(label), markup);
        GtkWidget* b = gtk_button_new();
        gtk_container_add(GTK_CONTAINER(b), label);
        gtk_widget_set_can_focus(b, FALSE);
        gtk_widget_set_tooltip_text(b, FKEY_TIP[i]);
        add_class(b, "mr-fkey");
        g_object_set_data(G_OBJECT(b), "fkey", GINT_TO_POINTER(i));
        g_signal_connect(b, "clicked", G_CALLBACK(cb_fkey), a);
        gtk_box_pack_start(GTK_BOX(row), b, TRUE, TRUE, 0);
        a->fkeys[i] = b;
    }
    return row;
}

static GtkWidget* build_receiver(App* a)
{
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    a->rxLabel = gtk_label_new("");
    add_class(a->rxLabel, "mr-rx");
    gtk_widget_set_tooltip_text(a->rxLabel, "PgUp/PgDn: speed    Up/Down: RIT    Ctrl+Up/Down: bandwidth");
    gtk_box_pack_start(GTK_BOX(row), a->rxLabel, FALSE, FALSE, 0);

    a->ritView = gtk_drawing_area_new();
    gtk_widget_set_size_request(a->ritView, 120, 18);
    gtk_widget_set_valign(a->ritView, GTK_ALIGN_CENTER);
    gtk_widget_set_hexpand(a->ritView, TRUE);
    gtk_widget_add_events(a->ritView, GDK_BUTTON_PRESS_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    gtk_widget_set_tooltip_text(a->ritView, "Receiver passband. Click left/right to tune the RIT, click the band to reset.");
    g_signal_connect(a->ritView, "draw", G_CALLBACK(cb_rit_draw), a);
    g_signal_connect(a->ritView, "button-press-event", G_CALLBACK(cb_rit_press), a);
    g_signal_connect(a->ritView, "scroll-event", G_CALLBACK(cb_rit_scroll), a);
    gtk_box_pack_start(GTK_BOX(row), a->ritView, TRUE, TRUE, 0);

    GtkWidget* icon = gtk_image_new_from_icon_name("audio-volume-medium-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_tooltip_text(icon, "Self-monitor level");
    gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);
    a->monitorScale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, MIN_MONITOR_DB, MAX_MONITOR_DB, 1);
    gtk_widget_set_size_request(a->monitorScale, 130, -1);
    gtk_widget_set_can_focus(a->monitorScale, FALSE);
    gtk_scale_set_value_pos(GTK_SCALE(a->monitorScale), GTK_POS_RIGHT);
    gtk_scale_add_mark(GTK_SCALE(a->monitorScale), 0, GTK_POS_BOTTOM, NULL);
    gtk_widget_set_tooltip_text(a->monitorScale, "Self-monitor level");
    g_signal_connect(a->monitorScale, "format-value", G_CALLBACK(cb_monitor_format), NULL);
    g_signal_connect(a->monitorScale, "value-changed", G_CALLBACK(cb_monitor_changed), a);
    gtk_box_pack_start(GTK_BOX(row), a->monitorScale, FALSE, FALSE, 0);
    return row;
}

// Master.dta (Super Check Partial) or calls.txt in the folder, the number of calls loaded
static int load_call_list(CallList* calls, const char* dir)
{
    static const char* names[] = { "Master.dta", "MASTER.DTA", "calls.txt" };
    int n = 0;
    for (size_t i = 0; i < G_N_ELEMENTS(names) && !n; ++i) {
        gchar* path = g_build_filename(dir, names[i], NULL);
        gchar* data = NULL;
        gsize size = 0;
        if (g_file_get_contents(path, &data, &size, NULL))
            n = i < 2 ? calls_parse_master_dta(calls, data, size) : calls_parse_text(calls, data);
        g_free(data);
        g_free(path);
    }
    return n;
}

App* app_new(GtkApplication* gtkApp)
{
    App* a = g_new0(App, 1);
    a->statsVersion = -1;
    g_mutex_init(&a->lock);
    contest_init(&a->tst);
    load_settings(&a->tst.cfg);
    contest_set_my_call(&a->tst, a->tst.cfg.call);
    contest_set_wpm(&a->tst, a->tst.cfg.wpm);
    contest_apply_bandwidth(&a->tst);
    contest_apply_pitch(&a->tst);

    // a call list in the data folder or in the current folder replaces the generated calls
    if (!load_call_list(&a->tst.calls, path_data_dir())) {
        gchar* cwd = g_get_current_dir();
        load_call_list(&a->tst.calls, cwd);
        g_free(cwd);
    }

    install_css();
    theme_init();
    theme_apply((ThemeMode)a->tst.cfg.theme);

    a->window = gtk_application_window_new(gtkApp);
    gtk_window_set_default_size(GTK_WINDOW(a->window), 900, 680);
    gtk_window_set_title(GTK_WINDOW(a->window), "Morse Runner");
    GdkPixbuf* icon = create_app_icon(64);
    gtk_window_set_icon(GTK_WINDOW(a->window), icon);
    gtk_window_set_default_icon(icon);
    g_object_unref(icon);

    build_header(a);
    build_actions(a, gtkApp);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(root), 14);
    gtk_box_pack_start(GTK_BOX(root), build_dashboard(a), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), build_log(a), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), build_entry(a), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), build_keys(a), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), build_receiver(a), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(a->window), root);

    g_signal_connect(a->modeCombo, "changed", G_CALLBACK(cb_mode_changed), a);
    g_signal_connect(a->window, "key-press-event", G_CALLBACK(cb_key_press), a);
    g_signal_connect(a->window, "key-release-event", G_CALLBACK(cb_key_release), a);
    g_signal_connect(a->window, "focus-out-event", G_CALLBACK(cb_focus_out), a);
    g_signal_connect(a->window, "delete-event", G_CALLBACK(cb_delete), a);
    g_signal_connect(a->window, "destroy", G_CALLBACK(cb_destroy), a);

    sync_settings_widgets(a);
    update_controls(a);
    update_title(a);
    update_dashboard(a);
    show_placeholder(a);
    gtk_widget_show_all(a->window);
    focus_call(a);

    bool silent = false;
    a->audio = audio_open(SAMPLE_RATE, a->tst.cfg.bufSize, fill_audio, a, &silent);
    if (!a->audio)
        g_strlcpy(a->audioError, "Failed to initialize the audio output.", sizeof(a->audioError));
    else if (silent)
        g_strlcpy(a->audioError, "No audio output device is available, the simulation runs silently.",
                  sizeof(a->audioError));

    a->timer = g_timeout_add(50, cb_timer, a);
    return a;
}
