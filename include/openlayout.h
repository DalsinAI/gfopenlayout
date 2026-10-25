#ifndef OPENLAYOUT_H
#define OPENLAYOUT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OL_VERSION "0.3"

/*
 * OpenLayout logical units are exact across the three worlds we care about:
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
    OL_DISPLAY_NONE
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
    OL_OP_RULE
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
} ol_display_op;

ol_document *ol_document_new(void);
void ol_document_free(ol_document *doc);
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
    OL_BOX_EMPTY_LINE        = 1u << 4   /* with no content, still one line high
                                            (a word processor's empty paragraph) */
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
    /* 0.3: an OL_BOX_ABSOLUTE frame anchored to its page: where frame.x
     * and frame.y are measured from (OL_ANCHOR_X and OL_ANCHOR_Y), and
     * whether it is under the text (OL_ANCHOR_BEHIND) or over it. 0: a
     * 0.2 frame, from its containing block, in the flow's order. */
    uint32_t anchor;
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
 * aligned. A tab in its text goes on to the next stop; past the last, to
 * the next of every OL_TAB_INTERVAL from the left edge. A first line that
 * hangs (text_indent below 0) stops at the left edge first, so a list's
 * number and tab line its text up with the lines under it. */
#define OL_MAX_TABS 32
#define OL_TAB_INTERVAL (OL_UNITS_PER_INCH / 2)
int ol_node_set_tabs(ol_node *node, int count, const ol_unit *stops);

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
/* 1 after ol_layout; the number of pages after ol_layout_pages. */
int ol_page_count(const ol_document *doc);
/* The display ops of one page are together, in page order: the first and
 * how many. 0 when the page does not exist. */
int ol_display_page_range(const ol_document *doc, int page,
                          size_t *first, size_t *count);

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
} ol_device_op;

typedef void (*ol_draw_fn)(void *userdata, const ol_device_op *op);

/* Draws one page: calls draw, in drawing order, for each of its ops that
 * crosses clip (pixels; NULL: all of them), in pixels through scale.
 * Returns how many it drew. */
size_t ol_draw_page(const ol_document *doc, int page, const ol_scale *scale,
                    const ol_pixel_rect *clip, ol_draw_fn draw, void *userdata);

#ifdef __cplusplus
}
#endif
#endif
