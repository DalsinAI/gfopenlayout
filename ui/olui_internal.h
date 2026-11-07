/* GFOpenLayout UI's insides, shared by its files.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef OLUI_INTERNAL_H
#define OLUI_INTERNAL_H

#include "gfopenlayout_ui.h"

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
    K_SLIDER,       /* a value from min to max, dragged or keyed */
    K_SELECT,       /* a drop-down (or with size, a list box) of options */
    K_OPTION,       /* one of a select's choices */
    K_OPTGROUP,     /* a labelled group of a select's options */
    K_TEXTAREA,     /* text of many lines, wrapped, edited and scrolled */
    K_VLIST         /* 0.4: a long list whose rows the program gives as they are drawn */
};

/* What else an element is (olui_el.flags) */
enum {
    F_PASSWORD = 1u << 0,    /* input type=password: drawn a bullet a character */
    F_NUMBER = 1u << 1,      /* input type=number, spinner: a number with arrow buttons */
    F_READONLY = 1u << 2,    /* readonly: the caret moves and text is copied, not changed */
    F_NOWRAP = 1u << 3,      /* textarea wrap=off: lines break only at newlines */
    F_COLS = 1u << 4,        /* cols was given */
    /* 0.4: a desktop's */
    F_SELECTABLE = 1u << 5,  /* selectable: clicked, selected; Shift adds */
    F_DRAGGABLE = 1u << 6,   /* draggable */
    F_DROPPABLE = 1u << 7,   /* droppable: a drop target */
    F_MARQUEE = 1u << 8,     /* marquee: a scope; a band dragged on its space selects */
    F_MENU = 1u << 9,        /* menu: a select never shown, its pop-up opened by olui_menu_open */
    F_SEPARATOR = 1u << 10   /* option separator: a line between rows, never chosen */
};

/* overflow (olui_cs.overflow) */
enum {
    O_VISIBLE = 0,
    O_AUTO,                 /* scrolls what it holds, a bar when it is taller */
    O_SCROLL,               /* the same */
    O_HIDDEN                /* cut to the box, scrolled only by the program */
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
    S_HIDDEN = 1u << 7,      /* olui_set_hidden: display none, whatever the CSS */
    S_DRAGGING = 1u << 8,    /* 0.4: being dragged (:dragging) */
    S_DROP = 1u << 9         /* 0.4: what is dragged is over it (:drop-target) */
};

/* A computed style: what GFOpenLayout lays out, and how olui draws the box */
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
    int overflow;               /* O_...: a box with a height scrolls what it holds */
    /* 0.4 */
    int positioned;             /* P_...: position absolute or fixed is a frame at left and top (box.frame) */
    int32_t z;                  /* z-index (auto: 0): its layer among its parent's children */
    int opacity;                /* 0 to 255: how much of it shows (its own; drawing multiplies those it is in) */
    int shadows;                /* text-shadow: how many, the first on top */
    ol_unit shadow_x[OLUI_MAX_SHADOWS], shadow_y[OLUI_MAX_SHADOWS];
    uint32_t shadow_argb[OLUI_MAX_SHADOWS];
    ol_unit outline;            /* outline's width (0: none), drawn round the border box */
    uint32_t outline_argb;
    int bg_image;               /* background-image's picture: url(N), or a name the program gave N; -1: none */
    int bg_name;                /* its url's text, interned in the sheet (olui_sheet_name), from 1; 0: none */
    int bg_repeat;              /* OLUI_REPEAT_... */
    int bg_align_x, bg_align_y; /* background-position in thousandths of the room left (500: the middle) */
} olui_cs;

/* position (olui_cs.positioned) */
enum {
    P_STATIC = 0,
    P_OUT,                      /* absolute or fixed: out of the flow, a GFOpenLayout frame */
    P_RELATIVE                  /* relative: in the flow; its z-index counts */
};

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
    /* 0.3 */
    unsigned flags;             /* F_... */
    char *for_id;               /* a label's for */
    char *num_attr[3];          /* a number field's min, max and step as written (fixed point) */
    int tabindex;               /* Tab's order: above 0 first, 0 in the document's order, below 0 never */
    int rows, cols;             /* a textarea's size in lines and characters; a select's size */
    int maxlength;              /* characters a field or textarea takes, 0: any number */
    int decimals;               /* a number field's places after the point: ivalue is the value times 10^decimals */
    int step;                   /* and its step, in the same units */
    size_t anchor;              /* a textarea's selection: from anchor to caret */
    int32_t want_x;             /* the caret's x that Up and Down keep to (-1: where it is) */
    size_t *lines;              /* a textarea's lines as wrapped: where each starts */
    int nlines, lines_cap;
    unsigned text_rev;          /* moves on with each change to text */
    unsigned wrap_rev;          /* lines are of text_rev's text */
    int32_t wrap_room;          /* at this width (pixels) */
    ol_unit wrap_font;          /* and font size */
    /* 0.4 */
    int placed;                 /* olui_set_place: at place_x, place_y instead of its CSS's left and top */
    int32_t place_x, place_y;   /* pixels from its containing block's content corner */
    int band_base;              /* selected when a Shift band began: it stays so */
    char *menu_id;              /* contextmenu: the menu the menu button opens on it */
    /* a vlist's */
    int vrows, vcur;
    olui_row_fn vfn;
    void *vud;
    unsigned char *vsel;        /* a bit a row */
    int ncols;
    int colw[8];                /* CSS pixels */
    olui_el *vrow;              /* made: what styles a row (row, under it) */
};

/* ---- CSS (olui_css.c) ---- */
typedef struct olui_sheet olui_sheet;
olui_sheet *olui_sheet_new(void);
void olui_sheet_free(olui_sheet *sheet);
/* 0.4: shared by several windows: another holder, and a count that moves on when it changes */
olui_sheet *olui_sheet_ref(olui_sheet *sheet);
unsigned olui_sheet_gen(const olui_sheet *sheet);
void olui_sheet_changed(olui_sheet *sheet);
int olui_sheet_add(olui_sheet *sheet, const char *css);
/* Every state (S_...) the sheet's selectors name: a change to another
 * state changes no element's style */
unsigned olui_sheet_states(const olui_sheet *sheet);
/* el's style from its parent's (NULL: the defaults) and the rules that match it */
void olui_style_compute(olui_sheet *sheet, olui_el *el, const olui_cs *parent);
/* 0.4: background-image's names: the program's number for a name (olui_image_name), and a name's text
 * from its index (cs.bg_name) */
int olui_sheet_name_image(olui_sheet *sheet, const char *name, int image);
const char *olui_sheet_name(const olui_sheet *sheet, int index);
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

/* a named font family's name, by the number in text_flags (OL_TEXT_FONT_OF): NULL for 0 */
const char *olui_family_name(unsigned id);

#endif
