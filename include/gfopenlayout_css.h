/* GFCSS: the CSS engine shared by GFOpenLayout UI (programs' windows) and
 * GFOpenBrowser Lite (web pages). It parses stylesheets, matches their
 * selectors against the caller's elements, runs the cascade and hands back
 * each element's computed style in integers, and turns that into
 * GFOpenLayout's ol_style and ol_box_style.
 *
 * Integers only, as the rest of GFOpenLayout: decimals are fixed point.
 *   - numbers (line-height 1.5, opacity 0.5, flex-grow 2) are in
 *     thousandths: GFCSS_MILLI is 1;
 *   - percentages are in thousandths of a percent: 37.5% is 37500, 100%
 *     is GFCSS_PCT(100);
 *   - lengths are GFOpenLayout units (a CSS pixel is 60).
 *
 * The caller describes its elements through gfcss_element_ops (a few
 * functions on its own element pointers); nothing is allocated for an
 * element while it is styled. docs/GFCSS.md says what is supported.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#ifndef GFOPENLAYOUT_CSS_H
#define GFOPENLAYOUT_CSS_H

#include <stddef.h>
#include <stdint.h>

#include "gfopenlayout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GFCSS_VERSION "1.0"

#define GFCSS_MILLI 1000
#define GFCSS_PCT(n) ((int32_t)(n) * 1000)

typedef struct gfcss_engine gfcss_engine;
typedef struct gfcss_sheet gfcss_sheet;

/* Where a sheet comes from: the cascade puts the user agent's first, the
 * user's next, the author's last; !important turns that round. */
enum {
    GFCSS_ORIGIN_UA = 0,
    GFCSS_ORIGIN_USER = 1,
    GFCSS_ORIGIN_AUTHOR = 2
};

/* What @media is evaluated against (gfcss_set_media) */
enum {
    GFCSS_MEDIA_SCREEN = 0,
    GFCSS_MEDIA_PRINT = 1
};

/* An element's states, for the pseudo-classes. The caller sets ENABLED on
 * form controls that are not disabled, and LINK on links not visited, as
 * a browser does; GFCSS_STATE_USER(0) to (15) are for pseudo-classes the
 * caller names with gfcss_add_state (GFOpenLayout UI's :selected, :open). */
enum {
    GFCSS_STATE_HOVER    = 1u << 0,
    GFCSS_STATE_ACTIVE   = 1u << 1,
    GFCSS_STATE_FOCUS    = 1u << 2,
    GFCSS_STATE_CHECKED  = 1u << 3,
    GFCSS_STATE_DISABLED = 1u << 4,
    GFCSS_STATE_ENABLED  = 1u << 5,
    GFCSS_STATE_LINK     = 1u << 6,
    GFCSS_STATE_VISITED  = 1u << 7
};
#define GFCSS_STATE_USER(n) (1u << (16 + (n)))

/* What the engine asks about an element. Strings stay the caller's; they
 * need only last until the call returns. */
typedef struct {
    const char *tag;        /* its type, matched without regard to case; NULL or "": none */
    const char *id;         /* NULL: none */
    const char *classes;    /* separated by spaces; NULL: none */
    const char *style;      /* its style attribute's declarations; NULL: none */
    uint32_t state;         /* GFCSS_STATE_... */
    int32_t index;          /* its place among its parent's elements, from 1; 0: not known (worked out from prev) */
    int32_t count;          /* how many elements its parent has; 0: not known (worked out from next) */
    int32_t empty;          /* 1 when it has no children and no text (:empty) */
} gfcss_info;

/* How the engine walks the caller's elements. el is the caller's own
 * pointer. info, parent and prev are required (each answers NULL when
 * there is none). next may be NULL: :last-child and :nth-last-child then
 * use info's count, and :last-of-type, :only-of-type and
 * :nth-last-of-type match nothing. attr may be NULL when no selector asks
 * for an attribute; it must answer for "id" and "class" as well. */
typedef struct {
    void (*info)(void *userdata, const void *el, gfcss_info *info);
    const void *(*parent)(void *userdata, const void *el);
    const void *(*prev)(void *userdata, const void *el);  /* the element before it under its parent */
    const void *(*next)(void *userdata, const void *el);  /* and after it */
    const char *(*attr)(void *userdata, const void *el, const char *name);  /* NULL: not there */
} gfcss_element_ops;

/* Pseudo-elements (gfcss_compute's pseudo) */
enum {
    GFCSS_PSEUDO_NONE = 0,
    GFCSS_PSEUDO_BEFORE,
    GFCSS_PSEUDO_AFTER,
    GFCSS_PSEUDO_FIRST_LINE,
    GFCSS_PSEUDO_FIRST_LETTER
};

/* ---- Computed values ------------------------------------------------------ */

/* A length that may depend on layout: units plus a percentage of what it is
 * measured against (calc(100% - 20px) is units -1200, pct 100000). */
enum {
    GFCSS_LEN = 0,          /* units + pct */
    GFCSS_LEN_AUTO,
    GFCSS_LEN_NONE,         /* max-width, max-height */
    GFCSS_LEN_NORMAL,       /* gap */
    GFCSS_LEN_CONTENT       /* flex-basis */
};

typedef struct {
    int32_t kind;           /* GFCSS_LEN... */
    ol_unit units;
    int32_t pct;            /* thousandths of a percent */
} gfcss_length;

/* Text kept from the stylesheet (a url, a font's name, content's string):
 * a stretch of the sheet's own text, escapes as written (gfcss_unescape
 * turns them into UTF-8). It lasts as long as the sheet, or, from a style
 * attribute, as long as the caller's string. */
enum {
    GFCSS_TEXT_NONE = 0,
    GFCSS_TEXT_STRING,      /* content: "..." ; a font family's name */
    GFCSS_TEXT_URL,         /* url(...): what is in the brackets */
    GFCSS_TEXT_FUNCTION,    /* an image function, all of it: linear-gradient(...) */
    GFCSS_TEXT_NORMAL       /* content: normal */
};

typedef struct {
    int32_t kind;           /* GFCSS_TEXT_... */
    uint32_t length;
    const char *text;
} gfcss_text;

enum { GFCSS_DISPLAY_INLINE = 0, GFCSS_DISPLAY_BLOCK, GFCSS_DISPLAY_INLINE_BLOCK, GFCSS_DISPLAY_NONE,
       GFCSS_DISPLAY_LIST_ITEM, GFCSS_DISPLAY_FLEX, GFCSS_DISPLAY_INLINE_FLEX, GFCSS_DISPLAY_TABLE,
       GFCSS_DISPLAY_INLINE_TABLE, GFCSS_DISPLAY_TABLE_ROW, GFCSS_DISPLAY_TABLE_CELL,
       GFCSS_DISPLAY_TABLE_HEADER_GROUP, GFCSS_DISPLAY_TABLE_ROW_GROUP, GFCSS_DISPLAY_TABLE_FOOTER_GROUP,
       GFCSS_DISPLAY_TABLE_COLUMN, GFCSS_DISPLAY_TABLE_COLUMN_GROUP, GFCSS_DISPLAY_TABLE_CAPTION,
       GFCSS_DISPLAY_CONTENTS };
enum { GFCSS_POSITION_STATIC = 0, GFCSS_POSITION_RELATIVE, GFCSS_POSITION_ABSOLUTE, GFCSS_POSITION_FIXED,
       GFCSS_POSITION_STICKY };
enum { GFCSS_FLOAT_NONE = 0, GFCSS_FLOAT_LEFT, GFCSS_FLOAT_RIGHT };
enum { GFCSS_CLEAR_NONE = 0, GFCSS_CLEAR_LEFT, GFCSS_CLEAR_RIGHT, GFCSS_CLEAR_BOTH };
enum { GFCSS_VISIBLE = 0, GFCSS_HIDDEN, GFCSS_COLLAPSE };
enum { GFCSS_OVERFLOW_VISIBLE = 0, GFCSS_OVERFLOW_HIDDEN, GFCSS_OVERFLOW_SCROLL, GFCSS_OVERFLOW_AUTO,
       GFCSS_OVERFLOW_CLIP };
enum { GFCSS_CONTENT_BOX = 0, GFCSS_BORDER_BOX };
enum { GFCSS_BORDER_NONE = 0, GFCSS_BORDER_HIDDEN, GFCSS_BORDER_SOLID, GFCSS_BORDER_DOTTED,
       GFCSS_BORDER_DASHED, GFCSS_BORDER_DOUBLE, GFCSS_BORDER_GROOVE, GFCSS_BORDER_RIDGE, GFCSS_BORDER_INSET,
       GFCSS_BORDER_OUTSET };
enum { GFCSS_REPEAT = 0, GFCSS_REPEAT_X, GFCSS_REPEAT_Y, GFCSS_NO_REPEAT };
enum { GFCSS_FONT_DEFAULT = 0, GFCSS_FONT_SERIF, GFCSS_FONT_SANS_SERIF, GFCSS_FONT_MONOSPACE,
       GFCSS_FONT_CURSIVE, GFCSS_FONT_FANTASY };
enum { GFCSS_FONT_NORMAL = 0, GFCSS_FONT_ITALIC, GFCSS_FONT_OBLIQUE };
enum { GFCSS_VARIANT_NORMAL = 0, GFCSS_VARIANT_SMALL_CAPS };
enum { GFCSS_LINE_NORMAL = 0, GFCSS_LINE_NUMBER, GFCSS_LINE_LENGTH };
/* line-height normal, as a number: what gfcss_to_ol makes of it */
#define GFCSS_LINE_NORMAL_MILLI 1200
enum { GFCSS_ALIGN_START = 0, GFCSS_ALIGN_END, GFCSS_ALIGN_LEFT, GFCSS_ALIGN_RIGHT, GFCSS_ALIGN_CENTER,
       GFCSS_ALIGN_JUSTIFY };
enum { GFCSS_UNDERLINE = 1u << 0, GFCSS_LINE_THROUGH = 1u << 1, GFCSS_OVERLINE = 1u << 2 };
enum { GFCSS_TRANSFORM_NONE = 0, GFCSS_CAPITALIZE, GFCSS_UPPERCASE, GFCSS_LOWERCASE };
enum { GFCSS_WS_NORMAL = 0, GFCSS_WS_PRE, GFCSS_WS_NOWRAP, GFCSS_WS_PRE_WRAP, GFCSS_WS_PRE_LINE };
enum { GFCSS_VALIGN_BASELINE = 0, GFCSS_VALIGN_TOP, GFCSS_VALIGN_MIDDLE, GFCSS_VALIGN_BOTTOM,
       GFCSS_VALIGN_TEXT_TOP, GFCSS_VALIGN_TEXT_BOTTOM, GFCSS_VALIGN_SUB, GFCSS_VALIGN_SUPER,
       GFCSS_VALIGN_LENGTH };
enum { GFCSS_LIST_DISC = 0, GFCSS_LIST_CIRCLE, GFCSS_LIST_SQUARE, GFCSS_LIST_DECIMAL,
       GFCSS_LIST_DECIMAL_LEADING_ZERO, GFCSS_LIST_LOWER_ROMAN, GFCSS_LIST_UPPER_ROMAN,
       GFCSS_LIST_LOWER_ALPHA, GFCSS_LIST_UPPER_ALPHA, GFCSS_LIST_LOWER_GREEK, GFCSS_LIST_NONE };
enum { GFCSS_LIST_OUTSIDE = 0, GFCSS_LIST_INSIDE };
enum { GFCSS_CURSOR_AUTO = 0, GFCSS_CURSOR_DEFAULT, GFCSS_CURSOR_POINTER, GFCSS_CURSOR_TEXT,
       GFCSS_CURSOR_MOVE, GFCSS_CURSOR_WAIT, GFCSS_CURSOR_HELP, GFCSS_CURSOR_CROSSHAIR,
       GFCSS_CURSOR_NOT_ALLOWED, GFCSS_CURSOR_PROGRESS, GFCSS_CURSOR_GRAB, GFCSS_CURSOR_NONE,
       GFCSS_CURSOR_OTHER };
enum { GFCSS_ROW = 0, GFCSS_ROW_REVERSE, GFCSS_COLUMN, GFCSS_COLUMN_REVERSE };
enum { GFCSS_NOWRAP = 0, GFCSS_WRAP, GFCSS_WRAP_REVERSE };
/* align-items and justify-content */
enum { GFCSS_FLEX_NORMAL = 0, GFCSS_FLEX_STRETCH, GFCSS_FLEX_START, GFCSS_FLEX_END, GFCSS_FLEX_CENTER,
       GFCSS_FLEX_BASELINE, GFCSS_FLEX_SPACE_BETWEEN, GFCSS_FLEX_SPACE_AROUND, GFCSS_FLEX_SPACE_EVENLY,
       GFCSS_FLEX_LEFT, GFCSS_FLEX_RIGHT };
enum { GFCSS_SEPARATE = 0, GFCSS_COLLAPSE_BORDERS };
enum { GFCSS_CAPTION_TOP = 0, GFCSS_CAPTION_BOTTOM };

/* Sides, in CSS's order */
enum { GFCSS_TOP = 0, GFCSS_RIGHT, GFCSS_BOTTOM, GFCSS_LEFT };

/* The properties, as gfcss_is_set numbers them (longhands; a shorthand
 * sets its longhands). Properties added with gfcss_add_property follow
 * GFCSS_P_COUNT. */
enum {
    GFCSS_P_DISPLAY = 0, GFCSS_P_POSITION, GFCSS_P_FLOAT, GFCSS_P_CLEAR, GFCSS_P_Z_INDEX,
    GFCSS_P_VISIBILITY, GFCSS_P_OVERFLOW_X, GFCSS_P_OVERFLOW_Y, GFCSS_P_BOX_SIZING,
    GFCSS_P_WIDTH, GFCSS_P_HEIGHT, GFCSS_P_MIN_WIDTH, GFCSS_P_MIN_HEIGHT, GFCSS_P_MAX_WIDTH,
    GFCSS_P_MAX_HEIGHT,
    GFCSS_P_MARGIN_TOP, GFCSS_P_MARGIN_RIGHT, GFCSS_P_MARGIN_BOTTOM, GFCSS_P_MARGIN_LEFT,
    GFCSS_P_PADDING_TOP, GFCSS_P_PADDING_RIGHT, GFCSS_P_PADDING_BOTTOM, GFCSS_P_PADDING_LEFT,
    GFCSS_P_BORDER_TOP_WIDTH, GFCSS_P_BORDER_RIGHT_WIDTH, GFCSS_P_BORDER_BOTTOM_WIDTH,
    GFCSS_P_BORDER_LEFT_WIDTH,
    GFCSS_P_BORDER_TOP_STYLE, GFCSS_P_BORDER_RIGHT_STYLE, GFCSS_P_BORDER_BOTTOM_STYLE,
    GFCSS_P_BORDER_LEFT_STYLE,
    GFCSS_P_BORDER_TOP_COLOR, GFCSS_P_BORDER_RIGHT_COLOR, GFCSS_P_BORDER_BOTTOM_COLOR,
    GFCSS_P_BORDER_LEFT_COLOR,
    GFCSS_P_BORDER_TOP_LEFT_RADIUS, GFCSS_P_BORDER_TOP_RIGHT_RADIUS, GFCSS_P_BORDER_BOTTOM_RIGHT_RADIUS,
    GFCSS_P_BORDER_BOTTOM_LEFT_RADIUS,
    GFCSS_P_OUTLINE_WIDTH, GFCSS_P_OUTLINE_STYLE, GFCSS_P_OUTLINE_COLOR,
    GFCSS_P_COLOR, GFCSS_P_BACKGROUND_COLOR, GFCSS_P_BACKGROUND_IMAGE, GFCSS_P_BACKGROUND_REPEAT,
    GFCSS_P_BACKGROUND_POSITION_X, GFCSS_P_BACKGROUND_POSITION_Y, GFCSS_P_OPACITY,
    GFCSS_P_FONT_FAMILY, GFCSS_P_FONT_GENERIC, GFCSS_P_FONT_SIZE, GFCSS_P_FONT_WEIGHT,
    GFCSS_P_FONT_STYLE, GFCSS_P_FONT_VARIANT, GFCSS_P_LINE_HEIGHT,
    GFCSS_P_TEXT_ALIGN, GFCSS_P_TEXT_INDENT, GFCSS_P_TEXT_DECORATION_LINE, GFCSS_P_TEXT_TRANSFORM,
    GFCSS_P_WHITE_SPACE, GFCSS_P_VERTICAL_ALIGN,
    GFCSS_P_LIST_STYLE_TYPE, GFCSS_P_LIST_STYLE_POSITION, GFCSS_P_LIST_STYLE_IMAGE,
    GFCSS_P_LETTER_SPACING, GFCSS_P_WORD_SPACING, GFCSS_P_CURSOR,
    GFCSS_P_FLEX_DIRECTION, GFCSS_P_FLEX_WRAP, GFCSS_P_FLEX_GROW, GFCSS_P_FLEX_SHRINK,
    GFCSS_P_FLEX_BASIS, GFCSS_P_ROW_GAP, GFCSS_P_COLUMN_GAP, GFCSS_P_ALIGN_ITEMS,
    GFCSS_P_JUSTIFY_CONTENT,
    GFCSS_P_BORDER_COLLAPSE, GFCSS_P_BORDER_SPACING_H, GFCSS_P_BORDER_SPACING_V, GFCSS_P_CAPTION_SIDE,
    GFCSS_P_CONTENT,
    GFCSS_P_COUNT
};

/* At most this many properties of the caller's own (gfcss_add_property) */
#define GFCSS_MAX_EXT 8
#define GFCSS_SET_WORDS ((GFCSS_P_COUNT + GFCSS_MAX_EXT + 31) / 32)

/* An element's computed style. size first: later versions add fields at
 * the end, and gfcss_compute writes no more than size bytes when size is
 * set smaller. Set size before the first call (gfcss_computed_init does,
 * or 0 for all of it). */
typedef struct gfcss_computed {
    uint32_t size;

    int32_t display, position, floating, clear;
    int32_t z_index, z_auto;            /* z_auto 1: z-index auto */
    int32_t visibility, overflow_x, overflow_y, box_sizing;

    gfcss_length width, height, min_width, min_height, max_width, max_height;
    gfcss_length margin[4];             /* GFCSS_TOP... ; may be auto */
    gfcss_length padding[4];

    ol_unit border_width[4];            /* 0 when the side's style is none or hidden */
    int32_t border_style[4];
    uint32_t border_color[4];           /* AARRGGBB, currentColor resolved */
    gfcss_length border_radius[4];      /* top left, top right, bottom right, bottom left */
    ol_unit outline_width;
    int32_t outline_style;
    uint32_t outline_color;

    uint32_t color;                     /* AARRGGBB */
    uint32_t background_color;
    gfcss_text background_image;        /* none, url() or an image function */
    int32_t background_repeat;
    gfcss_length background_position[2];/* across, down */
    int32_t opacity;                    /* thousandths: 1000 is opaque */

    int32_t font_generic;               /* GFCSS_FONT_...: the family list's, or its first name's */
    gfcss_text font_family;             /* the first family's name, as written (a string's inside) */
    ol_unit font_size;
    int32_t font_weight;                /* 100 to 900 */
    int32_t font_style, font_variant;
    int32_t line_height_kind;           /* GFCSS_LINE_... */
    int32_t line_height;                /* NUMBER: thousandths; LENGTH: units */

    int32_t text_align;
    gfcss_length text_indent;
    uint32_t text_decoration;           /* GFCSS_UNDERLINE... (not inherited, as in CSS) */
    int32_t text_transform, white_space;
    int32_t vertical_align;             /* GFCSS_VALIGN_...; LENGTH: vertical_align_length */
    gfcss_length vertical_align_length;
    int32_t list_style_type, list_style_position;
    gfcss_text list_style_image;
    ol_unit letter_spacing, word_spacing;   /* normal: 0 */
    int32_t cursor;

    int32_t flex_direction, flex_wrap;
    int32_t flex_grow, flex_shrink;     /* thousandths */
    gfcss_length flex_basis;            /* auto, content or a length */
    gfcss_length row_gap, column_gap;   /* normal or a length */
    int32_t align_items, justify_content;

    int32_t border_collapse;
    ol_unit border_spacing[2];          /* across, down */
    int32_t caption_side;
    gfcss_text content;                 /* ::before and ::after: a string, or none or normal */

    uint32_t ext[GFCSS_MAX_EXT];        /* the caller's properties (gfcss_add_property) */
    uint32_t set[GFCSS_SET_WORDS];      /* which properties a declaration gave the element (gfcss_is_set) */
} gfcss_computed;

/* ---- The engine and its sheets ------------------------------------------- */

gfcss_engine *gfcss_new(void);
void gfcss_free(gfcss_engine *e);

/* A pseudo-class of the caller's: name (without the colon) matches
 * elements whose info.state has bit. Before the sheets that use it are
 * parsed (a selector with a pseudo-class nobody knows is left out). 1, or 0
 * when there is no room (16 at most). */
int gfcss_add_state(gfcss_engine *e, const char *name, uint32_t bit);

/* A property of the caller's (GFOpenLayout UI's accent-color): parse turns
 * a declaration's value into a number (a colour, say) and returns 1, or 0
 * when the value is not one of its own (the declaration is then left out).
 * inherit, initial and unset are handled by the engine. Before the sheets
 * that use it are parsed. Returns its slot, 0 to GFCSS_MAX_EXT - 1 (its
 * value is in gfcss_computed.ext[slot], its property number GFCSS_P_COUNT
 * + slot), or -1. */
typedef int (*gfcss_ext_fn)(void *userdata, int slot, const char *value, size_t length, uint32_t *out);
#define GFCSS_INHERITED 1u
int gfcss_add_property(gfcss_engine *e, const char *name, unsigned flags, uint32_t initial,
                       gfcss_ext_fn parse, void *userdata);

/* The medium and viewport @media is evaluated against, and that vw, vh,
 * vmin and vmax are of. A size of 0 is not known: a width or height
 * feature is then false, and vw and vh are 0. Screen, 0 by 0 at first. */
void gfcss_set_media(gfcss_engine *e, int media, ol_unit width, ol_unit height);

/* A new empty sheet at the end of the cascade, owned by the engine. */
gfcss_sheet *gfcss_sheet_new(gfcss_engine *e, int origin);
/* Takes a sheet out of the cascade and frees it (and its imports). */
void gfcss_sheet_free(gfcss_sheet *sheet);
/* More CSS for a sheet, after what it has (the text is copied). 1, or 0
 * when memory ran out. Mistakes in the CSS are skipped as CSS says. */
int gfcss_sheet_parse(gfcss_sheet *sheet, const char *css, size_t length);
/* How many style rules (one a selector) it holds, imports' not counted. */
size_t gfcss_sheet_rules(const gfcss_sheet *sheet);
/* Every GFCSS_STATE_... bit a selector in the sheet (or any, for the
 * engine's) names, :enabled's as GFCSS_STATE_DISABLED too: a change of any
 * other state changes no element's style. */
uint32_t gfcss_sheet_states(const gfcss_sheet *sheet);
uint32_t gfcss_states(const gfcss_engine *e);

/* @import: the sheet's imports in order, for the caller to fetch. url is
 * unescaped and ended with a 0. applies says whether its media list holds
 * now. gfcss_sheet_import gives a sheet that stands in the import's place
 * in the cascade (before the importing sheet's own rules), to parse the
 * fetched text into; NULL when there is no such import or no memory. */
int gfcss_sheet_import_count(const gfcss_sheet *sheet);
const char *gfcss_sheet_import_url(const gfcss_sheet *sheet, int index);
int gfcss_sheet_import_applies(const gfcss_sheet *sheet, int index);
gfcss_sheet *gfcss_sheet_import(gfcss_sheet *sheet, int index);

/* ---- Styling ------------------------------------------------------------------ */

/* The initial values (CSS's), size set. */
void gfcss_computed_init(gfcss_computed *cs);

/* el's computed style into out: its inherited properties from parent (the
 * parent element's computed style, NULL: initial values), the rules of
 * every sheet that match it, its style attribute. With a pseudo-element,
 * the style of el's ::before (and so on), parent then being el's own; it
 * returns 0 when no rule makes that pseudo-element (for ::before and
 * ::after, when content is not a string, or display is none). Returns 1,
 * or 0 when out of memory. When el has no parent its font size is what rem
 * is of, from then on. */
int gfcss_compute(gfcss_engine *e, const gfcss_element_ops *ops, void *userdata, const void *el,
                  int pseudo, const gfcss_computed *parent, gfcss_computed *out);

/* An anonymous box's style (a run of text in an element): the initial
 * values with parent's inherited ones. */
void gfcss_inherit(gfcss_engine *e, const gfcss_computed *parent, gfcss_computed *out);

/* Declarations (a style attribute's text) applied on top of cs, as if they
 * came last in its cascade; parent is what inherit takes. 1, or 0 when out
 * of memory. */
int gfcss_apply_declarations(gfcss_engine *e, const char *decls, size_t length,
                             const gfcss_computed *parent, gfcss_computed *cs);

/* 1 when a declaration gave the element property p (GFCSS_P_...) */
int gfcss_is_set(const gfcss_computed *cs, int p);

/* A length against what it is measured from (the containing block's
 * width, for widths, margins and padding); auto, none and normal are 0. */
ol_unit gfcss_resolve(const gfcss_length *len, ol_unit of);

/* The computed style as GFOpenLayout's: style and box are set from their
 * init functions' values and cs. Percentages are of containing_width. With
 * a GFOpenLayout that has OL_HAS_WEB_BOXES (0.9) each side's border, min
 * and max widths, percentage widths, vertical-align and auto margins are
 * set too; without, one border (the widest side's) and a width worked out
 * here. Widths are border-box widths, as GFOpenLayout's box.width is: a
 * content-box width has the padding and border added. */
void gfcss_to_ol(const gfcss_computed *cs, ol_unit containing_width, ol_style *style, ol_box_style *box);

/* ---- Small things ---------------------------------------------------------- */

/* A colour (#rgb, rgb(), hsl(), a name...) into AARRGGBB. 1, or 0 when it
 * is not one (currentColor is not one here). */
int gfcss_parse_colour(const char *s, size_t length, uint32_t *argb);
/* CSS text with its escapes into UTF-8, ended with a 0 (cut to fit size);
 * returns the length it has (or would have, uncut). */
size_t gfcss_unescape(const char *s, size_t length, char *out, size_t size);
/* A sheet's text as gfcss_text (background_image, content...) unescaped */
size_t gfcss_text_copy(const gfcss_text *t, char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
