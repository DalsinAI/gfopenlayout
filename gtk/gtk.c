/* GFOpenGTK's core: GTK's widgets kept as a tree of their own, made into
 * GFOpenLayout UI markup when their window is shown (and again when the
 * tree changes), olui's events sent back as GTK signals, and the main loop
 * with glib's timeouts and idle functions. What is the Amiga's (or the
 * host's) is behind gtkc_internal.h.
 *
 * A window is built again (its markup loaded into its olui once more) only
 * outside its own olui's handlers: olui is in the middle of something
 * then. A widget's state (text, a tick, hidden, disabled) goes straight to
 * its element instead, and is in the widget too, for the next build.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gtkc_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned next_id = 1;
static guint next_handler = 1;
static GList *toplevels;        /* every window not destroyed, each with a reference */
static GSList *grabs;           /* gtk_grab_add's, the latest first */
static GList *closing;          /* windows to close once their olui is out of its handlers */
static GSList *program_css;     /* gtkc_add_css's, in order (the latest first) */
static guint main_level, quits;
static int initialised;

static void build(gtkc_widget *win);
static void close_window(gtkc_widget *win);
static void do_close(gtkc_widget *win);
static gtkc_widget *toplevel_of(gtkc_widget *w);

/* ---- Widgets: made, referenced, destroyed ------------------------------------------- */

static gtkc_widget *widget_new(int kind)
{
    gtkc_widget *w = g_new0(gtkc_widget, 1);
    w->kind = kind;
    w->id = next_id++;
    w->pub.ref_count = 1;
    w->pub.flags = GTK_FLOATING | GTK_SENSITIVE | GTK_PARENT_SENSITIVE;
    w->usize_w = w->usize_h = -1;
    w->xalign = 500;
    return w;
}

/* Its window, built again when it is next safe to (it may not be open). */
static void changed(gtkc_widget *w)
{
    gtkc_widget *win = toplevel_of(w);
    if (win && win->top) win->top->dirty = 1;
}

static gtkc_widget *toplevel_of(gtkc_widget *w)
{
    while (w && w->pub.parent) w = GTKC(w->pub.parent);
    return w && w->kind == GTKC_WINDOW ? w : NULL;
}

static void free_widget(gtkc_widget *w)
{
    gtkc_handler *h, *hn;
    for (h = w->handlers; h; h = hn) {
        hn = h->next;
        free(h->name);
        free(h);
    }
    free(w->text);
    free(w->name);
    if (w->top) {
        free(w->top->markup);
        free(w->top);
    }
    free(w);
}

GtkWidget *gtk_widget_ref(GtkWidget *widget)
{
    if (widget) ++widget->ref_count;
    return widget;
}

void gtk_widget_unref(GtkWidget *widget)
{
    if (!widget || !widget->ref_count) return;
    /* the last reference, and not destroyed yet: destroyed now, so it leaves no grab or child behind */
    if (widget->ref_count == 1 && !(widget->flags & GTK_DESTROYED)) gtk_widget_destroy(widget);
    if (--widget->ref_count == 0) free_widget(GTKC(widget));
}

void gtk_widget_sink(GtkWidget *widget)
{
    if (widget && (widget->flags & GTK_FLOATING)) {
        widget->flags &= ~(guint32)GTK_FLOATING;
        gtk_widget_unref(widget);
    }
}

static void unlink_child(gtkc_widget *parent, gtkc_widget *c)
{
    if (c->prev) c->prev->next = c->next; else parent->first = c->next;
    if (c->next) c->next->prev = c->prev; else parent->last = c->prev;
    c->next = c->prev = NULL;
    c->pub.parent = NULL;
    if (parent->pub.child == &c->pub) parent->pub.child = parent->first ? &parent->first->pub : NULL;
}

static void clear_data(gtkc_widget *w)
{
    while (w->data) {
        gtkc_data *d = w->data;
        w->data = d->next;
        if (d->destroy) d->destroy(d->data);
        free(d->key);
        free(d);
    }
}

/* It and what it holds have no elements now: their window goes without them */
static void release_elements(gtkc_widget *w)
{
    gtkc_widget *c;
    if (w->el && !w->alias) olui_set_user(w->el, NULL);
    w->el = NULL;
    for (c = w->first; c; c = c->next) release_elements(c);
}

static gboolean emit(gtkc_widget *w, const char *name, GdkEvent *event);

void gtk_widget_destroy(GtkWidget *widget)
{
    gtkc_widget *w = GTKC(widget), *win;
    if (!w || (widget->flags & GTK_DESTROYED)) return;
    widget->flags |= GTK_DESTROYED;
    gtk_widget_ref(widget);
    if (w->top) gtk_grab_remove(widget);
    else if (g_slist_find(grabs, w)) grabs = g_slist_remove(grabs, w);
    emit(w, "destroy", NULL);
    while (w->first) {
        gtkc_widget *c = w->first;
        gtk_widget_ref(&c->pub);            /* it takes itself out of w, and w's reference with it */
        gtk_widget_destroy(&c->pub);
        if (c->pub.parent == widget) {      /* destroyed before but still here: out it goes */
            release_elements(c);
            unlink_child(w, c);
            gtk_widget_unref(&c->pub);
        }
        gtk_widget_unref(&c->pub);
    }
    if (w->group) {
        GSList *g = g_slist_remove(w->group, w), *p;
        for (p = g; p; p = p->next) GTKC(p->data)->group = g;
        w->group = NULL;
    }
    if (w->el && !w->alias) olui_set_user(w->el, NULL);
    w->el = NULL;
    clear_data(w);
    if (w->top) {
        close_window(w);
        if (g_list_find(toplevels, w)) {
            toplevels = g_list_remove(toplevels, w);
            gtk_widget_unref(widget);
        }
    }
    win = w->pub.parent ? toplevel_of(GTKC(w->pub.parent)) : NULL;
    if (w->pub.parent) {
        gtkc_widget *parent = GTKC(w->pub.parent);
        unlink_child(parent, w);
        if (parent->kind == GTKC_WINDOW || parent->kind == GTKC_FRAME || parent->kind == GTKC_BUTTON ||
            parent->kind == GTKC_TOGGLE || parent->kind == GTKC_CHECK || parent->kind == GTKC_RADIO)
            parent->pub.child = NULL;
        gtk_widget_unref(widget);
        if (win && win->top) win->top->dirty = 1;
    }
    gtk_widget_unref(widget);
}

void gtk_widget_destroyed(GtkWidget *widget, GtkWidget **pointer)
{
    (void)widget;
    if (pointer) *pointer = NULL;
}

/* ---- An object's data --------------------------------------------------------------- */

static gtkc_data **find_data(gtkc_widget *w, const gchar *key)
{
    gtkc_data **pp;
    for (pp = &w->data; *pp; pp = &(*pp)->next)
        if (!strcmp((*pp)->key, key)) return pp;
    return NULL;
}

void gtk_object_set_data_full(GtkObject *object, const gchar *key, gpointer data, GtkDestroyNotify destroy)
{
    gtkc_widget *w = GTKC(object);
    gtkc_data **pp, *d;
    if (!w || !key) return;
    if ((pp = find_data(w, key)) != NULL) {
        d = *pp;
        if (d->destroy) d->destroy(d->data);
        if (!data) {
            *pp = d->next;
            free(d->key);
            free(d);
            return;
        }
    } else {
        if (!data) return;
        d = g_new0(gtkc_data, 1);
        d->key = g_strdup(key);
        d->next = w->data;
        w->data = d;
    }
    d->data = data;
    d->destroy = destroy;
}

void gtk_object_set_data(GtkObject *object, const gchar *key, gpointer data)
{
    gtk_object_set_data_full(object, key, data, NULL);
}

gpointer gtk_object_get_data(GtkObject *object, const gchar *key)
{
    gtkc_data **pp;
    if (!object || !key) return NULL;
    pp = find_data(GTKC(object), key);
    return pp ? (*pp)->data : NULL;
}

void gtk_object_remove_data(GtkObject *object, const gchar *key) { gtk_object_set_data_full(object, key, NULL, NULL); }
void gtk_object_set_user_data(GtkObject *object, gpointer data) { gtk_object_set_data(object, "user_data", data); }
gpointer gtk_object_get_user_data(GtkObject *object) { return gtk_object_get_data(object, "user_data"); }

/* ---- Signals ------------------------------------------------------------------------ */

typedef void (*void_fn)(GtkWidget *, gpointer);
typedef gboolean (*event_fn)(GtkWidget *, GdkEvent *, gpointer);
typedef void (*void_slot_fn)(GtkObject *);
typedef gboolean (*event_slot_fn)(GtkObject *, GdkEvent *);

static int stop_depth;          /* gtk_signal_emit_stop_by_name: the emission it stops */
static struct emission { gtkc_widget *w; const char *name; int stopped; } emissions[16];

static char *signal_name(const gchar *name)
{
    char *s = g_strdup(name), *p;
    for (p = s; *p; ++p)
        if (*p == '-') *p = '_';
    return s;
}

static int same_signal(const char *canon, const char *name)
{
    for (; *canon && *name; ++canon, ++name)
        if (*canon != *name && !(*canon == '_' && *name == '-')) return 0;
    return *canon == *name;
}

static guint connect(GtkObject *object, const gchar *name, GtkSignalFunc fn, gpointer data, GtkObject *slot,
                     int after, int swapped)
{
    gtkc_widget *w = GTKC(object);
    gtkc_handler *h, **pp;
    if (!w || !name || !fn) return 0;
    h = g_new0(gtkc_handler, 1);
    h->id = next_handler++;
    h->name = signal_name(name);
    h->fn = fn;
    h->data = data;
    h->slot = slot;
    h->after = after ? 1 : 0;
    h->swapped = swapped ? 1 : 0;
    for (pp = &w->handlers; *pp; pp = &(*pp)->next) {}
    *pp = h;
    return h->id;
}

guint gtk_signal_connect(GtkObject *o, const gchar *name, GtkSignalFunc fn, gpointer data)
{
    return connect(o, name, fn, data, NULL, 0, 0);
}

guint gtk_signal_connect_after(GtkObject *o, const gchar *name, GtkSignalFunc fn, gpointer data)
{
    return connect(o, name, fn, data, NULL, 1, 0);
}

guint gtk_signal_connect_object(GtkObject *o, const gchar *name, GtkSignalFunc fn, GtkObject *slot)
{
    return connect(o, name, fn, NULL, slot, 0, 1);
}

guint gtk_signal_connect_object_after(GtkObject *o, const gchar *name, GtkSignalFunc fn, GtkObject *slot)
{
    return connect(o, name, fn, NULL, slot, 1, 1);
}

static gtkc_handler *find_handler(GtkObject *o, guint id)
{
    gtkc_handler *h;
    if (!o) return NULL;
    for (h = GTKC(o)->handlers; h; h = h->next)
        if (h->id == id && !h->dead) return h;
    return NULL;
}

static void sweep_handlers(gtkc_widget *w)
{
    gtkc_handler **pp = &w->handlers;
    while (*pp) {
        gtkc_handler *h = *pp;
        if (h->dead) {
            *pp = h->next;
            free(h->name);
            free(h);
        } else {
            pp = &h->next;
        }
    }
}

void gtk_signal_disconnect(GtkObject *o, guint id)
{
    gtkc_handler *h = find_handler(o, id);
    if (!h) return;
    h->dead = 1;
    if (!GTKC(o)->emitting) sweep_handlers(GTKC(o));
}

void gtk_signal_handler_block(GtkObject *o, guint id)
{
    gtkc_handler *h = find_handler(o, id);
    if (h) h->blocked = 1;
}

void gtk_signal_handler_unblock(GtkObject *o, guint id)
{
    gtkc_handler *h = find_handler(o, id);
    if (h) h->blocked = 0;
}

void gtk_signal_emit_stop_by_name(GtkObject *o, const gchar *name)
{
    int i;
    for (i = stop_depth - 1; i >= 0; --i)
        if (emissions[i].w == GTKC(o) && same_signal(emissions[i].name, name)) {
            emissions[i].stopped = 1;
            return;
        }
}

static int is_event_signal(const char *name)
{
    size_t n = strlen(name);
    return n > 6 && !strcmp(name + n - 6, "_event");
}

/* Sends a signal to w's handlers, those connected "after" last; TRUE when
 * an event signal's handler returned TRUE. */
static gboolean emit(gtkc_widget *w, const char *name, GdkEvent *event)
{
    int pass, slot = -1, is_event = is_event_signal(name);
    gboolean handled = FALSE;
    gtkc_handler *h;
    if (!w || !w->handlers) return FALSE;
    gtk_widget_ref(&w->pub);
    ++w->emitting;
    if (stop_depth < (int)G_N_ELEMENTS(emissions)) {
        slot = stop_depth++;
        emissions[slot].w = w;
        emissions[slot].name = name;
        emissions[slot].stopped = 0;
    }
    for (pass = 0; pass < 2 && !handled; ++pass)
        for (h = w->handlers; h && !handled; h = h->next) {
            if (h->dead || h->blocked || (int)h->after != pass || !same_signal(h->name, name)) continue;
            if (is_event) {
                handled = h->swapped ? ((event_slot_fn)(void (*)(void))h->fn)(h->slot, event)
                                     : ((event_fn)(void (*)(void))h->fn)(&w->pub, event, h->data);
            } else if (h->swapped) {
                ((void_slot_fn)(void (*)(void))h->fn)(h->slot);
            } else {
                ((void_fn)(void (*)(void))h->fn)(&w->pub, h->data);
            }
            if (slot >= 0 && emissions[slot].stopped) {
                pass = 2;
                break;
            }
        }
    if (slot >= 0) --stop_depth;
    if (--w->emitting == 0) sweep_handlers(w);
    gtk_widget_unref(&w->pub);
    return handled;
}

void gtk_signal_emit_by_name(GtkObject *o, const gchar *name, ...)
{
    gtkc_widget *w = GTKC(o);
    char *canon;
    if (!w || !name) return;
    canon = signal_name(name);
    if (!strcmp(canon, "clicked") && (w->kind == GTKC_BUTTON || w->kind == GTKC_TOGGLE || w->kind == GTKC_CHECK ||
                                      w->kind == GTKC_RADIO))
        gtk_button_clicked(&w->pub);
    else
        emit(w, canon, NULL);
    free(canon);
}

/* ---- Containers --------------------------------------------------------------------- */

static void add_child(gtkc_widget *parent, gtkc_widget *c)
{
    if (!parent || !c || c->pub.parent) return;
    if (c->pub.flags & GTK_FLOATING) c->pub.flags &= ~(guint32)GTK_FLOATING;
    else gtk_widget_ref(&c->pub);
    c->pub.parent = &parent->pub;
    c->prev = parent->last;
    if (parent->last) parent->last->next = c; else parent->first = c;
    parent->last = c;
    if (!parent->pub.child) parent->pub.child = &c->pub;
    if (GTK_WIDGET_IS_SENSITIVE(&parent->pub)) c->pub.flags |= GTK_PARENT_SENSITIVE;
    else c->pub.flags &= ~(guint32)GTK_PARENT_SENSITIVE;
    changed(parent);
}

void gtk_container_add(GtkContainer *container, GtkWidget *widget)
{
    gtkc_widget *c = GTKC(container), *w = GTKC(widget);
    if (!c || !w) return;
    if (c->kind == GTKC_VBOX || c->kind == GTKC_HBOX) {
        gtk_box_pack_start_defaults(container, widget);
        return;
    }
    if (c->kind == GTKC_TABLE) {
        gtk_table_attach_defaults(container, widget, 0, 1, 0, 1);
        return;
    }
    if (c->first) {
        g_warning("gtk_container_add: a %s holds one widget", c->kind == GTKC_WINDOW ? "window" : "bin");
        return;
    }
    add_child(c, w);
    /* a button's label is the button's element, its text the button's */
    if ((c->kind == GTKC_BUTTON || c->kind == GTKC_TOGGLE || c->kind == GTKC_CHECK || c->kind == GTKC_RADIO) &&
        w->kind == GTKC_LABEL)
        w->alias = 1;
}

void gtk_container_remove(GtkContainer *container, GtkWidget *widget)
{
    gtkc_widget *c = GTKC(container), *w = GTKC(widget);
    if (!c || !w || w->pub.parent != container) return;
    changed(c);
    release_elements(w);
    w->alias = 0;
    unlink_child(c, w);
    gtk_widget_unref(widget);
}

void gtk_container_set_border_width(GtkContainer *container, guint width)
{
    if (!container) return;
    GTKC(container)->border = (int)width;
    changed(GTKC(container));
}

GList *gtk_container_children(GtkContainer *container)
{
    GList *l = NULL;
    gtkc_widget *c;
    if (!container) return NULL;
    for (c = GTKC(container)->first; c; c = c->next) l = g_list_append(l, c);
    return l;
}

void gtk_container_foreach(GtkContainer *container, GtkCallback fn, gpointer data)
{
    GList *l = gtk_container_children(container), *p;
    for (p = l; p; p = p->next) fn(GTK_WIDGET(p->data), data);
    g_list_free(l);
}

/* ---- Boxes and tables ------------------------------------------------------------- */

static GtkWidget *box_new(int kind, gboolean homogeneous, gint spacing)
{
    gtkc_widget *b = widget_new(kind);
    b->homogeneous = homogeneous ? 1 : 0;
    b->spacing = spacing > 0 ? spacing : 0;
    return &b->pub;
}

GtkWidget *gtk_hbox_new(gboolean homogeneous, gint spacing) { return box_new(GTKC_HBOX, homogeneous, spacing); }
GtkWidget *gtk_vbox_new(gboolean homogeneous, gint spacing) { return box_new(GTKC_VBOX, homogeneous, spacing); }

static void pack(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding, int end)
{
    gtkc_widget *b = GTKC(box), *c = GTKC(child);
    if (!b || !c || (b->kind != GTKC_VBOX && b->kind != GTKC_HBOX)) return;
    c->expand = expand ? 1 : 0;
    c->fill = fill ? 1 : 0;
    c->padding = (int)padding;
    c->pack_end = (unsigned char)end;
    add_child(b, c);
}

void gtk_box_pack_start(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding)
{
    pack(box, child, expand, fill, padding, 0);
}

void gtk_box_pack_end(GtkBox *box, GtkWidget *child, gboolean expand, gboolean fill, guint padding)
{
    pack(box, child, expand, fill, padding, 1);
}

void gtk_box_pack_start_defaults(GtkBox *box, GtkWidget *child) { pack(box, child, TRUE, TRUE, 0, 0); }
void gtk_box_pack_end_defaults(GtkBox *box, GtkWidget *child) { pack(box, child, TRUE, TRUE, 0, 1); }

void gtk_box_set_homogeneous(GtkBox *box, gboolean homogeneous)
{
    if (!box) return;
    GTKC(box)->homogeneous = homogeneous ? 1 : 0;
    changed(GTKC(box));
}

void gtk_box_set_spacing(GtkBox *box, gint spacing)
{
    if (!box) return;
    GTKC(box)->spacing = spacing > 0 ? spacing : 0;
    changed(GTKC(box));
}

GtkWidget *gtk_table_new(guint rows, guint columns, gboolean homogeneous)
{
    gtkc_widget *t = widget_new(GTKC_TABLE);
    t->rows = rows ? rows : 1;
    t->cols = columns ? columns : 1;
    t->homogeneous = homogeneous ? 1 : 0;
    return &t->pub;
}

void gtk_table_attach(GtkTable *table, GtkWidget *child, guint left, guint right, guint top, guint bottom,
                      GtkAttachOptions xoptions, GtkAttachOptions yoptions, guint xpadding, guint ypadding)
{
    gtkc_widget *t = GTKC(table), *c = GTKC(child);
    if (!t || !c || t->kind != GTKC_TABLE || right <= left || bottom <= top || c->pub.parent) return;
    if (right > t->cols) t->cols = right;
    if (bottom > t->rows) t->rows = bottom;
    c->col0 = left;
    c->col1 = right;
    c->row0 = top;
    c->row1 = bottom;
    c->xopt = (unsigned)xoptions;
    c->yopt = (unsigned)yoptions;
    c->xpad = xpadding;
    c->ypad = ypadding;
    add_child(t, c);
}

void gtk_table_attach_defaults(GtkTable *table, GtkWidget *child, guint left, guint right, guint top, guint bottom)
{
    gtk_table_attach(table, child, left, right, top, bottom, GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 0, 0);
}

void gtk_table_set_row_spacings(GtkTable *table, guint spacing)
{
    if (!table) return;
    GTKC(table)->row_spacing = spacing;
    changed(GTKC(table));
}

void gtk_table_set_col_spacings(GtkTable *table, guint spacing)
{
    if (!table) return;
    GTKC(table)->col_spacing = spacing;
    changed(GTKC(table));
}

/* ---- Windows ------------------------------------------------------------------------- */

GtkWidget *gtk_window_new(GtkWindowType type)
{
    gtkc_widget *w = widget_new(GTKC_WINDOW);
    w->top = g_new0(gtkc_top, 1);
    w->top->type = (int)type;
    w->top->resizable = 1;
    w->pub.flags = (w->pub.flags & ~(guint32)GTK_FLOATING) | GTK_TOPLEVEL;   /* the toplevel list's reference */
    toplevels = g_list_append(toplevels, w);
    return &w->pub;
}

void gtk_window_set_title(GtkWindow *window, const gchar *title)
{
    gtkc_widget *w = GTKC(window);
    char *old;
    if (!w || !w->top) return;
    old = w->text;      /* freed after the window has the new one: Intuition may be drawing the old */
    w->text = g_strdup(title ? title : "");
    if (w->top->open) gtkc_host_title(w);
    free(old);
}

const gchar *gtk_window_get_title(GtkWindow *window)
{
    return window && GTKC(window)->top ? GTKC(window)->text : NULL;
}

void gtk_window_set_default_size(GtkWindow *window, gint width, gint height)
{
    gtkc_widget *w = GTKC(window);
    if (!w || !w->top) return;
    w->top->default_w = width > 0 ? width : 0;
    w->top->default_h = height > 0 ? height : 0;
}

void gtk_window_set_position(GtkWindow *window, GtkWindowPosition position)
{
    if (window && GTKC(window)->top) GTKC(window)->top->position = (int)position;
}

void gtk_window_set_modal(GtkWindow *window, gboolean modal)
{
    gtkc_widget *w = GTKC(window);
    if (!w || !w->top) return;
    w->top->modal = modal ? 1 : 0;
    if (w->top->open) {
        if (modal) gtk_grab_add(window);
        else gtk_grab_remove(window);
    }
}

void gtk_window_set_transient_for(GtkWindow *window, GtkWindow *parent)
{
    if (window && GTKC(window)->top) GTKC(window)->top->transient_for = GTKC(parent);
}

void gtk_window_set_policy(GtkWindow *window, gint allow_shrink, gint allow_grow, gint auto_shrink)
{
    (void)allow_shrink;
    (void)auto_shrink;
    if (window && GTKC(window)->top) GTKC(window)->top->resizable = allow_grow ? 1 : 0;
}

void gtk_window_set_resizable(GtkWindow *window, gboolean resizable)
{
    if (window && GTKC(window)->top) GTKC(window)->top->resizable = resizable ? 1 : 0;
}

GtkWidget *gtk_dialog_new(void)
{
    GtkWidget *win = gtk_window_new(GTK_WINDOW_DIALOG), *outer = gtk_vbox_new(FALSE, 0);
    GtkWidget *sep = gtk_hseparator_new();
    gtk_container_add(GTK_CONTAINER(win), outer);
    gtk_widget_show(outer);
    win->vbox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), win->vbox, TRUE, TRUE, 0);
    gtk_widget_show(win->vbox);
    win->action_area = gtk_hbox_new(TRUE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(win->action_area), 10);
    gtk_box_pack_end(GTK_BOX(outer), win->action_area, FALSE, TRUE, 0);
    gtk_widget_show(win->action_area);
    gtk_box_pack_end(GTK_BOX(outer), sep, FALSE, TRUE, 0);
    gtk_widget_show(sep);
    return win;
}

/* ---- Showing, hiding, sensitivity ----------------------------------------------------- */

static void open_window(gtkc_widget *win);

void gtk_widget_show(GtkWidget *widget)
{
    gtkc_widget *w = GTKC(widget);
    if (!w || (widget->flags & GTK_DESTROYED)) return;
    if (w->top) {
        widget->flags |= GTK_VISIBLE;
        if (!w->top->open) open_window(w);
        return;
    }
    if (widget->flags & GTK_VISIBLE) return;
    widget->flags |= GTK_VISIBLE;
    if (w->el && !w->alias) olui_set_hidden(gtkc_window_olui(&toplevel_of(w)->pub), w->el, 0);
}

void gtk_widget_hide(GtkWidget *widget)
{
    gtkc_widget *w = GTKC(widget);
    if (!w || !(widget->flags & GTK_VISIBLE)) return;
    widget->flags &= ~(guint32)GTK_VISIBLE;
    if (w->top) {
        if (w->top->modal) gtk_grab_remove(widget);
        close_window(w);
        return;
    }
    if (w->el && !w->alias) olui_set_hidden(gtkc_window_olui(&toplevel_of(w)->pub), w->el, 1);
}

static void show_all(gtkc_widget *w, int show)
{
    gtkc_widget *c;
    for (c = w->first; c; c = c->next) show_all(c, show);
    if (show) gtk_widget_show(&w->pub);
    else gtk_widget_hide(&w->pub);
}

void gtk_widget_show_all(GtkWidget *widget) { if (widget) show_all(GTKC(widget), 1); }
void gtk_widget_hide_all(GtkWidget *widget) { if (widget) show_all(GTKC(widget), 0); }

void gtk_widget_realize(GtkWidget *widget)
{
    gtkc_widget *win = GTKC(gtk_widget_get_toplevel(widget));
    if (!widget) return;
    widget->flags |= GTK_REALIZED;
    if (win && win->top) widget->window = (GdkWindow *)win->top;
}

static int has_element(const gtkc_widget *w)
{
    return w->el && !w->alias;
}

/* Sensitivity down from w: its own, and what is above it */
static void push_sensitive(gtkc_widget *w, int parent_sensitive)
{
    gtkc_widget *c, *win;
    int on;
    if (parent_sensitive) w->pub.flags |= GTK_PARENT_SENSITIVE;
    else w->pub.flags &= ~(guint32)GTK_PARENT_SENSITIVE;
    on = GTK_WIDGET_IS_SENSITIVE(&w->pub);
    if (has_element(w) && (win = toplevel_of(w)) != NULL && win->top->ui)
        olui_set_disabled(win->top->ui, w->el, !on);
    for (c = w->first; c; c = c->next) push_sensitive(c, on);
}

void gtk_widget_set_sensitive(GtkWidget *widget, gboolean sensitive)
{
    gtkc_widget *w = GTKC(widget);
    if (!w) return;
    if (sensitive) widget->flags |= GTK_SENSITIVE;
    else widget->flags &= ~(guint32)GTK_SENSITIVE;
    push_sensitive(w, widget->parent ? GTK_WIDGET_IS_SENSITIVE(widget->parent) : 1);
}

void gtk_widget_set_usize(GtkWidget *widget, gint width, gint height)
{
    gtkc_widget *w = GTKC(widget);
    if (!w) return;
    w->usize_w = width;
    w->usize_h = height;
    if (w->top) {
        if (width > 0) w->top->default_w = width;
        if (height > 0) w->top->default_h = height;
    } else {
        changed(w);
    }
}

void gtk_widget_grab_focus(GtkWidget *widget)
{
    gtkc_widget *w = GTKC(widget), *win = toplevel_of(w);
    if (w && win && win->top->ui && w->el) olui_focus(win->top->ui, w->el);
}

void gtk_widget_grab_default(GtkWidget *widget)
{
    if (widget) widget->flags |= GTK_HAS_DEFAULT;
}

gboolean gtk_widget_is_focus(GtkWidget *widget)
{
    gtkc_widget *w = GTKC(widget), *win = toplevel_of(w);
    return w && win && win->top->ui && w->el && olui_focused(win->top->ui) == w->el;
}

GtkWidget *gtk_widget_get_toplevel(GtkWidget *widget)
{
    while (widget && widget->parent) widget = widget->parent;
    return widget;
}

void gtk_widget_set_name(GtkWidget *widget, const gchar *name)
{
    if (!widget) return;
    free(GTKC(widget)->name);
    GTKC(widget)->name = g_strdup(name);
}

const gchar *gtk_widget_get_name(GtkWidget *widget) { return widget ? GTKC(widget)->name : NULL; }
void gtk_widget_set_events(GtkWidget *widget, gint events) { if (widget) GTKC(widget)->events = events; }
void gtk_widget_add_events(GtkWidget *widget, gint events) { if (widget) GTKC(widget)->events |= events; }
gint gtk_widget_get_events(GtkWidget *widget) { return widget ? GTKC(widget)->events : 0; }

/* ---- Labels and buttons -------------------------------------------------------------- */

static olui *ui_of(gtkc_widget *w)
{
    gtkc_widget *win = toplevel_of(w);
    return win ? win->top->ui : NULL;
}

/* 0 to 1 as per mille, read from the float's bits: the layer does no
 * floating point (a soft-float build has no float routines linked in) */
static int per_mille(gfloat f)
{
    union { gfloat f; guint32 u; } v;
    int e;
    guint32 m;
    v.f = f;
    if (v.u & 0x80000000u) return 0;
    e = (int)((v.u >> 23) & 0xff) - 127;
    if (e >= 0) return 1000;
    if (e < -11) return 0;                  /* below 0.0005 */
    m = (v.u & 0x7fffffu) | 0x800000u;      /* the value is m / 2^(23 - e) */
    return (int)((((m >> 8) * 2000u) >> (15 - e)) + 1) / 2;
}

void gtk_misc_set_alignment(GtkMisc *misc, gfloat xalign, gfloat yalign)
{
    gtkc_widget *w = GTKC(misc);
    (void)yalign;
    if (!w) return;
    w->xalign = per_mille(xalign);
    changed(w);
}

void gtk_misc_set_padding(GtkMisc *misc, gint xpad, gint ypad)
{
    gtkc_widget *w = GTKC(misc);
    if (!w) return;
    w->misc_xpad = xpad > 0 ? xpad : 0;
    w->misc_ypad = ypad > 0 ? ypad : 0;
    changed(w);
}

GtkWidget *gtk_label_new(const gchar *text)
{
    gtkc_widget *w = widget_new(GTKC_LABEL);
    w->text = g_strdup(text ? text : "");
    return &w->pub;
}

void gtk_label_set_text(GtkLabel *label, const gchar *text)
{
    gtkc_widget *w = GTKC(label);
    if (!w) return;
    if (w->text && text && !strcmp(w->text, text)) return;
    free(w->text);
    w->text = g_strdup(text ? text : "");
    if (strchr(w->text, '\n')) changed(w);     /* lines are <br>s: built again */
    else if (w->el && ui_of(w)) olui_set_text(ui_of(w), w->el, w->text);
}

void gtk_label_get(GtkLabel *label, gchar **text)
{
    if (text) *text = label ? GTKC(label)->text : NULL;
}

const gchar *gtk_label_get_text(GtkLabel *label) { return label ? GTKC(label)->text : NULL; }

void gtk_label_set_justify(GtkLabel *label, GtkJustification justify)
{
    if (!label) return;
    GTKC(label)->justify = (int)justify;
    changed(GTKC(label));
}

void gtk_label_set_line_wrap(GtkLabel *label, gboolean wrap)
{
    if (label) GTKC(label)->wrap = wrap ? 1 : 0;
}

static GtkWidget *button_with(int kind, const gchar *label)
{
    gtkc_widget *b = widget_new(kind);
    b->pub.flags |= GTK_CAN_FOCUS;
    if (label) {
        GtkWidget *l = gtk_label_new(label);
        if (kind != GTKC_BUTTON && kind != GTKC_TOGGLE) GTKC(l)->xalign = 0;
        gtk_container_add(&b->pub, l);
        gtk_widget_show(l);
    }
    return &b->pub;
}

GtkWidget *gtk_button_new(void) { return button_with(GTKC_BUTTON, NULL); }
GtkWidget *gtk_button_new_with_label(const gchar *label) { return button_with(GTKC_BUTTON, label); }

void gtk_button_set_label(GtkButton *button, const gchar *label)
{
    if (!button) return;
    if (button->child && GTKC(button->child)->kind == GTKC_LABEL) {
        gtk_label_set_text(button->child, label);
    } else if (!button->child) {
        GtkWidget *l = gtk_label_new(label);
        gtk_container_add(button, l);
        gtk_widget_show(l);
    }
}

const gchar *gtk_button_get_label(GtkButton *button)
{
    return button && button->child && GTKC(button->child)->kind == GTKC_LABEL ? GTKC(button->child)->text : NULL;
}

static void set_active(gtkc_widget *w, int active, int send);

void gtk_button_clicked(GtkButton *button)
{
    gtkc_widget *w = GTKC(button);
    if (!w || !GTK_WIDGET_IS_SENSITIVE(button)) return;
    gtk_widget_ref(button);
    if (w->kind == GTKC_TOGGLE || w->kind == GTKC_CHECK) set_active(w, !button->active, 1);
    else if (w->kind == GTKC_RADIO) set_active(w, 1, 1);
    emit(w, "clicked", NULL);
    gtk_widget_unref(button);
}

GtkWidget *gtk_toggle_button_new(void) { return button_with(GTKC_TOGGLE, NULL); }
GtkWidget *gtk_toggle_button_new_with_label(const gchar *label) { return button_with(GTKC_TOGGLE, label); }
GtkWidget *gtk_check_button_new(void) { return button_with(GTKC_CHECK, NULL); }
GtkWidget *gtk_check_button_new_with_label(const gchar *label) { return button_with(GTKC_CHECK, label); }

/* A toggle's state to active, on its element too, and "toggled" if it
 * changed (send); a radio button turned on turns the one that was off. */
static void set_active(gtkc_widget *w, int active, int send)
{
    active = active ? 1 : 0;
    if (w->kind == GTKC_RADIO && !active) {
        /* a radio button is turned off by turning another on */
        return;
    }
    if (w->pub.active == active) return;
    gtk_widget_ref(&w->pub);
    if (w->kind == GTKC_RADIO) {
        /* a copy, each kept: a handler of "toggled" may destroy any of them */
        GSList *p, *group = NULL;
        for (p = w->group; p; p = p->next) {
            group = g_slist_prepend(group, p->data);
            gtk_widget_ref(&GTKC(p->data)->pub);
        }
        for (p = group; p; p = p->next) {
            gtkc_widget *o = GTKC(p->data);
            if (o != w && o->pub.active && !(o->pub.flags & GTK_DESTROYED)) {
                o->pub.active = FALSE;
                if (has_element(o) && ui_of(o)) olui_set_checked(ui_of(o), o->el, 0);
                if (send) emit(o, "toggled", NULL);
            }
        }
        for (p = group; p; p = p->next) gtk_widget_unref(&GTKC(p->data)->pub);
        g_slist_free(group);
    }
    w->pub.active = active;
    if (has_element(w) && ui_of(w)) olui_set_checked(ui_of(w), w->el, active);
    if (send) emit(w, "toggled", NULL);
    gtk_widget_unref(&w->pub);
}

void gtk_toggle_button_set_active(GtkToggleButton *button, gboolean active)
{
    if (button) set_active(GTKC(button), active, 1);
}

gboolean gtk_toggle_button_get_active(GtkToggleButton *button) { return button ? button->active : FALSE; }
void gtk_toggle_button_toggled(GtkToggleButton *button) { if (button) emit(GTKC(button), "toggled", NULL); }

GtkWidget *gtk_radio_button_new_with_label(GSList *group, const gchar *label)
{
    gtkc_widget *r = GTKC(button_with(GTKC_RADIO, label));
    GSList *p;
    if (group) {
        gtkc_widget *member = GTKC(group->data);
        r->group_id = member->group_id;
        r->group = g_slist_prepend(member->group, r);
    } else {
        r->group_id = r->id;
        r->group = g_slist_prepend(NULL, r);
        r->pub.active = TRUE;   /* a group's first button is on */
    }
    for (p = r->group; p; p = p->next) GTKC(p->data)->group = r->group;
    return &r->pub;
}

GtkWidget *gtk_radio_button_new(GSList *group) { return gtk_radio_button_new_with_label(group, NULL); }

GtkWidget *gtk_radio_button_new_with_label_from_widget(GtkRadioButton *member, const gchar *label)
{
    return gtk_radio_button_new_with_label(member ? GTKC(member)->group : NULL, label);
}

GSList *gtk_radio_button_group(GtkRadioButton *button) { return button ? GTKC(button)->group : NULL; }

/* ---- Entries --------------------------------------------------------------------------- */

GtkWidget *gtk_entry_new(void)
{
    gtkc_widget *e = widget_new(GTKC_ENTRY);
    e->pub.flags |= GTK_CAN_FOCUS;
    e->text = g_strdup("");
    return &e->pub;
}

GtkWidget *gtk_entry_new_with_max_length(guint16 max)
{
    GtkWidget *e = gtk_entry_new();
    GTKC(e)->max_length = max;
    return e;
}

static void entry_text(gtkc_widget *e, const gchar *text, int to_element)
{
    char *t;
    size_t n = strlen(text ? text : "");
    if (e->max_length > 0 && n > (size_t)e->max_length) n = (size_t)e->max_length;
    t = g_strndup(text ? text : "", n);
    free(e->text);
    e->text = t;
    if (to_element && e->el && ui_of(e)) olui_set_text(ui_of(e), e->el, e->text);
}

void gtk_entry_set_text(GtkEntry *entry, const gchar *text)
{
    gtkc_widget *e = GTKC(entry);
    if (!e || e->kind != GTKC_ENTRY) return;
    if (e->text && text && !strcmp(e->text, text)) return;
    entry_text(e, text, 1);
    emit(e, "changed", NULL);
}

void gtk_entry_append_text(GtkEntry *entry, const gchar *text)
{
    gchar *t;
    if (!entry || !text) return;
    t = g_strconcat(GTKC(entry)->text ? GTKC(entry)->text : "", text, NULL);
    gtk_entry_set_text(entry, t);
    free(t);
}

gchar *gtk_entry_get_text(GtkEntry *entry)
{
    return entry ? GTKC(entry)->text : NULL;
}

void gtk_entry_set_max_length(GtkEntry *entry, guint16 max)
{
    if (!entry) return;
    GTKC(entry)->max_length = max;
    changed(GTKC(entry));
}

void gtk_entry_set_visibility(GtkEntry *entry, gboolean visible)
{
    if (!entry) return;
    GTKC(entry)->invisible = visible ? 0 : 1;
    changed(GTKC(entry));
}

void gtk_entry_set_editable(GtkEntry *entry, gboolean editable)
{
    if (!entry) return;
    GTKC(entry)->readonly = editable ? 0 : 1;
    changed(GTKC(entry));
}

/* a character's offset in bytes (UTF-8) */
static size_t byte_of(const char *s, gint chars)
{
    size_t i = 0;
    if (chars < 0) return strlen(s);
    while (s[i] && chars > 0) {
        ++i;
        while (((unsigned char)s[i] & 0xc0u) == 0x80u) ++i;
        --chars;
    }
    return i;
}

void gtk_editable_select_region(GtkEditable *editable, gint start, gint end)
{
    gtkc_widget *e = GTKC(editable);
    if (!e || !e->text || !e->el || !ui_of(e)) return;
    olui_set_selection(ui_of(e), e->el, byte_of(e->text, start), byte_of(e->text, end));
}

gchar *gtk_editable_get_chars(GtkEditable *editable, gint start, gint end)
{
    gtkc_widget *e = GTKC(editable);
    size_t from, to;
    if (!e || !e->text) return g_strdup("");
    from = byte_of(e->text, start);
    to = byte_of(e->text, end);
    return to > from ? g_strndup(e->text + from, to - from) : g_strdup("");
}

/* ---- Frames and separators --------------------------------------------------------------- */

GtkWidget *gtk_frame_new(const gchar *label)
{
    gtkc_widget *f = widget_new(GTKC_FRAME);
    f->text = label ? g_strdup(label) : NULL;
    return &f->pub;
}

void gtk_frame_set_label(GtkFrame *frame, const gchar *label)
{
    if (!frame) return;
    free(GTKC(frame)->text);
    GTKC(frame)->text = label ? g_strdup(label) : NULL;
    changed(GTKC(frame));
}

void gtk_frame_set_shadow_type(GtkFrame *frame, GtkShadowType type)
{
    (void)frame;
    (void)type;
}

GtkWidget *gtk_hseparator_new(void) { return &widget_new(GTKC_HSEP)->pub; }
GtkWidget *gtk_vseparator_new(void) { return &widget_new(GTKC_VSEP)->pub; }

/* ---- Sizes: what the widgets want, before a window is open ----------------------------- */

static int text_width(const char *s)
{
    int line = 0, widest = 0;
    if (!s) return 0;
    for (; *s; ++s) {
        if (*s == '\n') {
            line = 0;
            continue;
        }
        if (((unsigned char)*s & 0xc0u) != 0x80u && ++line > widest) widest = line;
    }
    return widest * gtkc_host_char_width();
}

static int want_width(gtkc_widget *w);

/* A table's columns: each one's width (that of its widest one-column
 * cell; a cell over several makes them wide enough between them) and
 * whether it grows (a cell in it has GTK_EXPAND). */
static void table_columns(gtkc_widget *t, int *width, int *grows)
{
    gtkc_widget *c;
    unsigned i;
    for (i = 0; i < t->cols; ++i) width[i] = grows[i] = 0;
    for (c = t->first; c; c = c->next) {
        if (!(c->pub.flags & GTK_VISIBLE)) continue;
        if (c->xopt & GTK_EXPAND)
            for (i = c->col0; i < c->col1; ++i) grows[i] = 1;
        if (c->col1 == c->col0 + 1) {
            int need = want_width(c) + 2 * (int)c->xpad;
            if (need > width[c->col0]) width[c->col0] = need;
        }
    }
    for (c = t->first; c; c = c->next) {
        int need, have = 0, span = (int)(c->col1 - c->col0);
        if (!(c->pub.flags & GTK_VISIBLE) || span < 2) continue;
        need = want_width(c) + 2 * (int)c->xpad - (span - 1) * (int)t->col_spacing;
        for (i = c->col0; i < c->col1; ++i) have += width[i];
        if (need > have)
            for (i = c->col0; i < c->col1; ++i) width[i] += (need - have + span - 1) / span;
    }
    if (t->homogeneous) {
        int widest = 0;
        for (i = 0; i < t->cols; ++i) widest = MAX(widest, width[i]);
        for (i = 0; i < t->cols; ++i) {
            width[i] = widest;
            grows[i] = 1;
        }
    }
}

static int want_width(gtkc_widget *w)
{
    gtkc_widget *c;
    int n = 0, sum = 0, widest = 0, inner = 0;
    if (w->usize_w > 0) return w->usize_w;
    for (c = w->first; c; c = c->next) {
        int cw;
        if (!(c->pub.flags & GTK_VISIBLE)) continue;
        cw = want_width(c) + (w->kind == GTKC_HBOX ? 2 * c->padding : 0);
        sum += cw;
        widest = MAX(widest, cw);
        ++n;
    }
    switch (w->kind) {
    case GTKC_LABEL: inner = text_width(w->text) + 2 * w->misc_xpad + 2; break;
    case GTKC_BUTTON: case GTKC_TOGGLE: inner = widest + 24; break;
    case GTKC_CHECK: case GTKC_RADIO: inner = widest + 28; break;
    case GTKC_ENTRY: inner = 20 * gtkc_host_char_width(); break;
    case GTKC_VSEP: inner = 4; break;
    case GTKC_HSEP: inner = 0; break;
    case GTKC_FRAME: inner = MAX(widest + 20, text_width(w->text) + 32); break;
    case GTKC_HBOX:
        inner = (w->homogeneous ? widest * n : sum) + (n > 1 ? (n - 1) * w->spacing : 0);
        break;
    case GTKC_TABLE: {
        int *width = g_new0(int, w->cols * 2);
        unsigned i;
        table_columns(w, width, width + w->cols);
        for (i = 0; i < w->cols; ++i) inner += width[i];
        inner += (int)(w->cols - 1) * (int)w->col_spacing;
        free(width);
        break;
    }
    default: inner = widest; break;     /* a vbox, a window */
    }
    return inner + 2 * w->border;
}

/* ---- Markup: the widgets made into olui's -------------------------------------------------- */

typedef struct {
    char *s;
    size_t len, room;
} buf;

static void put(buf *b, const char *s, size_t n)
{
    if (b->len + n + 1 > b->room) {
        b->room = (b->len + n + 1) * 2;
        b->s = g_realloc(b->s, b->room);
    }
    memcpy(b->s + b->len, s, n);
    b->len += n;
    b->s[b->len] = 0;
}

static void puts_(buf *b, const char *s) { put(b, s, strlen(s)); }

static void putf(buf *b, const char *fmt, ...) G_GNUC_PRINTF(2, 3);
static void putf(buf *b, const char *fmt, ...)
{
    char tmp[160];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) put(b, tmp, (size_t)MIN(n, (int)sizeof(tmp) - 1));
}

/* text, its <, &, quotes made entities; lines made <br> (lines) */
static void put_text(buf *b, const char *s, int lines)
{
    for (; s && *s; ++s) {
        switch (*s) {
        case '<': puts_(b, "&lt;"); break;
        case '>': puts_(b, "&gt;"); break;
        case '&': puts_(b, "&amp;"); break;
        case '"': puts_(b, "&quot;"); break;
        case '\'': puts_(b, "&#39;"); break;
        case '\n': puts_(b, lines ? "<br>" : " "); break;
        default: put(b, s, 1); break;
        }
    }
}

/* id, hidden, disabled and a style: what every element has */
static void put_common(buf *b, gtkc_widget *w, const char *style)
{
    putf(b, " id=g%u", w->id);
    if (!(w->pub.flags & GTK_VISIBLE)) puts_(b, " hidden");
    if (!GTK_WIDGET_IS_SENSITIVE(&w->pub)) puts_(b, " disabled");
    if (style && *style) {
        puts_(b, " style=\"");
        puts_(b, style);
        puts_(b, "\"");
    }
}

static const char *label_text(gtkc_widget *w)
{
    gtkc_widget *c;
    if (w->kind == GTKC_LABEL) return w->text;
    for (c = w->first; c; c = c->next) {
        const char *t = label_text(c);
        if (t) return t;
    }
    return NULL;
}

static void put_widget(buf *b, gtkc_widget *w, const char *outer);

/* style: what the parent's packing says, then the widget's own size */
static void style_for(char *style, size_t room, gtkc_widget *w, const char *outer)
{
    size_t n;
    snprintf(style, room, "%s", outer ? outer : "");
    n = strlen(style);
    if (w->usize_w > 0 && n < room) n += (size_t)snprintf(style + n, room - n, "width:%dpx;", w->usize_w);
    if (w->usize_h > 0 && n < room) n += (size_t)snprintf(style + n, room - n, "height:%dpx;", w->usize_h);
    if (w->border > 0 && n < room) snprintf(style + n, room - n, "padding:%dpx;", w->border);
}

/* a box's child's share (expand, homogeneous) and padding, along the box */
static void packed_style(char *style, size_t room, const gtkc_widget *box, const gtkc_widget *c)
{
    size_t n;
    snprintf(style, room, "%s", (c->expand || box->homogeneous) ? "flex:1;" : "");
    n = strlen(style);
    if (c->padding)
        snprintf(style + n, room - n,
                 box->kind == GTKC_HBOX ? "margin-left:%dpx;margin-right:%dpx;" : "margin-top:%dpx;margin-bottom:%dpx;",
                 c->padding, c->padding);
}

/* on the heap, as these recurse once for each widget in another */
#define STYLE_ROOM 256
#define INNER_ROOM 320

static void put_box(buf *b, gtkc_widget *w, const char *style)
{
    int across = w->kind == GTKC_HBOX, grows = 0, ends = 0;
    gtkc_widget *c;
    char *inner = g_new(char, INNER_ROOM);
    for (c = w->first; c; c = c->next) {
        if (c->expand || w->homogeneous) grows = 1;
        if (c->pack_end) ends = 1;
    }
    putf(b, "<%s", across ? "row" : "column");
    snprintf(inner, INNER_ROOM, "%sgap:%dpx;", style, w->spacing);
    put_common(b, w, inner);
    puts_(b, ">");
    /* pack_start's in order, then pack_end's from the end back */
    for (c = w->first; c; c = c->next)
        if (!c->pack_end) {
            packed_style(inner, INNER_ROOM, w, c);
            put_widget(b, c, inner);
        }
    if (ends && !grows) puts_(b, "<spacer></spacer>");
    for (c = w->last; c; c = c->prev)
        if (c->pack_end) {
            packed_style(inner, INNER_ROOM, w, c);
            put_widget(b, c, inner);
        }
    putf(b, "</%s>", across ? "row" : "column");
    g_free(inner);
}

/* A table is a column of rows: a cell is as wide as its columns (those
 * that don't grow their widest cell's width, those that do share the rest
 * alike), so the columns line up; a cell over several rows is in its top
 * one. */
static void put_table(buf *b, gtkc_widget *t, const char *style)
{
    int *width = g_new0(int, t->cols * 2), *grows = width + t->cols;
    unsigned r, col, i;
    char *inner = g_new(char, INNER_ROOM);
    table_columns(t, width, grows);
    puts_(b, "<column");
    snprintf(inner, INNER_ROOM, "%sgap:%upx;", style, t->row_spacing);
    put_common(b, t, inner);
    puts_(b, ">");
    for (r = 0; r < t->rows; ++r) {
        gtkc_widget *c;
        int row_grows = 0;
        for (c = t->first; c; c = c->next)
            if (c->row0 == r && (c->yopt & GTK_EXPAND)) row_grows = 1;
        putf(b, "<row style=\"gap:%upx;%s\">", t->col_spacing, row_grows ? "flex:1;" : "");
        for (col = 0; col < t->cols;) {
            gtkc_widget *cell = NULL;
            unsigned end = col + 1;
            int fixed = 0, share = 0;
            for (c = t->first; c; c = c->next)
                if (c->col0 <= col && col < c->col1 && c->row0 <= r && r < c->row1) {
                    cell = c;
                    break;
                }
            if (cell) end = cell->col1;
            for (i = col; i < end; ++i) {
                if (grows[i]) ++share;
                else fixed += width[i];
            }
            fixed += (int)(end - col - 1) * (int)t->col_spacing;
            snprintf(inner, INNER_ROOM, "width:%dpx;flex:%d;", fixed, share);
            if (cell && cell->row0 == r && cell->col0 == col) {
                size_t n = strlen(inner);
                if (cell->xpad || cell->ypad)
                    snprintf(inner + n, INNER_ROOM - n, "margin:%upx %upx;", cell->ypad, cell->xpad);
                put_widget(b, cell, inner);
            } else {
                putf(b, "<div style=\"%s\"></div>", inner);
            }
            col = end;
        }
        puts_(b, "</row>");
    }
    puts_(b, "</column>");
    free(width);
    g_free(inner);
}

static void put_widget(buf *b, gtkc_widget *w, const char *outer)
{
    char *style = g_new(char, STYLE_ROOM);
    const char *t;
    style_for(style, STYLE_ROOM, w, outer);
    switch (w->kind) {
    case GTKC_VBOX: case GTKC_HBOX: put_box(b, w, style); break;
    case GTKC_TABLE: put_table(b, w, style); break;
    case GTKC_LABEL: {
        size_t n = strlen(style);
        const char *align = w->xalign < 250 ? "left" : w->xalign > 750 ? "right" : "center";
        if (w->justify == GTK_JUSTIFY_RIGHT && strchr(w->text, '\n')) align = "right";
        snprintf(style + n, STYLE_ROOM - n, "text-align:%s;", align);
        n = strlen(style);
        if ((w->misc_xpad || w->misc_ypad) && n < STYLE_ROOM)
            snprintf(style + n, STYLE_ROOM - n, "padding:%dpx %dpx;", w->misc_ypad, w->misc_xpad);
        puts_(b, "<label");
        put_common(b, w, style);
        puts_(b, ">");
        put_text(b, w->text, 1);
        puts_(b, "</label>");
        break;
    }
    case GTKC_BUTTON: case GTKC_TOGGLE:
        puts_(b, "<button");
        put_common(b, w, style);
        if (w->kind == GTKC_TOGGLE && w->pub.active) puts_(b, " checked");
        puts_(b, ">");
        put_text(b, label_text(w), 0);
        puts_(b, "</button>");
        break;
    case GTKC_CHECK: case GTKC_RADIO:
        puts_(b, w->kind == GTKC_CHECK ? "<checkbox" : "<radio");
        put_common(b, w, style);
        if (w->kind == GTKC_RADIO) putf(b, " name=r%u value=g%u", w->group_id, w->id);
        if (w->pub.active) puts_(b, " checked");
        puts_(b, ">");
        put_text(b, label_text(w), 0);
        puts_(b, w->kind == GTKC_CHECK ? "</checkbox>" : "</radio>");
        break;
    case GTKC_ENTRY:
        puts_(b, "<input");
        put_common(b, w, style);
        if (w->invisible) puts_(b, " type=password");
        if (w->max_length > 0) putf(b, " maxlength=%d", w->max_length);
        if (w->readonly) puts_(b, " readonly");
        puts_(b, " value=\"");
        put_text(b, w->text, 0);
        puts_(b, "\">");
        break;
    case GTKC_FRAME:
        puts_(b, "<fieldset");
        put_common(b, w, style);
        puts_(b, ">");
        if (w->text && *w->text) {
            puts_(b, "<legend>");
            put_text(b, w->text, 0);
            puts_(b, "</legend>");
        }
        if (w->first) put_widget(b, w->first, "flex:1;");
        puts_(b, "</fieldset>");
        break;
    case GTKC_HSEP:
        puts_(b, "<hr");
        put_common(b, w, style);
        puts_(b, ">");
        break;
    case GTKC_VSEP: {
        size_t n = strlen(style);
        snprintf(style + n, STYLE_ROOM - n, "width:0;border-left:1px solid #808080;margin:0 2px;");
        puts_(b, "<div");
        put_common(b, w, style);
        puts_(b, "></div>");
        break;
    }
    case GTKC_WINDOW:
        puts_(b, "<column");
        put_common(b, w, style);
        puts_(b, ">");
        if (w->first) put_widget(b, w->first, "flex:1;");
        puts_(b, "</column>");
        break;
    default:
        break;
    }
    (void)t;
    g_free(style);
}

/* Elements found again after a build: each widget's, and olui's back to it */
static void bind(olui *ui, gtkc_widget *w, olui_el *parent_el)
{
    gtkc_widget *c;
    char id[16];
    if (w->alias) {
        w->el = parent_el;
    } else {
        snprintf(id, sizeof(id), "g%u", w->id);
        w->el = olui_find(ui, id);
        if (w->el) olui_set_user(w->el, w);
    }
    for (c = w->first; c; c = c->next) bind(ui, c, w->el);
}

static void unbind(gtkc_widget *w)
{
    gtkc_widget *c;
    w->el = NULL;
    for (c = w->first; c; c = c->next) unbind(c);
}

/* ---- olui's events as GTK's signals --------------------------------------------------------- */

static void dispatch(olui *ui, const olui_event *ev, void *userdata)
{
    gtkc_widget *w = ev->el ? olui_user(ev->el) : NULL;
    (void)ui;
    (void)userdata;
    if (!w || (w->pub.flags & GTK_DESTROYED) || w->alias) return;
    gtk_widget_ref(&w->pub);
    switch (w->kind) {
    case GTKC_BUTTON:
        if (ev->type == OLUI_CLICK) emit(w, "clicked", NULL);
        break;
    case GTKC_TOGGLE:
        if (ev->type == OLUI_CLICK) {
            set_active(w, !w->pub.active, 1);
            emit(w, "clicked", NULL);
        }
        break;
    case GTKC_CHECK:
        if (ev->type == OLUI_CHANGE && (ev->checked ? 1 : 0) != w->pub.active) {
            set_active(w, ev->checked, 1);
            emit(w, "clicked", NULL);
        }
        break;
    case GTKC_RADIO:
        if (ev->type == OLUI_CHANGE && ev->checked && !w->pub.active) {
            set_active(w, 1, 1);
            emit(w, "clicked", NULL);
        }
        break;
    case GTKC_ENTRY:
        if (ev->type == OLUI_CHANGE) {
            entry_text(w, olui_get_text(ev->el), 0);
            emit(w, "changed", NULL);
        } else if (ev->type == OLUI_SUBMIT) {
            emit(w, "activate", NULL);
        }
        break;
    default:
        break;
    }
    gtk_widget_unref(&w->pub);
}

/* ---- Windows: opened, built, closed ------------------------------------------------------- */

static void build(gtkc_widget *win)
{
    gtkc_top *top = win->top;
    buf b = {NULL, 0, 0};
    char err[160];
    olui_el *focus;
    gtkc_widget *focused = NULL;
    if (!top->ui) return;
    put_widget(&b, win, "flex:1;");
    focus = olui_focused(top->ui);
    if (focus) focused = olui_user(focus);
    if (focused) gtk_widget_ref(&focused->pub);
    unbind(win);
    if (!olui_load(top->ui, b.s ? b.s : "", NULL, err, sizeof(err)))
        g_warning("GFOpenGTK: window %s: %s", win->text ? win->text : "", err);
    free(top->markup);
    top->markup = b.s;
    bind(top->ui, win, NULL);
    if (focused) {
        if (focused->el && !(focused->pub.flags & GTK_DESTROYED)) olui_focus(top->ui, focused->el);
        gtk_widget_unref(&focused->pub);
    }
    top->dirty = 0;
}

static void walk_bottom(olui *ui, gtkc_widget *w, int *bottom)
{
    gtkc_widget *c;
    olui_rect r;
    if (w->el && !w->alias && olui_box(ui, w->el, &r) && r.y + r.h > *bottom) *bottom = r.y + r.h;
    for (c = w->first; c; c = c->next) walk_bottom(ui, c, bottom);
}

void gtkc_natural_size(gtkc_widget *win, int *w, int *h)
{
    gtkc_top *top = win->top;
    int bottom = 0, root_pad = 0;
    olui_rect r;
    *w = top->default_w > 0 ? top->default_w : want_width(win) + 16;
    if (*w < 120) *w = 120;
    if (top->default_h > 0) {
        *h = top->default_h;
        return;
    }
    /* laid out as short as can be, what it holds hangs out of the bottom as tall as it wants */
    olui_layout(top->ui, *w, 1);
    if (win->el && olui_box(top->ui, win->el, &r)) root_pad = r.y;
    walk_bottom(top->ui, win, &bottom);
    *h = bottom + win->border + root_pad;
    if (*h < 24) *h = 24;
}

static void open_window(gtkc_widget *win)
{
    gtkc_top *top = win->top;
    GSList *p;
    int w, h;
    if (!initialised) gtk_init(NULL, NULL);
    top->ui = olui_new();
    if (!top->ui) {
        g_warning("GFOpenGTK: no memory for a window");
        return;
    }
    olui_on(top->ui, NULL, 0, dispatch, NULL);
    w = top->default_w > 0 ? top->default_w : want_width(win) + 16;
    h = top->default_h > 0 ? top->default_h : 100;
    if (!gtkc_host_open(win, w, h)) {
        olui_free(top->ui);
        top->ui = NULL;
        g_warning("GFOpenGTK: the window \"%s\" didn't open", win->text ? win->text : "");
        return;
    }
    /* the program's CSS after the host's theme, in the order given */
    for (p = program_css; p; p = p->next) olui_add_css(top->ui, p->data);
    top->open = 1;
    win->pub.flags |= GTK_REALIZED | GTK_MAPPED;
    win->pub.window = (GdkWindow *)top;
    build(win);
    gtkc_natural_size(win, &w, &h);
    gtkc_host_size(win, w, h);
    if (top->modal) gtk_grab_add(&win->pub);
    emit(win, "show", NULL);
}

static void do_close(gtkc_widget *win)
{
    gtkc_top *top = win->top;
    if (!top->open) return;
    gtkc_host_close(win);
    unbind(win);
    olui_free(top->ui);
    top->ui = NULL;
    top->open = 0;
    win->pub.window = NULL;
    win->pub.flags &= ~(guint32)(GTK_REALIZED | GTK_MAPPED);
}

static void close_window(gtkc_widget *win)
{
    if (!win->top->open) return;
    if (win->top->busy) {
        if (!g_list_find(closing, win)) {
            gtk_widget_ref(&win->pub);
            closing = g_list_append(closing, win);
        }
        return;
    }
    do_close(win);
}


void gtkc_flush(void)
{
    GList *p, *next;
    for (p = closing; p; p = next) {
        gtkc_widget *win = p->data;
        next = p->next;
        if (win->top->busy) continue;
        closing = g_list_remove(closing, win);
        /* hidden or destroyed: closed; shown again since: kept */
        if ((win->pub.flags & GTK_DESTROYED) || !(win->pub.flags & GTK_VISIBLE)) do_close(win);
        gtk_widget_unref(&win->pub);
    }
    for (p = toplevels; p; p = p->next) {
        gtkc_widget *win = p->data;
        if (!win->top->open || win->top->busy) continue;
        if (win->top->dirty) {
            build(win);
            gtkc_host_layout(win);
        } else {
            gtkc_host_draw(win);
        }
    }
}

/* ---- Input ------------------------------------------------------------------------------- */

void gtkc_enter(gtkc_widget *win)
{
    gtk_widget_ref(&win->pub);
    ++win->top->busy;
}

void gtkc_leave(gtkc_widget *win)
{
    --win->top->busy;
    gtk_widget_unref(&win->pub);
}

gboolean gtkc_takes_input(gtkc_widget *win)
{
    gtkc_widget *g = grabs ? toplevel_of(GTKC(grabs->data)) : NULL;
    return !g || g == win;
}

static void key_event(GdkEventKey *k, gtkc_widget *win, guint keyval, guint state, char *text)
{
    memset(k, 0, sizeof(*k));
    k->type = GDK_KEY_PRESS;
    k->window = win->pub.window;
    k->time = gtkc_host_now();
    k->state = state;
    k->keyval = keyval;
    text[0] = keyval >= 0x20 && keyval < 0x100 ? (char)keyval : 0;
    text[1] = 0;
    k->string = text;
    k->length = text[0] ? 1 : 0;
}

gboolean gtkc_key_signal(gtkc_widget *win, guint keyval, guint state)
{
    GdkEventKey k;
    char text[2];
    olui_el *focus;
    gtkc_widget *f = NULL;
    gboolean handled;
    if (!win->top->open || !gtkc_takes_input(win)) return TRUE;
    key_event(&k, win, keyval, state, text);
    gtkc_enter(win);
    /* the window's handlers first, then those of the widget with the keyboard, as GTK sends them */
    handled = emit(win, "key_press_event", (GdkEvent *)&k);
    if (!handled && win->top->ui && (focus = olui_focused(win->top->ui)) != NULL &&
        (f = olui_user(focus)) != NULL && f != win)
        handled = emit(f, "key_press_event", (GdkEvent *)&k);
    gtkc_leave(win);
    return handled;
}

void gtkc_close_signal(gtkc_widget *win)
{
    GdkEventAny e;
    if (!gtkc_takes_input(win)) return;
    memset(&e, 0, sizeof(e));
    e.type = GDK_DELETE;
    e.window = win->pub.window;
    gtkc_enter(win);
    if (!emit(win, "delete_event", (GdkEvent *)&e)) gtk_widget_destroy(&win->pub);
    gtkc_leave(win);
}

static unsigned olui_code(guint keyval)
{
    switch (keyval) {
    case GDK_Return: case GDK_KP_Enter: return OLUI_KEY_RETURN;
    case GDK_Escape: return OLUI_KEY_ESCAPE;
    case GDK_Tab: return OLUI_KEY_TAB;
    case GDK_BackSpace: return OLUI_KEY_BACKSPACE;
    case GDK_Delete: return OLUI_KEY_DELETE;
    case GDK_Left: return OLUI_KEY_LEFT;
    case GDK_Right: return OLUI_KEY_RIGHT;
    case GDK_Up: return OLUI_KEY_UP;
    case GDK_Down: return OLUI_KEY_DOWN;
    case GDK_Home: return OLUI_KEY_HOME;
    case GDK_End: return OLUI_KEY_END;
    case GDK_Page_Up: return OLUI_KEY_PAGEUP;
    case GDK_Page_Down: return OLUI_KEY_PAGEDOWN;
    default: return keyval >= 0x20 && keyval < 0x100 ? keyval : 0;
    }
}

gboolean gtkc_key(GtkWidget *window, guint keyval, guint state)
{
    gtkc_widget *win = GTKC(gtk_widget_get_toplevel(window));
    unsigned code, qual = 0;
    int used = 0;
    if (!win || !win->top || !win->top->open) return FALSE;
    gtk_widget_ref(&win->pub);      /* a handler may destroy it */
    if (gtkc_key_signal(win, keyval, state)) {
        used = 1;
    } else if ((code = olui_code(keyval)) != 0 && win->top->open) {
        if (state & GDK_SHIFT_MASK) qual |= OLUI_QUAL_SHIFT;
        if (state & (GDK_CONTROL_MASK | GDK_MOD4_MASK)) qual |= OLUI_QUAL_COMMAND;
        gtkc_enter(win);
        used = olui_key(win->top->ui, code, qual);
        gtkc_leave(win);
    }
    gtkc_flush();
    gtk_widget_unref(&win->pub);
    return used ? TRUE : FALSE;
}

gboolean gtkc_click(GtkWidget *widget)
{
    gtkc_widget *w = GTKC(widget), *win = toplevel_of(w);
    olui_rect r;
    int x, y;
    if (!w || !win || !win->top->open || !gtkc_takes_input(win)) return FALSE;
    gtkc_flush();
    if (!w->el || !olui_box(win->top->ui, w->el, &r)) return FALSE;
    x = r.x + r.w / 2;
    y = r.y + r.h / 2;
    gtkc_enter(win);
    olui_mouse(win->top->ui, x, y, OLUI_MOUSE_MOVE);
    olui_mouse(win->top->ui, x, y, OLUI_MOUSE_DOWN);
    if (win->top->ui) olui_mouse(win->top->ui, x, y, OLUI_MOUSE_UP);
    gtkc_leave(win);
    gtkc_flush();
    return TRUE;
}

gboolean gtkc_type(GtkWidget *entry, const gchar *text)
{
    GtkWidget *win = gtk_widget_get_toplevel(entry);
    gtkc_flush();
    if (!GTKC(entry)->el) return FALSE;
    gtk_widget_grab_focus(entry);
    gtk_widget_ref(win);
    for (; text && *text && !(win->flags & GTK_DESTROYED); ++text) gtkc_key(win, (guchar)*text, 0);
    gtk_widget_unref(win);
    return TRUE;
}

void gtkc_window_close(GtkWidget *window)
{
    gtkc_widget *win = GTKC(gtk_widget_get_toplevel(window));
    if (!win || !win->top) return;
    gtkc_close_signal(win);
    gtkc_flush();
}

struct olui *gtkc_window_olui(GtkWidget *window)
{
    gtkc_widget *win = GTKC(gtk_widget_get_toplevel(window));
    return win && win->top ? win->top->ui : NULL;
}

struct olui_el *gtkc_widget_el(GtkWidget *widget) { return widget ? GTKC(widget)->el : NULL; }

const gchar *gtkc_window_markup(GtkWidget *window)
{
    gtkc_widget *win = GTKC(window);
    return win && win->top ? win->top->markup : NULL;
}

gboolean gtkc_window_size(GtkWidget *window, gint *width, gint *height)
{
    gtkc_widget *win = GTKC(window);
    if (!win || !win->top || !win->top->open) return FALSE;
    if (width) *width = win->top->width;
    if (height) *height = win->top->height;
    return TRUE;
}

gboolean gtkc_add_css(const gchar *css)
{
    GList *p;
    if (!css) return FALSE;
    program_css = g_slist_append(program_css, g_strdup(css));
    for (p = toplevels; p; p = p->next) {
        gtkc_widget *win = p->data;
        if (win->top->ui && !olui_add_css(win->top->ui, css)) return FALSE;
    }
    return TRUE;
}

guint32 gtkc_now(void) { return gtkc_host_now(); }

GList *gtkc_toplevels(void) { return toplevels; }

/* ---- Grabs ------------------------------------------------------------------------------- */

void gtk_grab_add(GtkWidget *widget)
{
    if (!widget || g_slist_find(grabs, widget)) return;
    grabs = g_slist_prepend(grabs, widget);
}

void gtk_grab_remove(GtkWidget *widget)
{
    grabs = g_slist_remove(grabs, widget);
}

GtkWidget *gtk_grab_get_current(void) { return grabs ? grabs->data : NULL; }

/* ---- The main loop: timeouts, idle functions, input ------------------------------------------- */

typedef struct source source;
struct source {
    source *next;
    guint id;
    int idle;
    guint interval;
    guint32 due;
    GSourceFunc fn;
    gpointer data;
    int dead;
    int running;                /* in its function: a loop run from there leaves it be */
};
static source *sources;
static guint next_source = 1;

static guint add_source(int idle, guint interval, GSourceFunc fn, gpointer data)
{
    source *s, **pp;
    if (!fn) return 0;
    if (!initialised) gtk_init(NULL, NULL);
    s = g_new0(source, 1);
    s->id = next_source++;
    s->idle = idle;
    s->interval = interval;
    s->due = gtkc_host_now() + interval;
    s->fn = fn;
    s->data = data;
    for (pp = &sources; *pp; pp = &(*pp)->next) {}
    *pp = s;
    return s->id;
}

guint g_timeout_add(guint interval, GSourceFunc fn, gpointer data) { return add_source(0, interval, fn, data); }
guint g_idle_add(GSourceFunc fn, gpointer data) { return add_source(1, 0, fn, data); }

gboolean g_source_remove(guint id)
{
    source *s;
    for (s = sources; s; s = s->next)
        if (s->id == id && !s->dead) {
            s->dead = 1;
            return TRUE;
        }
    return FALSE;
}

guint gtk_timeout_add(guint32 interval, GtkFunction fn, gpointer data)
{
    return add_source(0, interval, (GSourceFunc)fn, data);
}

void gtk_timeout_remove(guint id) { g_source_remove(id); }
guint gtk_idle_add(GtkFunction fn, gpointer data) { return add_source(1, 0, (GSourceFunc)fn, data); }
void gtk_idle_remove(guint id) { g_source_remove(id); }

static void sweep_sources(void)
{
    source **pp = &sources;
    while (*pp) {
        source *s = *pp;
        if (s->dead) {
            *pp = s->next;
            free(s);
        } else {
            pp = &s->next;
        }
    }
}

/* Runs what is due (idle: the idle functions); 1 when one ran. */
static int run_sources(int idle)
{
    static int depth;           /* a function may run a loop, with these still being walked */
    source *s;
    int ran = 0;
    guint32 now = gtkc_host_now();
    ++depth;
    for (s = sources; s; s = s->next) {
        gboolean again;
        if (s->dead || s->running || s->idle != idle || (!idle && (gint32)(now - s->due) < 0)) continue;
        ran = 1;
        s->running = 1;
        again = s->fn(s->data);
        s->running = 0;
        if (!again) s->dead = 1;
        else if (!idle) s->due = gtkc_host_now() + s->interval;
    }
    if (!--depth) sweep_sources();
    return ran;
}

/* ms until the next timeout (-1: none) */
static long next_due(void)
{
    source *s;
    long best = -1;
    guint32 now = gtkc_host_now();
    for (s = sources; s; s = s->next) {
        long d;
        if (s->dead || s->running) continue;
        if (s->idle) return 0;
        d = (gint32)(s->due - now);
        if (d < 0) d = 0;
        if (best < 0 || d < best) best = d;
    }
    return best;
}

gboolean g_main_pending(void)
{
    long d = next_due();
    return d == 0 || gtkc_host_pending();
}

gboolean g_main_iteration(gboolean may_block)
{
    int did;
    if (!initialised) gtk_init(NULL, NULL);
    gtkc_flush();
    did = run_sources(0);
    if (gtkc_host_pending()) did |= gtkc_host_wait(0) > 0;
    gtkc_flush();
    if (!did) did = run_sources(1);
    if (!did && may_block) {
        int r = gtkc_host_wait(next_due());
        if (r < 0) {
            /* nothing can ever happen: not a timeout, nor a window open */
            g_warning("GFOpenGTK: the main loop has nothing to wait for");
            gtk_main_quit();
        }
        did = run_sources(0) || r > 0;
    }
    gtkc_flush();
    return did ? TRUE : FALSE;
}

void gtk_main(void)
{
    guint level = ++main_level;
    while (quits < 1) g_main_iteration(TRUE);
    --quits;
    --main_level;
    (void)level;
}

void gtk_main_quit(void)
{
    if (main_level > quits) ++quits;
}

guint gtk_main_level(void) { return main_level; }

gboolean gtk_main_iteration_do(gboolean blocking)
{
    g_main_iteration(blocking);
    return quits > 0;
}

gboolean gtk_main_iteration(void) { return gtk_main_iteration_do(TRUE); }
gboolean gtk_events_pending(void) { return g_main_pending(); }

void gtk_exit(gint code)
{
    exit(code);
}

/* ---- Starting ------------------------------------------------------------------------------ */

static void at_exit(void)
{
    GList *p;
    /* gtk_exit from a callback, or main returning with one still up: no window outlives the task */
    for (p = toplevels; p; p = p->next) {
        gtkc_widget *win = p->data;
        win->top->busy = 0;
        do_close(win);
    }
    gtkc_host_exit();
}

gboolean gtk_init_check(int *argc, char ***argv)
{
    (void)argc;
    (void)argv;
    if (initialised) return TRUE;
    if (!gtkc_host_init()) return FALSE;
    initialised = 1;
    atexit(at_exit);
    return TRUE;
}

void gtk_init(int *argc, char ***argv)
{
    if (!gtk_init_check(argc, argv)) {
        fprintf(stderr, "GFOpenGTK: the display could not be opened\n");
        exit(20);
    }
}

gchar *gtk_set_locale(void) { return (gchar *)"C"; }

/* ---- Key names ----------------------------------------------------------------------------- */

static const struct { guint keyval; const char *name; } key_names[] = {
    {GDK_BackSpace, "BackSpace"}, {GDK_Tab, "Tab"}, {GDK_Return, "Return"}, {GDK_Pause, "Pause"},
    {GDK_Escape, "Escape"}, {GDK_Delete, "Delete"}, {GDK_Home, "Home"}, {GDK_Left, "Left"}, {GDK_Up, "Up"},
    {GDK_Right, "Right"}, {GDK_Down, "Down"}, {GDK_Page_Up, "Page_Up"}, {GDK_Page_Down, "Page_Down"},
    {GDK_End, "End"}, {GDK_Insert, "Insert"}, {GDK_Help, "Help"}, {GDK_KP_Enter, "KP_Enter"},
    {GDK_KP_Multiply, "KP_Multiply"}, {GDK_KP_Add, "KP_Add"}, {GDK_KP_Subtract, "KP_Subtract"},
    {GDK_KP_Decimal, "KP_Decimal"}, {GDK_KP_Divide, "KP_Divide"}, {GDK_KP_0, "KP_0"}, {GDK_KP_1, "KP_1"},
    {GDK_KP_2, "KP_2"}, {GDK_KP_3, "KP_3"}, {GDK_KP_4, "KP_4"}, {GDK_KP_5, "KP_5"}, {GDK_KP_6, "KP_6"},
    {GDK_KP_7, "KP_7"}, {GDK_KP_8, "KP_8"}, {GDK_KP_9, "KP_9"}, {GDK_F1, "F1"}, {GDK_F2, "F2"}, {GDK_F3, "F3"},
    {GDK_F4, "F4"}, {GDK_F5, "F5"}, {GDK_F6, "F6"}, {GDK_F7, "F7"}, {GDK_F8, "F8"}, {GDK_F9, "F9"},
    {GDK_F10, "F10"}, {GDK_Shift_L, "Shift_L"}, {GDK_Shift_R, "Shift_R"}, {GDK_Control_L, "Control_L"},
    {GDK_Control_R, "Control_R"}, {GDK_Caps_Lock, "Caps_Lock"}, {GDK_Meta_L, "Meta_L"}, {GDK_Meta_R, "Meta_R"},
    {GDK_Alt_L, "Alt_L"}, {GDK_Alt_R, "Alt_R"}, {GDK_space, "space"}, {GDK_comma, "comma"},
    {GDK_minus, "minus"}, {GDK_period, "period"}, {GDK_slash, "slash"}, {GDK_semicolon, "semicolon"},
    {GDK_equal, "equal"}, {GDK_bracketleft, "bracketleft"}, {GDK_backslash, "backslash"},
    {GDK_bracketright, "bracketright"}
};

gchar *gdk_keyval_name(guint keyval)
{
    static char chars[256][2];
    size_t i;
    for (i = 0; i < G_N_ELEMENTS(key_names); ++i)
        if (key_names[i].keyval == keyval) return (gchar *)key_names[i].name;
    if (keyval > 0x20 && keyval < 0x100) {
        chars[keyval][0] = (char)keyval;
        chars[keyval][1] = 0;
        return chars[keyval];
    }
    return NULL;
}

guint gdk_keyval_from_name(const gchar *name)
{
    size_t i;
    if (!name) return GDK_VoidSymbol;
    for (i = 0; i < G_N_ELEMENTS(key_names); ++i)
        if (!strcmp(key_names[i].name, name)) return key_names[i].keyval;
    if (name[0] && !name[1]) return (guchar)name[0];
    return GDK_VoidSymbol;
}

guint gdk_keyval_to_upper(guint keyval)
{
    if ((keyval >= 'a' && keyval <= 'z') || (keyval >= 0xe0 && keyval <= 0xfe && keyval != 0xf7)) return keyval - 0x20;
    return keyval;
}

guint gdk_keyval_to_lower(guint keyval)
{
    if ((keyval >= 'A' && keyval <= 'Z') || (keyval >= 0xc0 && keyval <= 0xde && keyval != 0xd7)) return keyval + 0x20;
    return keyval;
}
