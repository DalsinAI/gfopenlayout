/* GFOpenLayout UI: programs' windows described in HTML and CSS, laid out by
 * GFOpenLayout and drawn in the GFOpenGadTools theme, with the program's own
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
 * on GFOpenGfx, the IDCMP loop) is gfopenlayout_ui_amiga.h. All sizes and
 * positions here are pixels; GFOpenLayout's units stay inside.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef OPENLAYOUT_UI_H
#define OPENLAYOUT_UI_H

#include <stddef.h>
#include <stdint.h>

#include "gfopenlayout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLUI_VERSION "0.4"

typedef struct olui olui;
typedef struct olui_el olui_el;

/* What happened (olui_event.type) */
enum {
    OLUI_CLICK = 1,     /* a button was pressed (mouse, Return or space) */
    OLUI_CHANGE,        /* a checkbox or radio button changed, a field's or textarea's text, a select's choice */
    OLUI_SUBMIT,        /* Return in a field */
    OLUI_SELECT,        /* a list's selected item changed; a tab was chosen */
    OLUI_ACTIVATE,      /* a list item double-clicked, or Return on it */
    OLUI_EXPAND,        /* a tree item opened */
    OLUI_COLLAPSE,      /* and closed */
    OLUI_FOCUS,         /* a widget took the keyboard */
    OLUI_CUT,           /* text to put on the clipboard (value), taken out of the field after */
    OLUI_COPY,          /* text to put on the clipboard (value) */
    OLUI_PASTE,         /* the field wants the clipboard's text: give it with olui_insert_text */
    OLUI_EVENT_COUNT
};

typedef struct {
    int type;           /* OLUI_... */
    olui_el *el;        /* the widget: the button, field, list, tabs */
    const char *id;     /* its id attribute ("" when it has none) */
    olui_el *item;      /* a list's item, a tabs' tab; else NULL */
    const char *value;  /* a field's text (none for a password); an item's or a tab's value (its id when it has no value); a radio's value;
                         * a select's option's value (its text when it has none); what to copy or cut */
    int index;          /* an item's place among the visible rows, a tab's number, a select's option's from 0,
                         * a slider's or number field's value; else -1 */
    int checked;        /* a checkbox's or radio button's state */
} olui_event;

typedef void (*olui_handler)(olui *ui, const olui_event *ev, void *userdata);

/* ---- Making a window's contents ----------------------------------------- */

olui *olui_new(void);
void olui_free(olui *ui);

/* The program's measure: text widths, ascents and descents in GFOpenLayout
 * units for a style (its font_size, and OL_TEXT_BOLD, OL_TEXT_MONO in
 * text_flags). The Amiga glue gives the RastPort's fonts. Without one,
 * text is measured as about 0.6 em a character. */
void olui_set_measure(olui *ui, ol_measure_text_fn measure, void *userdata);

/* CSS in the order given, after the built-in rules: the theme's
 * (olui_theme_css, or the Amiga glue's), then the program's own. 1, or 0
 * when memory ran out. */
int olui_add_css(olui *ui, const char *css);

/* The window's contents as markup (the tags are in GFOpenLayout-UI.md),
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
    OLUI_MARK_CARET,    /* a field's caret: a thin bar */
    OLUI_MARK_DROPDOWN, /* a select's mark: an arrow pointing down */
    OLUI_MARK_SPIN_UP,  /* a number field's buttons: an arrow pointing up */
    OLUI_MARK_SPIN_DOWN /* and down */
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

/* Draws what crosses area (NULL: all of it), back to front; the drawer
 * cuts what it draws to area. */
void olui_draw(olui *ui, const olui_rect *area, olui_draw_fn draw, void *userdata);

/* 1 when something changed that needs drawing, with the area it is in: a
 * hover, press, focus, check or selection that changes no size, where it
 * changed; else the window. olui_draw clears it. */
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
#define OLUI_QUAL_COMMAND 2u    /* the Amiga key (Ctrl on the host): with x, c, v and a, cut, copy, paste and all */

/* The pointer, in the window's inner box's pixels. 1 when it was used. */
int olui_mouse(olui *ui, int x, int y, int what);
/* A key for the widget with the keyboard (Tab moves between them). 1 when it was used.
 * Shift with an arrow is a textarea's selection; elsewhere Shift-Up and
 * Shift-Down are PageUp and PageDown, Shift-Left and Shift-Right Home and End. */
int olui_key(olui *ui, unsigned code, unsigned qualifiers);

/* Time going by, ms since the last call: the program calls it now and then
 * (the Amiga glue on IDCMP_INTUITICKS, about every 100 ms). A tooltip shows
 * when the pointer has rested on an element with a title for
 * OLUI_TIP_DELAY ms, and a number field's held arrow button repeats. 1 when
 * something needs drawing (olui_damaged says where). */
#define OLUI_TIP_DELAY 700
int olui_tick(olui *ui, unsigned ms);

/* ---- The widgets ------------------------------------------------------------- */

olui_el *olui_find(olui *ui, const char *id);
const char *olui_id(const olui_el *el);
const char *olui_tag(const olui_el *el);

/* A label's, button's, checkbox's or item's text; a field's or textarea's
 * contents (a password's too); a select's chosen option's text. */
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
/* The widget with the keyboard, or NULL. */
olui_el *olui_focused(const olui *ui);

/* A list's, textarea's, list box's or scrolling box's (overflow auto,
 * scroll or hidden) scroll, in pixels from the top; set, it is kept inside
 * what it holds. */
int32_t olui_get_scroll(const olui_el *el);
void olui_set_scroll(olui *ui, olui_el *el, int32_t y);

/* Selects: the chosen option, from 0 among the options (-1: none), the
 * number of options, and an option's value (its text when it has none) or
 * text; NULL past the end. Choosing it from the program sends no event. */
int olui_select_index(const olui_el *select);
void olui_select_set(olui *ui, olui_el *select, int index);
int olui_select_count(const olui_el *select);
const char *olui_select_option_value(const olui_el *select, int index);
const char *olui_select_option_text(const olui_el *select, int index);
/* Options added by the program (value may be NULL), and all taken away */
olui_el *olui_select_add(olui *ui, olui_el *select, const char *text, const char *value);
void olui_select_clear(olui *ui, olui_el *select);
/* 1 while its pop-up list is open; opened and closed by the program */
int olui_select_is_open(const olui *ui, const olui_el *select);
void olui_select_open(olui *ui, olui_el *select, int open);

/* A number field (input type=number, spinner): its value times
 * 10^decimals (the places its value, min, max and step are written with,
 * 4 at most), held from min to max. Setting it sends no event. */
int olui_number_value(const olui_el *el);
int olui_number_decimals(const olui_el *el);
void olui_set_number(olui *ui, olui_el *el, int value);

/* A field's or textarea's selection, in bytes of its text (from == to: the
 * caret, no selection); 1 when something is selected. Set, it is kept to
 * the text and its characters, and the caret (at to) is scrolled into view. */
int olui_get_selection(const olui_el *el, size_t *from, size_t *to);
void olui_set_selection(olui *ui, olui_el *el, size_t from, size_t to);
/* Text put in a field or textarea where its selection is (or at the
 * caret), as typed: maxlength and readonly hold, a field takes one line.
 * OLUI_CHANGE follows. 1 when something went in. */
int olui_insert_text(olui *ui, olui_el *el, const char *utf8);
/* Cut, copy or paste (OLUI_CUT, OLUI_COPY, OLUI_PASTE) in the widget with
 * the keyboard, as the Amiga key with x, c and v does: a menu's Edit items
 * call it. It sends the event the program puts on, or takes from, the
 * clipboard; 1 when it did. A password is never copied. */
int olui_edit(olui *ui, int what);

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

/* ---- A desktop's elements (0.4, gfopenlayout.library 11) ----------------------
 *
 * Elements the program makes, places and takes away while the window is open
 * (a desktop's icons, a drawer's files), positioned and layered by CSS:
 * position:absolute (or fixed) takes an element out of the flow to its
 * left and top from its parent's content corner (right and bottom are not
 * used: the styling doesn't know the parent's size), as wide as its width or
 * its content; z-index puts it in a layer, for drawing and for the pointer
 * alike: positioned children below 0, those in the flow, positioned ones at
 * 0 or auto, then those above 0, each layer in the markup's order. Each
 * element is a layer of its own: what it holds stays in its layer. */

/* Markup (a fragment, as olui_load takes, <style> blocks too) made into
 * elements at the end of parent's children (NULL: the window's). The first
 * new element, or NULL with why in err. */
olui_el *olui_add(olui *ui, olui_el *parent, const char *markup, char *err, size_t errlen);
/* An element and what it holds taken away and freed; not the window, nor a
 * tab or its button. 1 when it was. */
int olui_remove(olui *ui, olui_el *el);
/* Its style attribute's declarations from now on (NULL: none). 1, or 0
 * when memory ran out. */
int olui_set_style(olui *ui, olui_el *el, const char *decls);
/* A positioned element's place, in pixels from its parent's content
 * corner, in place of its CSS's left and top (dragging an icon, a grid's
 * cell); OLUI_PLACE_CSS for x gives it back to its CSS. */
#define OLUI_PLACE_CSS ((int32_t)0x80000000)
void olui_set_place(olui *ui, olui_el *el, int32_t x, int32_t y);
/* The topmost element at x, y in the window's inner box (the one the
 * pointer would reach, layers counted), or NULL. */
olui_el *olui_hit(olui *ui, int x, int y);
/* Walking the elements: the parent, the first child and the next sibling
 * (runs of text among other content are elements too, their tag ""). */
olui_el *olui_parent(const olui_el *el);
olui_el *olui_first_child(const olui_el *el);
olui_el *olui_next(const olui_el *el);

/* ---- The theme ------------------------------------------------------------------ */

/* Turns a GFOpenGadTools theme file's text (THEME_SPEC.md, version 1) into
 * CSS for olui_add_css, light or dark (the theme's light when it has no
 * dark). Returns a malloc'd string, or NULL with why in err. Built in
 * when GFOpenGadTools' theme reader is linked (OLUI_THEME: see the README).
 * The program's free() takes it back, through gfopenlayout.library too
 * (libgfopenlayout_link.a copies it to the program's own memory). */
char *olui_theme_css(const char *theme_text, int dark, char *err, size_t errlen);
/* Frees what olui_theme_css returned with the allocator that made it: what
 * free() does when the UI is linked in, and OLUI_ThemeCss's in the library
 * (a program calling OLUI_ThemeCss itself gives it back with OLUI_FreeCss). */
void olui_free_css(char *css);

#ifdef __cplusplus
}
#endif
#endif
