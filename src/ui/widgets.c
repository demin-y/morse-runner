// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
#include "widgets.h"

#include <math.h>

static const char* CSS =
    "@define-color mr_accent " MR_COLOR_ACCENT ";"
    ".mr-card {"
    "  background-color: alpha(@theme_fg_color, 0.045);"
    "  border-radius: 10px;"
    "  padding: 10px 14px;"
    "}"
    ".mr-caption {"
    "  font-size: 0.75em;"
    "  font-weight: bold;"
    "  letter-spacing: 1px;"
    "  color: alpha(@theme_fg_color, 0.55);"
    "}"
    ".mr-value {"
    "  font-size: 2.0em;"
    "  font-weight: 300;"
    "  font-feature-settings: \"tnum\";"
    "}"
    ".mr-subvalue {"
    "  font-feature-settings: \"tnum\";"
    "  color: alpha(@theme_fg_color, 0.7);"
    "}"
    ".mr-mode {"
    "  font-weight: bold;"
    "  color: @mr_accent;"
    "}"
    ".mr-mode.mr-compet {"
    "  color: " MR_COLOR_ERROR ";"
    "}"
    "entry.mr-qso {"
    "  font-family: monospace;"
    "  font-size: 1.6em;"
    "  font-weight: bold;"
    "  padding: 6px 10px;"
    "}"
    "button.mr-fkey {"
    "  padding: 6px 4px;"
    "}"
    "treeview.mr-log {"
    "  font-family: monospace;"
    "}"
    ".mr-log-frame {"
    "  border-radius: 10px;"
    "  border: 1px solid alpha(@theme_fg_color, 0.12);"
    "}"
    ".mr-placeholder {"
    "  color: alpha(@theme_fg_color, 0.5);"
    "}"
    ".mr-placeholder-title {"
    "  font-size: 1.4em;"
    "  font-weight: bold;"
    "  color: alpha(@theme_fg_color, 0.6);"
    "}"
    ".mr-rx {"
    "  font-feature-settings: \"tnum\";"
    "  color: alpha(@theme_fg_color, 0.75);"
    "}"
    ".mr-settings-header {"
    "  font-weight: bold;"
    "  margin-top: 6px;"
    "}"
    ".mr-record {"
    "  font-size: 1.3em;"
    "  font-weight: bold;"
    "  color: @mr_accent;"
    "}";

static void rounded_rect(cairo_t* cr, double x, double y, double w, double h, double r)
{
    r = fmin(r, fmin(w, h) / 2);
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

void install_css(void)
{
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

GdkPixbuf* create_app_icon(int size)
{
    return gdk_pixbuf_new_from_resource_at_scale("/net/morserunner/MorseRunner/morserunner.png", size, size, TRUE, NULL);
}

void theme_colors(GtkWidget* w, GdkRGBA* fg, GdkRGBA* accent)
{
    GtkStyleContext* ctx = gtk_widget_get_style_context(w);
    gtk_style_context_get_color(ctx, gtk_style_context_get_state(ctx), fg);
    gdk_rgba_parse(accent, MR_COLOR_ACCENT);
}

void draw_histogram(GtkWidget* w, cairo_t* cr, const int* bins, int count)
{
    const int width = gtk_widget_get_allocated_width(w);
    const int height = gtk_widget_get_allocated_height(w);
    GdkRGBA fg, accent;
    theme_colors(w, &fg, &accent);

    // baseline
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.15);
    cairo_rectangle(cr, 0, height - 1, width, 1);
    cairo_fill(cr);
    if (!count) return;

    // scale: a bar of full height is 25 QSOs in 5 minutes (300 Q/hr)
    int top = 25;
    for (int i = 0; i < count; ++i) if (bins[i] > top) top = bins[i];

    const double slot = (double)width / count;
    const double bar = fmax(1.0, slot - fmax(1.0, slot * 0.25));
    cairo_set_source_rgba(cr, accent.red, accent.green, accent.blue, 0.9);
    for (int i = 0; i < count; ++i) {
        if (!bins[i]) continue;
        double h = fmax(2.0, (height - 2) * (double)bins[i] / top);
        rounded_rect(cr, i * slot, height - 1 - h, bar, h, fmin(2.0, bar / 2));
        cairo_fill(cr);
    }
}

// the scale spans +-1000 Hz around the zero beat
RitGeometry rit_geometry(GtkWidget* w, int bandwidth, int rit)
{
    const double width = gtk_widget_get_allocated_width(w);
    const double span = 2400.0;
    RitGeometry g;
    g.center = width / 2 + rit * width / span;
    g.left = g.center - bandwidth * width / span / 2;
    g.right = g.center + bandwidth * width / span / 2;
    return g;
}

void draw_rit(GtkWidget* w, cairo_t* cr, int bandwidth, int rit)
{
    const int width = gtk_widget_get_allocated_width(w);
    const int height = gtk_widget_get_allocated_height(w);
    GdkRGBA fg, accent;
    theme_colors(w, &fg, &accent);

    // track
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.08);
    rounded_rect(cr, 0, 0, width, height, 6);
    cairo_fill(cr);

    // passband
    RitGeometry g = rit_geometry(w, bandwidth, rit);
    cairo_set_source_rgba(cr, accent.red, accent.green, accent.blue, 0.35);
    rounded_rect(cr, g.left, 3, g.right - g.left, height - 6, 4);
    cairo_fill(cr);

    // zero beat mark and the receive frequency
    cairo_set_source_rgba(cr, fg.red, fg.green, fg.blue, 0.35);
    cairo_rectangle(cr, width / 2.0 - 0.5, 0, 1, height);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, accent.red, accent.green, accent.blue, 1);
    cairo_rectangle(cr, g.center - 1, 3, 2, height - 6);
    cairo_fill(cr);
}

GtkWidget* make_caption(const char* text)
{
    GtkWidget* l = gtk_label_new(text);
    // the whole width, not the measured one: the CSS letter spacing is not measured
    gtk_label_set_xalign(GTK_LABEL(l), 0);
    gtk_style_context_add_class(gtk_widget_get_style_context(l), "mr-caption");
    label_shrink(l);
    return l;
}

void label_shrink(GtkWidget* label)
{
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
}

void combo_shrink(GtkWidget* combo, int chars)
{
    GList* cells = gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(combo));
    for (GList* c = cells; c; c = c->next)
        if (GTK_IS_CELL_RENDERER_TEXT(c->data))
            g_object_set(c->data, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", chars, "max-width-chars", chars,
                         NULL);
    g_list_free(cells);
}

void label_wrap(GtkWidget* label, int maxChars)
{
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);   // long words too
    gtk_label_set_max_width_chars(GTK_LABEL(label), maxChars);
}
