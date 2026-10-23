// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_MAIN_WINDOW_H
#define MR_MAIN_WINDOW_H

#include <gtk/gtk.h>

typedef struct App App;

// creates the main window; the App is freed when the window is destroyed
App* app_new(GtkApplication* gtkApp);

#endif
