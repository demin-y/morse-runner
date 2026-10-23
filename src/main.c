// Morse Runner - CW contest simulator
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "ui/main_window.h"
#include "ui/theme.h"

#include <gtk/gtk.h>

static void on_activate(GtkApplication* app, gpointer data)
{
    (void)data;
    // a second launch just raises the existing window
    GList* windows = gtk_application_get_windows(app);
    if (windows) {
        gtk_window_present(GTK_WINDOW(windows->data));
        return;
    }
    app_new(app);
}

int main(int argc, char** argv)
{
    theme_preinit();
    g_set_application_name("Morse Runner");
    g_set_prgname("morserunner");

    GtkApplication* app = gtk_application_new("net.morserunner.MorseRunner", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
