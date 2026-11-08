/* GFOpenGTK inside: a widget's own fields, a window's, and what the core
 * (gtk.c) and a host (gtkc_host.c, gtkc_amiga.c) ask of each other.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GFOPENGTK_INTERNAL_H
#define GFOPENGTK_INTERNAL_H

#include <gtk/gtk.h>

#include "gfopenlayout_ui.h"

/* What a widget is */
enum {
    GTKC_WINDOW = 1,
    GTKC_VBOX,
    GTKC_HBOX,
    GTKC_TABLE,
    GTKC_LABEL,
    GTKC_BUTTON,
    GTKC_TOGGLE,
    GTKC_CHECK,
    GTKC_RADIO,
    GTKC_ENTRY,
    GTKC_FRAME,
    GTKC_HSEP,
    GTKC_VSEP
};

typedef struct gtkc_handler gtkc_handler;
struct gtkc_handler {
    gtkc_handler *next;
    guint id;
    char *name;                 /* '-' made '_' */
    GtkSignalFunc fn;
    gpointer data;
    GtkObject *slot;            /* connect_object's: called with it, no data */
    unsigned after : 1, swapped : 1, blocked : 1, dead : 1;
};

typedef struct gtkc_data gtkc_data;
struct gtkc_data {
    gtkc_data *next;
    char *key;
    gpointer data;
    GtkDestroyNotify destroy;
};

typedef struct gtkc_top gtkc_top;
typedef struct gtkc_widget gtkc_widget;

struct gtkc_widget {
    GtkWidget pub;
    int kind;                   /* GTKC_... */
    unsigned id;                /* its element is g<id> */
    gtkc_widget *first, *last, *next, *prev;    /* children, in the order added */
    gtkc_handler *handlers;
    int emitting;               /* signals of its being sent now */
    gtkc_data *data;
    olui_el *el;                /* its element while its window is open; a button's label has the button's */
    unsigned alias : 1;         /* el is its parent's (a button's label) */
    char *text;                 /* a label's, an entry's, a window's title, a frame's label */
    char *name;
    gint events;
    /* as a box's child */
    unsigned char expand, fill, pack_end;
    int padding;
    /* as a table's child */
    unsigned col0, col1, row0, row1, xopt, yopt, xpad, ypad;   /* left, right, top, bottom */
    /* a box's, a table's, any container's */
    int border, spacing, homogeneous;
    unsigned rows, cols, row_spacing, col_spacing;
    /* sizes and a label's look */
    int usize_w, usize_h;       /* -1: its own */
    int xalign;                 /* per mille: 0 left, 500 the middle, 1000 right */
    int misc_xpad, misc_ypad;
    int justify, wrap;
    /* an entry's */
    int max_length, invisible, readonly;
    /* a radio button's group: every member has the list */
    GSList *group;
    unsigned group_id;
    /* a window's */
    gtkc_top *top;
};

#define GTKC(w) ((gtkc_widget *)(w))

struct gtkc_top {
    olui *ui;                   /* while open */
    void *host;                 /* the host's window */
    char *markup;               /* built from last */
    int open;
    int width, height;          /* inner, while open */
    int default_w, default_h;   /* 0: its own */
    int type, position, modal, resizable;
    int dirty;                  /* its widgets changed: to be built again */
    int busy;                   /* > 0 while its olui is calling its handlers: not built or freed then */
    gtkc_widget *transient_for;
};

/* ---- The core, for the host ------------------------------------------------------------ */

/* A key's signals (key_press_event on the window, then on the widget with
 * the keyboard). TRUE: a handler took it, and olui is not to have it. */
gboolean gtkc_key_signal(gtkc_widget *win, guint keyval, guint state);
/* The close gadget: delete_event, then destroyed unless a handler said not. */
void gtkc_close_signal(gtkc_widget *win);
/* Around each olui call that may call handlers (olui_mouse, olui_key,
 * olui_tick, olui_amiga_event): the window isn't built again or freed
 * until gtkc_leave and the next gtkc_flush. */
void gtkc_enter(gtkc_widget *win);
void gtkc_leave(gtkc_widget *win);
/* FALSE while another window has the grab (a modal dialog is up). */
gboolean gtkc_takes_input(gtkc_widget *win);
/* Every window not destroyed, open or not (the core's list: not to be changed) */
GList *gtkc_toplevels(void);
/* The size a window's contents want, in pixels, its olui made and loaded. */
void gtkc_natural_size(gtkc_widget *win, int *w, int *h);

/* ---- A host, for the core --------------------------------------------------------------- */

int gtkc_host_init(void);
void gtkc_host_exit(void);
/* Opens win's window at about w by h inside and gives its olui a measure
 * and a theme (CSS before the program's). 0 when it couldn't. */
int gtkc_host_open(gtkc_widget *win, int w, int h);
/* The window made w by h inside (as near as the screen allows), laid out and drawn. */
void gtkc_host_size(gtkc_widget *win, int w, int h);
void gtkc_host_layout(gtkc_widget *win);
void gtkc_host_draw(gtkc_widget *win);
void gtkc_host_close(gtkc_widget *win);
void gtkc_host_title(gtkc_widget *win);
/* Waits up to ms (-1: until something happens) for input and hands it to
 * the windows. 1 when something came. */
int gtkc_host_wait(long ms);
int gtkc_host_pending(void);
guint32 gtkc_host_now(void);
/* About how wide a character is, in pixels, before a window is open. */
int gtkc_host_char_width(void);

#endif
