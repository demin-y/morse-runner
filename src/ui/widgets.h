// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#ifndef MR_WIDGETS_H
#define MR_WIDGETS_H

#include <gtk/gtk.h>

// one accent that reads well on light and dark themes, and the check marks of the log
#define MR_COLOR_ACCENT "#3584e4"
#define MR_COLOR_OK "#2ec27e"
#define MR_COLOR_ERROR "#e01b24"

// application-wide style sheet
void install_css(void);
// the application icon, from the resources built into the program
GdkPixbuf* create_app_icon(int size);

// accent / foreground colors of the current theme
void theme_colors(GtkWidget* w, GdkRGBA* fg, GdkRGBA* accent);

// QSO rate histogram, 5-minute bins
void draw_histogram(GtkWidget* w, cairo_t* cr, const int* bins, int count);

// receiver passband with the RIT offset
typedef struct {
    double center, left, right;   // pixels
} RitGeometry;
RitGeometry rit_geometry(GtkWidget* w, int bandwidth, int rit);
void draw_rit(GtkWidget* w, cairo_t* cr, int bandwidth, int rit);

// a small upper case caption
GtkWidget* make_caption(const char* text);
// a translation may be much longer than the English text: a label either
// shortens with "…" (one line) or wraps (several lines) instead of widening the window
void label_shrink(GtkWidget* label);
void label_wrap(GtkWidget* label, int maxChars);
// the same for the text of a combo box: about `chars` characters, then "…"
void combo_shrink(GtkWidget* combo, int chars);

#endif
