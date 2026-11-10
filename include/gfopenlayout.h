#ifndef OPENLAYOUT_H
#define OPENLAYOUT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* OL_VERSION stays "0.9" from gfopenlayout.library 10 on: the OL_ calls are
 * still version 9's, and the programs built for them the same bytes. */
#define OL_VERSION "0.9"
/* 0.9: per-side borders, widths in thousandths with their least and most,
 * auto side margins, inline-blocks, cells' vertical alignment and table
 * headers repeated on every page (a browser's and a word processor's boxes) */
#define OL_HAS_WEB_BOXES 1
/* 0.11 (gfopenlayout.library 11): frames as wide as their content (OL_BOX_FIT_CONTENT) */
#define OL_HAS_FIT_CONTENT 1
/* 0.13 (gfopenlayout.library 13): reusable vector drawing, image crops and
 * box-local clipping in the document display list. */
#define OL_HAS_DRAWING_OPS 1

/*
 * GFOpenLayout logical units are exact across the three worlds we care about:
 *   1 point     = 80 OL units
 *   1 CSS pixel = 60 OL units (96 CSS px/in)
 *   1 twip      = 4 OL units (20 twips/point)
 *   1 inch      = 5760 OL units
 */
typedef int32_t ol_unit;
#define OL_UNITS_PER_POINT 80
#define OL_UNITS_PER_CSSPX 60
#define OL_UNITS_PER_TWIP 4
#define OL_UNITS_PER_INCH 5760

#define OL_POINT(v) ((ol_unit)((v) * OL_UNITS_PER_POINT))
#define OL_CSSPX(v) ((ol_unit)((v) * OL_UNITS_PER_CSSPX))
#define OL_TWIP(v) ((ol_unit)((v) * OL_UNITS_PER_TWIP))

typedef uint32_t ol_id;

typedef struct ol_document ol_document;
typedef struct ol_node ol_node;

typedef struct {
    ol_unit x;
    ol_unit y;
    ol_unit width;
    ol_unit height;
} ol_rect;

/* Resizable Office/Web drawing geometry uses thousandths of its bounds.
 * Keeping points normalised means page placement and zoom never rewrite or
 * allocate a path. */
typedef struct {
    int32_t x, y;
} ol_point;

#define OL_SHAPE_COORDS 1000
#define OL_MAX_SHAPE_POINTS 128

typedef enum {
    OL_SHAPE_NONE = 0,
    OL_SHAPE_RECT,
    OL_SHAPE_ROUND_RECT,
    OL_SHAPE_ELLIPSE,
    OL_SHAPE_LINE,
    OL_SHAPE_POLYLINE,
    OL_SHAPE_POLYGON
} ol_shape_type;

enum {
    OL_SHAPE_ARROW_START = 1u << 0,
    OL_SHAPE_ARROW_END   = 1u << 1
};

typedef enum {
    OL_STROKE_SOLID = 0,
    OL_STROKE_DASHED,
    OL_STROKE_DOTTED,
    OL_STROKE_DASH_DOT
} ol_stroke_style;

typedef struct {
    uint32_t size;             /* sizeof(ol_shape_style) */
    uint32_t type;             /* ol_shape_type */
    uint32_t fill_argb;        /* alpha 0: no fill */
    uint32_t stroke_argb;      /* alpha 0 or stroke_width 0: no stroke */
    ol_unit stroke_width;
    int32_t radius_permille;   /* round rectangle corner, 0..500 */
    uint32_t flags;            /* OL_SHAPE_... */
    uint32_t stroke_style;     /* ol_stroke_style */
    int32_t rotation_tenths;   /* 0.14: anticlockwise tenths of a degree */
} ol_shape_style;

/* Source-image crop insets in thousandths. The four insets are non-negative
 * and opposite sides must add to less than OL_SHAPE_COORDS. */
typedef struct {
    int32_t left, top, right, bottom;
} ol_image_crop;

/* Immutable, node-owned data referenced by a display op. It remains valid
 * until the document is freed or the node's graphic is changed and laid out
 * again. Applications must not free it or its points. */
typedef struct {
    ol_shape_style shape;
    int32_t point_count;
    const ol_point *points;
    ol_image_crop crop;
} ol_graphic;

typedef enum {
    OL_ROLE_DOCUMENT = 0,
    OL_ROLE_BLOCK,
    OL_ROLE_PARAGRAPH,
    OL_ROLE_HEADING,
    OL_ROLE_TEXT,
    OL_ROLE_LINK,
    OL_ROLE_IMAGE,
    OL_ROLE_LIST,
    OL_ROLE_LIST_ITEM,
    OL_ROLE_TABLE,
    OL_ROLE_ROW,
    OL_ROLE_CELL,
    OL_ROLE_BUTTON,
    OL_ROLE_INPUT
} ol_role;

typedef enum {
    OL_DISPLAY_BLOCK = 0,
    OL_DISPLAY_INLINE,
    OL_DISPLAY_NONE,
    /* 0.9: in the line like a picture, its box laid out as a block: as
     * wide as box.width (or width_permille of the line), or as its content
     * up to the line's width; standing on the baseline by its bottom (its
     * margin's), or at the line's top, middle or bottom (box.valign). It
     * goes to the next line, as a word does, when it does not fit. */
    OL_DISPLAY_INLINE_BLOCK
} ol_display;

typedef enum {
    OL_ALIGN_START = 0,
    OL_ALIGN_CENTER,
    OL_ALIGN_END,
    OL_ALIGN_JUSTIFY      /* 0.2: every line but a paragraph's last fills the width */
} ol_text_align;

enum {
    OL_TEXT_BOLD      = 1u << 0,
    OL_TEXT_ITALIC    = 1u << 1,
    OL_TEXT_UNDERLINE = 1u << 2,
    OL_TEXT_MONO      = 1u << 3,
    OL_TEXT_STRIKE    = 1u << 4,
    OL_TEXT_SUPER     = 1u << 5,
    OL_TEXT_SUB       = 1u << 6
};

/* 0.2: the font a text uses, as a number the measure callback and the
 * renderer agree on (0: the default), in text_flags' top 16 bits, so an
 * ol_style stays the size it was. */
#define OL_TEXT_FONT(n) (((uint32_t)(n) & 0xffffu) << 16)
#define OL_TEXT_FONT_OF(flags) ((unsigned)((flags) >> 16))

enum {
    OL_ACTION_ACTIVATE = 1u << 0,
    OL_ACTION_FOCUS    = 1u << 1,
    OL_ACTION_EDIT     = 1u << 2,
    OL_ACTION_SELECT   = 1u << 3
};

enum {
    OL_STATE_DISABLED = 1u << 0,
    OL_STATE_CHECKED  = 1u << 1,
    OL_STATE_SELECTED = 1u << 2,
    OL_STATE_FOCUSED  = 1u << 3
};

typedef struct {
    ol_display display;
    ol_text_align text_align;

    ol_unit margin_top;
    ol_unit margin_right;
    ol_unit margin_bottom;
    ol_unit margin_left;

    ol_unit padding_top;
    ol_unit padding_right;
    ol_unit padding_bottom;
    ol_unit padding_left;

    ol_unit font_size;
    ol_unit line_height;

    uint32_t foreground;  /* AARRGGBB */
    uint32_t background;  /* AARRGGBB, alpha 0 means transparent */
    uint32_t text_flags;
} ol_style;

typedef struct {
    ol_unit width;
    ol_unit ascent;
    ol_unit descent;
} ol_text_metrics;

typedef int (*ol_measure_text_fn)(void *userdata,
                                  const char *utf8,
                                  size_t length,
                                  const ol_style *style,
                                  ol_text_metrics *metrics);

typedef enum {
    OL_OP_TEXT = 1,
    OL_OP_FILL_RECT,
    OL_OP_IMAGE,
    OL_OP_RULE,
    OL_OP_SHAPE
} ol_op_type;

typedef struct {
    ol_id node_id;
    ol_rect bounds;
    ol_unit baseline;
    size_t text_offset;
    size_t text_length;
    ol_style style;
} ol_text_op;

typedef struct {
    ol_id node_id;
    ol_rect bounds;
    uint32_t argb;
} ol_fill_rect_op;

typedef struct {
    ol_id node_id;
    ol_rect bounds;
    int image_id;
} ol_image_op;

typedef struct {
    ol_id node_id;
    ol_unit x1, y1, x2, y2;
    ol_unit thickness;
    uint32_t argb;
} ol_rule_op;

typedef struct {
    ol_op_type type;
    union {
        ol_text_op text;
        ol_fill_rect_op fill_rect;
        ol_image_op image;
        ol_rule_op rule;
    } u;
    /* 0.2: the page the op is on, from 0. After ol_layout_pages the op's
     * coordinates are the page's (its top left corner is 0,0, margins
     * included); after ol_layout every op is on page 0. */
    int32_t page;
    /* Version 13 extension, deliberately after page: every field through
     * page has the same offset as libraries 2..12. A positive clip is
     * present; an internal negative height is an empty intersection. */
    const ol_graphic *graphic;
    ol_rect clip;
} ol_display_op;

ol_document *ol_document_new(void);
void ol_document_free(ol_document *doc);
/* 0.5: room for ops display ops, made now in one block; 1, or 0 when memory runs out. */
int ol_document_reserve(ol_document *doc, size_t ops);
/* 0.5: a new document with room for ops display ops, that block taken before any other. A program that lays
 * a document out again after each change (a new ol_document each time, the last one freed just before) can
 * give the same room each time, a little more than the last layout's ol_display_count: the display list
 * then takes back the very block the last one had, and doesn't grow by doubling. On an Amiga libnix's malloc
 * kept the doubled blocks of a 30-page document's list (3 MB) in its pools after every relayout. NULL when
 * memory runs out. */
ol_document *ol_document_new_reserved(size_t ops);
ol_node *ol_document_root(ol_document *doc);
const ol_node *ol_document_root_const(const ol_document *doc);

void ol_style_init(ol_style *style);
ol_node *ol_node_append(ol_document *doc, ol_node *parent, ol_role role,
                        const char *utf8);
ol_id ol_node_id(const ol_node *node);
ol_role ol_node_role(const ol_node *node);
const char *ol_node_text(const ol_node *node);
const char *ol_node_name(const ol_node *node);
const char *ol_node_value(const ol_node *node);
const char *ol_node_href(const ol_node *node);
const ol_style *ol_node_style(const ol_node *node);
ol_rect ol_node_bounds(const ol_node *node);

int ol_node_set_text(ol_node *node, const char *utf8);
int ol_node_set_name(ol_node *node, const char *utf8);
int ol_node_set_value(ol_node *node, const char *utf8);
int ol_node_set_href(ol_node *node, const char *utf8);
void ol_node_set_style(ol_node *node, const ol_style *style);
void ol_node_set_actions(ol_node *node, uint32_t actions);
void ol_node_set_states(ol_node *node, uint32_t states);
uint32_t ol_node_actions(const ol_node *node);
uint32_t ol_node_states(const ol_node *node);
void ol_node_set_image(ol_node *node, int image_id, ol_unit width, ol_unit height);
void ol_shape_style_init(ol_shape_style *style);
/* Copies style and points into the node. style NULL or OL_SHAPE_NONE clears
 * the shape. A polygon needs at least three points; a line/polyline two. */
int ol_node_set_shape(ol_node *node, const ol_shape_style *style,
                      int count, const ol_point *points);
/* Sets a source crop for image ops; NULL clears it. Returns 0 for invalid
 * insets. Crop metadata is ignored by pre-13 renderers, which draw the full
 * image as before. */
int ol_node_set_image_crop(ol_node *node, const ol_image_crop *crop);
/* Makes an allocation-free device path for a shape, including primitive
 * flattening and rotation about bounds' centre. Returns the point count, or
 * 0 when graphic/bounds/output are invalid or capacity is too small. */
int ol_shape_path(const ol_graphic *graphic, const ol_rect *bounds,
                  ol_point *points, int capacity, int *closed);

const ol_node *ol_node_by_id(const ol_document *doc, ol_id id);
const ol_node *ol_node_parent(const ol_node *node);
const ol_node *ol_find_role_name(const ol_document *doc, ol_role role,
                                 const char *name);
const ol_node *ol_hit_test(const ol_document *doc, ol_unit x, ol_unit y);
const ol_node *ol_hit_action(const ol_document *doc, ol_unit x, ol_unit y,
                             uint32_t action);

int ol_layout(ol_document *doc, ol_unit viewport_width,
              ol_measure_text_fn measure, void *measure_userdata);
ol_unit ol_document_content_height(const ol_document *doc);
uint32_t ol_document_generation(const ol_document *doc);

size_t ol_display_count(const ol_document *doc);
const ol_display_op *ol_display_get(const ol_document *doc, size_t index);

const char *ol_role_name(ol_role role);

/* ---- Documents: pages, tables, frames (0.2) ---------------------------- */

/* What a node's ol_style has no room for. Set with ol_node_set_box; a
 * node without one has ol_box_style_init's values. */
enum {
    OL_BOX_PAGE_BREAK_BEFORE = 1u << 0,  /* starts a new page */
    OL_BOX_KEEP_TOGETHER     = 1u << 1,  /* never split across pages */
    OL_BOX_KEEP_WITH_NEXT    = 1u << 2,  /* on the same page as the next block's first line */
    OL_BOX_ABSOLUTE          = 1u << 3,  /* a frame at box.frame, out of the flow */
    OL_BOX_EMPTY_LINE        = 1u << 4,  /* with no content, still one line high
                                            (a word processor's empty paragraph) */
    /* 0.7: every line of the block's own text exactly its style's
     * line_height high (a word processor's exact line spacing), even when
     * its text is taller: the text stands on the line's bottom and its top
     * runs over the line above. Without it line_height is the least a line
     * is (a word processor's "at least"). */
    OL_BOX_EXACT_LINES       = 1u << 5,
    /* 0.8: the block's children side by side, left to right, as a CSS
     * flex row (a program's toolbar, a row of buttons, a list beside its
     * panel): each as wide as box.width, or as its content, sharing what
     * room is left by box.grow (or giving up room in proportion when there
     * is too little); as tall as the row (box.align); gap apart. A row
     * with nothing that grows puts its children at its text_align. */
    OL_BOX_ROW               = 1u << 6,
    /* 0.9: each side its own border, box.border_widths and border_argbs
     * (top, right, bottom, left), in place of border_width, border_argb and
     * border_sides. These borders take room: they are inside the box, its
     * padding and content inside them (CSS's border box), and each is drawn
     * as a rule along its side within the box. A side 0 wide has none. */
    OL_BOX_SIDE_BORDERS      = 1u << 7,
    /* 0.9: CSS's margin-left and margin-right auto: a block narrower than
     * its containing block (box.width, width_permille or max_width) goes to
     * its right (OL_BOX_AUTO_LEFT alone) or in its middle (both). */
    OL_BOX_AUTO_LEFT         = 1u << 8,
    OL_BOX_AUTO_RIGHT        = 1u << 9,
    /* 0.9: on a table's row: a header, drawn again at the top of every page
     * the table goes on to (ol_layout_pages). The header is the rows that
     * have it from the table's first row on, one after another; it is kept
     * on the page of the first row after it, and not repeated when it is
     * as high as a page or higher. The copies are ops of the header's
     * nodes, whose bounds stay where the header first is: ol_hit_test_page
     * finds nothing over a copy. Not in an inline-block or a frame
     * anchored to its page. A frame placed from its block moves down with
     * it below a copy; the body's text on a page that starts with a copy
     * goes round a frame placed from the page or its margins as if the
     * copy were not there. */
    OL_BOX_REPEAT_HEADER     = 1u << 10,
    /* 0.11: an OL_BOX_ABSOLUTE frame with no frame.width is as wide as its
     * content (its text on one line, its widest child; box.width when it
     * has one), as CSS's absolute boxes are; without it, one CSS pixel. */
    OL_BOX_FIT_CONTENT       = 1u << 11,
    /* 0.13: descendants are clipped to this box's border box. The effective
     * intersection is carried by every affected op, so drawing is stateless
     * and partial redraw can begin at any op. */
    OL_BOX_CLIP_CONTENT      = 1u << 12,
    /* 0.18: the block's children in a grid of box.grid_columns columns,
     * row after row in their order, box.gap apart both ways (MUI's column
     * groups, GTK's tables). A column is as wide as its widest cell asks;
     * what room is left goes to the columns whose cells grow; a row is as
     * tall as its tallest cell, its cells across it as box.align says. */
    OL_BOX_GRID              = 1u << 13
};

/* 0.8: how a row's children stand across it (box.align) */
enum {
    OL_CROSS_STRETCH = 0,      /* as tall as the row */
    OL_CROSS_START   = 1,      /* at its top */
    OL_CROSS_CENTRE  = 2,
    OL_CROSS_END     = 3
};

/* 0.9: where a table cell's content stands in a row taller than it, and
 * where an inline-block stands in its line (box.valign). A cell's
 * OL_VALIGN_BASELINE is its top, as before 0.9; an inline-block's is its
 * bottom on the line's baseline, as a picture's is. */
enum {
    OL_VALIGN_BASELINE = 0,
    OL_VALIGN_TOP,
    OL_VALIGN_MIDDLE,
    OL_VALIGN_BOTTOM
};

enum {
    OL_BORDER_TOP    = 1u << 0,
    OL_BORDER_RIGHT  = 1u << 1,
    OL_BORDER_BOTTOM = 1u << 2,
    OL_BORDER_LEFT   = 1u << 3,
    OL_BORDER_ALL    = 15u
};

typedef struct {
    uint32_t size;            /* sizeof(ol_box_style) */
    uint32_t flags;           /* OL_BOX_... */
    ol_unit text_indent;      /* the first line, from the left edge; below 0 hangs */
    /* OL_BOX_ABSOLUTE: where the frame is, from its containing block's
     * content corner (the galley's top left for the document's own
     * frames); height 0 grows with its content */
    ol_rect frame;
    ol_unit border_width;     /* 0: none */
    uint32_t border_argb;
    uint32_t border_sides;    /* OL_BORDER_... */
    int32_t column_span;      /* a table cell's columns; 0 or 1: one */
    /* 0.4: an OL_BOX_ABSOLUTE frame anchored to its page: where frame.x
     * and frame.y are measured from (OL_ANCHOR_X and OL_ANCHOR_Y), and
     * whether it is under the text (OL_ANCHOR_BEHIND) or over it. 0: a
     * 0.2 frame, from its containing block, in the flow's order. Through
     * gfopenlayout.library, OL_BoxStyleInit stops before this field (size
     * OL_BOX_STYLE_SIZE_0_3): a program that anchors frames sets size to
     * sizeof(ol_box_style) and anchor itself, and opens version 5. */
    uint32_t anchor;
    /* 0.6: how the body's text goes round an anchored frame (OL_WRAP_...),
     * and the room it keeps from the frame's edges: top, right, bottom,
     * left. Through gfopenlayout.library, a program sets size to
     * sizeof(ol_box_style) and opens version 7; with an older size its
     * frames have no wrap. */
    uint32_t wrap;
    ol_unit wrap_gap[4];
    /* 0.8: programs' windows (CSS width, height, flex-grow, gap and
     * align-items). Through gfopenlayout.library, a program sets size to
     * sizeof(ol_box_style) and opens version 8; with an older size these
     * are 0 (not set).
     * - width: the box's own width, margins outside it; 0: what its
     *   containing block gives (in a row: its content's);
     * - height: its own height; 0: its content's. A block's children that
     *   grow share what its height leaves below them;
     * - grow: in a row, its share of the room left across; in a block with
     *   a height, its share of the room left below;
     * - gap: between a row's children, and between a block's;
     * - align: a row's children across it (OL_CROSS_...). */
    ol_unit width;
    ol_unit height;
    int32_t grow;
    ol_unit gap;
    uint32_t align;
    /* 0.9: web boxes (CSS's per-side borders, percentage widths, min-width,
     * max-width and vertical-align, for a browser and a word processor).
     * Through gfopenlayout.library, a program sets size to
     * sizeof(ol_box_style) and opens version 9; with an older size these
     * are 0 (not set).
     * - border_widths, border_argbs: each side's border, with
     *   OL_BOX_SIDE_BORDERS;
     * - min_width, max_width: the least and the most the box is wide, its
     *   margins outside them; the least wins over the most, as in CSS;
     * - width_permille: its width in thousandths of its containing block's
     *   content width (a table cell's: the table's), when it has no width;
     * - max_width_permille: the most, the same way; with max_width too, the
     *   narrower;
     * - valign: a table cell's content in its row, an inline-block in its
     *   line (OL_VALIGN_...). */
    ol_unit border_widths[4];   /* top, right, bottom, left; used when flags has OL_BOX_SIDE_BORDERS */
    uint32_t border_argbs[4];   /* AARRGGBB each */
    ol_unit min_width, max_width;      /* 0: none */
    int32_t width_permille;            /* of the containing block's content width; 0: none; an explicit width wins */
    int32_t max_width_permille;        /* 0: none */
    uint32_t valign;                   /* OL_VALIGN_... */
    /* 0.15: newspaper columns inside a bounded absolute text frame. The
     * frame's content flows from the top of one column to the next; 0 or 1
     * is ordinary single-column flow. The gap is ignored for one column.
     * A frame needs a positive frame.height, and 2..16 columns whose gaps
     * leave at least one layout unit for each column. Content beyond the
     * last column is not displayed. Older box sizes leave both fields 0. */
    int32_t text_columns;
    ol_unit text_column_gap;
    /* 0.18: OL_BOX_GRID's count of columns (1 to OL_MAX_COLUMNS; 0: 1).
     * Older box sizes leave it 0. */
    int32_t grid_columns;
} ol_box_style;

/* An anchored frame (box.anchor not 0) is laid out on its own and placed
 * on the page its containing block starts on, without moving the page's
 * breaks: across from the page's left edge (OL_ANCHOR_PAGE), from its
 * left margin (OL_ANCHOR_MARGIN) or from the containing block's content
 * corner (OL_ANCHOR_BLOCK), and down from the page's top edge, its top
 * margin or the block's content corner the same way. Its ops come before
 * the page's others (OL_ANCHOR_BEHIND), or after them, so a picture can
 * stand behind the text and a text box in front of it. After ol_layout
 * the page is the galley, without margins. Its nodes are left out of
 * ol_hit_test and its text out of ol_hit_text. */
enum {
    OL_ANCHOR_BLOCK = 0,
    OL_ANCHOR_PAGE = 1,
    OL_ANCHOR_MARGIN = 2
};
#define OL_ANCHOR_X(from) ((uint32_t)(from) & 3u)
#define OL_ANCHOR_Y(from) (((uint32_t)(from) & 3u) << 2)
#define OL_ANCHOR_X_OF(anchor) ((anchor) & 3u)
#define OL_ANCHOR_Y_OF(anchor) (((anchor) >> 2) & 3u)
#define OL_ANCHOR_BEHIND (1u << 4)
#define OL_ANCHOR_ON (1u << 5)      /* anchored even with both from the block and in front */

/* The size of an ol_box_style before 0.4's anchor, as programs built for
 * gfopenlayout.library 3 and 4 have it */
#define OL_BOX_STYLE_SIZE_0_3 offsetof(ol_box_style, anchor)

/* 0.6: text round an anchored frame (box.wrap). The body's lines on the
 * frame's page keep wrap_gap from it:
 * - OL_WRAP_SQUARE: beside it, on either side that has room (a word
 *   processor's square and tight wrapping): a line that a frame crosses
 *   is the stretches either side of it, each at least OL_WRAP_MIN wide,
 *   filled left to right; with none, the line goes below the frame;
 * - OL_WRAP_TOP_BOTTOM: above and below it, none beside it.
 * Only the body's own lines wrap: not those in a table's cells, nor in a
 * frame. A line meets a frame when it is on the frame's page (it fits in
 * that page's body) and crosses it at its nominal height (its block's
 * line_height, or 1.25 times its font size), so a line much taller than
 * that may still reach a frame below it. A wrapped frame goes on the page
 * its paragraph's first line is on. Its place is worked out where its
 * paragraph starts, so the lines after it wrap round it at once; a frame
 * above its paragraph, or one whose page moves, takes ol_layout_pages
 * another pass (OL_WRAP_PASSES in all at most), until the frames stay
 * where the lines were wrapped round them. A document with no frame to
 * wrap round is laid out once, as before. */
enum {
    OL_WRAP_NONE = 0,
    OL_WRAP_SQUARE = 1,
    OL_WRAP_TOP_BOTTOM = 2
};
#define OL_WRAP_MIN OL_CSSPX(48)
#define OL_WRAP_PASSES 4

/* 0.16: fixed exclusion rectangles inside a block or bounded text frame.
 * bounds are from the node's border-box corner; the node's own text and
 * descendant blocks flow around them using the existing OL_WRAP_SQUARE or
 * OL_WRAP_TOP_BOTTOM line fitter. They do not draw anything and do not
 * affect sibling nodes, so an application can describe a picture or shape
 * that overlaps a Publisher-style text frame without duplicating it in the
 * display list. gaps are top, right, bottom and left. */
typedef struct {
    ol_rect bounds;
    uint32_t wrap;
    ol_unit gap[4];
} ol_exclusion;

#define OL_MAX_EXCLUSIONS 1024

/* Copies the exclusions. count 0 clears them. Returns 0 for invalid input or
 * no memory. Existing box and document ABI fields do not move. */
int ol_node_set_exclusions(ol_node *node, int count,
                           const ol_exclusion *exclusions);

/* 0.17: flattens a node-owned closed shape into conservative horizontal
 * exclusion bands. bounds are in the target text frame's coordinates and
 * therefore need not be the shape node's layout bounds. The returned bands
 * use OL_WRAP_SQUARE and copy gap (top, right, bottom, left); the normal
 * exclusion fitter supplies the requested clearance around their union.
 * Adjacent equal-width bands are coalesced. Returns their count, or 0 for
 * an open/non-shape node, invalid input or insufficient output capacity. */
#define OL_CONTOUR_BANDS_DEFAULT 16
#define OL_CONTOUR_BANDS_MAX 64
int ol_node_shape_exclusions(const ol_node *shape, const ol_rect *bounds,
                             const ol_unit *gap, int bands,
                             ol_exclusion *out, int capacity);
/* The size of an ol_box_style before 0.6's wrap, as programs built for
 * gfopenlayout.library 5 and 6 have it */
#define OL_BOX_STYLE_SIZE_0_5 offsetof(ol_box_style, wrap)
/* The size of an ol_box_style before 0.8's width, height, grow, gap and
 * align, as programs built for gfopenlayout.library 7 have it */
#define OL_BOX_STYLE_SIZE_0_7 offsetof(ol_box_style, width)
/* The size of an ol_box_style before 0.9's web boxes, as programs built for
 * gfopenlayout.library 8 have it */
#define OL_BOX_STYLE_SIZE_0_8 offsetof(ol_box_style, border_widths)
/* The size before 0.15's bounded text-frame columns. Programs using these
 * fields through gfopenlayout.library open OPENLAYOUTLIB_COLUMNS_VERSION. */
#define OL_BOX_STYLE_SIZE_0_14 offsetof(ol_box_style, text_columns)
/* The size before 0.18's grid. Programs using it through
 * gfopenlayout.library open OPENLAYOUTLIB_GRID_VERSION. */
#define OL_BOX_STYLE_SIZE_0_17 offsetof(ol_box_style, grid_columns)

/* 0.6: where an anchored frame went after ol_layout_pages (or ol_layout,
 * in the galley): its page and its rectangle on it, the frame's width and
 * its height as laid out. For an editor to find the text box under the
 * pointer and draw round it. 0 when the node is not an anchored frame that
 * was laid out. */
int ol_frame_place(const ol_document *doc, const ol_node *frame, int *page, ol_rect *rect);

void ol_box_style_init(ol_box_style *box);
/* Copies box->size bytes at most, so a smaller older ol_box_style works. */
void ol_node_set_box(ol_node *node, const ol_box_style *box);
const ol_box_style *ol_node_box(const ol_node *node);

/* A table's column widths (at most OL_MAX_COLUMNS). Without them, its
 * columns share its width equally; with them, they are scaled down when
 * they add up to more than the table's width. */
#define OL_MAX_COLUMNS 64
int ol_node_set_columns(ol_node *node, int count, const ol_unit *widths);

/* A block's tab stops (at most OL_MAX_TABS), from its left edge, left
 * aligned (ol_node_set_tab_stops below sets them with their alignment). A tab in its text goes on to the next stop; past the last, to
 * the next of every OL_TAB_INTERVAL from the left edge. A first line that
 * hangs (text_indent below 0) stops at the left edge first, so a list's
 * number and tab line its text up with the lines under it. */
#define OL_MAX_TABS 32
#define OL_TAB_INTERVAL (OL_UNITS_PER_INCH / 2)
int ol_node_set_tabs(ol_node *node, int count, const ol_unit *stops);

/* 0.3: tab stops that align what follows them, and leaders. A right stop
 * ends the text after its tab (up to the next tab or the line's end) at
 * the stop, a centred one centres it there, a decimal one puts its first
 * '.' there (its end, when it has none); text that has no room keeps to
 * the left. A stop may be past the line's end (a contents page's page
 * numbers at the right margin or beyond). A leader fills the tab's space:
 * GFOpenLayout puts it in the tab's text op, in its style's text_flags
 * (OL_TEXT_LEADER_OF), for the renderer to draw. */
typedef enum {
    OL_TAB_LEFT = 0,
    OL_TAB_CENTER,
    OL_TAB_RIGHT,
    OL_TAB_DECIMAL
} ol_tab_align;

typedef enum {
    OL_LEADER_NONE = 0,
    OL_LEADER_DOT,            /* ....... */
    OL_LEADER_HYPHEN,         /* ------- */
    OL_LEADER_UNDERSCORE,     /* _______ */
    OL_LEADER_MIDDLE_DOT,     /* · · · · */
    OL_LEADER_HEAVY           /* a thick rule */
} ol_tab_leader;

typedef struct {
    ol_unit position;         /* from the block's left edge */
    int32_t align;            /* ol_tab_align */
    int32_t leader;           /* ol_tab_leader */
} ol_tab_stop;

int ol_node_set_tab_stops(ol_node *node, int count, const ol_tab_stop *stops);

/* A tab's leader, in its text op's style.text_flags (bits 8 to 10, which
 * OL_TEXT_... leave free). */
#define OL_TEXT_LEADER(leader) (((uint32_t)(leader) & 7u) << 8)
#define OL_TEXT_LEADER_OF(flags) ((unsigned)(((flags) >> 8) & 7u))

/* Pages. ol_layout_pages lays the document out at the page's width less
 * its margins, as a galley, then cuts the galley into pages: between
 * lines, never inside a line, a picture, a table row, a frame or a
 * OL_BOX_KEEP_TOGETHER block (unless it is taller than a page), and
 * before every OL_BOX_PAGE_BREAK_BEFORE block. A page does not start with
 * the space between paragraphs. Node bounds stay in galley coordinates;
 * ol_hit_test_page and ol_caret_box answer in a page's. */
typedef struct {
    ol_unit width, height;
    ol_unit margin_top, margin_right, margin_bottom, margin_left;
} ol_page;

int ol_layout_pages(ol_document *doc, const ol_page *page,
                    ol_measure_text_fn measure, void *measure_userdata);
/* Lay a continuous flow through equal newspaper columns, top-to-bottom and
 * then left-to-right on each physical page. count 1 is ol_layout_pages.
 * gap is between columns. Display ranges, hit testing and caret boxes use
 * physical page coordinates. */
int ol_layout_pages_columns(ol_document *doc, const ol_page *page,
                            int count, ol_unit gap,
                            ol_measure_text_fn measure, void *measure_userdata);
typedef enum {
    OL_SECTION_CONTINUOUS = 0,
    OL_SECTION_NEW_PAGE,
    OL_SECTION_ODD_PAGE,
    OL_SECTION_EVEN_PAGE
} ol_section_break;

/* A section starts at a top-level flow node. Its paragraphs are laid out at
 * that section's column width. The first section's break is ignored. */
typedef struct {
    ol_id first_node;
    int columns;
    ol_unit gap;
    ol_section_break break_before;
} ol_page_section;

/* Variable column geometry by section. New/odd/even page sections are
 * supported. A multi-column section is balanced before a continuous
 * successor, which shares the rectangular lower band on the physical page. */
int ol_layout_pages_sections(ol_document *doc, const ol_page *page,
                             const ol_page_section *sections, int section_count,
                             ol_measure_text_fn measure, void *measure_userdata);
/* 1 after ol_layout; the number of pages after ol_layout_pages. */
int ol_page_count(const ol_document *doc);
/* The display ops of one page are together, in page order: the first and
 * how many. 0 when the page does not exist. */
int ol_display_page_range(const ol_document *doc, int page,
                          size_t *first, size_t *count);

/* The first rendered box belonging to node or one of its descendants, in
 * physical page coordinates. This is the stable-node bridge for navigators,
 * generated references and editor object outlines. */
int ol_node_page_box(const ol_document *doc, ol_id node_id,
                     int *page, ol_rect *box);

/* The deepest node at (x, y) on a page, in its coordinates. */
const ol_node *ol_hit_test_page(const ol_document *doc, int page,
                                ol_unit x, ol_unit y);

/* Editing: the character at a point, and where the caret goes for one.
 * Offsets are bytes into a text node's UTF-8 text, on character
 * boundaries. The measure callback must be the one the layout used. */
typedef struct {
    ol_id node_id;            /* a text, link or other inline node */
    size_t offset;
} ol_text_position;

/* The text position nearest to (x, y) on a page: on the line at y (or the
 * nearest line), before or after the character under x. */
int ol_hit_text(const ol_document *doc, int page, ol_unit x, ol_unit y,
                ol_measure_text_fn measure, void *measure_userdata,
                ol_text_position *pos);
/* The caret for a text position: its page and a zero-width box the
 * line's height. 0 when the position is not laid out. */
int ol_caret_box(const ol_document *doc, const ol_text_position *pos,
                 ol_measure_text_fn measure, void *measure_userdata,
                 int *page, ol_rect *box);

/* ---- Drawing at the display's resolution (0.2) --------------------------
 *
 * Layout is in units and never changes with the screen; a renderer draws
 * a page straight at its display's pixels through an ol_scale. pixels =
 * units * num / den: num / den is set once (dots per inch and zoom, or a
 * length to fit), then every coordinate is one multiply and shift, no
 * division. Edges are rounded, not lengths, so boxes that touch in units
 * touch in pixels at every zoom, and each word lands where it belongs.
 * Text is measured for layout at full resolution and drawn with a font
 * at the scaled pixel size, so line and page breaks do not move with the
 * zoom. Nothing is drawn into a big bitmap and scaled down: ol_draw_page
 * hands the renderer only the ops that cross the area being drawn,
 * already in pixels. */
typedef struct {
    int32_t num, den;         /* pixels per unit, as a fraction */
    int32_t origin_x;         /* where the page's top left corner is, in pixels */
    int32_t origin_y;
    uint32_t mul;             /* num / den in 8.24 fixed point: ol_scale_set's */
} ol_scale;

typedef struct {
    int32_t x, y, width, height;
} ol_pixel_rect;

/* num / den pixels a unit; the origin is left as it was. */
void ol_scale_set(ol_scale *scale, int32_t num, int32_t den);
/* dpi pixels an inch at zoom_percent (72, 100: a point a pixel). */
void ol_scale_dpi(ol_scale *scale, int dpi, int zoom_percent);
/* length units in pixels pixels: a page's width to a window's. */
void ol_scale_fit(ol_scale *scale, ol_unit length, int32_t pixels);

/* A position in pixels, and back (for the pointer). */
int32_t ol_scale_x(const ol_scale *scale, ol_unit x);
int32_t ol_scale_y(const ol_scale *scale, ol_unit y);
ol_unit ol_unscale_x(const ol_scale *scale, int32_t px);
ol_unit ol_unscale_y(const ol_scale *scale, int32_t py);
/* A length (a font size, a rule): at least one pixel when it is not 0. */
int32_t ol_scale_length(const ol_scale *scale, ol_unit length);
/* A box by its edges. */
ol_pixel_rect ol_scale_rect(const ol_scale *scale, ol_rect r);

/* One display op in pixels. */
typedef struct {
    const ol_display_op *op;  /* the op in units: its node, text, style, image */
    ol_pixel_rect box;        /* text, fill, image: edges rounded */
    int32_t baseline;         /* text */
    int32_t font_pixels;      /* text: the font's size at this scale */
    int32_t x1, y1, x2, y2;   /* rule */
    int32_t thickness;        /* rule: at least one pixel */
    /* Version 13 extension. clip width/height 0 means none. For shapes,
     * thickness is the scaled stroke width. */
    ol_pixel_rect clip;
} ol_device_op;

typedef void (*ol_draw_fn)(void *userdata, const ol_device_op *op);

/* Draws one page: calls draw, in drawing order, for each of its ops that
 * crosses clip (pixels; NULL: all of them), in pixels through scale.
 * Returns how many it drew. */
size_t ol_draw_page(const ol_document *doc, int page, const ol_scale *scale,
                    const ol_pixel_rect *clip, ol_draw_fn draw, void *userdata);

/* ---- CSS: nodes as elements, and a document's stylesheets (GFCSS) ---------
 *
 * A node may say what element it is: its tag, its attributes (id, class
 * and style among them) and its states for :hover, :focus and the rest
 * (GFCSS_STATE_... in gfopenlayout_css.h). A document holds stylesheets in
 * the cascade's order and the viewport @media is evaluated against. Its
 * tagged nodes are then styled through GFCSS: ol_document_apply_styles
 * sets each one's ol_style and ol_box_style from the rules that match it
 * (its style attribute last), and ol_layout and ol_layout_pages do so
 * first by themselves when a sheet, an element or the width has changed
 * since. A text node with no tag under an element takes the element's
 * inherited style; other nodes with no tag keep the styles set on them, so
 * a document with no stylesheets lays out as it always did. ::before and
 * ::after with a string content are text nodes made first and last in
 * their element (ol_node_is_generated). The sheets must say how elements
 * are shown (a user-agent sheet: div{display:block} and so on): CSS's
 * initial display is inline. */
enum {
    OL_ORIGIN_USER_AGENT = 0,
    OL_ORIGIN_USER = 1,
    OL_ORIGIN_AUTHOR = 2
};

/* 1, or 0 when memory ran out. A NULL value takes the attribute away. */
int ol_node_set_tag(ol_node *node, const char *tag);
const char *ol_node_tag(const ol_node *node);
int ol_node_set_attr(ol_node *node, const char *name, const char *value);
const char *ol_node_attr(const ol_node *node, const char *name);
void ol_node_set_css_state(ol_node *node, uint32_t states);
uint32_t ol_node_css_state(const ol_node *node);
/* 1 for a text node made for a ::before or ::after */
int ol_node_is_generated(const ol_node *node);
/* Walking the tree */
const ol_node *ol_node_first_child(const ol_node *node);
const ol_node *ol_node_next_sibling(const ol_node *node);

/* A stylesheet after those the document has (the text is copied); 1, or
 * 0 when memory ran out. Mistakes in it are skipped as CSS says. */
int ol_document_add_stylesheet(ol_document *doc, const char *css, size_t length, int origin);
/* The viewport @media is evaluated against, and vw and vh are of; media
 * 0 screen, 1 print (GFCSS_MEDIA_...). Until it is set, its width is the
 * width laid out at, its height not known. */
void ol_document_set_viewport(ol_document *doc, ol_unit width, ol_unit height, int media);
/* Styles every tagged node now; percentages are of containing_width at
 * the top. 1, or 0 when memory ran out. */
int ol_document_apply_styles(ol_document *doc, ol_unit containing_width);
/* The sheets' @import rules, in order, for the caller to fetch: url (as
 * written, unescaped), and ol_document_add_import with the fetched text,
 * which takes the import's place in the cascade. */
int ol_document_import_count(const ol_document *doc);
const char *ol_document_import_url(const ol_document *doc, int index);
int ol_document_add_import(ol_document *doc, int index, const char *css, size_t length);

#ifdef __cplusplus
}
#endif
#endif
