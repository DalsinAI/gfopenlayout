/* OpenLayout UI's insides, shared by its files.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef OLUI_INTERNAL_H
#define OLUI_INTERNAL_H

#include "openlayout_ui.h"

/* What an element is */
enum {
    K_BOX = 0,      /* div, row, column, label, p, span, ... */
    K_TEXT,         /* a run of text among other content (no tag) */
    K_BUTTON,
    K_CHECK,
    K_RADIO,
    K_INPUT,
    K_LIST,
    K_ITEM,
    K_TABS,
    K_TAB,          /* a page of a tabs */
    K_STRIP,        /* a tabs' row of tab buttons (made, not written) */
    K_TABBTN,       /* a tab button in the strip (made) */
    K_PROGRESS,
    K_IMAGE,
    K_SLIDER        /* a value from min to max, dragged or keyed */
};

/* States: what the :pseudo-classes match */
enum {
    S_HOVER = 1u << 0,
    S_ACTIVE = 1u << 1,      /* pressed */
    S_FOCUS = 1u << 2,
    S_CHECKED = 1u << 3,
    S_DISABLED = 1u << 4,
    S_SELECTED = 1u << 5,
    S_OPEN = 1u << 6,
    S_HIDDEN = 1u << 7       /* olui_set_hidden: display none, whatever the CSS */
};

/* A computed style: what OpenLayout lays out, and how olui draws the box */
typedef struct {
    ol_style ol;
    ol_box_style box;
    int stops;
    uint32_t stop_argb[OLUI_MAX_STOPS];
    int stop_pos[OLUI_MAX_STOPS];
    uint32_t border_argb;
    ol_unit border;
    ol_unit radius;
    uint32_t accent;            /* marks, carets, progress fills: the accent-color property */
    uint32_t mark_bg;           /* a checkbox's or radio button's box: mark-background */
    uint32_t mark_border;       /* and its edge: mark-border-color */
    uint32_t muted;             /* a field's placeholder: placeholder-color */
    int column;                 /* flex-direction column (the default for a block) */
} olui_cs;

struct olui_el {
    int kind;
    char tag[16];
    char *id, *cls, *name, *value, *text, *style, *placeholder, *title;
    unsigned state;
    int ivalue, imax;           /* progress, a slider's value; an image's number and size (ivalue, w, h) */
    int imin;                   /* a slider's least value */
    int img_w, img_h;
    olui_el *parent, *first, *last, *next;
    olui_cs cs;
    ol_node *node;              /* in this layout */
    int depth;                  /* an item's level in its tree, from 0 */
    int32_t scroll;             /* a list's: pixels scrolled */
    size_t caret;               /* a field's: the caret, in bytes */
    int32_t xoff;               /* a field's: pixels scrolled across */
    olui_el *tab;               /* a tab button's page; a page's button */
    unsigned styled_gen;        /* cs is from this stylesheet generation */
    unsigned styled_state;      /* and these states */
    void *user;
};

/* ---- CSS (olui_css.c) ---- */
typedef struct olui_sheet olui_sheet;
olui_sheet *olui_sheet_new(void);
void olui_sheet_free(olui_sheet *sheet);
int olui_sheet_add(olui_sheet *sheet, const char *css);
/* el's style from its parent's (NULL: the defaults) and the rules that match it */
void olui_style_compute(const olui_sheet *sheet, olui_el *el, const olui_cs *parent);
/* one declaration list ("color: red; padding: 2px") on cs */
void olui_style_apply_decls(olui_cs *cs, const char *decls, const olui_cs *parent);
int olui_parse_colour(const char *s, uint32_t *argb);

/* ---- Markup (olui_markup.c) ---- */
olui_el *olui_el_new(int kind, const char *tag);
void olui_el_free(olui_el *el);
void olui_el_append(olui_el *parent, olui_el *child);
void olui_el_unlink(olui_el *el);
int olui_set_str(char **dst, const char *s);
/* The markup as a tree under a new root element. NULL with why in err. */
olui_el *olui_markup_parse(const char *markup, char *err, size_t errlen);

#endif
