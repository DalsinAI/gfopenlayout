/* GFOpenGTK's own calls, beside GTK's (gtk.h includes this): the olui
 * under a window, for a program that wants its CSS or a widget of olui's
 * own, and input as if from the user, for tests and for programs that drive
 * a window themselves.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GFOPENGTK_GTKC_H
#define GFOPENGTK_GTKC_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

struct olui;
struct olui_el;

/* CSS for every window from now on, after the theme's (windows open
 * already are styled again when they are next built). FALSE when memory ran out. */
gboolean gtkc_add_css(const gchar *css);

/* A shown window's olui (NULL before it is shown), and the olui element
 * a widget is (NULL when it has none: a box, before its window is shown). */
struct olui *gtkc_window_olui(GtkWidget *window);
struct olui_el *gtkc_widget_el(GtkWidget *widget);
/* The markup the window was last built from (for looking at; NULL before
 * it is shown). */
const gchar *gtkc_window_markup(GtkWidget *window);
/* The inner size a shown window has. */
gboolean gtkc_window_size(GtkWidget *window, gint *width, gint *height);

/* Input as the user gives it, through the same path: a key typed in the
 * window (key_press_event first, then the widget with the keyboard), the
 * left button pressed and let go in the middle of a widget, text typed
 * into an entry, and the window's close gadget (delete_event, then
 * destroyed unless a handler returned TRUE). Each TRUE when it was taken. */
gboolean gtkc_key(GtkWidget *window, guint keyval, guint state);
gboolean gtkc_click(GtkWidget *widget);
gboolean gtkc_type(GtkWidget *entry, const gchar *text);
void gtkc_window_close(GtkWidget *window);

/* What the handlers asked done to the windows: those closed closed, those
 * changed built again, and all drawn. The loop does it each round, and
 * the calls above after their input. */
void gtkc_flush(void);

/* The loop's clock, in ms. On the host time is the loop's own: it moves
 * on only while the loop waits (for the next timeout), so a test runs at
 * once and the same each time. */
guint32 gtkc_now(void);

#ifdef __cplusplus
}
#endif
#endif
