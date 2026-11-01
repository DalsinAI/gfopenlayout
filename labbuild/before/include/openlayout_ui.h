/* OpenLayout UI: programs' windows described in HTML and CSS, laid out by
 * OpenLayout and drawn in the OpenGadTools theme, with the program's own
 * C functions called back when something happens.
 *
 *     olui *ui = olui_new();
 *     olui_load(ui, "<row><button id=ok>OK</button></row>", "button{flex:1}", err, sizeof err);
 *     olui_on(ui, "ok", OLUI_CLICK, ok_pressed, data);
 *     olui_layout(ui, width, height);
 *     olui_draw(ui, NULL, my_draw, my_rastport);
 *     ... olui_mouse() and olui_key() from the window's events ...
 *
 * Plain C99 with no Amiga calls: the Amiga window glue (a RastPort drawer
 * on OpenGfx, the IDCMP loop) is openlayout_ui_amiga.h. All sizes and
 * positions here are pixels; OpenLayout's units stay inside.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OPENLAYOUT_UI_H
#define OPENLAYOUT_UI_H

#include <stddef.h>
#include <stdint.h>

#include "openlayout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLUI_VERSION "0.2"

typedef struct olui olui;
typedef struct olui_el olui_el;

/* What happened (olui_event.type) */
enum {
    OLUI_CLICK = 1,     /* a button was pressed (mouse, Return or space) */
    OLUI_CHANGE,        /* a checkbox or radio button changed, or a field's text */
    OLUI_SUBMIT,        /* Return in a field */
    OLUI_SELECT,        /* a list's selected item changed; a tab was chosen */
    OLUI_ACTIVATE,      /* a list item double-clicked, or Return on it */
    OLUI_EXPAND,        /* a tree item opened */
    OLUI_COLLAPSE,      /* and closed */
    OLUI_FOCUS,         /* a widget took the keyboard */
    OLUI_EVENT_COUNT
};

typedef struct {
    int type;           /* OLUI_... */
    olui_el *el;        /* the widget: the button, field, list, tabs */
    const char *id;     /* its id attribute ("" when it has none) */
    olui_el *item;      /* a list's item, a tabs' tab; else NULL */
    const char *value;  /* a field's text; an item's or a tab's value (its id when it has no value); a radio's value */
    int index;          /* an item's place among the visible rows, a tab's number; else -1 */
    int checked;        /* a checkbox's or radio button's state */
} olui_event;

typedef void (*olui_handler)(olui *ui, const olui_event *ev, void *userdata);

/* ---- Making a window's contents ----------------------------------------- */

olui *olui_new(void);
void olui_free(olui *ui);

/* The program's measure: text widths, ascents and descents in OpenLayout
 * units for a style (its font_size, and OL_TEXT_BOLD, OL_TEXT_MONO in
 * text_flags). The Amiga glue gives the RastPort's fonts. Without one,
 * text is measured as about 0.6 em a character. */
void olui_set_measure(olui *ui, ol_measure_text_fn measure, void *userdata);

/* CSS in the order given, after the built-in rules: the theme's
 * (olui_theme_css, or the Amiga glue's), then the program's own. 1, or 0
 * when memory ran out. */
int olui_add_css(olui *ui, const char *css);

/* The window's contents as markup (the tags are in OpenLayout-UI.md),
 * with an optional stylesheet of the program's (added as olui_add_css
 * does). Replaces what was loaded before; the CSS stays. 1, or 0 with why
 * in err. */
int olui_load(olui *ui, const char *markup, const char *css, char *err, size_t errlen);

/* A handler for an id's events of a type (0: every type); id NULL: every
 * widget's. The most recent first; each one that matches is called. */
int olui_on(olui *ui, const char *id, int type, olui_handler fn, void *userdata);

/* Pixels a CSS pixel, as a percentage (100: one). */
void olui_set_zoom(olui *ui, int percent);

/* Lays the contents out in a window's inner box, in pixels. 1, or 0 when
 * memory ran out. Changes made through the calls below lay out again by
 * themselves at the next olui_draw or input. */
int olui_layout(olui *ui, int width, int height);

/* ---- Drawing --------------------------------------------------------------- */

/* What olui_draw hands the program's drawer, in pixels, back to front. */
enum {
    OLUI_DRAW_BOX = 1,  /* a box: fill (top to bottom through stops), border, rounded corners */
    OLUI_DRAW_TEXT,     /* a run of text at a baseline */
    OLUI_DRAW_MARK,     /* a small mark drawn in a box: mark says which */
    OLUI_DRAW_CLIP,     /* from here everything is cut to clip, until the next CLIP (its box empty: none) */
    OLUI_DRAW_IMAGE     /* a picture's place: image is the program's number for it */
};

enum {
    OLUI_MARK_TICK = 1, /* a checkbox's tick */
    OLUI_MARK_DOT,      /* a radio button's dot */
    OLUI_MARK_OPEN,     /* a tree item's arrow, open (pointing down) */
    OLUI_MARK_CLOSED,   /* and closed (pointing right) */
    OLUI_MARK_CARET     /* a field's caret: a thin bar */
};

#define OLUI_MAX_STOPS 4

typedef struct {
    int32_t x, y, w, h;
} olui_rect;

typedef struct {
    int kind;                       /* OLUI_DRAW_... */
    olui_rect box;
    olui_rect clip;                 /* OLUI_DRAW_CLIP */
    /* box */
    int stops;                      /* 0: no fill; 1: flat; more: a gradient down the box */
    uint32_t stop_argb[OLUI_MAX_STOPS];
    int stop_pos[OLUI_MAX_STOPS];   /* 0 to 1000 down the box */
    uint32_t border_argb;
    int border;                     /* pixels; 0: none */
    int radius;                     /* corners, pixels */
    /* text */
    const char *text;               /* UTF-8, not ended: length bytes */
    size_t length;
    int32_t baseline;
    int font_pixels;
    uint32_t text_flags;            /* OL_TEXT_... */
    uint32_t argb;                  /* text, mark */
    /* mark */
    int mark;                       /* OLUI_MARK_... */
    /* image */
    int image;
    const olui_el *el;              /* whose it is */
} olui_draw_op;

typedef void (*olui_draw_fn)(void *userdata, const olui_draw_op *op);

/* Draws what crosses area (NULL: all of it), back to front. */
void olui_draw(olui *ui, const olui_rect *area, olui_draw_fn draw, void *userdata);

/* 1 when something changed that needs drawing, with the area it is in;
 * olui_draw clears it. */
int olui_damaged(const olui *ui, olui_rect *area);

/* ---- Input ----------------------------------------------------------------- */

enum {
    OLUI_MOUSE_MOVE = 0,
    OLUI_MOUSE_DOWN,        /* the left button */
    OLUI_MOUSE_UP,
    OLUI_MOUSE_DOUBLE,      /* a second down soon after the first, in the same place */
    OLUI_WHEEL_UP,
    OLUI_WHEEL_DOWN
};

/* Keys other than characters (olui_key's code is otherwise a character, Unicode) */
enum {
    OLUI_KEY_LEFT = 0x110000,
    OLUI_KEY_RIGHT,
    OLUI_KEY_UP,
    OLUI_KEY_DOWN,
    OLUI_KEY_HOME,
    OLUI_KEY_END,
    OLUI_KEY_PAGEUP,
    OLUI_KEY_PAGEDOWN,
    OLUI_KEY_BACKSPACE,
    OLUI_KEY_DELETE,
    OLUI_KEY_RETURN,
    OLUI_KEY_TAB,
    OLUI_KEY_ESCAPE
};
#define OLUI_QUAL_SHIFT 1u

/* The pointer, in the window's inner box's pixels. 1 when it was used. */
int olui_mouse(olui *ui, int x, int y, int what);
/* A key for the widget with the keyboard (Tab moves between them). 1 when it was used. */
int olui_key(olui *ui, unsigned code, unsigned qualifiers);

/* ---- The widgets ------------------------------------------------------------- */

olui_el *olui_find(olui *ui, const char *id);
const char *olui_id(const olui_el *el);
const char *olui_tag(const olui_el *el);

/* A label's, button's, checkbox's or item's text; a field's contents. */
const char *olui_get_text(const olui_el *el);
int olui_set_text(olui *ui, olui_el *el, const char *utf8);
/* value attribute (an item's, a radio's, a tab's) */
const char *olui_get_value(const olui_el *el);

int olui_get_checked(const olui_el *el);
void olui_set_checked(olui *ui, olui_el *el, int checked);
void olui_set_disabled(olui *ui, olui_el *el, int disabled);
void olui_set_hidden(olui *ui, olui_el *el, int hidden);
/* A progress bar's value, 0 to its max. */
void olui_set_progress(olui *ui, olui_el *el, int value);
/* A slider's value, from its min to its max (attributes min, max and value;
 * 0 to 100 when not given). It sends OLUI_CHANGE as it moves, with the
 * value in the event's index (and as text in value); setting it doesn't. */
int olui_slider_value(const olui_el *el);
void olui_set_slider(olui *ui, olui_el *el, int value);
/* Where a widget is, in the window's inner box's pixels, as last laid out.
 * 0 when it isn't shown or not laid out yet. */
int olui_box(olui *ui, const olui_el *el, olui_rect *r);
/* Gives the keyboard to a widget. */
void olui_focus(olui *ui, olui_el *el);

/* Lists and trees: items under a list or another item (parent NULL: the
 * list's own); value may be NULL. */
olui_el *olui_list_add(olui *ui, olui_el *list, olui_el *parent, const char *text, const char *value);
void olui_list_clear(olui *ui, olui_el *list);
olui_el *olui_list_selected(const olui_el *list);
void olui_list_select(olui *ui, olui_el *list, olui_el *item);
void olui_item_open(olui *ui, olui_el *item, int open);
int olui_item_is_open(const olui_el *item);

/* Tabs: the one shown, from 0. */
int olui_tabs_current(const olui_el *tabs);
void olui_tabs_select(olui *ui, olui_el *tabs, int index);

/* The program's own pointer on a widget. */
void olui_set_user(olui_el *el, void *user);
void *olui_user(const olui_el *el);

/* ---- The theme ------------------------------------------------------------------ */

/* Turns an OpenGadTools theme file's text (THEME_SPEC.md, version 1) into
 * CSS for olui_add_css, light or dark (the theme's light when it has no
 * dark). Returns a malloc'd string, or NULL with why in err. Built in
 * when OpenGadTools' theme reader is linked (OLUI_THEME: see the README). */
char *olui_theme_css(const char *theme_text, int dark, char *err, size_t errlen);

#ifdef __cplusplus
}
#endif
#endif
