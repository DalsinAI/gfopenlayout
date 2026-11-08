/* GFOpenGTK: GTK's API on GFOpenLayout UI, so a GTK program's windows
 * (dialogs, settings, its main window's controls) open on AmigaOS 3.x by
 * relinking it, not by writing its interface again. Each GTK window is an
 * olui window: its widgets are made into olui markup when it is shown, laid
 * out by GFOpenLayout, drawn in the GFOpenGadTools theme, and olui's events
 * come back as GTK signals. docs/GTK-COMPAT.md says what is done and what
 * is not.
 *
 * The API is GTK 1.2's (gtk_signal_connect, gtk_window_set_policy,
 * gtk_toggle_button_set_state, gtk_label_get), with GTK 2.x's names for
 * the same calls (g_signal_connect, G_CALLBACK, gtk_window_set_resizable,
 * gtk_widget_set_size_request, gtk_label_get_text) beside it.
 *
 * Every widget type is the one struct: GTK_BOX(w), GTK_LABEL(w) and the
 * rest are casts, and GTK_BIN(w)->child, GTK_TOGGLE_BUTTON(w)->active,
 * GTK_DIALOG(d)->vbox and ->action_area, w->parent and w->window are there
 * to read as GTK programs do. There is no type checking.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GFOPENGTK_GTK_H
#define GFOPENGTK_GTK_H

#include <glib.h>
#include <gdk/gdk.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef GTKC_GTK2
#define GTK_MAJOR_VERSION 2
#define GTK_MINOR_VERSION 0
#define GTK_MICRO_VERSION 0
#else
#define GTK_MAJOR_VERSION 1
#define GTK_MINOR_VERSION 2
#define GTK_MICRO_VERSION 10
#endif
#define GFOPENGTK_VERSION "0.1"

typedef struct _GtkWidget GtkWidget;
typedef struct _GtkStyle GtkStyle;     /* not looked into */

struct _GtkWidget {
    guint32 flags;              /* GTK_VISIBLE, GTK_SENSITIVE, ... (GTK_WIDGET_FLAGS) */
    guint ref_count;
    GtkWidget *parent;
    GdkWindow *window;          /* not NULL once its window is open */
    GtkStyle *style;
    GtkWidget *child;           /* GtkBin's: a button's label, a frame's or a window's contents */
    gboolean active;            /* GtkToggleButton's, a check or radio button's */
    GtkWidget *vbox;            /* GtkDialog's */
    GtkWidget *action_area;
};

typedef GtkWidget GtkObject;
typedef GtkWidget GtkContainer;
typedef GtkWidget GtkBin;
typedef GtkWidget GtkBox;
typedef GtkWidget GtkHBox;
typedef GtkWidget GtkVBox;
typedef GtkWidget GtkTable;
typedef GtkWidget GtkWindow;
typedef GtkWidget GtkDialog;
typedef GtkWidget GtkMisc;
typedef GtkWidget GtkLabel;
typedef GtkWidget GtkButton;
typedef GtkWidget GtkToggleButton;
typedef GtkWidget GtkCheckButton;
typedef GtkWidget GtkRadioButton;
typedef GtkWidget GtkEditable;
typedef GtkWidget GtkEntry;
typedef GtkWidget GtkFrame;
typedef GtkWidget GtkSeparator;
typedef GtkWidget GtkHSeparator;
typedef GtkWidget GtkVSeparator;

#define GTK_OBJECT(o) ((GtkObject *)(o))
#define GTK_WIDGET(o) ((GtkWidget *)(o))
#define GTK_CONTAINER(o) ((GtkContainer *)(o))
#define GTK_BIN(o) ((GtkBin *)(o))
#define GTK_BOX(o) ((GtkBox *)(o))
#define GTK_HBOX(o) ((GtkHBox *)(o))
#define GTK_VBOX(o) ((GtkVBox *)(o))
#define GTK_TABLE(o) ((GtkTable *)(o))
#define GTK_WINDOW(o) ((GtkWindow *)(o))
#define GTK_DIALOG(o) ((GtkDialog *)(o))
#define GTK_MISC(o) ((GtkMisc *)(o))
#define GTK_LABEL(o) ((GtkLabel *)(o))
#define GTK_BUTTON(o) ((GtkButton *)(o))
#define GTK_TOGGLE_BUTTON(o) ((GtkToggleButton *)(o))
#define GTK_CHECK_BUTTON(o) ((GtkCheckButton *)(o))
#define GTK_RADIO_BUTTON(o) ((GtkRadioButton *)(o))
#define GTK_EDITABLE(o) ((GtkEditable *)(o))
#define GTK_ENTRY(o) ((GtkEntry *)(o))
#define GTK_FRAME(o) ((GtkFrame *)(o))
#define GTK_SEPARATOR(o) ((GtkSeparator *)(o))
#define G_OBJECT(o) ((GtkObject *)(o))

/* ---- Flags ------------------------------------------------------------------------- */

enum {
    GTK_DESTROYED = 1 << 0,
    GTK_FLOATING = 1 << 1,
    GTK_TOPLEVEL = 1 << 4,
    GTK_NO_WINDOW = 1 << 5,
    GTK_REALIZED = 1 << 6,
    GTK_MAPPED = 1 << 7,
    GTK_VISIBLE = 1 << 8,
    GTK_SENSITIVE = 1 << 9,
    GTK_PARENT_SENSITIVE = 1 << 10,
    GTK_CAN_FOCUS = 1 << 11,
    GTK_HAS_FOCUS = 1 << 12,
    GTK_CAN_DEFAULT = 1 << 13,
    GTK_HAS_DEFAULT = 1 << 14
};
#define GTK_OBJECT_FLAGS(o) (GTK_OBJECT(o)->flags)
#define GTK_OBJECT_DESTROYED(o) ((GTK_OBJECT_FLAGS(o) & GTK_DESTROYED) != 0)
#define GTK_OBJECT_FLOATING(o) ((GTK_OBJECT_FLAGS(o) & GTK_FLOATING) != 0)
#define GTK_WIDGET_FLAGS(w) (GTK_WIDGET(w)->flags)
#define GTK_WIDGET_SET_FLAGS(w, f) G_STMT_START { GTK_WIDGET_FLAGS(w) |= (f); } G_STMT_END
#define GTK_WIDGET_UNSET_FLAGS(w, f) G_STMT_START { GTK_WIDGET_FLAGS(w) &= ~(guint32)(f); } G_STMT_END
#define GTK_WIDGET_TOPLEVEL(w) ((GTK_WIDGET_FLAGS(w) & GTK_TOPLEVEL) != 0)
#define GTK_WIDGET_REALIZED(w) ((GTK_WIDGET_FLAGS(w) & GTK_REALIZED) != 0)
#define GTK_WIDGET_MAPPED(w) ((GTK_WIDGET_FLAGS(w) & GTK_MAPPED) != 0)
#define GTK_WIDGET_VISIBLE(w) ((GTK_WIDGET_FLAGS(w) & GTK_VISIBLE) != 0)
#define GTK_WIDGET_SENSITIVE(w) ((GTK_WIDGET_FLAGS(w) & GTK_SENSITIVE) != 0)
#define GTK_WIDGET_PARENT_SENSITIVE(w) ((GTK_WIDGET_FLAGS(w) & GTK_PARENT_SENSITIVE) != 0)
#define GTK_WIDGET_IS_SENSITIVE(w) (GTK_WIDGET_SENSITIVE(w) && GTK_WIDGET_PARENT_SENSITIVE(w))
#define GTK_WIDGET_CAN_FOCUS(w) ((GTK_WIDGET_FLAGS(w) & GTK_CAN_FOCUS) != 0)
#define GTK_WIDGET_HAS_FOCUS(w) ((GTK_WIDGET_FLAGS(w) & GTK_HAS_FOCUS) != 0)
#define GTK_WIDGET_CAN_DEFAULT(w) ((GTK_WIDGET_FLAGS(w) & GTK_CAN_DEFAULT) != 0)
#define GTK_WIDGET_HAS_DEFAULT(w) ((GTK_WIDGET_FLAGS(w) & GTK_HAS_DEFAULT) != 0)
#define GTK_IS_WIDGET(w) ((w) != NULL)

/* ---- Enums ------------------------------------------------------------------------- */

typedef enum { GTK_WINDOW_TOPLEVEL, GTK_WINDOW_DIALOG, GTK_WINDOW_POPUP } GtkWindowType;
typedef enum {
    GTK_WIN_POS_NONE, GTK_WIN_POS_CENTER, GTK_WIN_POS_MOUSE, GTK_WIN_POS_CENTER_ALWAYS,
    GTK_WIN_POS_CENTER_ON_PARENT
} GtkWindowPosition;
typedef enum { GTK_JUSTIFY_LEFT, GTK_JUSTIFY_RIGHT, GTK_JUSTIFY_CENTER, GTK_JUSTIFY_FILL } GtkJustification;
typedef enum { GTK_EXPAND = 1 << 0, GTK_SHRINK = 1 << 1, GTK_FILL = 1 << 2 } GtkAttachOptions;
typedef enum { GTK_PACK_START, GTK_PACK_END } GtkPackType;
typedef enum {
    GTK_STATE_NORMAL, GTK_STATE_ACTIVE, GTK_STATE_PRELIGHT, GTK_STATE_SELECTED, GTK_STATE_INSENSITIVE
} GtkStateType;
typedef enum {
    GTK_SHADOW_NONE, GTK_SHADOW_IN, GTK_SHADOW_OUT, GTK_SHADOW_ETCHED_IN, GTK_SHADOW_ETCHED_OUT
} GtkShadowType;

/* ---- Signals ----------------------------------------------------------------------- */

typedef void (*GtkSignalFunc)(void);
typedef GtkSignalFunc GCallback;
typedef GDestroyNotify GtkDestroyNotify;
typedef gint (*GtkFunction)(gpointer data);
typedef void (*GtkCallback)(GtkWidget *widget, gpointer data);
#define GTK_SIGNAL_FUNC(f) ((GtkSignalFunc)(f))
#define G_CALLBACK(f) ((GCallback)(f))

/* A handler for a signal by its name ("clicked", "delete_event": '-' and
 * '_' are the same). Signals named ..._event are called
 *     gboolean fn(GtkWidget *, GdkEvent *, gpointer data)
 * (TRUE: handled, stop); the rest
 *     void fn(GtkWidget *, gpointer data).
 * The id for gtk_signal_disconnect, never 0. */
guint gtk_signal_connect(GtkObject *object, const gchar *name, GtkSignalFunc fn, gpointer data);
guint gtk_signal_connect_after(GtkObject *object, const gchar *name, GtkSignalFunc fn, gpointer data);
/* fn is called with slot_object in place of the widget (and no data). */
guint gtk_signal_connect_object(GtkObject *object, const gchar *name, GtkSignalFunc fn, GtkObject *slot_object);
guint gtk_signal_connect_object_after(GtkObject *object, const gchar *name, GtkSignalFunc fn,
                                      GtkObject *slot_object);
void gtk_signal_disconnect(GtkObject *object, guint id);
void gtk_signal_handler_block(GtkObject *object, guint id);
void gtk_signal_handler_unblock(GtkObject *object, guint id);
/* A signal sent as if it happened: those without an event ("clicked",
 * "toggled", "activate", ...); the rest of the arguments are left out. */
void gtk_signal_emit_by_name(GtkObject *object, const gchar *name, ...);
/* Stops the signal being sent, from inside one of its handlers. */
void gtk_signal_emit_stop_by_name(GtkObject *object, const gchar *name);

#define g_signal_connect(o, name, fn, data) gtk_signal_connect(GTK_OBJECT(o), (name), (GtkSignalFunc)(fn), (data))
#define g_signal_connect_after(o, name, fn, data) \
    gtk_signal_connect_after(GTK_OBJECT(o), (name), (GtkSignalFunc)(fn), (data))
#define g_signal_connect_swapped(o, name, fn, slot) \
    gtk_signal_connect_object(GTK_OBJECT(o), (name), (GtkSignalFunc)(fn), GTK_OBJECT(slot))
#define g_signal_handler_disconnect(o, id) gtk_signal_disconnect(GTK_OBJECT(o), (id))
#define g_signal_handler_block(o, id) gtk_signal_handler_block(GTK_OBJECT(o), (id))
#define g_signal_handler_unblock(o, id) gtk_signal_handler_unblock(GTK_OBJECT(o), (id))
#define g_signal_emit_by_name gtk_signal_emit_by_name
#define g_signal_stop_emission_by_name(o, name) gtk_signal_emit_stop_by_name(GTK_OBJECT(o), (name))

/* ---- Objects ----------------------------------------------------------------------- */

void gtk_object_set_data(GtkObject *object, const gchar *key, gpointer data);
/* destroy is called on data when the key is set again, removed, or the object goes. */
void gtk_object_set_data_full(GtkObject *object, const gchar *key, gpointer data, GtkDestroyNotify destroy);
gpointer gtk_object_get_data(GtkObject *object, const gchar *key);
void gtk_object_remove_data(GtkObject *object, const gchar *key);
void gtk_object_set_user_data(GtkObject *object, gpointer data);
gpointer gtk_object_get_user_data(GtkObject *object);
#define gtk_object_ref(o) gtk_widget_ref(GTK_WIDGET(o))
#define gtk_object_unref(o) gtk_widget_unref(GTK_WIDGET(o))
#define gtk_object_destroy(o) gtk_widget_destroy(GTK_WIDGET(o))
#define gtk_object_sink(o) gtk_widget_sink(GTK_WIDGET(o))
#define g_object_set_data(o, k, d) gtk_object_set_data(GTK_OBJECT(o), (k), (d))
#define g_object_set_data_full(o, k, d, f) gtk_object_set_data_full(GTK_OBJECT(o), (k), (d), (f))
#define g_object_get_data(o, k) gtk_object_get_data(GTK_OBJECT(o), (k))
#define g_object_ref(o) ((gpointer)gtk_widget_ref(GTK_WIDGET(o)))
#define g_object_unref(o) gtk_widget_unref(GTK_WIDGET(o))

/* ---- The main loop ----------------------------------------------------------------- */

/* Opens what the windows need (on the Amiga: intuition, graphics, the
 * timer); argc and argv are kept as they are. gtk_init stops the program
 * when it can't. */
void gtk_init(int *argc, char ***argv);
gboolean gtk_init_check(int *argc, char ***argv);
gchar *gtk_set_locale(void);
/* Runs the loop until gtk_main_quit; inside a handler, a loop of its own
 * (a modal dialog's), which the next gtk_main_quit ends. */
void gtk_main(void);
void gtk_main_quit(void);
guint gtk_main_level(void);
/* One round of the loop (waiting for something when there is nothing to do);
 * TRUE when gtk_main_quit has been called for the loop it is in. */
gboolean gtk_main_iteration(void);
gboolean gtk_main_iteration_do(gboolean blocking);
gboolean gtk_events_pending(void);
void gtk_exit(gint code);

guint gtk_timeout_add(guint32 interval, GtkFunction fn, gpointer data);
void gtk_timeout_remove(guint id);
guint gtk_idle_add(GtkFunction fn, gpointer data);
void gtk_idle_remove(guint id);

/* Windows other than the grab's and its own take no input (a modal dialog). */
void gtk_grab_add(GtkWidget *widget);
void gtk_grab_remove(GtkWidget *widget);
GtkWidget *gtk_grab_get_current(void);

/* ---- Widgets ----------------------------------------------------------------------- */

void gtk_widget_show(GtkWidget *widget);
void gtk_widget_show_all(GtkWidget *widget);
void gtk_widget_hide(GtkWidget *widget);
void gtk_widget_hide_all(GtkWidget *widget);
void gtk_widget_realize(GtkWidget *widget);
/* "destroy" is sent, what it holds destroyed, its window closed; it is
 * freed when the last reference goes. */
void gtk_widget_destroy(GtkWidget *widget);
/* A "destroy" handler that sets *pointer to NULL. */
void gtk_widget_destroyed(GtkWidget *widget, GtkWidget **pointer);
GtkWidget *gtk_widget_ref(GtkWidget *widget);
void gtk_widget_unref(GtkWidget *widget);
void gtk_widget_sink(GtkWidget *widget);
void gtk_widget_set_sensitive(GtkWidget *widget, gboolean sensitive);
/* Its size in pixels (-1: its own); a window's: the size it opens at. */
void gtk_widget_set_usize(GtkWidget *widget, gint width, gint height);
#define gtk_widget_set_size_request gtk_widget_set_usize
void gtk_widget_grab_focus(GtkWidget *widget);
void gtk_widget_grab_default(GtkWidget *widget);
GtkWidget *gtk_widget_get_toplevel(GtkWidget *widget);
void gtk_widget_set_name(GtkWidget *widget, const gchar *name);
const gchar *gtk_widget_get_name(GtkWidget *widget);
void gtk_widget_set_events(GtkWidget *widget, gint events);
void gtk_widget_add_events(GtkWidget *widget, gint events);
gint gtk_widget_get_events(GtkWidget *widget);
gboolean gtk_widget_is_focus(GtkWidget *widget);

void gtk_container_add(GtkContainer *container, GtkWidget *widget);
void gtk_container_remove(GtkContainer *container, GtkWidget *widget);
void gtk_container_set_border_width(GtkContainer *container, guint width);
#define gtk_container_border_width gtk_container_set_border_width
/* Its children, a GList to free with g_list_free. */
GList *gtk_container_children(GtkContainer *container);
#define gtk_container_get_children gtk_container_children
void gtk_container_foreach(GtkContainer *container, GtkCallback fn, gpointer data);

/* ---- Boxes and tables ------------------------------------------------------------- */

GtkWidget *gtk_hbox_new(gboolean homogeneous, gint spacing);
GtkWidget *gtk_vbox_new(gboolean homogeneous, gint spacing);
void gtk_box_pack_start(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding);
void gtk_box_pack_end(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding);
void gtk_box_pack_start_defaults(GtkBox *box, GtkWidget *child);
void gtk_box_pack_end_defaults(GtkBox *box, GtkWidget *child);
void gtk_box_set_homogeneous(GtkBox *box, gboolean homogeneous);
void gtk_box_set_spacing(GtkBox *box, gint spacing);

GtkWidget *gtk_table_new(guint rows, guint columns, gboolean homogeneous);
void gtk_table_attach(GtkTable *table, GtkWidget *child, guint left, guint right, guint top, guint bottom,
                      GtkAttachOptions xoptions, GtkAttachOptions yoptions, guint xpadding, guint ypadding);
void gtk_table_attach_defaults(GtkTable *table, GtkWidget *child, guint left, guint right, guint top, guint bottom);
void gtk_table_set_row_spacings(GtkTable *table, guint spacing);
void gtk_table_set_col_spacings(GtkTable *table, guint spacing);

/* ---- Windows and dialogs ----------------------------------------------------------- */

GtkWidget *gtk_window_new(GtkWindowType type);
void gtk_window_set_title(GtkWindow *window, const gchar *title);
const gchar *gtk_window_get_title(GtkWindow *window);
void gtk_window_set_default_size(GtkWindow *window, gint width, gint height);
void gtk_window_set_position(GtkWindow *window, GtkWindowPosition position);
void gtk_window_set_modal(GtkWindow *window, gboolean modal);
void gtk_window_set_transient_for(GtkWindow *window, GtkWindow *parent);
/* 1.2's: allow_grow is what counts (a size gadget); 2.x's set_resizable is that. */
void gtk_window_set_policy(GtkWindow *window, gint allow_shrink, gint allow_grow, gint auto_shrink);
void gtk_window_set_resizable(GtkWindow *window, gboolean resizable);
#define gtk_window_present(w) gtk_widget_show(GTK_WIDGET(w))

/* A window with a vbox (GTK_DIALOG(d)->vbox) over an hbox for its buttons (->action_area). */
GtkWidget *gtk_dialog_new(void);

/* ---- Labels and buttons ------------------------------------------------------------ */

void gtk_misc_set_alignment(GtkMisc *misc, gfloat xalign, gfloat yalign);
void gtk_misc_set_padding(GtkMisc *misc, gint xpad, gint ypad);

GtkWidget *gtk_label_new(const gchar *text);
void gtk_label_set_text(GtkLabel *label, const gchar *text);
#define gtk_label_set gtk_label_set_text
/* 1.2's: *text is the label's own (not to be freed) */
void gtk_label_get(GtkLabel *label, gchar **text);
const gchar *gtk_label_get_text(GtkLabel *label);
void gtk_label_set_justify(GtkLabel *label, GtkJustification justify);
void gtk_label_set_line_wrap(GtkLabel *label, gboolean wrap);

GtkWidget *gtk_button_new(void);
GtkWidget *gtk_button_new_with_label(const gchar *label);
void gtk_button_clicked(GtkButton *button);
void gtk_button_set_label(GtkButton *button, const gchar *label);
const gchar *gtk_button_get_label(GtkButton *button);

GtkWidget *gtk_toggle_button_new(void);
GtkWidget *gtk_toggle_button_new_with_label(const gchar *label);
/* "toggled" is sent when it changes */
void gtk_toggle_button_set_active(GtkToggleButton *button, gboolean active);
#define gtk_toggle_button_set_state gtk_toggle_button_set_active
gboolean gtk_toggle_button_get_active(GtkToggleButton *button);
void gtk_toggle_button_toggled(GtkToggleButton *button);

GtkWidget *gtk_check_button_new(void);
GtkWidget *gtk_check_button_new_with_label(const gchar *label);

/* group: gtk_radio_button_group's of another in it, or NULL for a new group
 * (whose first button is on). */
GtkWidget *gtk_radio_button_new(GSList *group);
GtkWidget *gtk_radio_button_new_with_label(GSList *group, const gchar *label);
GtkWidget *gtk_radio_button_new_with_label_from_widget(GtkRadioButton *member, const gchar *label);
GSList *gtk_radio_button_group(GtkRadioButton *button);
#define gtk_radio_button_get_group gtk_radio_button_group

/* ---- Entries ----------------------------------------------------------------------- */

GtkWidget *gtk_entry_new(void);
GtkWidget *gtk_entry_new_with_max_length(guint16 max);
void gtk_entry_set_text(GtkEntry *entry, const gchar *text);
void gtk_entry_append_text(GtkEntry *entry, const gchar *text);
/* the entry's own text, kept up to date as it is typed (not to be freed) */
gchar *gtk_entry_get_text(GtkEntry *entry);
void gtk_entry_set_max_length(GtkEntry *entry, guint16 max);
/* FALSE: a password, each character drawn as a bullet */
void gtk_entry_set_visibility(GtkEntry *entry, gboolean visible);
void gtk_entry_set_editable(GtkEntry *entry, gboolean editable);
#define gtk_editable_set_editable(e, b) gtk_entry_set_editable(GTK_ENTRY(e), (b))
void gtk_editable_select_region(GtkEditable *editable, gint start, gint end);
/* characters start to end (-1: the end), g_malloc'd */
gchar *gtk_editable_get_chars(GtkEditable *editable, gint start, gint end);

/* ---- Frames and separators ----------------------------------------------------------- */

GtkWidget *gtk_frame_new(const gchar *label);
void gtk_frame_set_label(GtkFrame *frame, const gchar *label);
void gtk_frame_set_shadow_type(GtkFrame *frame, GtkShadowType type);
GtkWidget *gtk_hseparator_new(void);
GtkWidget *gtk_vseparator_new(void);

#ifdef __cplusplus
}
#endif

#include <gtk/gtkc.h>

#endif
