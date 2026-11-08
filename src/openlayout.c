#include "gfopenlayout.h"
#include "openlayout_css.h"

#include <stdlib.h>
#include <string.h>

struct ol_node {
    struct ol_document *doc;
    ol_id id;
    ol_role role;
    struct ol_node *parent;
    struct ol_node *first_child;
    struct ol_node *last_child;
    struct ol_node *next_sibling;

    char *text;
    char *name;
    char *value;
    char *href;
    void *css;                /* GFCSS: its element (tag, attributes, states): openlayout_css.c */

    ol_style style;
    ol_rect bounds;
    uint32_t actions;
    uint32_t states;

    int image_id;
    ol_unit image_width;
    ol_unit image_height;
    ol_graphic *graphic;      /* 0.13: lazily allocated shape/crop metadata */

    ol_box_style box;
    ol_unit *columns;
    int column_count;
    ol_unit *tabs;
    unsigned char *tab_info;  /* each stop's ol_tab_align | ol_tab_leader << 4 */
    int tab_count;
    ol_unit force_height;     /* 0.8: the height its row or block gives it this pass; 0: none */
};

/* A stretch of the galley a page may not end inside: a line, a table row,
 * a frame, a block kept together. */
typedef struct {
    ol_unit top;
    ol_unit bottom;
} ol_span;

/* A frame anchored to its page (box.anchor): its ops, first to end, in the
 * frame's own coordinates until they are placed on their page. */
typedef struct {
    size_t first, end;
    ol_unit x, y;             /* its containing block's content corner, in the galley */
    ol_rect frame;
    uint32_t anchor;
    const ol_node *node;      /* 0.6: the frame, for its wrap and its laid out size */
    ol_unit line_y;           /* 0.6: its block's first line (or its corner), whose page a wrapped frame goes on */
    int page;                 /* 0.6: a wrapped frame's page, once the pages are cut; -1: the corner's */
} ol_anchor;

/* 0.6: where the body's lines keep away from a frame they wrap round
 * (its wrap gaps included), in the galley: x0 to x1 across, y0 to y1 down;
 * only the lines of its page meet it, those from page_top on that fit
 * above page_bottom (its page's body) with no page break before them. */
typedef struct {
    const ol_node *node;      /* the frame */
    ol_unit x0, y0, x1, y1;
    ol_unit page_top, page_bottom;
    ol_unit h;                /* the frame's height, as laid out */
    int page;                 /* its page */
    uint32_t wrap;            /* OL_WRAP_SQUARE or OL_WRAP_TOP_BOTTOM */
    /* out of zone_same: */
    ol_unit line_dy;          /* how far below its block's corner the block's first line went (the last pass's) */
    int after;                /* down from its block, and below its corner: it meets no line before its block */
    int live;                 /* worked out in this pass (an after zone is used only then) */
} ol_wrap_zone;

/* 0.9: an inline-block on the line being filled: its place holder op (a
 * picture's, which the line's alignment moves along with the rest), and its
 * box laid out again where finish_line puts it. */
typedef struct {
    size_t op;
    ol_node *node;
    ol_unit w, h;             /* its width and height, margins included */
    ol_unit x, y;             /* where it goes, once its line is finished */
} ol_inline_box;

/* 0.9: a table's header rows (OL_BOX_REPEAT_HEADER), for the pages it goes
 * on to: top to bottom in the galley, their ops first to end, and the
 * table's rows' end. */
typedef struct {
    ol_unit top, bottom, end;
    size_t first, end_op;
} ol_repeat;

/* Whether two zones are the same, but for line_dy. */
static int zone_same(const ol_wrap_zone *a, const ol_wrap_zone *b)
{
    return !memcmp(a, b, offsetof(ol_wrap_zone, line_dy));
}

struct ol_document {
    ol_node *root;
    ol_node **nodes;
    size_t node_count;
    size_t node_capacity;

    ol_display_op *ops;
    size_t op_count;
    size_t op_capacity;

    ol_unit content_height;
    ol_unit viewport_width;
    uint32_t generation;
    ol_id next_id;
    void *css;                /* GFCSS: its stylesheets: openlayout_css.c */

    /* collected while laying out, for pages */
    ol_span *keeps;
    size_t keep_count;
    size_t keep_capacity;
    ol_unit *breaks;
    size_t break_count;
    size_t break_capacity;
    ol_anchor *anchors;
    size_t anchor_count;
    size_t anchor_capacity;
    int anchor_depth;         /* laying out an anchored frame */
    /* 0.6: the zones this pass's lines wrap round: the last pass's, each
     * worked out again where its paragraph starts in this one (on the
     * pages cut so far, live_starts); and the next pass's, from this one's */
    ol_wrap_zone *zones;
    size_t zone_count, zone_capacity;
    ol_wrap_zone *next_zones;
    size_t next_capacity;
    int wrap_off;             /* in a table or a frame: its lines do not wrap */
    int paging;               /* ol_layout_pages: frames go on pages */
    int zones_moved;          /* a zone moved where lines before its paragraph met it */
    ol_unit body_height;
    ol_unit *live_starts;     /* the pages cut so far in this pass: where each starts */
    int live_count, live_capacity;
    /* 0.9: the inline-blocks of the lines being filled, innermost last */
    ol_inline_box *iboxes;
    size_t ibox_count, ibox_capacity;
    /* 0.9: the tables' header rows that go again on the pages they go on to */
    ol_repeat *repeats;
    size_t repeat_count, repeat_capacity;

    /* pages: one after ol_layout, page 0 being the whole galley */
    int paged;
    ol_page page;
    int page_count;
    ol_unit *page_start;      /* galley y of each page's first content */
    ol_unit *page_end;        /* and of its end */
    size_t *page_first;       /* each page's first display op */
    size_t *page_ops;         /* and how many */
    ol_unit *page_shift;      /* 0.9: how far down a repeated table header puts its content */
    int *page_repeat;         /* 0.9: that header (doc->repeats); -1: none */
    int page_capacity;
    /* Equal newspaper columns: the narrow virtual pages (regions) combined
     * into physical pages after layout. Node bounds remain in their galley. */
    int column_count, region_count;
    ol_unit column_gap, column_width;
    ol_unit *region_start, *region_end, *region_shift;
    int *region_page;
    ol_unit *region_x, *region_y, *region_width;
};

typedef struct {
    ol_document *doc;
    ol_measure_text_fn measure;
    void *measure_userdata;

    ol_unit x0;
    ol_unit right;
    const ol_unit *tabs;      /* the block's tab stops, from x0 */
    const unsigned char *tab_info;
    int tab_count;
    /* a right, centred or decimal tab waiting for the end of the text after it */
    int tab_align;            /* ol_tab_align; OL_TAB_LEFT: none waits */
    size_t tab_op;            /* its op */
    ol_unit tab_stop;         /* the stop, as x */
    int line_aligned_tab;     /* the line has had one: not justified */
    ol_unit cursor_x;
    ol_unit line_top;
    ol_unit line_height;      /* the tallest style line height or picture */
    ol_unit ascent;           /* the line's tallest ascent and descent */
    ol_unit descent;
    ol_unit content_right;    /* the line's end, less its trailing spaces */
    size_t line_first_op;
    ol_text_align align;
    int has_content;
    int lines;
    /* 0.6: wrapping round frames. right is the end of the stretch of the
     * line being filled; the column ends at col_right. */
    int wraps;                /* the body's: its lines wrap round the zones (not a cell's or a frame's) */
    int room_pending;         /* the line's stretch is worked out when its first piece comes */
    int narrow;               /* the stretch is cut short by a zone */
    ol_unit col_right;
    ol_unit line_base;        /* where the line starts without zones (x0, or the first line's indent) */
    ol_unit nominal;          /* the height a line is taken to be, to find the zones it meets */
    ol_unit seg_height;       /* the tallest stretch finished on this line */
    ol_unit exact;            /* 0.7: every line exactly this high (OL_BOX_EXACT_LINES); 0: as its content */
    size_t ibox_first;        /* 0.9: its inline-blocks are doc->iboxes from here */
} ol_inline_flow;

static char *ol_strdup0(const char *s)
{
    size_t n;
    char *p;
    if (!s) s = "";
    n = strlen(s);
    p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

static int reserve_nodes(ol_document *doc, size_t extra)
{
    size_t need, cap;
    ol_node **p;
    need = doc->node_count + extra;
    if (need <= doc->node_capacity) return 1;
    cap = doc->node_capacity ? doc->node_capacity : 16;
    while (cap < need) cap *= 2;
    p = (ol_node **)realloc(doc->nodes, cap * sizeof(*p));
    if (!p) return 0;
    doc->nodes = p;
    doc->node_capacity = cap;
    return 1;
}

static int reserve_ops(ol_document *doc, size_t extra)
{
    size_t need, cap;
    ol_display_op *p;
    need = doc->op_count + extra;
    if (need <= doc->op_capacity) return 1;
    cap = doc->op_capacity ? doc->op_capacity : 64;
    while (cap < need) cap *= 2;
    p = (ol_display_op *)realloc(doc->ops, cap * sizeof(*p));
    if (!p) return 0;
    doc->ops = p;
    doc->op_capacity = cap;
    return 1;
}

int ol_document_reserve(ol_document *doc, size_t ops)
{
    ol_display_op *p;
    if (!doc) return 0;
    if (ops <= doc->op_capacity) return 1;
    p = (ol_display_op *)realloc(doc->ops, ops * sizeof(*p));
    if (!p) return 0;
    doc->ops = p;
    doc->op_capacity = ops;
    return 1;
}

static ol_display_op *append_op(ol_document *doc, ol_op_type type)
{
    ol_display_op *op;
    if (!reserve_ops(doc, 1)) return NULL;
    op = &doc->ops[doc->op_count++];
    memset(op, 0, sizeof(*op));
    op->type = type;
    return op;
}

static int rect_empty(ol_rect r)
{
    return r.width <= 0 || r.height <= 0;
}

static void rect_add(ol_rect *dst, ol_rect src)
{
    ol_unit x1, y1, x2, y2;
    if (!dst || rect_empty(src)) return;
    if (rect_empty(*dst)) {
        *dst = src;
        return;
    }
    x1 = dst->x < src.x ? dst->x : src.x;
    y1 = dst->y < src.y ? dst->y : src.y;
    x2 = dst->x + dst->width > src.x + src.width
        ? dst->x + dst->width : src.x + src.width;
    y2 = dst->y + dst->height > src.y + src.height
        ? dst->y + dst->height : src.y + src.height;
    dst->x = x1;
    dst->y = y1;
    dst->width = x2 - x1;
    dst->height = y2 - y1;
}

static ol_rect rect_intersection(ol_rect a, ol_rect b)
{
    ol_rect r;
    ol_unit ax = a.x + a.width, ay = a.y + a.height;
    ol_unit bx = b.x + b.width, by = b.y + b.height;
    r.x = a.x > b.x ? a.x : b.x;
    r.y = a.y > b.y ? a.y : b.y;
    ax = ax < bx ? ax : bx;
    ay = ay < by ? ay : by;
    r.width = ax > r.x ? ax - r.x : 0;
    r.height = ay > r.y ? ay - r.y : 0;
    if (r.width <= 0 || r.height <= 0) {
        r.width = 0;
        r.height = -1;           /* empty clip, distinct from no clip */
    }
    return r;
}

static const ol_graphic *node_graphic(const ol_node *node)
{
    return node ? node->graphic : NULL;
}

static int point_in_rect(ol_rect r, ol_unit x, ol_unit y)
{
    return !rect_empty(r) &&
           x >= r.x && y >= r.y &&
           x < r.x + r.width &&
           y < r.y + r.height;
}

static int node_depth(const ol_node *node)
{
    int d = 0;
    while (node && node->parent) {
        ++d;
        node = node->parent;
    }
    return d;
}

static ol_display default_display_for_role(ol_role role)
{
    switch (role) {
    case OL_ROLE_TEXT:
    case OL_ROLE_LINK:
    case OL_ROLE_IMAGE:
    case OL_ROLE_BUTTON:
    case OL_ROLE_INPUT:
        return OL_DISPLAY_INLINE;
    default:
        return OL_DISPLAY_BLOCK;
    }
}

void ol_style_init(ol_style *style)
{
    if (!style) return;
    memset(style, 0, sizeof(*style));
    style->display = OL_DISPLAY_BLOCK;
    style->text_align = OL_ALIGN_START;
    style->font_size = OL_CSSPX(16);
    style->line_height = OL_CSSPX(20);
    style->foreground = 0xff000000u;
    style->background = 0x00000000u;
}

ol_document *ol_document_new_reserved(size_t ops)
{
    /* the display list's block first, before the document's own small ones: on an Amiga the block a document
     * laid out just before freed is still whole then, and takes it (libnix's malloc carves small blocks out
     * of the first free space it finds) */
    ol_display_op *list = ops ? (ol_display_op *)malloc(ops * sizeof(*list)) : NULL;
    ol_document *doc;
    if (ops && !list) return NULL;
    doc = ol_document_new();
    if (!doc) {
        free(list);
        return NULL;
    }
    if (list) {
        doc->ops = list;
        doc->op_capacity = ops;
    }
    return doc;
}

ol_document *ol_document_new(void)
{
    ol_document *doc = (ol_document *)calloc(1, sizeof(*doc));
    ol_node *root;
    if (!doc) return NULL;
    doc->next_id = 1;
    if (!reserve_nodes(doc, 1)) {
        free(doc);
        return NULL;
    }
    root = (ol_node *)calloc(1, sizeof(*root));
    if (!root) {
        free(doc->nodes);
        free(doc);
        return NULL;
    }
    root->doc = doc;
    root->id = doc->next_id++;
    root->role = OL_ROLE_DOCUMENT;
    root->image_id = -1;
    ol_style_init(&root->style);
    ol_box_style_init(&root->box);
    root->style.display = OL_DISPLAY_BLOCK;
    doc->nodes[doc->node_count++] = root;
    doc->root = root;
    return doc;
}

static void free_node(ol_node *node)
{
    if (!node) return;
    ol_css_node_free(node->css);
    free(node->text);
    free(node->name);
    free(node->value);
    free(node->href);
    free(node->columns);
    free(node->tabs);
    free(node->tab_info);
    if (node->graphic) {
        free((void *)node->graphic->points);
        free(node->graphic);
    }
    free(node);
}

void ol_document_free(ol_document *doc)
{
    size_t i;
    if (!doc) return;
    ol_css_document_free(doc->css);
    for (i = 0; i < doc->node_count; ++i) free_node(doc->nodes[i]);
    free(doc->nodes);
    free(doc->ops);
    free(doc->keeps);
    free(doc->breaks);
    free(doc->anchors);
    free(doc->zones);
    free(doc->next_zones);
    free(doc->live_starts);
    free(doc->iboxes);
    free(doc->repeats);
    free(doc->page_shift);
    free(doc->page_repeat);
    free(doc->page_start);
    free(doc->page_end);
    free(doc->page_first);
    free(doc->page_ops);
    free(doc->region_start);
    free(doc->region_end);
    free(doc->region_shift);
    free(doc->region_page);
    free(doc->region_x);
    free(doc->region_y);
    free(doc->region_width);
    free(doc);
}

ol_node *ol_document_root(ol_document *doc)
{
    return doc ? doc->root : NULL;
}

const ol_node *ol_document_root_const(const ol_document *doc)
{
    return doc ? doc->root : NULL;
}

ol_node *ol_node_append(ol_document *doc, ol_node *parent, ol_role role,
                        const char *utf8)
{
    ol_node *node;
    if (!doc) return NULL;
    if (!parent) parent = doc->root;
    if (parent->doc != doc) return NULL;
    if (!reserve_nodes(doc, 1)) return NULL;
    node = (ol_node *)calloc(1, sizeof(*node));
    if (!node) return NULL;
    node->doc = doc;
    node->id = doc->next_id++;
    node->role = role;
    node->parent = parent;
    node->image_id = -1;
    ol_style_init(&node->style);
    ol_box_style_init(&node->box);
    node->style.display = default_display_for_role(role);
    node->text = ol_strdup0(utf8);
    if (!node->text) {
        free(node);
        --doc->next_id;     /* ids stay one more than their place in nodes */
        return NULL;
    }
    if (role == OL_ROLE_LINK) node->actions |= OL_ACTION_ACTIVATE;
    if (!parent->first_child) parent->first_child = node;
    else parent->last_child->next_sibling = node;
    parent->last_child = node;
    doc->nodes[doc->node_count++] = node;
    return node;
}

ol_id ol_node_id(const ol_node *node) { return node ? node->id : 0; }
ol_role ol_node_role(const ol_node *node) { return node ? node->role : OL_ROLE_DOCUMENT; }
const char *ol_node_text(const ol_node *node) { return node ? node->text : NULL; }
const char *ol_node_name(const ol_node *node) { return node ? node->name : NULL; }
const char *ol_node_value(const ol_node *node) { return node ? node->value : NULL; }
const char *ol_node_href(const ol_node *node) { return node ? node->href : NULL; }
const ol_style *ol_node_style(const ol_node *node) { return node ? &node->style : NULL; }
ol_rect ol_node_bounds(const ol_node *node)
{
    ol_rect r = {0, 0, 0, 0};
    return node ? node->bounds : r;
}

static int set_string(char **dst, const char *s)
{
    char *p = ol_strdup0(s);
    if (!p) return 0;
    free(*dst);
    *dst = p;
    return 1;
}

int ol_node_set_text(ol_node *node, const char *utf8)
{
    return node ? set_string(&node->text, utf8) : 0;
}

int ol_node_set_name(ol_node *node, const char *utf8)
{
    return node ? set_string(&node->name, utf8) : 0;
}

int ol_node_set_value(ol_node *node, const char *utf8)
{
    return node ? set_string(&node->value, utf8) : 0;
}

int ol_node_set_href(ol_node *node, const char *utf8)
{
    return node ? set_string(&node->href, utf8) : 0;
}

void ol_node_set_style(ol_node *node, const ol_style *style)
{
    if (node && style) node->style = *style;
}

void ol_node_set_actions(ol_node *node, uint32_t actions)
{
    if (node) node->actions = actions;
}

void ol_node_set_states(ol_node *node, uint32_t states)
{
    if (node) node->states = states;
}

uint32_t ol_node_actions(const ol_node *node) { return node ? node->actions : 0; }
uint32_t ol_node_states(const ol_node *node) { return node ? node->states : 0; }

void ol_node_set_image(ol_node *node, int image_id, ol_unit width, ol_unit height)
{
    if (!node) return;
    node->image_id = image_id;
    node->image_width = width;
    node->image_height = height;
}

void ol_shape_style_init(ol_shape_style *style)
{
    if (!style) return;
    memset(style, 0, sizeof(*style));
    style->size = sizeof(*style);
    style->fill_argb = 0xffffffffu;
    style->stroke_argb = 0xff000000u;
    style->stroke_width = OL_CSSPX(1);
}

int ol_node_set_shape(ol_node *node, const ol_shape_style *style,
                      int count, const ol_point *points)
{
    ol_point *copy = NULL;
    ol_graphic *graphic;
    ol_shape_style s;
    if (!node || count < 0 || count > OL_MAX_SHAPE_POINTS || (count && !points)) return 0;
    memset(&s, 0, sizeof(s));
    if (style) {
        size_t n = style->size < sizeof(s) ? style->size : sizeof(s);
        if (n > sizeof(style->size))
            memcpy((char *)&s + sizeof(style->size), (const char *)style + sizeof(style->size),
                   n - sizeof(style->size));
        s.size = sizeof(s);
    }
    if (!style || s.type == OL_SHAPE_NONE) count = 0;
    else if (s.type > OL_SHAPE_POLYGON || s.stroke_style > OL_STROKE_DASH_DOT ||
             s.stroke_width < 0 || s.radius_permille < 0 ||
             s.radius_permille > 500 ||
             ((s.type == OL_SHAPE_LINE || s.type == OL_SHAPE_POLYLINE) && count < 2) ||
             (s.type == OL_SHAPE_POLYGON && count < 3))
        return 0;
    if (s.type == OL_SHAPE_RECT || s.type == OL_SHAPE_ROUND_RECT || s.type == OL_SHAPE_ELLIPSE)
        count = 0;
    if (count) {
        copy = (ol_point *)malloc((size_t)count * sizeof(*copy));
        if (!copy) return 0;
        memcpy(copy, points, (size_t)count * sizeof(*copy));
    }
    graphic = node->graphic;
    if (!graphic && s.type != OL_SHAPE_NONE) {
        graphic = (ol_graphic *)calloc(1, sizeof(*graphic));
        if (!graphic) {
            free(copy);
            return 0;
        }
        node->graphic = graphic;
    }
    if (graphic) {
        free((void *)graphic->points);
        graphic->shape = s;
        graphic->point_count = count;
        graphic->points = copy;
        if (s.type == OL_SHAPE_NONE && !graphic->crop.left && !graphic->crop.top &&
            !graphic->crop.right && !graphic->crop.bottom) {
            free(graphic);
            node->graphic = NULL;
        }
    }
    return 1;
}

int ol_node_set_image_crop(ol_node *node, const ol_image_crop *crop)
{
    ol_image_crop c = {0, 0, 0, 0};
    if (!node) return 0;
    if (crop) c = *crop;
    if (c.left < 0 || c.left >= OL_SHAPE_COORDS || c.top < 0 || c.top >= OL_SHAPE_COORDS ||
        c.right < 0 || c.right >= OL_SHAPE_COORDS ||
        c.bottom < 0 || c.bottom >= OL_SHAPE_COORDS ||
        c.left + c.right >= OL_SHAPE_COORDS || c.top + c.bottom >= OL_SHAPE_COORDS)
        return 0;
    if (!node->graphic && (c.left || c.top || c.right || c.bottom)) {
        node->graphic = (ol_graphic *)calloc(1, sizeof(*node->graphic));
        if (!node->graphic) return 0;
    }
    if (node->graphic) {
        node->graphic->crop = c;
        if (node->graphic->shape.type == OL_SHAPE_NONE && !c.left && !c.top && !c.right && !c.bottom) {
            free((void *)node->graphic->points);
            free(node->graphic);
            node->graphic = NULL;
        }
    }
    return 1;
}

void ol_box_style_init(ol_box_style *box)
{
    if (!box) return;
    memset(box, 0, sizeof(*box));
    box->size = sizeof(*box);
    box->border_argb = 0xff000000u;
    box->border_sides = OL_BORDER_ALL;
}

void ol_node_set_box(ol_node *node, const ol_box_style *box)
{
    size_t n;
    if (!node || !box) return;
    ol_box_style_init(&node->box);
    n = box->size < sizeof(node->box) ? box->size : sizeof(node->box);
    if (n > sizeof(box->size))
        memcpy((char *)&node->box + sizeof(box->size), (const char *)box + sizeof(box->size),
               n - sizeof(box->size));
}

const ol_box_style *ol_node_box(const ol_node *node)
{
    return node ? &node->box : NULL;
}

int ol_node_set_columns(ol_node *node, int count, const ol_unit *widths)
{
    ol_unit *copy = NULL;
    if (!node || count < 0 || count > OL_MAX_COLUMNS || (count && !widths)) return 0;
    if (count) {
        copy = (ol_unit *)malloc((size_t)count * sizeof(*copy));
        if (!copy) return 0;
        memcpy(copy, widths, (size_t)count * sizeof(*copy));
    }
    free(node->columns);
    node->columns = copy;
    node->column_count = count;
    return 1;
}

/* stops in order, left to right, each with its alignment and leader */
static int set_tabs(ol_node *node, int count, const ol_unit *at, const ol_tab_stop *stops)
{
    ol_unit *copy = NULL;
    unsigned char *info = NULL;
    int i, j;
    if (!node || count < 0 || count > OL_MAX_TABS || (count && !at && !stops)) return 0;
    if (count) {
        copy = (ol_unit *)malloc((size_t)count * sizeof(*copy));
        info = (unsigned char *)malloc((size_t)count);
        if (!copy || !info) {
            free(copy);
            free(info);
            return 0;
        }
        for (i = 0; i < count; ++i) {
            ol_unit v = stops ? stops[i].position : at[i];
            unsigned char k = 0;
            if (stops) {
                unsigned a = stops[i].align >= 0 && stops[i].align <= OL_TAB_DECIMAL ? (unsigned)stops[i].align : 0u;
                unsigned l = stops[i].leader >= 0 && stops[i].leader <= OL_LEADER_HEAVY ? (unsigned)stops[i].leader : 0u;
                k = (unsigned char)(a | l << 4);
            }
            for (j = i; j > 0 && copy[j - 1] > v; --j) {
                copy[j] = copy[j - 1];
                info[j] = info[j - 1];
            }
            copy[j] = v;
            info[j] = k;
        }
    }
    free(node->tabs);
    free(node->tab_info);
    node->tabs = copy;
    node->tab_info = info;
    node->tab_count = count;
    return 1;
}

int ol_node_set_tabs(ol_node *node, int count, const ol_unit *stops)
{
    if (count && !stops) return 0;
    return set_tabs(node, count, stops, NULL);
}

int ol_node_set_tab_stops(ol_node *node, int count, const ol_tab_stop *stops)
{
    if (count && !stops) return 0;
    return set_tabs(node, count, NULL, stops);
}

/* Node ids are given out in order and nodes are never removed, so a node's
 * id is its place in the list plus one. */
static ol_node *node_for_id(const ol_document *doc, ol_id id)
{
    if (!doc || !id || id > doc->node_count) return NULL;
    return doc->nodes[id - 1];
}

const ol_node *ol_node_by_id(const ol_document *doc, ol_id id)
{
    return node_for_id(doc, id);
}

const ol_node *ol_node_parent(const ol_node *node)
{
    return node ? node->parent : NULL;
}

const ol_node *ol_node_first_child(const ol_node *node)
{
    return node ? node->first_child : NULL;
}

const ol_node *ol_node_next_sibling(const ol_node *node)
{
    return node ? node->next_sibling : NULL;
}

/* ---- What openlayout_css.c needs of the tree (GFCSS) ------------------------------- */

void *ol_css_node_data(const ol_node *node) { return node ? node->css : NULL; }
void ol_css_set_node_data(ol_node *node, void *css) { if (node) node->css = css; }
void *ol_css_document_data(const ol_document *doc) { return doc ? doc->css : NULL; }
void ol_css_set_document_data(ol_document *doc, void *css) { if (doc) doc->css = css; }
ol_document *ol_css_node_document(const ol_node *node) { return node ? node->doc : NULL; }
size_t ol_css_node_count(const ol_document *doc) { return doc ? doc->node_count : 0; }
ol_node *ol_css_first_child(ol_node *node) { return node ? node->first_child : NULL; }
ol_node *ol_css_next_sibling(ol_node *node) { return node ? node->next_sibling : NULL; }
ol_node *ol_css_parent(ol_node *node) { return node ? node->parent : NULL; }

/* node out of its parent's children, then back in as the first (first) or the last */
void ol_css_move_child(ol_node *node, int first)
{
    ol_node *parent = node ? node->parent : NULL, *c, *before = NULL;
    if (!parent) return;
    if (first ? parent->first_child == node : parent->last_child == node) return;
    for (c = parent->first_child; c && c != node; c = c->next_sibling) before = c;
    if (!c) return;
    if (before) before->next_sibling = node->next_sibling;
    else parent->first_child = node->next_sibling;
    if (parent->last_child == node) parent->last_child = before;
    node->next_sibling = NULL;
    if (first) {
        node->next_sibling = parent->first_child;
        parent->first_child = node;
        if (!parent->last_child) parent->last_child = node;
    } else {
        if (parent->last_child) parent->last_child->next_sibling = node;
        else parent->first_child = node;
        parent->last_child = node;
    }
}

const ol_node *ol_find_role_name(const ol_document *doc, ol_role role,
                                 const char *name)
{
    size_t i;
    if (!doc || !name) return NULL;
    for (i = 0; i < doc->node_count; ++i) {
        const ol_node *n = doc->nodes[i];
        if (n->role == role && n->name && strcmp(n->name, name) == 0)
            return n;
    }
    return NULL;
}

/* In a frame anchored to its page: its bounds are not the galley's. */
static int in_anchored_frame(const ol_node *n)
{
    for (; n; n = n->parent)
        if ((n->box.flags & OL_BOX_ABSOLUTE) && n->box.anchor) return 1;
    return 0;
}

const ol_node *ol_hit_test(const ol_document *doc, ol_unit x, ol_unit y)
{
    const ol_node *best = NULL;
    int best_depth = -1;
    size_t i;
    if (!doc) return NULL;
    for (i = 0; i < doc->node_count; ++i) {
        const ol_node *n = doc->nodes[i];
        int d;
        if (n->style.display == OL_DISPLAY_NONE || !point_in_rect(n->bounds, x, y) ||
            in_anchored_frame(n))
            continue;
        d = node_depth(n);
        if (d >= best_depth) {
            best = n;
            best_depth = d;
        }
    }
    return best;
}

const ol_node *ol_hit_action(const ol_document *doc, ol_unit x, ol_unit y,
                             uint32_t action)
{
    const ol_node *node = ol_hit_test(doc, x, y);
    while (node) {
        if (node->actions & action) return node;
        node = node->parent;
    }
    return NULL;
}

static int default_measure(const char *utf8, size_t length,
                           const ol_style *style, ol_text_metrics *m)
{
    size_t i, glyphs = 0;
    for (i = 0; i < length; ++i)
        if (((unsigned char)utf8[i] & 0xc0u) != 0x80u) ++glyphs;
    m->width = (ol_unit)((int64_t)style->font_size * 3 * (int64_t)glyphs / 5);
    m->ascent = style->font_size * 4 / 5;
    m->descent = style->font_size - m->ascent;
    return 1;
}

static int measure_with(ol_measure_text_fn measure, void *userdata,
                        const char *text, size_t len, const ol_style *style,
                        ol_text_metrics *m)
{
    if (measure)
        return measure(userdata, text, len, style, m);
    return default_measure(text, len, style, m);
}

static int measure_text(ol_inline_flow *flow, const char *text, size_t len,
                        const ol_style *style, ol_text_metrics *m)
{
    return measure_with(flow->measure, flow->measure_userdata, text, len, style, m);
}

/* ---- What a page may not cut ------------------------------------------- */

static int add_keep(ol_document *doc, ol_unit top, ol_unit bottom)
{
    if (bottom <= top) return 1;
    if (doc->keep_count == doc->keep_capacity) {
        size_t cap = doc->keep_capacity ? doc->keep_capacity * 2 : 64;
        ol_span *p = (ol_span *)realloc(doc->keeps, cap * sizeof(*p));
        if (!p) return 0;
        doc->keeps = p;
        doc->keep_capacity = cap;
    }
    doc->keeps[doc->keep_count].top = top;
    doc->keeps[doc->keep_count].bottom = bottom;
    ++doc->keep_count;
    return 1;
}

static int add_break(ol_document *doc, ol_unit y)
{
    if (doc->break_count == doc->break_capacity) {
        size_t cap = doc->break_capacity ? doc->break_capacity * 2 : 16;
        ol_unit *p = (ol_unit *)realloc(doc->breaks, cap * sizeof(*p));
        if (!p) return 0;
        doc->breaks = p;
        doc->break_capacity = cap;
    }
    doc->breaks[doc->break_count++] = y;
    return 1;
}

/* ---- Web boxes' widths and borders (0.9) ------------------------------------ */

/* p thousandths of w, in 32 bit divisions (which a 68040 does itself): w's
 * thousands times p, and the rest's share */
static ol_unit permille_of(ol_unit w, int32_t p)
{
    if (w <= 0 || p <= 0) return 0;
    return (w / 1000) * p + (w % 1000) * p / 1000;
}

/* A box w wide (its margins outside it) between its least and its most, in
 * a containing block cb wide: the most is max_width or max_width_permille of
 * cb, the narrower; the least wins over it. */
static ol_unit clamp_width(const ol_node *n, ol_unit w, ol_unit cb)
{
    ol_unit most = n->box.max_width, mp = permille_of(cb, n->box.max_width_permille);
    if (mp > 0 && (most <= 0 || mp < most)) most = mp;
    if (most > 0 && w > most) w = most;
    if (n->box.min_width > 0 && w < n->box.min_width) w = n->box.min_width;
    return w;
}

/* The width a box asks for in a containing block cb wide, its margins
 * outside it: box.width, or width_permille of cb; 0: none. */
static ol_unit asked_width(const ol_node *n, ol_unit cb)
{
    return n->box.width > 0 ? n->box.width : permille_of(cb, n->box.width_permille);
}

static int has_width(const ol_node *n)
{
    return n->box.width > 0 || n->box.width_permille > 0;
}

/* A block's width in a containing block cb wide, its margins included, and
 * how far right of the block's left edge it goes (*dx): as wide as cb, or
 * as it asks, between its least and most; never wider than cb, unless its
 * least is. A block narrower than cb goes to cb's right (OL_BOX_AUTO_LEFT)
 * or its middle (with OL_BOX_AUTO_RIGHT too). */
static ol_unit block_width(const ol_node *c, ol_unit cb, ol_unit *dx)
{
    ol_unit m = c->style.margin_left + c->style.margin_right;
    ol_unit asked = asked_width(c, cb), w = asked > 0 ? asked : cb - m, v = clamp_width(c, w, cb);
    *dx = 0;
    if (asked <= 0 && v == w) return cb;
    w = v + m;
    if (w > cb && !(c->box.min_width > 0 && v == c->box.min_width)) w = cb;
    if (w < cb && (c->box.flags & OL_BOX_AUTO_LEFT))
        *dx = (c->box.flags & OL_BOX_AUTO_RIGHT) ? (cb - w) / 2 : cb - w;
    return w;
}

/* A side's border with OL_BOX_SIDE_BORDERS (0 top, 1 right, 2 bottom, 3
 * left), which takes room inside the box; 0 without. */
static ol_unit side_border(const ol_node *n, int side)
{
    ol_unit w = n->box.border_widths[side];
    return (n->box.flags & OL_BOX_SIDE_BORDERS) && w > 0 ? w : 0;
}

/* What is between a box's edge and its content: its padding and its side
 * border. */
static ol_unit inset_top(const ol_node *n) { return n->style.padding_top + side_border(n, 0); }
static ol_unit inset_right(const ol_node *n) { return n->style.padding_right + side_border(n, 1); }
static ol_unit inset_bottom(const ol_node *n) { return n->style.padding_bottom + side_border(n, 2); }
static ol_unit inset_left(const ol_node *n) { return n->style.padding_left + side_border(n, 3); }

static int is_inline(const ol_node *n)
{
    return n->style.display == OL_DISPLAY_INLINE || n->style.display == OL_DISPLAY_INLINE_BLOCK;
}

/* ---- Lines ---------------------------------------------------------------- */

static int is_space(char c)
{
    return c == ' ' || c == '\t';
}

/* A tab, which is a piece of its own. */
static int op_is_tab(const ol_document *doc, const ol_display_op *op)
{
    const ol_node *n;
    if (op->type != OL_OP_TEXT || op->u.text.text_length != 1) return 0;
    n = node_for_id(doc, op->u.text.node_id);
    return n && n->text && n->text[op->u.text.text_offset] == '\t';
}

/* Where justification may widen a line: after a piece ending in a space. */
static int op_ends_in_space(const ol_document *doc, const ol_display_op *op)
{
    const ol_node *n;
    if (op->type != OL_OP_TEXT || !op->u.text.text_length) return 0;
    n = node_for_id(doc, op->u.text.node_id);
    return n && n->text &&
           is_space(n->text[op->u.text.text_offset + op->u.text.text_length - 1]);
}

static void shift_op_x(ol_display_op *op, ol_unit dx)
{
    switch (op->type) {
    case OL_OP_TEXT: op->u.text.bounds.x += dx; break;
    case OL_OP_IMAGE: op->u.image.bounds.x += dx; break;
    case OL_OP_FILL_RECT: op->u.fill_rect.bounds.x += dx; break;
    case OL_OP_RULE: op->u.rule.x1 += dx; op->u.rule.x2 += dx; break;
    case OL_OP_SHAPE: op->u.fill_rect.bounds.x += dx; break;
    }
    if (op->clip.height) op->clip.x += dx;
}

/* Where the text after a decimal tab has its first '.', from the tab;
 * all of it when it has none. */
static ol_unit decimal_width(ol_inline_flow *flow, ol_unit tab_x, ol_unit seg)
{
    ol_document *doc = flow->doc;
    size_t i, k;
    for (i = flow->tab_op + 1; i < doc->op_count; ++i) {
        const ol_display_op *op = &doc->ops[i];
        const ol_node *n;
        const char *text;
        if (op->type != OL_OP_TEXT) continue;
        n = node_for_id(doc, op->u.text.node_id);
        if (!n || !n->text) continue;
        text = n->text + op->u.text.text_offset;
        for (k = 0; k < op->u.text.text_length; ++k)
            if (text[k] == '.') {
                ol_text_metrics m;
                m.width = 0;
                if (k && !measure_text(flow, text, k, &op->u.text.style, &m)) return seg;
                return op->u.text.bounds.x + m.width - tab_x;
            }
    }
    return seg;
}

/* A right, centred or decimal tab: the text after it, which ends here
 * (at the next tab or the line's end), goes against its stop, and the
 * tab takes the space before it. No room: it stays where it is. */
static void resolve_tab(ol_inline_flow *flow)
{
    ol_document *doc = flow->doc;
    ol_display_op *tab;
    ol_unit tab_x, seg, start, dx;
    size_t i;
    if (flow->tab_align == OL_TAB_LEFT) return;
    tab = &doc->ops[flow->tab_op];
    tab_x = tab->u.text.bounds.x;
    seg = flow->content_right - tab_x;
    if (seg < 0) seg = 0;
    if (flow->tab_align == OL_TAB_CENTER) start = flow->tab_stop - seg / 2;
    else if (flow->tab_align == OL_TAB_DECIMAL) start = flow->tab_stop - decimal_width(flow, tab_x, seg);
    else start = flow->tab_stop - seg;
    if (start < tab_x) start = tab_x;
    dx = start - tab_x;
    for (i = flow->tab_op + 1; i < doc->op_count; ++i) shift_op_x(&doc->ops[i], dx);
    tab->u.text.bounds.width = dx;
    flow->cursor_x += dx;
    flow->content_right += dx;
    flow->tab_align = OL_TAB_LEFT;
    flow->line_aligned_tab = 1;
}

/* Whether a piece width wide still fits on the line: past its end only
 * as far as a right, centred or decimal stop waiting for it takes it. */
static int piece_fits(const ol_inline_flow *flow, ol_unit width)
{
    ol_unit end = flow->cursor_x + width, limit = flow->right;
    if (flow->tab_align != OL_TAB_LEFT) {
        ol_unit tab_x = flow->doc->ops[flow->tab_op].u.text.bounds.x, seg = end - tab_x;
        ol_unit at = flow->tab_align == OL_TAB_CENTER ? flow->tab_stop + seg / 2 : flow->tab_stop;
        if (at > end) end = at;
        if (flow->tab_stop > limit) limit = flow->tab_stop;
    }
    return end <= limit;
}

static int live_page(ol_document *doc, ol_unit y, ol_unit h);

/* 0.6: whether a line from top to bottom is on a zone's page (from its
 * top, inside its body, with no page break before it) and meets it. */
static int zone_meets(const ol_document *doc, const ol_wrap_zone *z, ol_unit top, ol_unit bottom)
{
    size_t i;
    if (z->y0 >= bottom || z->y1 <= top || top < z->page_top || bottom > z->page_bottom || (z->after && !z->live))
        return 0;
    for (i = doc->break_count; i > 0; --i) {   /* the breaks come in order, mostly */
        ol_unit b = doc->breaks[i - 1];
        if (b > z->page_top && b <= top) return 0;
        if (b <= z->page_top) break;
    }
    return 1;
}

/* 0.6: the first stretch of the line, at from or right of it, that no
 * square zone covers and that is wide enough (OL_WRAP_MIN, or the whole
 * column when that is narrower): *left to *right, and *narrow when a zone
 * cuts it short. 0 when there is none; *below is then where the zones
 * crossing the line end first. The line is taken to be its nominal height. */
static int free_stretch(const ol_inline_flow *flow, ol_unit from, ol_unit *left, ol_unit *right,
                        ol_unit *below, int *narrow)
{
    const ol_document *doc = flow->doc;
    ol_unit top = flow->line_top, bottom = top + flow->nominal;
    ol_unit lo = flow->line_base < flow->x0 ? flow->line_base : flow->x0, hi = flow->col_right;
    ol_unit at = lo, need = hi - (from > lo ? from : lo);
    size_t i;
    int moved, crossed = 0;

    if (need > OL_WRAP_MIN) need = OL_WRAP_MIN;
    if (need < 1) need = 1;
    *below = bottom;
    for (i = 0; i < doc->zone_count; ++i) {
        const ol_wrap_zone *z = &doc->zones[i];
        if (z->wrap != OL_WRAP_SQUARE || z->x1 <= lo || z->x0 >= hi || !zone_meets(doc, z, top, bottom)) continue;
        if (!crossed || z->y1 < *below) *below = z->y1;
        crossed = 1;
    }
    for (;;) {
        ol_unit end = hi, start;
        do {                                /* past the zones covering at */
            moved = 0;
            for (i = 0; crossed && i < doc->zone_count; ++i) {
                const ol_wrap_zone *z = &doc->zones[i];
                if (z->wrap != OL_WRAP_SQUARE || !zone_meets(doc, z, top, bottom)) continue;
                if (z->x0 <= at && z->x1 > at) {
                    at = z->x1;
                    moved = 1;
                }
            }
        } while (moved);
        if (at >= hi) return 0;
        for (i = 0; crossed && i < doc->zone_count; ++i) {
            const ol_wrap_zone *z = &doc->zones[i];
            if (z->wrap == OL_WRAP_SQUARE && z->x0 > at && z->x0 < end && zone_meets(doc, z, top, bottom))
                end = z->x0;
        }
        start = at > from ? at : from;
        if (end - start >= need) {
            *left = start;
            *right = end;
            *narrow = at != lo || end != hi;
            return 1;
        }
        at = end;
    }
}

/* 0.6: the line's room, when its first piece comes, the line having been
 * at from: below the top and bottom zones that cross it, and in the first
 * stretch the square ones leave, or below them when they leave none wide
 * enough. A line moved down leaves a keep where it was on the page it ends
 * on, so that the page is cut, and starts, as if it were still there: a
 * page whose first line goes below a frame at its top still starts above
 * the frame. 0 when there is no memory. */
static int line_room_from(ol_inline_flow *flow, ol_unit from)
{
    ol_document *doc = flow->doc;
    ol_unit lo = flow->line_base < flow->x0 ? flow->line_base : flow->x0;
    ol_unit left, right, below, ghost = from;
    size_t i, guard;
    int narrow, page = 0;

    flow->room_pending = 0;
    if (!flow->wraps || !doc->zone_count) return 1;
    if (doc->paging && (page = live_page(doc, from, flow->nominal)) < 0) return 0;
    for (guard = 0; guard <= 2 * doc->zone_count + 2; ++guard) {
        ol_unit top = flow->line_top, bottom = top + flow->nominal;
        int pushed = 0;
        if (doc->paging && top != from) {     /* moved onto another page: its zones follow it */
            int p = live_page(doc, top, flow->nominal);
            if (p < 0) return 0;
            if (p != page) {
                page = p;
                ghost = top;
            }
        }
        for (i = 0; i < doc->zone_count; ++i) {
            const ol_wrap_zone *z = &doc->zones[i];
            if (z->wrap == OL_WRAP_TOP_BOTTOM && z->x1 > lo && z->x0 < flow->col_right &&
                zone_meets(doc, z, top, bottom)) {
                flow->line_top = z->y1;
                pushed = 1;
            }
        }
        if (pushed) continue;
        if (free_stretch(flow, flow->line_base, &left, &right, &below, &narrow)) {
            flow->cursor_x = left;
            flow->content_right = left;
            flow->right = right;
            flow->narrow = narrow;
            break;
        }
        if (below <= flow->line_top) break;
        flow->line_top = below;
    }
    return flow->line_top == ghost || add_keep(doc, ghost, ghost + flow->nominal);
}

static int line_room(ol_inline_flow *flow)
{
    return line_room_from(flow, flow->line_top);
}

static int layout_inline_box(ol_inline_flow *flow, ol_node *node, ol_unit x, ol_unit y, ol_unit w,
                             ol_unit *bottom_out);

/* 0.9: the line's inline-blocks, once its height and baseline are known:
 * each where its place holder went across, and down by its valign (its
 * bottom on the baseline, or at the line's top, middle or bottom); its box
 * laid out there, its ops after the line's others, the place holders taken
 * out. */
static int place_inline_boxes(ol_inline_flow *flow, size_t first, ol_unit height, ol_unit baseline)
{
    ol_document *doc = flow->doc;
    size_t from = flow->ibox_first, n = doc->ibox_count, i, j, k;
    for (k = from; k < n; ++k) {
        ol_inline_box *b = &doc->iboxes[k];
        b->x = doc->ops[b->op].u.image.bounds.x;
        switch (b->node->box.valign) {
        case OL_VALIGN_TOP: b->y = flow->line_top; break;
        case OL_VALIGN_MIDDLE: b->y = flow->line_top + (height - b->h) / 2; break;
        case OL_VALIGN_BOTTOM: b->y = flow->line_top + height - b->h; break;
        default: b->y = baseline - b->h; break;
        }
    }
    for (i = j = first, k = from; i < doc->op_count; ++i) {
        if (k < n && doc->iboxes[k].op == i) {
            ++k;
            continue;
        }
        doc->ops[j++] = doc->ops[i];
    }
    doc->op_count = j;
    for (k = from; k < n; ++k) {
        ol_inline_box b = doc->iboxes[k];   /* a copy: the inline-blocks inside it come after it */
        ol_unit bottom;
        if (!layout_inline_box(flow, b.node, b.x, b.y, b.w, &bottom)) return 0;
    }
    doc->ibox_count = from;
    return 1;
}

/* Ends the flow's line: aligns it, puts its pieces on one baseline (a
 * picture stands on it), and keeps it whole on a page. last: a paragraph's
 * last line, or one ended by a line break, which justification leaves. */
static int finish_line(ol_inline_flow *flow, int last)
{
    ol_document *doc = flow->doc;
    size_t i, first = flow->line_first_op, end = doc->op_count;
    ol_unit extra, height, baseline;

    if (!flow->has_content) return 1;
    resolve_tab(flow);
    height = flow->ascent + flow->descent;
    if (height < flow->line_height) height = flow->line_height;
    if (height <= 0) height = OL_CSSPX(20);
    baseline = flow->line_top + (height - (flow->ascent + flow->descent)) / 2 + flow->ascent;
    if (flow->exact) {                       /* 0.7: exactly so high, the text on the line's bottom */
        height = flow->exact;
        baseline = flow->line_top + height - flow->descent;
    }

    extra = flow->right - flow->content_right;
    if (extra > 0 && end > first) {
        if (flow->align == OL_ALIGN_END || flow->align == OL_ALIGN_CENTER) {
            ol_unit dx = flow->align == OL_ALIGN_END ? extra : extra / 2;
            for (i = first; i < end; ++i) shift_op_x(&doc->ops[i], dx);
        } else if (flow->align == OL_ALIGN_JUSTIFY && !last && !flow->line_aligned_tab) {
            ol_unit gaps = 0, seen = 0, share, rest;
            size_t from;
            /* what is before a tab stays where the tab put it */
            for (from = end; from > first; --from)
                if (op_is_tab(doc, &doc->ops[from - 1])) break;
            for (i = from; i + 1 < end; ++i)
                if (op_ends_in_space(doc, &doc->ops[i])) ++gaps;
            /* each gap's share, the remainder spread over the first ones:
             * 32 bit divisions, which a 68040 does itself */
            share = gaps ? extra / gaps : 0;
            rest = gaps ? extra % gaps : 0;
            for (i = from; gaps && i < end; ++i) {
                shift_op_x(&doc->ops[i], share * seen + (seen < rest ? seen : rest));
                if (i + 1 < end && op_ends_in_space(doc, &doc->ops[i])) ++seen;
            }
        }
    }
    for (i = first; i < end; ++i) {
        ol_display_op *op = &doc->ops[i];
        if (op->type == OL_OP_TEXT) {
            op->u.text.bounds.y = flow->line_top;
            op->u.text.bounds.height = height;
            op->u.text.baseline = baseline;
        } else if (op->type == OL_OP_IMAGE) {
            op->u.image.bounds.y = baseline - op->u.image.bounds.height;
        }
    }
    if (doc->ibox_count > flow->ibox_first && !place_inline_boxes(flow, first, height, baseline)) return 0;
    flow->line_height = height;
    ++flow->lines;
    return add_keep(doc, flow->line_top, flow->line_top + height);
}

/* block: the block whose text it is, for its tab stops; NULL for none */
static void start_flow(ol_inline_flow *flow, ol_document *doc, const ol_node *block,
                       ol_measure_text_fn measure, void *measure_userdata,
                       ol_unit x0, ol_unit right, ol_unit top,
                       ol_text_align align, ol_unit indent)
{
    memset(flow, 0, sizeof(*flow));
    flow->doc = doc;
    flow->measure = measure;
    flow->measure_userdata = measure_userdata;
    flow->x0 = x0;
    flow->right = right;
    flow->col_right = right;
    if (block) {
        flow->tabs = block->tabs;
        flow->tab_info = block->tab_info;
        flow->tab_count = block->tab_count;
    }
    flow->cursor_x = x0 + indent;
    flow->line_base = flow->cursor_x;
    flow->content_right = flow->cursor_x;
    flow->line_top = top;
    flow->align = align;
    flow->line_first_op = doc->op_count;
    flow->ibox_first = doc->ibox_count;
    flow->wraps = !doc->wrap_off;
    flow->room_pending = flow->wraps;
    flow->exact = block && (block->box.flags & OL_BOX_EXACT_LINES) && block->style.line_height > 0
                ? block->style.line_height : 0;
    flow->nominal = block && block->style.line_height > 0 ? block->style.line_height
                  : block && block->style.font_size > 0 ? block->style.font_size + block->style.font_size / 4
                  : OL_CSSPX(20);
}

/* A stretch's or a line's own state, from left to right. */
static void start_stretch(ol_inline_flow *flow, ol_unit left, ol_unit right, int narrow)
{
    flow->cursor_x = left;
    flow->content_right = left;
    flow->right = right;
    flow->narrow = narrow;
    flow->line_height = 0;
    flow->ascent = 0;
    flow->descent = 0;
    flow->has_content = 0;
    flow->line_aligned_tab = 0;
    flow->line_first_op = flow->doc->op_count;
}

static int flow_newline(ol_inline_flow *flow, int last)
{
    ol_unit h;
    if (!finish_line(flow, last)) return 0;
    h = flow->line_height;
    if (flow->seg_height > h) h = flow->seg_height;      /* 0.6: a stretch beside a frame was taller */
    flow->line_top += h > 0 ? h : OL_CSSPX(20);
    flow->seg_height = 0;
    flow->line_base = flow->x0;
    start_stretch(flow, flow->x0, flow->col_right, 0);
    flow->room_pending = flow->wraps;
    return 1;
}

/* 0.6: on from a stretch that has no room for the next piece: to the next
 * stretch of the line beside a frame, below the frame when nothing is on
 * the line yet, or to a new line. */
static int flow_wrap_on(ol_inline_flow *flow, int last)
{
    ol_unit left, right, below = 0;
    int narrow;
    if (flow->wraps && flow->doc->zone_count && free_stretch(flow, flow->right, &left, &right, &below, &narrow)) {
        if (!finish_line(flow, last)) return 0;
        if (flow->has_content && flow->line_height > flow->seg_height) flow->seg_height = flow->line_height;
        start_stretch(flow, left, right, narrow);
        return 1;
    }
    if (flow->narrow && !flow->has_content && !flow->seg_height && below > flow->line_top) {
        ol_unit was = flow->line_top;
        flow->line_top = below;
        return line_room_from(flow, was);
    }
    if (!flow_newline(flow, last)) return 0;
    return !flow->room_pending || line_room(flow);
}

/* Ends the flow; *bottom is below its last line. */
static int flush_flow(ol_inline_flow *flow, ol_unit *bottom)
{
    ol_unit h;
    if (!finish_line(flow, 1)) return 0;
    h = flow->has_content ? flow->line_height : 0;
    if (flow->seg_height > h) h = flow->seg_height;
    *bottom = flow->line_top + h;
    return 1;
}

static int emit_text_piece(ol_inline_flow *flow, ol_node *node,
                           size_t offset, size_t length)
{
    ol_text_metrics m, word;
    ol_display_op *op;
    ol_unit word_width;
    const char *text = node->text + offset;
    size_t word_length = length;

    if (!length) return 1;
    while (word_length && is_space(text[word_length - 1])) --word_length;
    if (!word_length && !flow->has_content)
        return 1;                       /* no spaces at a line's start */
    if (!measure_text(flow, text, length, &node->style, &m)) return 0;
    word_width = m.width;
    if (!word_length) {
        word_width = 0;
    } else if (word_length < length) {
        if (!measure_text(flow, text, word_length, &node->style, &word)) return 0;
        word_width = word.width;
    }

    /* a word wraps when it does not fit; its trailing spaces may hang. 0.6: on to the line's next stretch
     * beside a frame, and below a stretch too narrow for it */
    if (flow->room_pending && !line_room(flow)) return 0;
    while (word_length && !piece_fits(flow, word_width) && (flow->has_content || flow->narrow))
        if (!flow_wrap_on(flow, 0)) return 0;

    op = append_op(flow->doc, OL_OP_TEXT);
    if (!op) return 0;
    op->u.text.node_id = node->id;
    op->u.text.bounds.x = flow->cursor_x;
    op->u.text.bounds.y = flow->line_top;
    op->u.text.bounds.width = m.width;
    op->u.text.text_offset = offset;
    op->u.text.text_length = length;
    op->u.text.style = node->style;

    if (m.ascent > flow->ascent) flow->ascent = m.ascent;
    if (m.descent > flow->descent) flow->descent = m.descent;
    if (node->style.line_height > flow->line_height) flow->line_height = node->style.line_height;
    if (word_length) flow->content_right = flow->cursor_x + word_width;
    flow->cursor_x += m.width;
    flow->has_content = 1;
    return 1;
}

/* The next tab stop after the flow's x: the block's own, the left edge for
 * a first line that hangs, then every OL_TAB_INTERVAL from the left edge.
 * *info: the stop's alignment and leader (0 for the others). */
static ol_unit next_tab(const ol_inline_flow *flow, unsigned *info)
{
    ol_unit at = flow->cursor_x - flow->x0;
    int i;
    *info = 0;
    for (i = 0; i < flow->tab_count; ++i)
        if (flow->tabs[i] > at) {
            if (at < 0 && flow->tabs[i] > 0) return flow->x0;
            if (flow->tab_info) *info = flow->tab_info[i];
            return flow->x0 + flow->tabs[i];
        }
    if (at < 0) return flow->x0;
    return flow->x0 + (at / OL_TAB_INTERVAL + 1) * OL_TAB_INTERVAL;
}

/* A tab: a piece to the next stop, or, when that is past the line's end,
 * the next line's first; it holds its place at a line's start too. To a
 * right, centred or decimal stop it is a piece as wide as resolve_tab
 * makes it, once the text after it is laid out. */
static int emit_tab(ol_inline_flow *flow, ol_node *node, size_t offset)
{
    ol_display_op *op;
    unsigned info;
    ol_unit to;
    int align;

    resolve_tab(flow);                      /* the text after the last tab ends here */
    if (flow->room_pending && !line_room(flow)) return 0;
    to = next_tab(flow, &info);
    align = (int)(info & 15u);
    if (align == OL_TAB_LEFT) {
        if (to > flow->right && flow->has_content) {
            /* the line ends here as at a line break: not widened to the edge (a justified contents line
             * whose stop is past the edge was spread across the line before its page number) */
            if (!flow_wrap_on(flow, 1)) return 0;
            to = next_tab(flow, &info);
            align = (int)(info & 15u);
        }
        if (align == OL_TAB_LEFT && to > flow->right)
            to = flow->right > flow->cursor_x ? flow->right : flow->cursor_x;
    }

    op = append_op(flow->doc, OL_OP_TEXT);
    if (!op) return 0;
    op->u.text.node_id = node->id;
    op->u.text.bounds.x = flow->cursor_x;
    op->u.text.bounds.y = flow->line_top;
    op->u.text.bounds.width = align == OL_TAB_LEFT ? to - flow->cursor_x : 0;
    op->u.text.text_offset = offset;
    op->u.text.text_length = 1;
    op->u.text.style = node->style;
    op->u.text.style.text_flags = (op->u.text.style.text_flags & ~OL_TEXT_LEADER(7)) | OL_TEXT_LEADER(info >> 4);

    if (node->style.line_height > flow->line_height) flow->line_height = node->style.line_height;
    if (align == OL_TAB_LEFT) {
        flow->cursor_x = to;
    } else {
        flow->tab_align = align;
        flow->tab_op = flow->doc->op_count - 1;
        flow->tab_stop = to;
    }
    flow->content_right = flow->cursor_x;
    flow->has_content = 1;
    return 1;
}

static int layout_inline_node(ol_inline_flow *flow, ol_node *node);

static int layout_inline_text(ol_inline_flow *flow, ol_node *node)
{
    const char *s = node->text ? node->text : "";
    size_t len = strlen(s), pos = 0, start;

    while (pos < len) {
        if (s[pos] == '\r') {
            ++pos;
            continue;
        }
        if (s[pos] == '\n') {
            if (!flow->has_content && node->style.line_height > flow->line_height)
                flow->line_height = node->style.line_height;
            if (!flow_newline(flow, 1)) return 0;
            ++pos;
            continue;
        }

        if (s[pos] == '\t') {
            if (!emit_tab(flow, node, pos)) return 0;
            ++pos;
            continue;
        }

        start = pos;
        if (s[pos] == ' ') {
            while (pos < len && s[pos] == ' ') ++pos;
        } else {
            while (pos < len && !is_space(s[pos]) && s[pos] != '\r' && s[pos] != '\n')
                ++pos;
            while (pos < len && s[pos] == ' ') ++pos;
        }
        if (!emit_text_piece(flow, node, start, pos - start)) return 0;
    }
    return 1;
}

static int layout_inline_image(ol_inline_flow *flow, ol_node *node)
{
    ol_unit w = node->image_width > 0 ? node->image_width : OL_CSSPX(16);
    ol_unit h = node->image_height > 0 ? node->image_height : OL_CSSPX(16);
    ol_display_op *op;

    if (flow->room_pending && !line_room(flow)) return 0;
    while (!piece_fits(flow, w) && (flow->has_content || flow->narrow))
        if (!flow_wrap_on(flow, 0)) return 0;

    op = append_op(flow->doc, OL_OP_IMAGE);
    if (!op) return 0;
    op->u.image.node_id = node->id;
    op->u.image.bounds.x = flow->cursor_x;
    op->u.image.bounds.y = flow->line_top;
    op->u.image.bounds.width = w;
    op->u.image.bounds.height = h;
    op->u.image.image_id = node->image_id;
    op->graphic = node_graphic(node);

    if (h > flow->ascent) flow->ascent = h;
    flow->cursor_x += w;
    flow->content_right = flow->cursor_x;
    flow->has_content = 1;
    return 1;
}

typedef struct {
    size_t ops, keeps, breaks, anchors, repeats;
} ol_mark;
static void mark_take(const ol_document *doc, ol_mark *m);
static void mark_back(ol_document *doc, const ol_mark *m);
static ol_unit content_width(ol_document *doc, const ol_node *node,
                             ol_measure_text_fn measure, void *measure_userdata);
static ol_unit content_width_in(ol_document *doc, const ol_node *node, int frame_itself,
                                ol_measure_text_fn measure, void *measure_userdata);

/* 0.9: an inline-block: as wide as it asks (in thousandths of the line's
 * width), or as its content up to the line's width, between its least and
 * its most; on to the next line when it does not fit, as a word is. Laid
 * out here for its height, and taken back: a place holder (a picture's op)
 * stands for it on the line until the line is finished. */
static int layout_inline_block(ol_inline_flow *flow, ol_node *node)
{
    ol_document *doc = flow->doc;
    ol_unit line_w = flow->col_right - flow->x0, m = node->style.margin_left + node->style.margin_right;
    ol_unit w = asked_width(node, line_w), bottom;
    ol_display_op *op;
    ol_inline_box *b;
    ol_mark mark;

    if (w <= 0) {
        w = content_width(doc, node, flow->measure, flow->measure_userdata) - m;
        if (w > line_w - m) w = line_w - m;
    }
    w = clamp_width(node, w, line_w);
    if (w < OL_CSSPX(1)) w = OL_CSSPX(1);
    w += m;

    if (flow->room_pending && !line_room(flow)) return 0;
    while (!piece_fits(flow, w) && (flow->has_content || flow->narrow))
        if (!flow_wrap_on(flow, 0)) return 0;

    mark_take(doc, &mark);
    if (!layout_inline_box(flow, node, flow->cursor_x, flow->line_top, w, &bottom)) return 0;
    mark_back(doc, &mark);

    if (doc->ibox_count == doc->ibox_capacity) {
        size_t cap = doc->ibox_capacity ? doc->ibox_capacity * 2 : 8;
        ol_inline_box *q = (ol_inline_box *)realloc(doc->iboxes, cap * sizeof(*q));
        if (!q) return 0;
        doc->iboxes = q;
        doc->ibox_capacity = cap;
    }
    op = append_op(doc, OL_OP_IMAGE);
    if (!op) return 0;
    b = &doc->iboxes[doc->ibox_count++];
    memset(b, 0, sizeof(*b));
    b->op = doc->op_count - 1;
    b->node = node;
    b->w = w;
    b->h = bottom - flow->line_top;
    op->u.image.node_id = node->id;
    op->u.image.bounds.x = flow->cursor_x;
    op->u.image.bounds.y = flow->line_top;
    op->u.image.bounds.width = w;
    op->u.image.bounds.height = b->h;
    op->u.image.image_id = -1;

    /* on the baseline it is as tall above it as a picture; at the line's
     * top, middle or bottom the line is at least as tall as it */
    if (node->box.valign == OL_VALIGN_BASELINE || node->box.valign > OL_VALIGN_BOTTOM) {
        if (b->h > flow->ascent) flow->ascent = b->h;
    } else if (b->h > flow->line_height) {
        flow->line_height = b->h;
    }
    flow->cursor_x += w;
    flow->content_right = flow->cursor_x;
    flow->has_content = 1;
    return 1;
}

static int layout_inline_node(ol_inline_flow *flow, ol_node *node)
{
    ol_node *child;
    if (!node || node->style.display == OL_DISPLAY_NONE) return 1;
    if (node->style.display == OL_DISPLAY_INLINE_BLOCK) return layout_inline_block(flow, node);

    if (node->role == OL_ROLE_IMAGE) {
        if (!layout_inline_image(flow, node)) return 0;
    } else if (node->text && node->text[0]) {
        if (!layout_inline_text(flow, node)) return 0;
    }

    for (child = node->first_child; child; child = child->next_sibling) {
        if (child->style.display == OL_DISPLAY_BLOCK) break;
        if (!layout_inline_node(flow, child)) return 0;
    }
    return 1;
}

/* Inline nodes' bounds: their display ops', and their inline children's. */
static void inline_bounds(ol_document *doc)
{
    size_t i;
    for (i = 0; i < doc->op_count; ++i) {
        const ol_display_op *op = &doc->ops[i];
        ol_node *n;
        ol_rect r;
        if (op->type == OL_OP_TEXT) {
            r = op->u.text.bounds;
            n = node_for_id(doc, op->u.text.node_id);
        } else if (op->type == OL_OP_IMAGE) {
            r = op->u.image.bounds;
            n = node_for_id(doc, op->u.image.node_id);
        } else {
            continue;
        }
        for (; n && n->style.display == OL_DISPLAY_INLINE; n = n->parent)
            rect_add(&n->bounds, r);
    }
}

/* ---- Blocks, tables, frames ---------------------------------------------- */

static int add_rule(ol_document *doc, ol_id id, ol_unit x1, ol_unit y1,
                    ol_unit x2, ol_unit y2, ol_unit thickness, uint32_t argb)
{
    ol_display_op *op = append_op(doc, OL_OP_RULE);
    if (!op) return 0;
    op->u.rule.node_id = id;
    op->u.rule.x1 = x1;
    op->u.rule.y1 = y1;
    op->u.rule.x2 = x2;
    op->u.rule.y2 = y2;
    op->u.rule.thickness = thickness;
    op->u.rule.argb = argb;
    /* a page may end above or below a horizontal rule, not through it */
    return y1 != y2 || add_keep(doc, y1, y1 + (thickness > 0 ? thickness : 1));
}

/* 0.9: OL_BOX_SIDE_BORDERS: each side's border a rule along that side,
 * inside the box: a rule covers from its line to the thickness right of and
 * below it (as ol_draw_page's box for it does), so each is the strip of the
 * box its width deep. The top and bottom ones go the box's whole width, the
 * left and right ones its whole height. */
static int add_side_borders(ol_document *doc, const ol_node *node)
{
    const ol_box_style *b = &node->box;
    ol_rect r = node->bounds;
    ol_unit x2 = r.x + r.width, y2 = r.y + r.height;
    ol_unit t = side_border(node, 0), rt = side_border(node, 1), bm = side_border(node, 2), l = side_border(node, 3);
    if (rect_empty(r)) return 1;
    if (t && !add_rule(doc, node->id, r.x, r.y, x2 - t, r.y, t, b->border_argbs[0])) return 0;
    if (bm && !add_rule(doc, node->id, r.x, y2 - bm, x2 - bm, y2 - bm, bm, b->border_argbs[2])) return 0;
    if (l && !add_rule(doc, node->id, r.x, r.y, r.x, y2 - l, l, b->border_argbs[3])) return 0;
    if (rt && !add_rule(doc, node->id, x2 - rt, r.y, x2 - rt, y2 - rt, rt, b->border_argbs[1])) return 0;
    return 1;
}

static int add_borders(ol_document *doc, const ol_node *node)
{
    const ol_box_style *b = &node->box;
    ol_rect r = node->bounds;
    ol_unit x2 = r.x + r.width, y2 = r.y + r.height;
    if (b->flags & OL_BOX_SIDE_BORDERS) return add_side_borders(doc, node);
    if (b->border_width <= 0 || !b->border_sides || rect_empty(r)) return 1;
    if ((b->border_sides & OL_BORDER_TOP) &&
        !add_rule(doc, node->id, r.x, r.y, x2, r.y, b->border_width, b->border_argb)) return 0;
    if ((b->border_sides & OL_BORDER_BOTTOM) &&
        !add_rule(doc, node->id, r.x, y2, x2, y2, b->border_width, b->border_argb)) return 0;
    if ((b->border_sides & OL_BORDER_LEFT) &&
        !add_rule(doc, node->id, r.x, r.y, r.x, y2, b->border_width, b->border_argb)) return 0;
    if ((b->border_sides & OL_BORDER_RIGHT) &&
        !add_rule(doc, node->id, x2, r.y, x2, y2, b->border_width, b->border_argb)) return 0;
    return 1;
}

static int layout_block(ol_document *doc, ol_node *node,
                        ol_unit x, ol_unit y, ol_unit width,
                        ol_measure_text_fn measure, void *measure_userdata,
                        ol_unit *bottom_out);
static void zones_live(ol_document *doc, const ol_node *block, ol_unit cx, ol_unit cy);

static int is_frame(const ol_node *node)
{
    return (node->box.flags & OL_BOX_ABSOLUTE) != 0;
}

/* A frame: at its place from origin (its containing block's content
 * corner); anchored to its page, at 0, 0 on its own, to be placed when the
 * pages are known, and leaving no keeps or breaks behind (one inside
 * another is laid out where the outer one is). */
static int layout_frame(ol_document *doc, ol_node *node, ol_unit origin_x, ol_unit origin_y, ol_unit line_y,
                        ol_measure_text_fn measure, void *measure_userdata)
{
    ol_unit bottom;
    ol_unit w = node->box.frame.width > 0 ? node->box.frame.width : OL_CSSPX(1);
    size_t keeps, breaks, first;
    /* 0.11: with no width and OL_BOX_FIT_CONTENT, as wide as its content (its margins left out) */
    if (node->box.frame.width <= 0 && (node->box.flags & OL_BOX_FIT_CONTENT)) {
        w = content_width_in(doc, node, 1, measure, measure_userdata) - node->style.margin_left -
            node->style.margin_right;
        if (w < OL_CSSPX(1)) w = OL_CSSPX(1);
    }
    ol_anchor *a;
    int ok;
    if (!node->box.anchor || doc->anchor_depth) {
        ++doc->wrap_off;                    /* a frame's own lines do not wrap round frames */
        ok = layout_block(doc, node, origin_x + node->box.frame.x, origin_y + node->box.frame.y,
                          w, measure, measure_userdata, &bottom);
        --doc->wrap_off;
        return ok;
    }
    if (doc->anchor_count == doc->anchor_capacity) {
        size_t cap = doc->anchor_capacity ? doc->anchor_capacity * 2 : 8;
        ol_anchor *p = (ol_anchor *)realloc(doc->anchors, cap * sizeof(*p));
        if (!p) return 0;
        doc->anchors = p;
        doc->anchor_capacity = cap;
    }
    keeps = doc->keep_count;
    breaks = doc->break_count;
    first = doc->op_count;
    ++doc->anchor_depth;
    ++doc->wrap_off;
    ok = layout_block(doc, node, 0, 0, w, measure, measure_userdata, &bottom);
    --doc->wrap_off;
    --doc->anchor_depth;
    doc->keep_count = keeps;
    doc->break_count = breaks;
    if (!ok) return 0;
    a = &doc->anchors[doc->anchor_count++];
    a->first = first;
    a->end = doc->op_count;
    a->x = origin_x;
    a->y = origin_y;
    a->frame = node->box.frame;
    a->anchor = node->box.anchor;
    a->node = node;
    a->line_y = line_y;
    a->page = -1;
    return 1;
}

static void move_op(ol_display_op *op, ol_unit dx, ol_unit dy);
static const ol_anchor *anchor_at(const ol_document *doc, size_t i);

/* 0.9: a node's blocks below it moved down dy (their bounds; inline
 * nodes' come from their ops at the end) */
static void move_bounds_below(ol_node *node, ol_unit dy)
{
    ol_node *c;
    for (c = node->first_child; c; c = c->next_sibling) {
        if (!rect_empty(c->bounds)) c->bounds.y += dy;
        move_bounds_below(c, dy);
    }
}

/* 0.9: a cell's content, its ops first to end, moved down dy in its row
 * (box.valign): all but its own background, its frames' places, and its
 * tables' repeated headers. */
static void move_cell_content(ol_document *doc, ol_node *cell, size_t first, size_t end, ol_unit dy)
{
    size_t i;
    for (i = first; i < end; ++i) {
        ol_display_op *op = &doc->ops[i];
        if (i == first && op->type == OL_OP_FILL_RECT && op->u.fill_rect.node_id == cell->id) continue;
        if (anchor_at(doc, i)) continue;
        move_op(op, 0, dy);
    }
    for (i = 0; i < doc->anchor_count; ++i)
        if (doc->anchors[i].first >= first && doc->anchors[i].end <= end) {
            doc->anchors[i].y += dy;
            doc->anchors[i].line_y += dy;
        }
    for (i = 0; i < doc->repeat_count; ++i)
        if (doc->repeats[i].first >= first && doc->repeats[i].end_op <= end) {
            doc->repeats[i].top += dy;
            doc->repeats[i].bottom += dy;
            doc->repeats[i].end += dy;
        }
    move_bounds_below(cell, dy);
}

/* 0.9: the width a table's cell asks for its column, its margins included:
 * box.width or width_permille of the table's, between its least and its
 * most; its least alone; 0: none. */
static ol_unit cell_hint(const ol_node *cell, ol_unit width)
{
    ol_unit asked = asked_width(cell, width), m = cell->style.margin_left + cell->style.margin_right;
    if (asked > 0) return clamp_width(cell, asked, width) + m;
    return cell->box.min_width > 0 ? cell->box.min_width + m : 0;
}

/* A table's rows, as a grid: each cell at its column, every cell in a row
 * as tall as the row's tallest, its background and borders to match. 0.9:
 * without the table's column widths, its cells' widths are its columns'
 * (the widest of a column's cells that span it alone), as if they were the
 * table's, the others sharing what is left; a cell's content stands at the
 * top, middle or bottom of its row (box.valign); and the header rows
 * (OL_BOX_REPEAT_HEADER from the first row on) are kept with the first row
 * after them, and noted for the pages the table goes on to. */
static int layout_table(ol_document *doc, ol_node *table, ol_unit x, ol_unit y,
                        ol_unit width, ol_measure_text_fn measure,
                        void *measure_userdata, ol_unit *bottom_out)
{
    ol_unit widths[OL_MAX_COLUMNS], col_x[OL_MAX_COLUMNS + 1], hints[OL_MAX_COLUMNS];
    ol_node *cells[OL_MAX_COLUMNS];
    size_t cell_first_op[OL_MAX_COLUMNS];
    int ncols = table->column_count, i, hinted = 0;
    const ol_unit *columns = table->columns;
    ol_node *row, *cell;
    /* 0.9: the header rows so far: top, bottom, ops; and below the first row after them */
    int heads = 0, in_header = 1;
    ol_unit head_top = 0, head_bottom = 0, first_body_bottom = -1;
    size_t head_first = 0, head_end = 0;

    if (!ncols) {
        for (row = table->first_child; row; row = row->next_sibling) {
            int n = 0;
            if (row->role != OL_ROLE_ROW || row->style.display == OL_DISPLAY_NONE) continue;
            for (cell = row->first_child; cell; cell = cell->next_sibling)
                if (cell->style.display != OL_DISPLAY_NONE)
                    n += cell->box.column_span > 1 ? cell->box.column_span : 1;
            if (n > ncols) ncols = n;
        }
    }
    if (ncols > OL_MAX_COLUMNS) ncols = OL_MAX_COLUMNS;

    if (ncols > 0 && !table->column_count) {        /* 0.9: the cells' widths as the columns' */
        memset(hints, 0, sizeof(hints));
        for (row = table->first_child; row; row = row->next_sibling) {
            int col = 0;
            if (row->role != OL_ROLE_ROW || row->style.display == OL_DISPLAY_NONE) continue;
            for (cell = row->first_child; cell && col < ncols; cell = cell->next_sibling) {
                int span;
                ol_unit h;
                if (cell->style.display == OL_DISPLAY_NONE) continue;
                span = cell->box.column_span > 1 ? cell->box.column_span : 1;
                h = span == 1 ? cell_hint(cell, width) : 0;
                if (h > hints[col]) {
                    hints[col] = h;
                    hinted = 1;
                }
                col += span;
            }
        }
        if (hinted) columns = hints;
    }

    if (ncols > 0) {
        if (table->column_count || hinted) {
            int64_t sum = 0;
            int unset = 0;
            ol_unit left;
            for (i = 0; i < ncols; ++i) {
                if (columns[i] > 0) sum += columns[i];
                else ++unset;
            }
            for (i = 0; i < ncols; ++i)
                widths[i] = columns[i] > 0
                    ? (sum > width ? (ol_unit)((int64_t)columns[i] * width / sum)
                                   : columns[i])
                    : 0;
            left = width - (ol_unit)(sum > width ? width : sum);
            for (i = 0; unset && i < ncols; ++i)
                if (widths[i] == 0) widths[i] = left / unset;
        } else {
            for (i = 0; i < ncols; ++i) widths[i] = width / ncols;
            widths[ncols - 1] = width - (width / ncols) * (ncols - 1);
        }
        col_x[0] = x;
        for (i = 0; i < ncols; ++i) col_x[i + 1] = col_x[i] + widths[i];
    }

    for (row = table->first_child; row; row = row->next_sibling) {
        ol_unit row_top = y, row_bottom = y;
        size_t row_bg = (size_t)-1, row_first = doc->op_count, cells_end;
        int col = 0, ncells = 0, k;

        if (row->style.display == OL_DISPLAY_NONE) continue;
        if (row->role != OL_ROLE_ROW || ncols <= 0) {
            /* a caption or other block between the rows */
            if (!layout_block(doc, row, x, y, width, measure, measure_userdata, &y)) return 0;
            if (heads) in_header = 0;
            continue;
        }
        if ((row->box.flags & OL_BOX_PAGE_BREAK_BEFORE) && !add_break(doc, y)) return 0;
        memset(&row->bounds, 0, sizeof(row->bounds));
        if ((row->style.background >> 24) != 0) {
            ol_display_op *bg = append_op(doc, OL_OP_FILL_RECT);
            if (!bg) return 0;
            row_bg = doc->op_count - 1;
            bg->u.fill_rect.node_id = row->id;
            bg->u.fill_rect.argb = row->style.background;
        }
        for (cell = row->first_child; cell && col < ncols; cell = cell->next_sibling) {
            int span;
            ol_unit cell_bottom;
            if (cell->style.display == OL_DISPLAY_NONE) continue;
            span = cell->box.column_span > 1 ? cell->box.column_span : 1;
            if (col + span > ncols) span = ncols - col;
            cells[ncells] = cell;
            cell_first_op[ncells] = doc->op_count;
            ++ncells;
            if (!layout_block(doc, cell, col_x[col], row_top, col_x[col + span] - col_x[col],
                              measure, measure_userdata, &cell_bottom))
                return 0;
            if (cell_bottom > row_bottom) row_bottom = cell_bottom;
            col += span;
        }
        cells_end = doc->op_count;
        for (k = 0; k < ncells; ++k) {
            ol_node *c = cells[k];
            ol_unit h = row_bottom - c->style.margin_bottom - c->bounds.y;
            if (h > c->bounds.height) {
                /* 0.9: its content in the middle or at the bottom of the room the row gives it */
                ol_unit dy = c->box.valign == OL_VALIGN_MIDDLE ? (h - c->bounds.height) / 2
                           : c->box.valign == OL_VALIGN_BOTTOM ? h - c->bounds.height : 0;
                if (dy > 0)
                    move_cell_content(doc, c, cell_first_op[k], k + 1 < ncells ? cell_first_op[k + 1] : cells_end, dy);
                c->bounds.height = h;
                if ((c->style.background >> 24) != 0 && cell_first_op[k] < doc->op_count &&
                    doc->ops[cell_first_op[k]].type == OL_OP_FILL_RECT)
                    doc->ops[cell_first_op[k]].u.fill_rect.bounds = c->bounds;
            }
            if (!add_borders(doc, c)) return 0;
        }
        row->bounds.x = x;
        row->bounds.y = row_top;
        row->bounds.width = width;
        row->bounds.height = row_bottom - row_top;
        if (row_bg != (size_t)-1) doc->ops[row_bg].u.fill_rect.bounds = row->bounds;
        if (!add_keep(doc, row_top, row_bottom)) return 0;
        if (in_header && (row->box.flags & OL_BOX_REPEAT_HEADER)) {
            if (!heads++) {
                head_top = row_top;
                head_first = row_first;
            }
            head_bottom = row_bottom;
            head_end = doc->op_count;
        } else {
            if (in_header && heads) first_body_bottom = row_bottom;
            in_header = 0;
        }
        y = row_bottom;
    }
    /* 0.9: a header with rows after it: never alone at a page's foot, and noted for the pages after (not in
     * a frame anchored to its page, nor in an inline-block, which are never cut) */
    if (heads && first_body_bottom >= 0 && !doc->anchor_depth) {
        ol_repeat *r;
        if (!add_keep(doc, head_top, first_body_bottom)) return 0;
        if (doc->repeat_count == doc->repeat_capacity) {
            size_t cap = doc->repeat_capacity ? doc->repeat_capacity * 2 : 4;
            ol_repeat *q = (ol_repeat *)realloc(doc->repeats, cap * sizeof(*q));
            if (!q) return 0;
            doc->repeats = q;
            doc->repeat_capacity = cap;
        }
        r = &doc->repeats[doc->repeat_count++];
        r->top = head_top;
        r->bottom = head_bottom;
        r->end = y;
        r->first = head_first;
        r->end_op = head_end;
    }
    *bottom_out = y;
    return 1;
}

/* 0.8: what a layout has added so far, to take back and lay out again
 * (ol_mark, above; 0.9: with the tables' repeated headers) */
static void mark_take(const ol_document *doc, ol_mark *m)
{
    m->ops = doc->op_count;
    m->keeps = doc->keep_count;
    m->breaks = doc->break_count;
    m->anchors = doc->anchor_count;
    m->repeats = doc->repeat_count;
}

static void mark_back(ol_document *doc, const ol_mark *m)
{
    doc->op_count = m->ops;
    doc->keep_count = m->keeps;
    doc->break_count = m->breaks;
    doc->anchor_count = m->anchors;
    doc->repeat_count = m->repeats;
}

static int is_grower(const ol_node *n)
{
    return n->style.display != OL_DISPLAY_NONE && !is_frame(n) && n->box.grow > 0;
}

/* 0.8: how wide a node is with nothing wrapped: its text on one line (the
 * longest of its lines), its inline children run on together, its block
 * children's widest, a row's children side by side; its padding and
 * margins round that. A box.width is its width. */
static ol_unit content_width(ol_document *doc, const ol_node *node,
                             ol_measure_text_fn measure, void *measure_userdata)
{
    return content_width_in(doc, node, 0, measure, measure_userdata);
}

/* frame_itself: node is a frame being measured for its own width (0.11's OL_BOX_FIT_CONTENT) */
static ol_unit content_width_in(ol_document *doc, const ol_node *node, int frame_itself,
                                ol_measure_text_fn measure, void *measure_userdata)
{
    ol_unit w = 0, run = 0, ml, mr;
    const ol_node *c;
    int n = 0;
    if (!node || node->style.display == OL_DISPLAY_NONE || (is_frame(node) && !frame_itself)) return 0;
    ml = node->style.margin_left;
    mr = node->style.margin_right;
    if (node->box.width > 0) return clamp_width(node, node->box.width, 0) + ml + mr;
    if (node->text && node->text[0]) {
        const char *t = node->text;
        while (*t) {
            ol_text_metrics m;
            size_t len = strcspn(t, "\n");
            memset(&m, 0, sizeof(m));
            if (len && measure_with(measure, measure_userdata, t, len, &node->style, &m) &&
                m.width > run)
                run = m.width;
            t += len;
            if (*t) ++t;
        }
    }
    if (node->role == OL_ROLE_IMAGE) run += node->image_width;
    for (c = node->first_child; c; c = c->next_sibling) {
        ol_unit cw;
        if (c->style.display == OL_DISPLAY_NONE || is_frame(c)) continue;
        cw = content_width(doc, c, measure, measure_userdata);
        if (node->box.flags & OL_BOX_ROW) {
            w += cw + (n++ ? node->box.gap : 0);
        } else if (is_inline(c)) {
            run += cw;
        } else {
            if (run > w) w = run;
            run = 0;
            if (cw > w) w = cw;
        }
    }
    if (run > w) w = run;
    if (node->role == OL_ROLE_TABLE && node->column_count) {
        int i;
        ol_unit sum = 0;
        for (i = 0; i < node->column_count; ++i) sum += node->columns[i];
        if (sum > w) w = sum;
    }
    /* 0.9: its side borders too, between its least and its most */
    return clamp_width(node, w + inset_left(node) + inset_right(node), 0) + ml + mr;
}

static int layout_block_in(ol_document *doc, ol_node *node,
                           ol_unit x, ol_unit y, ol_unit width,
                           ol_measure_text_fn measure, void *measure_userdata,
                           ol_unit *bottom_out);

/* 0.8: a row's children side by side (OL_BOX_ROW). Each is as wide as
 * its box.width or its content; what room is left goes to those that grow,
 * in proportion to box.grow, and with too little room those without a
 * width give it up in proportion to theirs. Laid out once to find the
 * row's height, and again only when a child must be as tall as the row or
 * stand lower in it. */
static int layout_row(ol_document *doc, ol_node *row, ol_unit x, ol_unit y, ol_unit width,
                      ol_measure_text_fn measure, void *measure_userdata, ol_unit *bottom_out)
{
    ol_node *c;
    ol_unit used = 0, flex = 0, left, cx, row_h = 0, fixed_h;
    int32_t grow = 0;
    int n = 0, pass, again = 0;
    ol_mark mark;

    for (c = row->first_child; c; c = c->next_sibling) {
        if (c->style.display == OL_DISPLAY_NONE || is_frame(c)) continue;
        c->force_height = 0;
        /* the width it asks for, kept in its bounds until it is laid out */
        c->bounds.width = content_width(doc, c, measure, measure_userdata);
        if (c->box.width <= 0 && c->box.width_permille > 0)       /* 0.9: of the row's width */
            c->bounds.width = clamp_width(c, permille_of(width, c->box.width_permille), width) +
                              c->style.margin_left + c->style.margin_right;
        used += c->bounds.width;
        if (!has_width(c)) flex += c->bounds.width;
        if (c->box.grow > 0) grow += c->box.grow;
        ++n;
    }
    if (!n) {
        *bottom_out = y;
        return 1;
    }
    used += row->box.gap * (n - 1);
    left = width - used;
    cx = x;
    if (left > 0 && !grow) {
        if (row->style.text_align == OL_ALIGN_CENTER) cx += left / 2;
        else if (row->style.text_align == OL_ALIGN_END) cx += left;
    }
    {
        ol_unit share_left = left;
        int32_t grow_left = grow;
        ol_unit flex_left = flex;
        for (c = row->first_child; c; c = c->next_sibling) {
            ol_unit w = c->bounds.width, d = 0;
            if (c->style.display == OL_DISPLAY_NONE || is_frame(c)) continue;
            if (left > 0 && c->box.grow > 0) {
                /* the last that grows takes what rounding left over */
                d = grow_left == c->box.grow ? share_left
                                             : (ol_unit)((int64_t)left * c->box.grow / grow);
                grow_left -= c->box.grow;
            } else if (left < 0 && !has_width(c) && flex > 0) {
                d = flex_left == w ? share_left : (ol_unit)((int64_t)left * w / flex);
                flex_left -= w;
            }
            if ((left > 0 && c->box.grow > 0) || (left < 0 && !has_width(c) && flex > 0))
                share_left -= d;
            w += d;
            {                               /* 0.9: between its least and its most, of the row's width */
                ol_unit m = c->style.margin_left + c->style.margin_right;
                w = clamp_width(c, w - m, width) + m;
            }
            if (w < OL_CSSPX(1)) w = OL_CSSPX(1);
            c->bounds.x = w;                /* its width, until it is laid out */
        }
    }

    fixed_h = row->force_height > 0 ? row->force_height : row->box.height;
    if (fixed_h > 0) {
        row_h = fixed_h - inset_top(row) - inset_bottom(row);
        if (row_h < 0) row_h = 0;
    }
    mark_take(doc, &mark);
    for (pass = 0; pass < 2; ++pass) {
        ol_unit at = cx, tallest = 0;
        for (c = row->first_child; c; c = c->next_sibling) {
            ol_unit w, bottom, top = y, h;
            if (c->style.display == OL_DISPLAY_NONE || is_frame(c)) continue;
            w = pass ? c->bounds.width + c->style.margin_left + c->style.margin_right
                     : c->bounds.x;
            if (pass) {
                h = c->bounds.height + c->style.margin_top + c->style.margin_bottom;
                switch (row->box.align) {
                case OL_CROSS_START: break;
                case OL_CROSS_CENTRE: top += (row_h - h) / 2; break;
                case OL_CROSS_END: top += row_h - h; break;
                default:
                    c->force_height = row_h - c->style.margin_top - c->style.margin_bottom;
                    if (c->force_height < OL_CSSPX(1)) c->force_height = OL_CSSPX(1);
                    break;
                }
            }
            if (!layout_block_in(doc, c, at, top, w, measure, measure_userdata, &bottom)) return 0;
            c->force_height = 0;
            if (bottom - y > tallest) tallest = bottom - y;
            if (!pass) {
                h = c->bounds.height + c->style.margin_top + c->style.margin_bottom;
                if (fixed_h <= 0 && h > row_h) row_h = h;
            }
            at += w + row->box.gap;
        }
        if (pass) break;
        /* again when one is not as tall as the row, or must stand lower in it */
        for (c = row->first_child; c && !again; c = c->next_sibling) {
            ol_unit h;
            if (c->style.display == OL_DISPLAY_NONE || is_frame(c)) continue;
            h = c->bounds.height + c->style.margin_top + c->style.margin_bottom;
            if (h != row_h && row->box.align != OL_CROSS_START) again = 1;
        }
        if (!again) {
            if (fixed_h <= 0 && tallest > row_h) row_h = tallest;
            break;
        }
        mark_back(doc, &mark);
    }
    /* the row is one stretch a page may not cut */
    doc->keep_count = mark.keeps;
    doc->break_count = mark.breaks;
    if (!add_keep(doc, y, y + row_h)) return 0;
    *bottom_out = y + row_h;
    return 1;
}

/* A block's own text and its children, one under another from *y; with
 * 0.8's gap between its blocks. */
static int layout_children(ol_document *doc, ol_node *node, ol_inline_flow *flow,
                           ol_unit inner_x, ol_unit inner_w, ol_unit *y,
                           ol_measure_text_fn measure, void *measure_userdata)
{
    ol_unit keep_with_next = 0, current_y = *y;
    int has_keep_with_next = 0, blocks = 0;
    ol_node *child;

    if (node->text && node->text[0]) {
        if (!layout_inline_text(flow, node)) return 0;
    }
    for (child = node->first_child; child; child = child->next_sibling) {
        if (child->style.display == OL_DISPLAY_NONE || is_frame(child)) continue;
        if (is_inline(child)) {
            if (!layout_inline_node(flow, child)) return 0;
        } else {
            ol_unit child_bottom, w, dx;
            size_t keeps_before;
            if (!flush_flow(flow, &current_y)) return 0;
            if (blocks++) current_y += node->box.gap;
            keeps_before = doc->keep_count;
            w = block_width(child, inner_w, &dx);      /* 0.9: its width asked for, and its auto margins */
            if (!layout_block_in(doc, child, inner_x + dx, current_y, w,
                                 measure, measure_userdata, &child_bottom))
                return 0;
            /* a heading stays with the next block's first line */
            if (has_keep_with_next && doc->keep_count > keeps_before) {
                if (!add_keep(doc, keep_with_next, doc->keeps[keeps_before].bottom)) return 0;
                has_keep_with_next = 0;
            }
            if ((child->box.flags & OL_BOX_KEEP_WITH_NEXT) && doc->keep_count > keeps_before) {
                keep_with_next = doc->keeps[doc->keep_count - 1].top;
                has_keep_with_next = 1;
            }
            current_y = child_bottom;
            start_flow(flow, doc, node, measure, measure_userdata, inner_x, inner_x + inner_w,
                       current_y, node->style.text_align, 0);
        }
    }
    *y = current_y;
    return 1;
}

static int layout_block_in(ol_document *doc, ol_node *node,
                           ol_unit x, ol_unit y, ol_unit width,
                           ol_measure_text_fn measure, void *measure_userdata,
                           ol_unit *bottom_out)
{
    ol_unit box_x, box_y, inner_x, inner_w, current_y, content_bottom, empty_top, fixed_h;
    ol_unit ml = node ? node->style.margin_left : 0, mr = node ? node->style.margin_right : 0;
    ol_unit mt = node ? node->style.margin_top : 0, mb = node ? node->style.margin_bottom : 0;
    ol_node *child;
    ol_inline_flow flow;
    size_t background_index = (size_t)-1, shape_index = (size_t)-1, first_op, keeps_at_start;
    ol_unit first_line;

    if (!node || node->style.display == OL_DISPLAY_NONE) {
        *bottom_out = y;
        return 1;
    }
    keeps_at_start = doc->keep_count;
    first_op = doc->op_count;
    if (is_frame(node)) ml = mr = mt = mb = 0;   /* a frame is where it is put */
    if ((node->box.flags & OL_BOX_PAGE_BREAK_BEFORE) && !add_break(doc, y)) return 0;
    fixed_h = node->force_height > 0 ? node->force_height : node->box.height;

    box_x = x + ml;
    box_y = y + mt;
    inner_x = box_x + inset_left(node);
    inner_w = width - ml - mr - inset_left(node) - inset_right(node);
    if (inner_w < OL_CSSPX(1)) inner_w = OL_CSSPX(1);

    memset(&node->bounds, 0, sizeof(node->bounds));

    if ((node->style.background >> 24) != 0) {
        ol_display_op *bg = append_op(doc, OL_OP_FILL_RECT);
        if (!bg) return 0;
        background_index = doc->op_count - 1;
        bg->u.fill_rect.node_id = node->id;
        bg->u.fill_rect.argb = node->style.background;
    }
    if (node->graphic && node->graphic->shape.type != OL_SHAPE_NONE) {
        ol_display_op *shape = append_op(doc, OL_OP_SHAPE);
        if (!shape) return 0;
        shape_index = doc->op_count - 1;
        shape->u.fill_rect.node_id = node->id;
        shape->u.fill_rect.argb = node->graphic->shape.fill_argb;
        shape->graphic = node->graphic;
    }

    current_y = empty_top = box_y + inset_top(node);
    zones_live(doc, node, inner_x, current_y);  /* 0.6: its frames, for its own lines too */
    start_flow(&flow, doc, node, measure, measure_userdata, inner_x, inner_x + inner_w,
               current_y, node->style.text_align, node->box.text_indent);

    if (node->role == OL_ROLE_TABLE) {
        int ok;
        ++doc->wrap_off;                    /* 0.6: a cell's lines do not wrap round frames */
        ok = layout_table(doc, node, inner_x, current_y, inner_w, measure, measure_userdata, &current_y);
        --doc->wrap_off;
        if (!ok) return 0;
        flow.line_top = current_y;
    } else if (node->box.flags & OL_BOX_ROW) {
        if (fixed_h > 0) node->force_height = fixed_h;
        if (!layout_row(doc, node, inner_x, current_y, inner_w, measure, measure_userdata,
                        &current_y))
            return 0;
        flow.line_top = current_y;
    } else {
        ol_unit start_y = current_y;
        ol_mark mark;
        int growers = 0;
        if (fixed_h > 0)
            for (child = node->first_child; child; child = child->next_sibling)
                if (is_grower(child)) { child->force_height = 0; ++growers; }
        mark_take(doc, &mark);
        if (!layout_children(doc, node, &flow, inner_x, inner_w, &current_y,
                             measure, measure_userdata))
            return 0;
        if (growers) {
            /* 0.8: what the height leaves below them, shared by those that grow */
            ol_unit end_y, left;
            int32_t grow = 0;
            if (!flush_flow(&flow, &end_y)) return 0;
            if (end_y < current_y) end_y = current_y;
            left = box_y + fixed_h - inset_bottom(node) - end_y;
            if (left > 0) {
                ol_unit share_left = left;
                for (child = node->first_child; child; child = child->next_sibling)
                    if (is_grower(child)) grow += child->box.grow;
                for (child = node->first_child; child; child = child->next_sibling) {
                    ol_unit d;
                    if (!is_grower(child)) continue;
                    d = grow == child->box.grow ? share_left
                                                : (ol_unit)((int64_t)left * child->box.grow / grow);
                    grow -= child->box.grow;
                    share_left -= d;
                    left = share_left;      /* what the next ones share, in proportion */
                    child->force_height = child->bounds.height + d;
                }
                mark_back(doc, &mark);
                current_y = start_y;
                start_flow(&flow, doc, node, measure, measure_userdata, inner_x, inner_x + inner_w,
                           current_y, node->style.text_align, node->box.text_indent);
                if (!layout_children(doc, node, &flow, inner_x, inner_w, &current_y,
                                     measure, measure_userdata))
                    return 0;
            }
            for (child = node->first_child; child; child = child->next_sibling)
                child->force_height = 0;
        }
    }
    node->force_height = 0;

    if (!flush_flow(&flow, &content_bottom)) return 0;
    if (content_bottom < current_y) content_bottom = current_y;
    if ((node->box.flags & OL_BOX_EMPTY_LINE) && content_bottom <= empty_top) {
        if (flow.wraps) {                   /* 0.6: the empty line goes below a frame too */
            flow.line_top = empty_top;
            if (!line_room(&flow)) return 0;
            empty_top = flow.line_top;
        }
        content_bottom = empty_top + (node->style.line_height > 0 ? node->style.line_height
                                                                  : OL_CSSPX(20));
        if (!add_keep(doc, empty_top, content_bottom)) return 0;
    }
    /* 0.6: where its first line went (below a frame, say), for the page its frames go on */
    first_line = doc->keep_count > keeps_at_start ? doc->keeps[keeps_at_start].top
                                                  : box_y + inset_top(node);
    if (fixed_h > 0) content_bottom = box_y + fixed_h - inset_bottom(node);  /* 0.8 */
    content_bottom += inset_bottom(node);

    node->bounds.x = box_x;
    node->bounds.y = box_y;
    node->bounds.width = width - ml - mr;
    if (node->bounds.width < 0) node->bounds.width = 0;
    node->bounds.height = content_bottom - box_y;
    if (is_frame(node) && node->box.frame.height > 0) node->bounds.height = node->box.frame.height;
    if (node->bounds.height < 0) node->bounds.height = 0;

    if (background_index != (size_t)-1)
        doc->ops[background_index].u.fill_rect.bounds = node->bounds;
    if (shape_index != (size_t)-1)
        doc->ops[shape_index].u.fill_rect.bounds = node->bounds;
    /* a cell's borders come when its row's height is known */
    if (node->role != OL_ROLE_CELL && !add_borders(doc, node)) return 0;
    if ((node->box.flags & (OL_BOX_KEEP_TOGETHER | OL_BOX_ABSOLUTE)) &&
        !add_keep(doc, node->bounds.y, node->bounds.y + node->bounds.height))
        return 0;

    /* frames last, above the flow they stand in */
    for (child = node->first_child; child; child = child->next_sibling)
        if (child->style.display != OL_DISPLAY_NONE && is_frame(child) &&
            !layout_frame(doc, child, inner_x, box_y + inset_top(node), first_line,
                          measure, measure_userdata))
            return 0;

    if ((node->box.flags & OL_BOX_CLIP_CONTENT) && !rect_empty(node->bounds)) {
        size_t i;
        for (i = first_op; i < doc->op_count; ++i) {
            if (doc->ops[i].clip.height)
                doc->ops[i].clip = rect_intersection(doc->ops[i].clip, node->bounds);
            else
                doc->ops[i].clip = node->bounds;
        }
    }

    *bottom_out = is_frame(node) ? node->bounds.y + node->bounds.height
                                 : content_bottom + mb;
    return 1;
}

static int layout_block(ol_document *doc, ol_node *node,
                        ol_unit x, ol_unit y, ol_unit width,
                        ol_measure_text_fn measure, void *measure_userdata,
                        ol_unit *bottom_out)
{
    return layout_block_in(doc, node, x, y, width, measure, measure_userdata, bottom_out);
}

/* 0.9: an inline-block's box, laid out as a block at x, y, w wide (its
 * margins included). Its lines do not wrap round frames, its frames stand
 * in it (none is anchored to a page) and its tables' headers are not
 * repeated; it leaves no keeps or breaks behind: its line keeps it whole. */
static int layout_inline_box(ol_inline_flow *flow, ol_node *node, ol_unit x, ol_unit y, ol_unit w,
                             ol_unit *bottom_out)
{
    ol_document *doc = flow->doc;
    size_t keeps = doc->keep_count, breaks = doc->break_count;
    int ok;
    ++doc->wrap_off;
    ++doc->anchor_depth;
    ok = layout_block_in(doc, node, x, y, w, flow->measure, flow->measure_userdata, bottom_out);
    --doc->anchor_depth;
    --doc->wrap_off;
    doc->keep_count = keeps;
    doc->break_count = breaks;
    return ok;
}

/* The document's children, one under another, as the galley. */
static int layout_galley(ol_document *doc, ol_unit width,
                         ol_measure_text_fn measure, void *measure_userdata)
{
    ol_node *child, *root = doc->root;
    ol_unit y = 0, keep_with_next = 0;
    int has_keep_with_next = 0;

    zones_live(doc, root, 0, 0);
    for (child = root->first_child; child; child = child->next_sibling) {
        size_t keeps_before = doc->keep_count;
        if (child->style.display == OL_DISPLAY_NONE || is_frame(child)) continue;
        if (is_inline(child)) {
            ol_inline_flow flow;
            start_flow(&flow, doc, NULL, measure, measure_userdata, 0, width, y,
                       child->style.text_align, 0);
            if (!layout_inline_node(&flow, child)) return 0;
            if (!flush_flow(&flow, &y)) return 0;
        } else {
            ol_unit bottom, dx, w;
            /* 0.9: its width asked for and its auto margins, in the galley too (0.8 gave the
             * document's own blocks the galley's width, whatever their box.width) */
            w = block_width(child, width, &dx);
            if (!layout_block(doc, child, dx, y, w, measure, measure_userdata, &bottom))
                return 0;
            y = bottom;
        }
        if (has_keep_with_next && doc->keep_count > keeps_before) {
            if (!add_keep(doc, keep_with_next, doc->keeps[keeps_before].bottom)) return 0;
            has_keep_with_next = 0;
        }
        if ((child->box.flags & OL_BOX_KEEP_WITH_NEXT) && doc->keep_count > keeps_before) {
            keep_with_next = doc->keeps[doc->keep_count - 1].top;
            has_keep_with_next = 1;
        }
    }
    for (child = root->first_child; child; child = child->next_sibling)
        if (child->style.display != OL_DISPLAY_NONE && is_frame(child) &&
            !layout_frame(doc, child, 0, 0, 0, measure, measure_userdata))
            return 0;

    inline_bounds(doc);
    root->bounds.x = 0;
    root->bounds.y = 0;
    root->bounds.width = width;
    root->bounds.height = y;
    doc->content_height = y;
    return 1;
}

/* The document galley with a different line width from each top-level
 * section boundary. Section starts are galley coordinates used by the
 * paginator after the pass. */
static int layout_galley_sections(ol_document *doc, ol_unit body_width,
                                  const ol_page_section *sections, int section_count,
                                  ol_unit *section_starts,
                                  ol_measure_text_fn measure, void *measure_userdata)
{
    ol_node *child, *root = doc->root;
    ol_unit y = 0, keep_with_next = 0;
    int has_keep_with_next = 0, section = 0;

    section_starts[0] = 0;
    zones_live(doc, root, 0, 0);
    for (child = root->first_child; child; child = child->next_sibling) {
        ol_unit width, column_width;
        size_t keeps_before;
        if (child->style.display == OL_DISPLAY_NONE || is_frame(child)) continue;
        if (section + 1 < section_count && child->id == sections[section + 1].first_node) {
            ++section;
            section_starts[section] = y;
            has_keep_with_next = 0; /* a section boundary ends the preceding keep chain */
        }
        column_width = (body_width - sections[section].gap * (sections[section].columns - 1)) /
                       sections[section].columns;
        width = column_width;
        keeps_before = doc->keep_count;
        if (is_inline(child)) {
            ol_inline_flow flow;
            start_flow(&flow, doc, NULL, measure, measure_userdata, 0, width, y,
                       child->style.text_align, 0);
            if (!layout_inline_node(&flow, child)) return 0;
            if (!flush_flow(&flow, &y)) return 0;
        } else {
            ol_unit bottom, dx, w;
            w = block_width(child, width, &dx);
            if (!layout_block(doc, child, dx, y, w, measure, measure_userdata, &bottom))
                return 0;
            y = bottom;
        }
        if (has_keep_with_next && doc->keep_count > keeps_before) {
            if (!add_keep(doc, keep_with_next, doc->keeps[keeps_before].bottom)) return 0;
            has_keep_with_next = 0;
        }
        if ((child->box.flags & OL_BOX_KEEP_WITH_NEXT) && doc->keep_count > keeps_before) {
            keep_with_next = doc->keeps[doc->keep_count - 1].top;
            has_keep_with_next = 1;
        }
    }
    if (section + 1 != section_count) return 0;
    for (child = root->first_child; child; child = child->next_sibling)
        if (child->style.display != OL_DISPLAY_NONE && is_frame(child) &&
            !layout_frame(doc, child, 0, 0, 0, measure, measure_userdata))
            return 0;

    inline_bounds(doc);
    root->bounds.x = 0;
    root->bounds.y = 0;
    root->bounds.width = body_width;
    root->bounds.height = y;
    doc->content_height = y;
    return 1;
}

static void reset_layout(ol_document *doc)
{
    size_t i;
    doc->op_count = 0;
    doc->keep_count = 0;
    doc->break_count = 0;
    doc->anchor_count = 0;
    doc->anchor_depth = 0;
    doc->ibox_count = 0;
    doc->repeat_count = 0;
    doc->paged = 0;
    doc->page_count = 0;
    doc->zones_moved = 0;
    for (i = 0; i < doc->zone_count; ++i) doc->zones[i].live = 0;   /* carried from the last pass */
    doc->live_count = 0;
    for (i = 0; i < doc->node_count; ++i)
        memset(&doc->nodes[i]->bounds, 0, sizeof(doc->nodes[i]->bounds));
}

static void clear_column_regions(ol_document *doc)
{
    free(doc->region_start); doc->region_start = NULL;
    free(doc->region_end); doc->region_end = NULL;
    free(doc->region_shift); doc->region_shift = NULL;
    free(doc->region_page); doc->region_page = NULL;
    free(doc->region_x); doc->region_x = NULL;
    free(doc->region_y); doc->region_y = NULL;
    free(doc->region_width); doc->region_width = NULL;
    doc->column_count = 0;
    doc->column_gap = doc->column_width = 0;
    doc->region_count = 0;
}

/* ---- Pages ----------------------------------------------------------------- */

static int add_page(ol_document *doc, ol_unit start, ol_unit end)
{
    if (doc->page_count == doc->page_capacity) {
        int cap = doc->page_capacity ? doc->page_capacity * 2 : 8;
        ol_unit *s = (ol_unit *)realloc(doc->page_start, (size_t)cap * sizeof(*s));
        ol_unit *e, *sh;
        size_t *f, *n;
        int *rp;
        if (!s) return 0;
        doc->page_start = s;
        e = (ol_unit *)realloc(doc->page_end, (size_t)cap * sizeof(*e));
        if (!e) return 0;
        doc->page_end = e;
        f = (size_t *)realloc(doc->page_first, (size_t)cap * sizeof(*f));
        if (!f) return 0;
        doc->page_first = f;
        n = (size_t *)realloc(doc->page_ops, (size_t)cap * sizeof(*n));
        if (!n) return 0;
        doc->page_ops = n;
        sh = (ol_unit *)realloc(doc->page_shift, (size_t)cap * sizeof(*sh));
        if (!sh) return 0;
        doc->page_shift = sh;
        rp = (int *)realloc(doc->page_repeat, (size_t)cap * sizeof(*rp));
        if (!rp) return 0;
        doc->page_repeat = rp;
        doc->page_capacity = cap;
    }
    doc->page_start[doc->page_count] = start;
    doc->page_end[doc->page_count] = end;
    doc->page_first[doc->page_count] = 0;
    doc->page_ops[doc->page_count] = 0;
    doc->page_shift[doc->page_count] = 0;
    doc->page_repeat[doc->page_count] = -1;
    ++doc->page_count;
    return 1;
}

/* The keeps sorted by top and merged into spans that do not overlap, and
 * the breaks sorted. Both come nearly in order, so insertion is enough. */
static void sort_keeps_and_breaks(ol_document *doc)
{
    size_t i, j, n = 0;
    for (i = 1; i < doc->keep_count; ++i) {
        ol_span k = doc->keeps[i];
        for (j = i; j > 0 && doc->keeps[j - 1].top > k.top; --j)
            doc->keeps[j] = doc->keeps[j - 1];
        doc->keeps[j] = k;
    }
    for (i = 0; i < doc->keep_count; ++i) {
        if (n && doc->keeps[i].top < doc->keeps[n - 1].bottom) {
            if (doc->keeps[i].bottom > doc->keeps[n - 1].bottom)
                doc->keeps[n - 1].bottom = doc->keeps[i].bottom;
        } else {
            doc->keeps[n++] = doc->keeps[i];
        }
    }
    doc->keep_count = n;
    for (i = 1; i < doc->break_count; ++i) {
        ol_unit b = doc->breaks[i];
        for (j = i; j > 0 && doc->breaks[j - 1] > b; --j)
            doc->breaks[j] = doc->breaks[j - 1];
        doc->breaks[j] = b;
    }
}

/* The first kept span that ends below y. */
static size_t span_after(const ol_document *doc, ol_unit y)
{
    size_t lo = 0, hi = doc->keep_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (doc->keeps[mid].bottom <= y) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

/* Where a page that may start at y starts: at the next content, so that
 * it does not begin with the space between paragraphs. */
static ol_unit content_from(const ol_document *doc, ol_unit y)
{
    size_t i = span_after(doc, y);
    if (i < doc->keep_count && doc->keeps[i].top > y) return doc->keeps[i].top;
    return y;
}

/* 0.9: the table header that goes again at the top of a page starting at
 * start, inside a table's rows below its header: the innermost such table's
 * (the one that starts last), when the header is less high than a page.
 * Its height, or 0; *which: its doc->repeats, or -1. */
static ol_unit header_for(const ol_document *doc, ol_unit start, ol_unit body_height, int *which)
{
    size_t i;
    ol_unit h = 0, top = 0;
    *which = -1;
    for (i = 0; i < doc->repeat_count; ++i) {
        const ol_repeat *r = &doc->repeats[i];
        if (r->bottom <= start && start < r->end && r->bottom - r->top < body_height &&
            (*which < 0 || r->top > top)) {
            *which = (int)i;
            top = r->top;
            h = r->bottom - r->top;
        }
    }
    return h;
}

static int cut_pages(ol_document *doc, ol_unit body_height)
{
    ol_unit total = doc->content_height, start = 0;
    size_t b = 0;

    if (doc->keep_count && doc->keeps[doc->keep_count - 1].bottom > total)
        total = doc->keeps[doc->keep_count - 1].bottom;
    for (;;) {
        int which = -1;
        /* 0.9: a page a table goes on to has its header first, and so less room */
        ol_unit shift = start > 0 && doc->repeat_count ? header_for(doc, start, body_height, &which) : 0;
        ol_unit limit = start + body_height - shift, end = limit;
        int forced = 0;

        while (b < doc->break_count && doc->breaks[b] <= start) ++b;
        if (b < doc->break_count && doc->breaks[b] <= limit && doc->breaks[b] < total) {
            end = doc->breaks[b++];
            forced = 1;
        }
        if (!forced) {
            if (limit >= total) {
                end = total;
            } else {
                size_t i = span_after(doc, limit);
                /* not inside a line, a row or a frame, unless it is taller than a page */
                if (i < doc->keep_count && doc->keeps[i].top < limit && doc->keeps[i].top > start)
                    end = doc->keeps[i].top;
            }
        }
        if (!add_page(doc, start, end)) return 0;
        doc->page_shift[doc->page_count - 1] = shift;
        doc->page_repeat[doc->page_count - 1] = which;
        if (end >= total) break;
        start = content_from(doc, end);
        if (start >= total) break;
    }
    return 1;
}

/* Cut one section's galley interval into column-height regions. The next
 * section may begin at the same galley coordinate as this one's last end. */
static int cut_page_range(ol_document *doc, ol_unit start, ol_unit total,
                          ol_unit body_height, ol_unit first_height,
                          int columns, int balance_last)
{
    size_t b = 0;
    int made = 0;
    if (total < start) return 0;
    while (b < doc->break_count && doc->breaks[b] <= start) ++b;
    for (;;) {
        int which = -1;
        int column = made % columns;
        int left = columns - column;
        ol_unit height = made < columns ? first_height : body_height;
        ol_unit cut_height = height;
        ol_unit shift = start > 0 && doc->repeat_count ? header_for(doc, start, height, &which) : 0;
        ol_unit remaining = total - start;
        ol_unit limit, end;
        int forced = 0;

        /* A continuous successor needs the final physical page to end in a
         * rectangular band. Once the section's remainder fits on this page,
         * divide it across the columns still available. Recompute after each
         * cut so paragraph keeps and explicit breaks cannot make later
         * columns overlap the following section. */
        if (balance_last && remaining > 0 && remaining <= height * left) {
            cut_height = (remaining + left - 1) / left;
            if (cut_height <= shift) cut_height = shift + 1;
            if (cut_height > height) cut_height = height;
        }
        limit = start + cut_height - shift;
        end = limit;
        if (height <= 0 || limit <= start) return 0;
        if (b < doc->break_count && doc->breaks[b] <= limit && doc->breaks[b] < total) {
            end = doc->breaks[b++];
            forced = 1;
        }
        if (!forced) {
            if (limit >= total) {
                end = total;
            } else {
                size_t i = span_after(doc, limit);
                if (i < doc->keep_count && doc->keeps[i].top < limit && doc->keeps[i].top > start)
                    end = doc->keeps[i].top;
            }
        }
        if (!add_page(doc, start, end)) return 0;
        doc->page_shift[doc->page_count - 1] = shift;
        doc->page_repeat[doc->page_count - 1] = which;
        ++made;
        if (end >= total) break;
        start = content_from(doc, end);
        if (start >= total) break;
    }
    return 1;
}

static int page_holding(const ol_document *doc, ol_unit y)
{
    int lo = 0, hi = doc->page_count - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (doc->page_start[mid] <= y) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

static int column_region_holding(const ol_document *doc, ol_unit y)
{
    int lo = 0, hi = doc->region_count - 1;
    if (hi < 0) return 0;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (doc->region_start[mid] <= y) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

/* The pages an op is drawn on: backgrounds and upright rules on every page
 * they cross, cut to it; everything else on the page its top is on. */
static void op_pages(const ol_document *doc, const ol_display_op *op,
                     int *first, int *last, int *split)
{
    ol_unit top, bottom;
    *split = 0;
    switch (op->type) {
    case OL_OP_TEXT: top = op->u.text.bounds.y; bottom = top; break;
    case OL_OP_IMAGE: top = op->u.image.bounds.y; bottom = top; break;
    case OL_OP_SHAPE: top = op->u.fill_rect.bounds.y; bottom = top; break;
    case OL_OP_FILL_RECT:
        top = op->u.fill_rect.bounds.y;
        bottom = top + op->u.fill_rect.bounds.height;
        *split = 1;
        break;
    case OL_OP_RULE:
        top = op->u.rule.y1 < op->u.rule.y2 ? op->u.rule.y1 : op->u.rule.y2;
        bottom = op->u.rule.y1 < op->u.rule.y2 ? op->u.rule.y2 : op->u.rule.y1;
        *split = top != bottom;
        break;
    default: top = bottom = 0; break;
    }
    *first = page_holding(doc, top);
    *last = *first;
    if (*split)
        while (*last + 1 < doc->page_count && doc->page_start[*last + 1] < bottom) ++*last;
}

/* An op moved onto page p: in the page's coordinates, cut to the page's
 * body when it crosses pages. */
static int place_op(const ol_document *doc, ol_display_op *op, int p, int split)
{
    /* 0.9: below a repeated table header; what comes down onto the page from
     * above it (the table's background and side borders, and its blocks')
     * goes up behind the header too */
    ol_unit dy = doc->page.margin_top + doc->page_shift[p] - doc->page_start[p];
    ol_unit dx = doc->page.margin_left, up = 0;
    ol_unit lo = doc->page_start[p], hi = doc->page_end[p];
    op->page = p;
    switch (op->type) {
    case OL_OP_TEXT:
        op->u.text.bounds.x += dx;
        op->u.text.bounds.y += dy;
        op->u.text.baseline += dy;
        break;
    case OL_OP_IMAGE:
        op->u.image.bounds.x += dx;
        op->u.image.bounds.y += dy;
        break;
    case OL_OP_SHAPE:
        op->u.fill_rect.bounds.x += dx;
        op->u.fill_rect.bounds.y += dy;
        break;
    case OL_OP_FILL_RECT: {
        ol_rect *r = &op->u.fill_rect.bounds;
        ol_unit t = r->y, b = r->y + r->height;
        if (split) {
            if (t < lo && p > 0) {
                t = lo;
                up = doc->page_shift[p];
            }
            if (b > hi && p + 1 < doc->page_count) b = hi;
            if (b <= t) return 0;
        }
        r->x += dx;
        r->y = t + dy - up;
        r->height = b - t + up;
        break;
    }
    case OL_OP_RULE:
        if (split) {
            ol_unit *a = op->u.rule.y1 < op->u.rule.y2 ? &op->u.rule.y1 : &op->u.rule.y2;
            ol_unit *z = a == &op->u.rule.y1 ? &op->u.rule.y2 : &op->u.rule.y1;
            if (*a < lo && p > 0) {
                *a = lo;
                up = doc->page_shift[p];
            }
            if (*z > hi && p + 1 < doc->page_count) *z = hi;
            if (*z <= *a) return 0;
            *a -= up;
        }
        op->u.rule.x1 += dx;
        op->u.rule.x2 += dx;
        op->u.rule.y1 += dy;
        op->u.rule.y2 += dy;
        break;
    }
    if (op->clip.height) {
        op->clip.x += dx;
        op->clip.y += dy - up;
    }
    return 1;
}

/* The anchored frame op i is in, or NULL; *k follows the anchors, which
 * are in op order and apart, as i grows. */
static const ol_anchor *anchor_of(const ol_document *doc, size_t i, size_t *k)
{
    while (*k < doc->anchor_count && doc->anchors[*k].end <= i) ++*k;
    return *k < doc->anchor_count && doc->anchors[*k].first <= i ? &doc->anchors[*k] : NULL;
}

/* Where an anchored frame's top left corner goes on a page that starts at
 * start in the galley. */
static void anchor_place_at(const ol_document *doc, const ol_anchor *a, int paged, ol_unit start,
                            ol_unit *ox, ol_unit *oy)
{
    ol_unit ml = paged ? doc->page.margin_left : 0, mt = paged ? doc->page.margin_top : 0;
    switch (OL_ANCHOR_X_OF(a->anchor)) {
    case OL_ANCHOR_PAGE: *ox = a->frame.x; break;
    case OL_ANCHOR_MARGIN: *ox = ml + a->frame.x; break;
    default: *ox = ml + a->x + a->frame.x; break;
    }
    switch (OL_ANCHOR_Y_OF(a->anchor)) {
    case OL_ANCHOR_PAGE: *oy = a->frame.y; break;
    case OL_ANCHOR_MARGIN: *oy = mt + a->frame.y; break;
    default: *oy = mt + a->y - start + a->frame.y; break;
    }
}

/* An anchored frame's page, and where its top left corner goes on it. */
static int anchor_place(const ol_document *doc, const ol_anchor *a, int paged, ol_unit *ox, ol_unit *oy)
{
    int p = !paged ? 0 : a->page >= 0 && a->page < doc->page_count ? a->page : page_holding(doc, a->y);
    anchor_place_at(doc, a, paged, paged ? doc->page_start[p] : 0, ox, oy);
    /* 0.9: one placed from its block moves down with it below a repeated table header */
    if (paged && OL_ANCHOR_Y_OF(a->anchor) == OL_ANCHOR_BLOCK) *oy += doc->page_shift[p];
    return p;
}

int ol_frame_place(const ol_document *doc, const ol_node *frame, int *page, ol_rect *rect)
{
    size_t k;
    if (!doc || !frame || !doc->page_count) return 0;
    for (k = 0; k < doc->anchor_count; ++k) {
        const ol_anchor *a = &doc->anchors[k];
        ol_unit ox, oy;
        int p;
        if (a->node != frame) continue;
        if (doc->region_count > 0 && doc->region_page && doc->region_x && doc->region_y) {
            int region = a->page >= 0 && a->page < doc->region_count
                         ? a->page : column_region_holding(doc, a->y);
            ol_unit start = doc->region_start[region];
            switch (OL_ANCHOR_X_OF(a->anchor)) {
            case OL_ANCHOR_PAGE: ox = a->frame.x; break;
            case OL_ANCHOR_MARGIN: ox = doc->page.margin_left + a->frame.x; break;
            default: ox = doc->region_x[region] + a->x + a->frame.x; break;
            }
            switch (OL_ANCHOR_Y_OF(a->anchor)) {
            case OL_ANCHOR_PAGE: oy = a->frame.y; break;
            case OL_ANCHOR_MARGIN: oy = doc->page.margin_top + a->frame.y; break;
            default: oy = doc->page.margin_top + doc->region_y[region] + doc->region_shift[region] +
                          a->y - start + a->frame.y; break;
            }
            p = doc->region_page[region];
            if (page) *page = p;
            if (rect) {
                rect->x = ox; rect->y = oy;
                rect->width = frame->bounds.width; rect->height = frame->bounds.height;
            }
            return 1;
        }
        p = anchor_place(doc, a, doc->paged, &ox, &oy);
        if (page) *page = p;
        if (rect) {
            rect->x = ox;
            rect->y = oy;
            rect->width = frame->bounds.width;
            rect->height = frame->bounds.height;
        }
        return 1;
    }
    return 0;
}

/* 0.6: a frame's zone, the frame h high, on a page that starts at start in
 * the galley. */
static void frame_zone(const ol_document *doc, const ol_anchor *a, ol_unit h, ol_unit start, ol_wrap_zone *z)
{
    const ol_box_style *box = &a->node->box;
    ol_unit ox, oy, w = a->frame.width > 0 ? a->frame.width : OL_CSSPX(1);
    anchor_place_at(doc, a, doc->paging, start, &ox, &oy);
    memset(z, 0, sizeof(*z));                /* compared whole, padding too */
    z->node = a->node;
    z->h = h;
    z->x0 = ox - box->wrap_gap[3];
    z->x1 = ox + w + box->wrap_gap[1];
    z->y0 = oy - box->wrap_gap[0];
    z->y1 = oy + h + box->wrap_gap[2];
    z->wrap = box->wrap == OL_WRAP_TOP_BOTTOM ? OL_WRAP_TOP_BOTTOM : OL_WRAP_SQUARE;
    z->after = OL_ANCHOR_Y_OF(a->anchor) == OL_ANCHOR_BLOCK && a->frame.y - box->wrap_gap[0] >= 0;
    z->live = 1;
    if (doc->paging) {
        ol_unit dy = start - doc->page.margin_top;
        z->x0 -= doc->page.margin_left;
        z->x1 -= doc->page.margin_left;
        z->y0 += dy;
        z->y1 += dy;
        z->page_top = start;
        z->page_bottom = start + doc->body_height;
    } else {
        z->page_top = -0x3fffffff;
        z->page_bottom = 0x3fffffff;
    }
}

/* 0.6: a page this pass has just reached: the zones carried from the last
 * pass that are on it move with it, to where it starts in this pass (a
 * frame placed from its page keeps its place on it). Zones worked out in
 * this pass already are where they belong. */
static void zones_follow_page(ol_document *doc, int page)
{
    size_t i;
    for (i = 0; i < doc->zone_count; ++i) {
        ol_wrap_zone *z = &doc->zones[i];
        ol_unit dy;
        if (z->page != page) continue;
        dy = doc->live_starts[page] - z->page_top;
        z->y0 += dy;
        z->y1 += dy;
        z->page_top += dy;
        z->page_bottom += dy;
    }
}

/* 0.6: the pages of this pass up to y, as cut_pages will cut them, from
 * the keeps and breaks laid out so far, a paragraph or a line about to
 * start at y, its first line h high: the index of the page that line is
 * on, its start in doc->live_starts. For the zones of the paragraph's frames,
 * before the pass's pages are cut. A keep added after (a block kept
 * together, a heading kept with what follows it) may still move a page
 * break; the pass's own pages then show it, and another pass follows. 
 * -1 when there is no memory. */
static int live_page(ol_document *doc, ol_unit y, ol_unit h)
{
    if (!doc->live_count) {
        if (!doc->live_capacity) {
            doc->live_starts = (ol_unit *)malloc(8 * sizeof(ol_unit));
            if (!doc->live_starts) return -1;
            doc->live_capacity = 8;
        }
        doc->live_starts[doc->live_count++] = 0;
        zones_follow_page(doc, 0);
    }
    for (;;) {
        ol_unit start = doc->live_starts[doc->live_count - 1], limit = start + doc->body_height;
        ol_unit end = limit, lo = limit, hi = limit, next = -1;
        size_t i;
        int grown, any = 0;
        for (i = 0; i < doc->break_count; ++i)
            if (doc->breaks[i] > start && doc->breaks[i] <= limit && doc->breaks[i] < end) {
                end = doc->breaks[i];
                any = 1;
            }
        /* y's first line is on this page: it fits, or it is taller than a page and starts it, with no page
         * break before it */
        if ((y + h <= limit || (y < limit && y <= start)) && (!any || end > y)) break;
        if (!any) {
            /* not inside a line, a row or a frame: the keeps across the limit, and the ones they overlap */
            do {
                grown = 0;
                for (i = 0; i <= doc->keep_count; ++i) {   /* and the paragraph's first line, to come */
                    ol_span line;
                    const ol_span *k = &line;
                    line.top = y;
                    line.bottom = y + h;
                    if (i < doc->keep_count) k = &doc->keeps[i];
                    if (k->top < hi && k->bottom > lo && (k->top < lo || k->bottom > hi)) {
                        if (k->top < lo) lo = k->top;
                        if (k->bottom > hi) hi = k->bottom;
                        grown = 1;
                    }
                }
            } while (grown);
            if (lo < limit && lo > start) end = lo;
        }
        /* the next page starts at the next content */
        for (i = 0; i <= doc->keep_count; ++i) {
            ol_span line;
            const ol_span *k = &line;
            line.top = y;
            line.bottom = y + h;
            if (i < doc->keep_count) k = &doc->keeps[i];
            if (k->top <= end && k->bottom > end) {
                next = end;
                break;
            }
            if (k->top > end && (next < 0 || k->top < next)) next = k->top;
        }
        if (next < 0) next = end;
        if (next <= start) next = limit;     /* never stand still */
        if (doc->live_count == doc->live_capacity) {
            ol_unit *q = (ol_unit *)realloc(doc->live_starts, (size_t)doc->live_capacity * 2 * sizeof(*q));
            if (!q) return -1;
            doc->live_starts = q;
            doc->live_capacity *= 2;
        }
        doc->live_starts[doc->live_count++] = next;
        zones_follow_page(doc, doc->live_count - 1);
    }
    return doc->live_count - 1;
}

/* The page, of the first n starting at starts, that y is in. */
static int page_among(const ol_unit *starts, int n, ol_unit y)
{
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (starts[mid] <= y) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

/* 0.6: a frame's zone worked out again where its paragraph starts (its
 * content corner cx, cy; its first line, line high, as far below as the
 * last pass put it), before the paragraph's lines: they and the lines after
 * them wrap round it where it is in this pass. A frame does not go on to
 * the page after the one the last pass put it on (its text pushed onto the
 * next page would take it there, and it back). When it moved where lines
 * already laid out (above cy) might have met it, another pass is needed. */
static void zone_live(ol_document *doc, const ol_node *frame, ol_unit cx, ol_unit cy, ol_unit line)
{
    ol_anchor a;
    ol_wrap_zone z, *e = NULL;
    ol_unit h, start = 0, dy;
    size_t i;
    for (i = 0; i < doc->zone_count; ++i)
        if (doc->zones[i].node == frame) {
            e = &doc->zones[i];
            break;
        }
    h = frame->box.frame.height > 0 ? frame->box.frame.height : e ? e->h : -1;
    if (h < 0) return;                        /* its height comes when it is laid out */
    dy = e ? e->line_dy : 0;
    if (doc->paging) {
        int p = live_page(doc, cy + dy, line);
        if (p < 0) {
            doc->zones_moved = 1;             /* the end of the pass has it, or fails */
            return;
        }
        if (e && e->page + 1 == p) p = e->page;   /* not onto the next page, which would push its text back */
        start = doc->live_starts[p];
    }
    memset(&a, 0, sizeof(a));
    a.x = cx;
    a.y = cy;
    a.frame = frame->box.frame;
    a.anchor = frame->box.anchor;
    a.node = frame;
    frame_zone(doc, &a, h, start, &z);
    z.page = doc->paging ? page_among(doc->live_starts, doc->live_count, start) : 0;
    z.line_dy = dy;
    if (e) {
        /* an after zone carried from the last pass met no line before this block */
        if (!zone_same(e, &z) && ((e->y0 < cy && !e->after) || z.y0 < cy)) doc->zones_moved = 1;
        *e = z;
        return;
    }
    if (doc->zone_count == doc->zone_capacity) {
        size_t cap = doc->zone_capacity ? doc->zone_capacity * 2 : 8;
        ol_wrap_zone *q = (ol_wrap_zone *)realloc(doc->zones, cap * sizeof(*q));
        if (!q) {
            doc->zones_moved = 1;
            return;
        }
        doc->zones = q;
        doc->zone_capacity = cap;
    }
    if (z.y0 < cy) doc->zones_moved = 1;
    doc->zones[doc->zone_count++] = z;
}

/* The frames of a block (its content corner cx, cy) that the body wraps round: their zones where it starts. */
static void zones_live(ol_document *doc, const ol_node *block, ol_unit cx, ol_unit cy)
{
    const ol_node *child;
    ol_unit line = block->style.line_height > 0 ? block->style.line_height : OL_CSSPX(20);
    if (doc->wrap_off || doc->anchor_depth) return;
    for (child = block->first_child; child; child = child->next_sibling)
        if (child->style.display != OL_DISPLAY_NONE && (child->box.flags & OL_BOX_ABSOLUTE) &&
            child->box.anchor && child->box.wrap != OL_WRAP_NONE)
            zone_live(doc, child, cx, cy, line);
}

static void move_op(ol_display_op *op, ol_unit dx, ol_unit dy)
{
    shift_op_x(op, dx);
    switch (op->type) {
    case OL_OP_TEXT: op->u.text.bounds.y += dy; op->u.text.baseline += dy; break;
    case OL_OP_IMAGE: op->u.image.bounds.y += dy; break;
    case OL_OP_FILL_RECT: op->u.fill_rect.bounds.y += dy; break;
    case OL_OP_RULE: op->u.rule.y1 += dy; op->u.rule.y2 += dy; break;
    case OL_OP_SHAPE: op->u.fill_rect.bounds.y += dy; break;
    }
    if (op->clip.height) op->clip.y += dy;
}

/* Under the text (0), the text (1), over it (2). */
static int op_layer(const ol_anchor *a)
{
    if (!a) return 1;
    return (a->anchor & OL_ANCHOR_BEHIND) ? 0 : 2;
}

/* The galley's ops sorted onto their pages, each page's together: on each
 * page the anchored frames behind the text, the text, the frames in front. */
/* The anchored frame holding op i, found by halves (the frames' ops come in order); NULL when none does. */
static const ol_anchor *anchor_at(const ol_document *doc, size_t i)
{
    size_t lo = 0, hi = doc->anchor_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (doc->anchors[mid].end <= i) lo = mid + 1;
        else hi = mid;
    }
    return lo < doc->anchor_count && doc->anchors[lo].first <= i ? &doc->anchors[lo] : NULL;
}

/* An op as it goes on the first page it is placed on (its frame's page for an anchored one, moved there):
 * what sort_ops_into_pages puts in its first place. 0 when it is on no page. */
static int first_placement(const ol_document *doc, const ol_anchor *a, ol_display_op *op)
{
    int first, last, split, p;
    if (a) {
        ol_unit ox, oy;
        p = anchor_place(doc, a, 1, &ox, &oy);
        move_op(op, ox, oy);
        op->page = p;
        return 1;
    }
    op_pages(doc, op, &first, &last, &split);
    for (p = first; p <= last; ++p) {
        ol_display_op tmp = *op;
        if (place_op(doc, &tmp, p, split)) {
            *op = tmp;
            return 1;
        }
    }
    return 0;
}

/* 0.5: the ops sorted into their pages where they are, not copied into a second array as large: on each page
 * the frames behind first, then the page's own ops, then the frames in front, each in the galley's order, an
 * op that crosses pages on each of them. Each op's first place is worked out first and kept in its page
 * field; the ops are then moved along the chains those places make (an op saved before its place is
 * written over), and the second and later places of the few ops that cross pages, kept aside, written last.
 * A display list laid out again after each edit then needs no block but its own (ol_document_reserve):
 * on an Amiga a second one of about 2 MB, and the old one's doubling, kept leaving 3 MB of libnix's pools
 * behind each time. */
#ifdef OL_CHECK_SORT
/* For the tests (make test builds with it): 0.4's sort, into an array of its own, to hold the one in place
 * against. ol_sort_checks counts the lists compared, ol_sort_mismatches those that differed. */
int ol_sort_checks, ol_sort_mismatches;

static ol_display_op *sort_ops_copy(const ol_document *doc, const ol_display_op *src, size_t n, size_t *count)
{
    size_t i, k, total = 0, *next, *at;
    ol_display_op *ops;
    int p, layer;
    at = (size_t *)calloc((size_t)(doc->page_count ? doc->page_count : 1), sizeof(*at));
    if (!at) return NULL;
    for (i = 0, k = 0; i < n; ++i) {
        const ol_anchor *a = anchor_of(doc, i, &k);
        int first, last, split;
        if (a) {
            ol_unit ox, oy;
            ++at[anchor_place(doc, a, 1, &ox, &oy)];
            ++total;
            continue;
        }
        op_pages(doc, &src[i], &first, &last, &split);
        for (p = first; p <= last; ++p) {
            ol_display_op tmp = src[i];
            if (place_op(doc, &tmp, p, split)) { ++at[p]; ++total; }
        }
    }
    ops = (ol_display_op *)malloc((total ? total : 1) * sizeof(*ops));
    next = (size_t *)malloc((size_t)(doc->page_count ? doc->page_count : 1) * sizeof(*next));
    if (!ops || !next) { free(ops); free(next); free(at); return NULL; }
    for (p = 0, i = 0; p < doc->page_count; ++p) { next[p] = i; i += at[p]; }
    for (layer = 0; layer < 3; ++layer)
        for (i = 0, k = 0; i < n; ++i) {
            const ol_anchor *a = anchor_of(doc, i, &k);
            int first, last, split;
            if (op_layer(a) != layer) continue;
            if (a) {
                ol_unit ox, oy;
                ol_display_op tmp = src[i];
                p = anchor_place(doc, a, 1, &ox, &oy);
                move_op(&tmp, ox, oy);
                tmp.page = p;
                ops[next[p]++] = tmp;
                continue;
            }
            op_pages(doc, &src[i], &first, &last, &split);
            for (p = first; p <= last; ++p) {
                ol_display_op tmp = src[i];
                if (place_op(doc, &tmp, p, split)) ops[next[p]++] = tmp;
            }
        }
    free(next);
    free(at);
    *count = total;
    return ops;
}
#endif

static int sort_ops_into_pages(ol_document *doc)
{
    typedef struct { size_t slot; ol_display_op op; } later_place;
    size_t i, k, total = 0, extra = 0, n = doc->op_count, e = 0;
    size_t *next;
    unsigned char *moved;
    later_place *more = NULL;
    int p, layer;
#ifdef OL_CHECK_SORT
    size_t ref_count = 0;
    ol_display_op *src = (ol_display_op *)malloc((n ? n : 1) * sizeof(*src)), *ref;
    if (!src) return 0;
    memcpy(src, doc->ops, n * sizeof(*src));
    ref = sort_ops_copy(doc, src, n, &ref_count);
    free(src);
    if (!ref) return 0;
#endif

    for (i = 0, k = 0; i < n; ++i) {
        const ol_anchor *a = anchor_of(doc, i, &k);
        int first, last, split, placed = 0;
        if (a) {
            ol_unit ox, oy;
            ++doc->page_ops[anchor_place(doc, a, 1, &ox, &oy)];
            ++total;
            continue;
        }
        op_pages(doc, &doc->ops[i], &first, &last, &split);
        for (p = first; p <= last; ++p) {
            ol_display_op tmp = doc->ops[i];
            if (place_op(doc, &tmp, p, split)) {
                ++doc->page_ops[p];
                ++total;
                if (placed++) ++extra;
            }
        }
    }
    if (!ol_document_reserve(doc, total > n ? total : n)) return 0;
    next = (size_t *)malloc((size_t)(doc->page_count ? doc->page_count : 1) * sizeof(*next));
    moved = (unsigned char *)calloc(n / 8 + 1, 1);
    more = extra ? (later_place *)malloc(extra * sizeof(*more)) : NULL;
    if (!next || !moved || (extra && !more)) {
        free(next);
        free(moved);
        free(more);
        return 0;
    }
    for (p = 0, i = 0; p < doc->page_count; ++p) {
        doc->page_first[p] = i;
        next[p] = i;
        i += doc->page_ops[p];
    }
    /* each op's first place, in its page field; its later places aside */
    for (layer = 0; layer < 3; ++layer)
        for (i = 0, k = 0; i < n; ++i) {
            const ol_anchor *a = anchor_of(doc, i, &k);
            int first, last, split, placed = 0;
            size_t slot = 0;
            if (op_layer(a) != layer) continue;
            if (a) {
                ol_unit ox, oy;
                doc->ops[i].page = (int32_t)next[anchor_place(doc, a, 1, &ox, &oy)]++;
                continue;
            }
            op_pages(doc, &doc->ops[i], &first, &last, &split);
            for (p = first; p <= last; ++p) {
                ol_display_op tmp = doc->ops[i];
                if (!place_op(doc, &tmp, p, split)) continue;
                if (!placed++) {
                    slot = next[p]++;
                } else {
                    more[e].slot = next[p]++;
                    more[e].op = tmp;
                    ++e;
                }
            }
            doc->ops[i].page = placed ? (int32_t)slot : -1;
        }
    /* along the chains: an op into its place, the one there saved first and carried on to its own */
    for (i = 0; i < n; ++i) {
        ol_display_op carry;
        size_t j;
        if ((moved[i / 8] & (1u << (i % 8))) || doc->ops[i].page < 0) continue;
        carry = doc->ops[i];
        j = (size_t)carry.page;
        moved[i / 8] |= (unsigned char)(1u << (i % 8));
        first_placement(doc, anchor_at(doc, i), &carry);
        for (;;) {
            if (j < n && !(moved[j / 8] & (1u << (j % 8))) && doc->ops[j].page >= 0) {
                ol_display_op held = doc->ops[j];
                size_t to = (size_t)held.page;
                moved[j / 8] |= (unsigned char)(1u << (j % 8));
                doc->ops[j] = carry;
                carry = held;
                first_placement(doc, anchor_at(doc, j), &carry);
                j = to;
            } else {                                 /* a place no op still waits in: written, and done with */
                doc->ops[j] = carry;
                if (j < n) moved[j / 8] |= (unsigned char)(1u << (j % 8));
                break;
            }
        }
    }
    for (i = 0; i < e; ++i) doc->ops[more[i].slot] = more[i].op;
    free(more);
    free(moved);
    free(next);
    doc->op_count = total;
#ifdef OL_CHECK_SORT
    ++ol_sort_checks;
    if (ref_count != total || memcmp(ref, doc->ops, total * sizeof(*ref))) ++ol_sort_mismatches;
    free(ref);
#endif
    return 1;
}

/* 0.9: the header rows' ops copied for each page a table goes on to, in
 * that page's coordinates (the header at the top of its body), and where
 * among the page's ops they go: after the frames behind the text and the
 * ops that come down onto the page from above it (the table's background,
 * its side rules), before the rows the page has of its own. Worked out
 * before the ops are sorted into their pages, and put in after. */
typedef struct {
    ol_display_op *ops;       /* page by page */
    size_t count;
    size_t *count_on;         /* each page's */
    size_t *at;               /* where on the page they go, from its first op */
} ol_header_copies;

static void header_copies_free(ol_header_copies *hc)
{
    free(hc->ops);
    free(hc->count_on);
    free(hc->at);
}

static int header_copies(ol_document *doc, ol_header_copies *hc)
{
    size_t i, k, n = 0, upto = 0;
    int p;
    memset(hc, 0, sizeof(*hc));
    for (p = 0; p < doc->page_count; ++p) {
        const ol_repeat *r;
        if (doc->page_repeat[p] < 0) continue;
        r = &doc->repeats[doc->page_repeat[p]];
        for (i = r->first; i < r->end_op; ++i)
            if (!anchor_at(doc, i)) ++n;
        if (r->end_op > upto) upto = r->end_op;
    }
    if (!n) return 1;
    hc->ops = (ol_display_op *)malloc(n * sizeof(*hc->ops));
    hc->count_on = (size_t *)calloc((size_t)doc->page_count, sizeof(*hc->count_on));
    hc->at = (size_t *)calloc((size_t)doc->page_count, sizeof(*hc->at));
    if (!hc->ops || !hc->count_on || !hc->at) {
        header_copies_free(hc);
        return 0;
    }
    for (k = 0; k < doc->anchor_count; ++k) {
        const ol_anchor *a = &doc->anchors[k];
        ol_unit ox, oy;
        if (op_layer(a) == 0) hc->at[anchor_place(doc, a, 1, &ox, &oy)] += a->end - a->first;
    }
    for (i = 0, k = 0; i < upto && i < doc->op_count; ++i) {
        int first, last, split, q;
        if (anchor_of(doc, i, &k)) continue;
        op_pages(doc, &doc->ops[i], &first, &last, &split);
        for (q = first; q <= last; ++q) {
            ol_display_op tmp;
            if (doc->page_repeat[q] < 0 || i >= doc->repeats[doc->page_repeat[q]].end_op) continue;
            tmp = doc->ops[i];
            if (place_op(doc, &tmp, q, split)) ++hc->at[q];
        }
    }
    for (p = 0; p < doc->page_count; ++p) {
        const ol_repeat *r;
        if (doc->page_repeat[p] < 0) continue;
        r = &doc->repeats[doc->page_repeat[p]];
        for (i = r->first; i < r->end_op; ++i) {
            ol_display_op *c;
            if (anchor_at(doc, i)) continue;
            c = &hc->ops[hc->count++];
            *c = doc->ops[i];
            move_op(c, doc->page.margin_left, doc->page.margin_top - r->top);
            c->page = p;
            ++hc->count_on[p];
        }
    }
    return 1;
}

/* n ops from src on to dst, which is not before it: the last first */
static void ops_along(ol_display_op *ops, size_t dst, size_t src, size_t n)
{
    while (n--) ops[dst + n] = ops[src + n];
}

/* 0.9: the copies put in among their pages' ops, the pages moved along to
 * make room, the last page first. */
static int header_copies_insert(ol_document *doc, const ol_header_copies *hc)
{
    size_t from = hc->count;
    int p;
    if (!hc->count) return 1;
    if (!ol_document_reserve(doc, doc->op_count + hc->count)) return 0;
    for (p = doc->page_count - 1; p >= 0; --p) {
        size_t src = doc->page_first[p], cnt = doc->page_ops[p], ins = hc->count_on[p], at, dst;
        at = hc->at[p] < cnt ? hc->at[p] : cnt;
        from -= ins;                        /* the copies on the pages before: as far as this one moves */
        dst = src + from;
        ops_along(doc->ops, dst + at + ins, src + at, cnt - at);
        ops_along(doc->ops, dst, src, at);
        if (ins) memcpy(&doc->ops[dst + at], &hc->ops[from], ins * sizeof(*doc->ops));
        doc->page_first[p] = dst;
        doc->page_ops[p] = cnt + ins;
    }
    doc->op_count += hc->count;
    return 1;
}

/* 0.6: after a pass, its frames' zones, where it put them, on its own
 * pages: the zones the next pass's lines wrap round. 0 when they are the
 * zones this pass's lines wrapped round (none moved where lines had
 * already met it, and its pages start where they were taken to): the pass
 * stands. 1 when they differ: they become the
 * document's, for another pass. -1 when there is no memory for them. */
#ifdef OL_CHECK_SORT
int ol_wrap_layouts, ol_wrap_passes, ol_wrap_unsettled;
#endif
static int wrap_zones(ol_document *doc)
{
    size_t k, i, n = 0, live = 0;
    ol_wrap_zone *swap;
    int same = !doc->zones_moved;
    for (k = 0; k < doc->anchor_count; ++k) {
        ol_anchor *a = &doc->anchors[k];
        ol_wrap_zone z;
        ol_unit start = 0;
        if (!a->node || a->node->box.wrap == OL_WRAP_NONE) continue;
        for (i = 0; i < doc->zone_count && doc->zones[i].node != a->node; ++i) {}
        if (doc->paging) {
            /* the page its first line is on, or the one it had, when that is earlier */
            int p = page_holding(doc, a->line_y);
            if (i < doc->zone_count && doc->zones[i].page + 1 == p) p = doc->zones[i].page;
            a->page = p;
            start = doc->page_start[p];
        }
        frame_zone(doc, a, a->node->bounds.height, start, &z);
        z.page = a->page >= 0 ? a->page : 0;
        z.line_dy = a->line_y - a->y;
        if (n == doc->next_capacity) {
            size_t cap = doc->next_capacity ? doc->next_capacity * 2 : 8;
            ol_wrap_zone *q = (ol_wrap_zone *)realloc(doc->next_zones, cap * sizeof(*q));
            if (!q) return -1;
            doc->next_zones = q;
            doc->next_capacity = cap;
        }
        doc->next_zones[n++] = z;
        if (same && (i == doc->zone_count || !zone_same(&doc->zones[i], &z))) same = 0;
    }
    live = doc->zone_count;
    if (same && live == n) return 0;
    swap = doc->zones;
    doc->zones = doc->next_zones;
    doc->next_zones = swap;
    k = doc->zone_capacity;
    doc->zone_capacity = doc->next_capacity;
    doc->next_capacity = k;
    doc->zone_count = n;
    return 1;
}

/* Without pages (ol_layout): the anchored frames placed in the galley, the
 * ones behind first and the ones in front last. */
static int place_anchors_flat(ol_document *doc)
{
    size_t i, k, n = 0;
    ol_display_op *ops;
    int layer;
    if (!doc->anchor_count) return 1;
    ops = (ol_display_op *)malloc((doc->op_count ? doc->op_count : 1) * sizeof(*ops));
    if (!ops) return 0;
    for (layer = 0; layer < 3; ++layer)
        for (i = 0, k = 0; i < doc->op_count; ++i) {
            const ol_anchor *a = anchor_of(doc, i, &k);
            if (op_layer(a) != layer) continue;
            ops[n] = doc->ops[i];
            if (a) {
                ol_unit ox, oy;
                anchor_place(doc, a, 0, &ox, &oy);
                move_op(&ops[n], ox, oy);
            }
            ++n;
        }
    free(doc->ops);
    doc->ops = ops;
    doc->op_capacity = doc->op_count ? doc->op_count : 1;
    return 1;
}

int ol_layout(ol_document *doc, ol_unit viewport_width,
              ol_measure_text_fn measure, void *measure_userdata)
{
    int pass, changed;
    if (!doc || viewport_width <= 0) return 0;
    clear_column_regions(doc);
    if (doc->css && !ol_css_restyle(doc, viewport_width)) return 0;   /* GFCSS: its sheets first */
    doc->zone_count = 0;
    doc->paging = 0;
    for (pass = 1;; ++pass) {               /* 0.6: again while the frames to wrap round move */
        reset_layout(doc);
        doc->viewport_width = viewport_width;
        if (!layout_galley(doc, viewport_width, measure, measure_userdata)) return 0;
        if ((changed = wrap_zones(doc)) < 0) return 0;
        if (!changed || pass == OL_WRAP_PASSES) break;
    }
#ifdef OL_CHECK_SORT
    if (doc->zone_count) { ++ol_wrap_layouts; ol_wrap_passes += pass; if (changed) ++ol_wrap_unsettled; }
#endif
    if (!place_anchors_flat(doc)) return 0;
    if (!add_page(doc, 0, doc->content_height)) return 0;
    doc->page_ops[0] = doc->op_count;
    ++doc->generation;
    return 1;
}

int ol_layout_pages(ol_document *doc, const ol_page *page,
                    ol_measure_text_fn measure, void *measure_userdata)
{
    ol_unit body_w, body_h;
    int pass, changed, ok;
    ol_header_copies copies;
    if (!doc || !page) return 0;
    clear_column_regions(doc);
    body_w = page->width - page->margin_left - page->margin_right;
    body_h = page->height - page->margin_top - page->margin_bottom;
    if (body_w <= 0 || body_h <= 0) return 0;
    if (doc->css && !ol_css_restyle(doc, body_w)) return 0;           /* GFCSS: its sheets first */
    /* 0.6: laid out again, round the frames to wrap round where the last pass put them, until they stay */
    doc->zone_count = 0;
    doc->paging = 1;
    doc->page = *page;
    doc->body_height = body_h;
    for (pass = 1;; ++pass) {
        reset_layout(doc);
        doc->viewport_width = body_w;
        if (!layout_galley(doc, body_w, measure, measure_userdata)) return 0;
        sort_keeps_and_breaks(doc);
        if (!cut_pages(doc, body_h)) return 0;
        if ((changed = wrap_zones(doc)) < 0) return 0;
        if (!changed || pass == OL_WRAP_PASSES) break;
    }
#ifdef OL_CHECK_SORT
    if (doc->zone_count) { ++ol_wrap_layouts; ol_wrap_passes += pass; if (changed) ++ol_wrap_unsettled; }
#endif
    if (!header_copies(doc, &copies)) return 0;
    ok = sort_ops_into_pages(doc) && header_copies_insert(doc, &copies);
    header_copies_free(&copies);
    if (!ok) return 0;
    doc->paged = 1;
    ++doc->generation;
    return 1;
}

static ol_id display_node_id(const ol_display_op *op)
{
    switch (op->type) {
    case OL_OP_TEXT: return op->u.text.node_id;
    case OL_OP_IMAGE: return op->u.image.node_id;
    case OL_OP_FILL_RECT: return op->u.fill_rect.node_id;
    case OL_OP_RULE: return op->u.rule.node_id;
    case OL_OP_SHAPE: return op->u.fill_rect.node_id;
    default: return 0;
    }
}

static const ol_node *anchored_ancestor(const ol_node *node)
{
    for (; node; node = node->parent)
        if ((node->box.flags & OL_BOX_ABSOLUTE) && node->box.anchor) return node;
    return NULL;
}

static const ol_anchor *anchor_for_frame(const ol_document *doc, const ol_node *frame)
{
    size_t i;
    for (i = 0; i < doc->anchor_count; ++i)
        if (doc->anchors[i].node == frame) return &doc->anchors[i];
    return NULL;
}

int ol_layout_pages_columns(ol_document *doc, const ol_page *page,
                            int count, ol_unit gap,
                            ol_measure_text_fn measure, void *measure_userdata)
{
    ol_page region_page;
    ol_unit body_w, body_h, column_w;
    ol_unit *starts, *ends, *shifts, *region_x, *region_y, *region_width;
    int *region_page_ids;
    int regions, pages, p, r;
    size_t i;
    if (!doc || !page || count < 1 || count > 16 || gap < 0) return 0;
    if (count == 1) return ol_layout_pages(doc, page, measure, measure_userdata);
    body_w = page->width - page->margin_left - page->margin_right;
    body_h = page->height - page->margin_top - page->margin_bottom;
    if (body_w <= gap * (count - 1) || body_h <= 0) return 0;
    column_w = (body_w - gap * (count - 1)) / count;
    memset(&region_page, 0, sizeof(region_page));
    region_page.width = column_w;
    region_page.height = body_h;
    if (!ol_layout_pages(doc, &region_page, measure, measure_userdata)) return 0;
    regions = doc->page_count;
    starts = (ol_unit *)malloc((size_t)regions * sizeof(*starts));
    ends = (ol_unit *)malloc((size_t)regions * sizeof(*ends));
    shifts = (ol_unit *)malloc((size_t)regions * sizeof(*shifts));
    region_page_ids = (int *)malloc((size_t)regions * sizeof(*region_page_ids));
    region_x = (ol_unit *)malloc((size_t)regions * sizeof(*region_x));
    region_y = (ol_unit *)calloc((size_t)regions, sizeof(*region_y));
    region_width = (ol_unit *)malloc((size_t)regions * sizeof(*region_width));
    if (!starts || !ends || !shifts || !region_page_ids || !region_x || !region_y || !region_width) {
        free(starts); free(ends); free(shifts);
        free(region_page_ids); free(region_x); free(region_y); free(region_width);
        return 0;
    }
    memcpy(starts, doc->page_start, (size_t)regions * sizeof(*starts));
    memcpy(ends, doc->page_end, (size_t)regions * sizeof(*ends));
    memcpy(shifts, doc->page_shift, (size_t)regions * sizeof(*shifts));
    for (r = 0; r < regions; ++r) {
        int column = r % count;
        region_page_ids[r] = r / count;
        region_x[r] = page->margin_left + column * (column_w + gap);
        region_width[r] = column_w;
    }

    /* The virtual pages are already contiguous in display order. Relabel
     * groups of count and move each narrow page into its physical column. */
    for (i = 0; i < doc->op_count; ++i) {
        ol_display_op *op = &doc->ops[i];
        int region = op->page;
        ol_unit dx, dy;
        const ol_node *frame;
        const ol_anchor *anchor;
        if (region < 0 || region >= regions) continue;
        dx = region_x[region];
        dy = page->margin_top;
        frame = anchored_ancestor(node_for_id(doc, display_node_id(op)));
        anchor = frame ? anchor_for_frame(doc, frame) : NULL;
        if (anchor) {
            if (OL_ANCHOR_X_OF(anchor->anchor) == OL_ANCHOR_PAGE) dx = 0;
            else if (OL_ANCHOR_X_OF(anchor->anchor) == OL_ANCHOR_MARGIN) dx = page->margin_left;
            if (OL_ANCHOR_Y_OF(anchor->anchor) == OL_ANCHOR_PAGE) dy = 0;
        }
        move_op(op, dx, dy);
        op->page = region / count;
    }
    pages = (regions + count - 1) / count;
    for (p = 0; p < pages; ++p) {
        size_t first = 0, n = 0;
        int last = (p + 1) * count;
        if (last > regions) last = regions;
        for (r = p * count; r < last; ++r) {
            if (!n) first = doc->page_first[r];
            n += doc->page_ops[r];
        }
        doc->page_first[p] = first;
        doc->page_ops[p] = n;
    }
    doc->page_count = pages;
    doc->page = *page;
    doc->body_height = body_h;
    doc->column_count = count;
    doc->column_gap = gap;
    doc->column_width = column_w;
    doc->region_count = regions;
    doc->region_start = starts;
    doc->region_end = ends;
    doc->region_shift = shifts;
    doc->region_page = region_page_ids;
    doc->region_x = region_x;
    doc->region_y = region_y;
    doc->region_width = region_width;
    return 1;
}

static int section_specs_valid(const ol_document *doc, const ol_page *page,
                               const ol_page_section *sections, int count)
{
    const ol_node *child;
    ol_unit body_width;
    int found = 0, i;
    if (!doc || !page || !sections || count < 1) return 0;
    body_width = page->width - page->margin_left - page->margin_right;
    if (body_width <= 0 || page->height - page->margin_top - page->margin_bottom <= 0) return 0;
    for (i = 0; i < count; ++i) {
        if (sections[i].columns < 1 || sections[i].columns > 16 || sections[i].gap < 0 ||
            body_width <= sections[i].gap * (sections[i].columns - 1) ||
            sections[i].break_before < OL_SECTION_CONTINUOUS ||
            sections[i].break_before > OL_SECTION_EVEN_PAGE)
            return 0;
    }
    for (child = doc->root->first_child; child; child = child->next_sibling) {
        if (child->style.display == OL_DISPLAY_NONE || is_frame(child)) continue;
        if (found < count && child->id == sections[found].first_node) ++found;
        else if (!found) return 0; /* the first section starts with the first flow node */
    }
    return found == count;
}

int ol_layout_pages_sections(ol_document *doc, const ol_page *page,
                             const ol_page_section *sections, int section_count,
                             ol_measure_text_fn measure, void *measure_userdata)
{
    ol_page virtual_page;
    ol_unit body_width, body_height;
    ol_unit *section_starts = NULL, *starts = NULL, *ends = NULL, *shifts = NULL;
    ol_unit *region_x = NULL, *region_y = NULL, *region_width = NULL;
    ol_unit *section_y = NULL;
    int *first_region = NULL, *region_page_ids = NULL, *section_page = NULL;
    size_t *virtual_ops = NULL, *physical_first = NULL, *physical_ops = NULL;
    int pass, changed, i, r, virtual_regions, physical_pages = 0, ok = 0;
    ol_header_copies copies;

    if (!section_specs_valid(doc, page, sections, section_count)) return 0;
    clear_column_regions(doc);
    body_width = page->width - page->margin_left - page->margin_right;
    body_height = page->height - page->margin_top - page->margin_bottom;
    section_starts = (ol_unit *)malloc((size_t)section_count * sizeof(*section_starts));
    first_region = (int *)malloc((size_t)(section_count + 1) * sizeof(*first_region));
    section_page = (int *)malloc((size_t)section_count * sizeof(*section_page));
    section_y = (ol_unit *)malloc((size_t)section_count * sizeof(*section_y));
    if (!section_starts || !first_region || !section_page || !section_y) goto done;
    if (doc->css && !ol_css_restyle(doc, body_width)) goto done;
    memset(&virtual_page, 0, sizeof(virtual_page));
    virtual_page.width = body_width;
    virtual_page.height = body_height;
    doc->zone_count = 0;
    doc->paging = 1;
    doc->page = virtual_page;
    doc->body_height = body_height;
    for (pass = 1;; ++pass) {
        reset_layout(doc);
        doc->viewport_width = body_width;
        if (!layout_galley_sections(doc, body_width, sections, section_count, section_starts,
                                    measure, measure_userdata))
            goto done;
        sort_keeps_and_breaks(doc);
        physical_pages = 0;
        for (i = 0; i < section_count; ++i) {
            ol_unit end = i + 1 < section_count ? section_starts[i + 1] : doc->content_height;
            ol_unit first_height = body_height;
            int base = physical_pages;
            first_region[i] = doc->page_count;
            if (i > 0 && sections[i].break_before == OL_SECTION_CONTINUOUS) {
                int prev_regions = first_region[i] - first_region[i - 1];
                int prev_columns = sections[i - 1].columns;
                int last_in_group = (prev_regions - 1) % prev_columns + 1;
                int last_region = first_region[i] - 1;
                int group_start = last_region - last_in_group + 1;
                ol_unit last_y, used = 0;
                if (prev_regions < 1) goto done;
                last_y = group_start - first_region[i - 1] < prev_columns ? section_y[i - 1] : 0;
                for (r = group_start; r <= last_region; ++r) {
                    ol_unit occupied = doc->page_shift[r] + doc->page_end[r] - doc->page_start[r];
                    if (occupied > used) used = occupied;
                }
                section_y[i] = last_y + used;
                if (section_y[i] < body_height) {
                    base = physical_pages - 1;
                    first_height = body_height - section_y[i];
                } else {
                    section_y[i] = 0;
                }
            } else {
                section_y[i] = 0;
                if (i > 0 && sections[i].break_before == OL_SECTION_ODD_PAGE &&
                    ((base + 1) & 1) == 0)
                    ++base;
                if (i > 0 && sections[i].break_before == OL_SECTION_EVEN_PAGE &&
                    ((base + 1) & 1) != 0)
                    ++base;
            }
            section_page[i] = base;
            if (!cut_page_range(doc, section_starts[i], end, body_height, first_height,
                                sections[i].columns,
                                i + 1 < section_count &&
                                sections[i + 1].break_before == OL_SECTION_CONTINUOUS))
                goto done;
            {
                int regions = doc->page_count - first_region[i];
                int after = base + (regions + sections[i].columns - 1) / sections[i].columns;
                if (after > physical_pages) physical_pages = after;
            }
        }
        first_region[section_count] = doc->page_count;
        if ((changed = wrap_zones(doc)) < 0) goto done;
        if (!changed || pass == OL_WRAP_PASSES) break;
    }
#ifdef OL_CHECK_SORT
    if (doc->zone_count) { ++ol_wrap_layouts; ol_wrap_passes += pass; if (changed) ++ol_wrap_unsettled; }
#endif
    if (!header_copies(doc, &copies)) goto done;
    ok = sort_ops_into_pages(doc) && header_copies_insert(doc, &copies);
    header_copies_free(&copies);
    if (!ok) { ok = 0; goto done; }

    virtual_regions = doc->page_count;
    starts = (ol_unit *)malloc((size_t)virtual_regions * sizeof(*starts));
    ends = (ol_unit *)malloc((size_t)virtual_regions * sizeof(*ends));
    shifts = (ol_unit *)malloc((size_t)virtual_regions * sizeof(*shifts));
    region_page_ids = (int *)malloc((size_t)virtual_regions * sizeof(*region_page_ids));
    region_x = (ol_unit *)malloc((size_t)virtual_regions * sizeof(*region_x));
    region_y = (ol_unit *)malloc((size_t)virtual_regions * sizeof(*region_y));
    region_width = (ol_unit *)malloc((size_t)virtual_regions * sizeof(*region_width));
    virtual_ops = (size_t *)malloc((size_t)virtual_regions * sizeof(*virtual_ops));
    if (!starts || !ends || !shifts || !region_page_ids || !region_x || !region_y || !region_width ||
        !virtual_ops) {
        ok = 0; goto done;
    }
    memcpy(starts, doc->page_start, (size_t)virtual_regions * sizeof(*starts));
    memcpy(ends, doc->page_end, (size_t)virtual_regions * sizeof(*ends));
    memcpy(shifts, doc->page_shift, (size_t)virtual_regions * sizeof(*shifts));
    memcpy(virtual_ops, doc->page_ops, (size_t)virtual_regions * sizeof(*virtual_ops));

    for (i = 0; i < section_count; ++i) {
        int columns = sections[i].columns;
        int regions = first_region[i + 1] - first_region[i];
        ol_unit width = (body_width - sections[i].gap * (columns - 1)) / columns;
        for (r = 0; r < regions; ++r) {
            int region = first_region[i] + r;
            int column = r % columns;
            region_page_ids[region] = section_page[i] + r / columns;
            region_x[region] = page->margin_left + column * (width + sections[i].gap);
            region_y[region] = r < columns ? section_y[i] : 0;
            region_width[region] = width;
        }
    }
    while (doc->page_count < physical_pages)
        if (!add_page(doc, 0, 0)) { ok = 0; goto done; }
    physical_first = (size_t *)calloc((size_t)physical_pages, sizeof(*physical_first));
    physical_ops = (size_t *)calloc((size_t)physical_pages, sizeof(*physical_ops));
    if (!physical_first || !physical_ops) { ok = 0; goto done; }
    for (r = 0; r < virtual_regions; ++r)
        physical_ops[region_page_ids[r]] += virtual_ops[r];
    for (i = 1; i < physical_pages; ++i)
        physical_first[i] = physical_first[i - 1] + physical_ops[i - 1];

    for (i = 0; i < (int)doc->op_count; ++i) {
        ol_display_op *op = &doc->ops[i];
        int region = op->page;
        ol_unit dx, dy;
        const ol_node *frame;
        const ol_anchor *anchor;
        if (region < 0 || region >= virtual_regions) continue;
        dx = region_x[region];
        dy = page->margin_top + region_y[region];
        frame = anchored_ancestor(node_for_id(doc, display_node_id(op)));
        anchor = frame ? anchor_for_frame(doc, frame) : NULL;
        if (anchor) {
            if (OL_ANCHOR_X_OF(anchor->anchor) == OL_ANCHOR_PAGE) dx = 0;
            else if (OL_ANCHOR_X_OF(anchor->anchor) == OL_ANCHOR_MARGIN) dx = page->margin_left;
            if (OL_ANCHOR_Y_OF(anchor->anchor) == OL_ANCHOR_PAGE) dy = 0;
        }
        move_op(op, dx, dy);
        op->page = region_page_ids[region];
    }
    for (i = 0; i < physical_pages; ++i) {
        doc->page_first[i] = physical_first[i];
        doc->page_ops[i] = physical_ops[i];
    }
    doc->page_count = physical_pages;
    doc->page = *page;
    doc->body_height = body_height;
    doc->column_count = 0;
    doc->column_gap = doc->column_width = 0;
    doc->region_count = virtual_regions;
    doc->region_start = starts; starts = NULL;
    doc->region_end = ends; ends = NULL;
    doc->region_shift = shifts; shifts = NULL;
    doc->region_page = region_page_ids; region_page_ids = NULL;
    doc->region_x = region_x; region_x = NULL;
    doc->region_y = region_y; region_y = NULL;
    doc->region_width = region_width; region_width = NULL;
    doc->paged = 1;
    ++doc->generation;
    ok = 1;

done:
    free(section_starts);
    free(first_region);
    free(section_page);
    free(section_y);
    free(starts); free(ends); free(shifts);
    free(region_page_ids); free(region_x); free(region_y); free(region_width);
    free(virtual_ops);
    free(physical_first); free(physical_ops);
    return ok;
}

int ol_page_count(const ol_document *doc)
{
    return doc ? doc->page_count : 0;
}

int ol_display_page_range(const ol_document *doc, int page,
                          size_t *first, size_t *count)
{
    if (!doc || page < 0 || page >= doc->page_count) return 0;
    if (first) *first = doc->page_first[page];
    if (count) *count = doc->page_ops[page];
    return 1;
}

static int op_box(const ol_display_op *op, ol_rect *box)
{
    if (!op || !box) return 0;
    switch (op->type) {
    case OL_OP_TEXT: *box = op->u.text.bounds; return 1;
    case OL_OP_FILL_RECT: *box = op->u.fill_rect.bounds; return 1;
    case OL_OP_IMAGE: *box = op->u.image.bounds; return 1;
    case OL_OP_SHAPE: *box = op->u.fill_rect.bounds; return 1;
    case OL_OP_RULE:
        box->x = op->u.rule.x1 < op->u.rule.x2 ? op->u.rule.x1 : op->u.rule.x2;
        box->y = op->u.rule.y1 < op->u.rule.y2 ? op->u.rule.y1 : op->u.rule.y2;
        box->width = (op->u.rule.x1 < op->u.rule.x2 ? op->u.rule.x2 - op->u.rule.x1
                                                   : op->u.rule.x1 - op->u.rule.x2) + op->u.rule.thickness;
        box->height = (op->u.rule.y1 < op->u.rule.y2 ? op->u.rule.y2 - op->u.rule.y1
                                                    : op->u.rule.y1 - op->u.rule.y2) + op->u.rule.thickness;
        return 1;
    default: return 0;
    }
}

int ol_node_page_box(const ol_document *doc, ol_id node_id,
                     int *page, ol_rect *box)
{
    const ol_node *target;
    size_t i;
    if (!doc || !box || !(target = node_for_id(doc, node_id))) return 0;
    if ((target->box.flags & OL_BOX_ABSOLUTE) && target->box.anchor &&
        ol_frame_place(doc, target, page, box))
        return 1;
    for (i = 0; i < doc->op_count; ++i) {
        const ol_display_op *op = &doc->ops[i];
        const ol_node *n = node_for_id(doc, display_node_id(op));
        for (; n && n != target; n = n->parent)
            if ((n->box.flags & OL_BOX_ABSOLUTE) && n->box.anchor) {
                n = NULL;          /* an anchored descendant is not the paragraph's own box */
                break;
            }
        if (n == target && op_box(op, box)) {
            if (page) *page = op->page;
            return 1;
        }
    }
    if (target->style.display == OL_DISPLAY_BLOCK && !rect_empty(target->bounds)) {
        ol_text_position pos;
        pos.node_id = node_id;
        pos.offset = 0;
        return ol_caret_box(doc, &pos, NULL, NULL, page, box);
    }
    return 0;
}

const ol_node *ol_hit_test_page(const ol_document *doc, int page,
                                ol_unit x, ol_unit y)
{
    ol_unit gy;
    if (!doc || page < 0 || page >= doc->page_count) return NULL;
    if (!doc->paged) return ol_hit_test(doc, x, y);
    if (doc->region_count > 0 && doc->region_page && doc->region_x && doc->region_width) {
        int region;
        for (region = 0; region < doc->region_count; ++region)
            if (doc->region_page[region] == page && x >= doc->region_x[region] &&
                x < doc->region_x[region] + doc->region_width[region])
                break;
        if (region >= doc->region_count) return NULL; /* outside a region, including a column gap */
        gy = y - doc->page.margin_top - doc->region_y[region] -
             doc->region_shift[region] + doc->region_start[region];
        if (gy < doc->region_start[region] || gy >= doc->region_end[region]) return NULL;
        return ol_hit_test(doc, x - doc->region_x[region], gy);
    }
    gy = y - doc->page.margin_top - doc->page_shift[page] + doc->page_start[page];
    if (gy < doc->page_start[page] || gy >= doc->page_end[page]) return NULL;
    return ol_hit_test(doc, x - doc->page.margin_left, gy);
}

static ol_unit distance(ol_unit v, ol_unit lo, ol_unit len)
{
    if (v < lo) return lo - v;
    if (v >= lo + len) return v - (lo + len) + 1;
    return 0;
}

int ol_hit_text(const ol_document *doc, int page, ol_unit x, ol_unit y,
                ol_measure_text_fn measure, void *measure_userdata,
                ol_text_position *pos)
{
    size_t first, count, i, best = (size_t)-1, k, best_k = 0;
    ol_unit best_dy = 0, best_dx = 0, best_d;
    const ol_display_op *op;
    const ol_node *node;
    const char *text;

    if (!pos || !ol_display_page_range(doc, page, &first, &count)) return 0;
    for (i = first; i < first + count; ++i) {
        ol_unit dy, dx;
        op = &doc->ops[i];
        if (op->type != OL_OP_TEXT || in_anchored_frame(node_for_id(doc, op->u.text.node_id))) continue;
        dy = distance(y, op->u.text.bounds.y, op->u.text.bounds.height);
        dx = distance(x, op->u.text.bounds.x, op->u.text.bounds.width);
        if (best == (size_t)-1 || dy < best_dy || (dy == best_dy && dx < best_dx)) {
            best = i;
            best_dy = dy;
            best_dx = dx;
        }
    }
    if (best == (size_t)-1) return 0;
    op = &doc->ops[best];
    node = node_for_id(doc, op->u.text.node_id);
    if (!node || !node->text) return 0;
    text = node->text + op->u.text.text_offset;

    /* the character boundary nearest x; widths only grow along the piece */
    best_d = x > op->u.text.bounds.x ? x - op->u.text.bounds.x : op->u.text.bounds.x - x;
    for (k = 1; k <= op->u.text.text_length; ++k) {
        ol_text_metrics m;
        ol_unit edge, d;
        if (k < op->u.text.text_length && ((unsigned char)text[k] & 0xc0u) == 0x80u) continue;
        if (op_is_tab(doc, op)) m.width = op->u.text.bounds.width;
        else if (!measure_with(measure, measure_userdata, text, k, &op->u.text.style, &m)) return 0;
        edge = op->u.text.bounds.x + m.width;
        d = x > edge ? x - edge : edge - x;
        if (d < best_d) {
            best_d = d;
            best_k = k;
        }
        if (edge >= x) break;
    }
    pos->node_id = op->u.text.node_id;
    pos->offset = op->u.text.text_offset + best_k;
    return 1;
}

int ol_caret_box(const ol_document *doc, const ol_text_position *pos,
                 ol_measure_text_fn measure, void *measure_userdata,
                 int *page, ol_rect *box)
{
    const ol_node *node;
    size_t i, found = (size_t)-1;

    if (!doc || !pos || !box) return 0;
    node = node_for_id(doc, pos->node_id);
    if (!node) return 0;
    for (i = 0; i < doc->op_count; ++i) {
        const ol_display_op *op = &doc->ops[i];
        size_t s, e;
        if (op->type != OL_OP_TEXT || op->u.text.node_id != pos->node_id) continue;
        s = op->u.text.text_offset;
        e = s + op->u.text.text_length;
        if (pos->offset >= s && pos->offset < e) {
            found = i;
            break;
        }
        if (pos->offset == e) found = i;   /* the end of a piece, unless the next has it */
    }
    if (found != (size_t)-1) {
        const ol_display_op *op = &doc->ops[found];
        ol_text_metrics m;
        size_t k = pos->offset - op->u.text.text_offset;
        m.width = 0;
        if (k && op_is_tab(doc, op))
            m.width = op->u.text.bounds.width;
        else if (k && !measure_with(measure, measure_userdata, node->text + op->u.text.text_offset,
                                    k, &op->u.text.style, &m))
            return 0;
        box->x = op->u.text.bounds.x + m.width;
        box->y = op->u.text.bounds.y;
        box->width = 0;
        box->height = op->u.text.bounds.height;
        if (page) *page = op->page;
        return 1;
    }
    /* nothing laid out for it (an empty paragraph): its content corner */
    if (node->style.display == OL_DISPLAY_BLOCK && !rect_empty(node->bounds)) {
        ol_unit gx = node->bounds.x + inset_left(node) + node->box.text_indent;
        ol_unit gy = node->bounds.y + inset_top(node);
        int p = 0;
        if (doc->paged) {
            if (doc->region_count > 0 && doc->region_page && doc->region_x && doc->region_y) {
                int region = column_region_holding(doc, gy);
                p = doc->region_page[region];
                gx += doc->region_x[region];
                gy += doc->page.margin_top + doc->region_y[region] +
                      doc->region_shift[region] - doc->region_start[region];
            } else {
                p = page_holding(doc, gy);
                gx += doc->page.margin_left;
                gy += doc->page.margin_top + doc->page_shift[p] - doc->page_start[p];
            }
        }
        box->x = gx;
        box->y = gy;
        box->width = 0;
        box->height = node->style.line_height > 0 ? node->style.line_height : OL_CSSPX(20);
        if (page) *page = p;
        return 1;
    }
    return 0;
}

ol_unit ol_document_content_height(const ol_document *doc)
{
    return doc ? doc->content_height : 0;
}

uint32_t ol_document_generation(const ol_document *doc)
{
    return doc ? doc->generation : 0;
}

size_t ol_display_count(const ol_document *doc)
{
    return doc ? doc->op_count : 0;
}

const ol_display_op *ol_display_get(const ol_document *doc, size_t index)
{
    if (!doc || index >= doc->op_count) return NULL;
    return &doc->ops[index];
}

const char *ol_role_name(ol_role role)
{
    switch (role) {
    case OL_ROLE_DOCUMENT: return "document";
    case OL_ROLE_BLOCK: return "block";
    case OL_ROLE_PARAGRAPH: return "paragraph";
    case OL_ROLE_HEADING: return "heading";
    case OL_ROLE_TEXT: return "text";
    case OL_ROLE_LINK: return "link";
    case OL_ROLE_IMAGE: return "image";
    case OL_ROLE_LIST: return "list";
    case OL_ROLE_LIST_ITEM: return "listitem";
    case OL_ROLE_TABLE: return "table";
    case OL_ROLE_ROW: return "row";
    case OL_ROLE_CELL: return "cell";
    case OL_ROLE_BUTTON: return "button";
    case OL_ROLE_INPUT: return "input";
    default: return "unknown";
    }
}

/* ---- Drawing at the display's resolution ------------------------------------ */

#define OL_SCALE_SHIFT 24

void ol_scale_set(ol_scale *scale, int32_t num, int32_t den)
{
    if (!scale) return;
    if (num <= 0 || den <= 0) {
        num = 1;
        den = OL_UNITS_PER_POINT;
    }
    scale->num = num;
    scale->den = den;
    scale->mul = (uint32_t)((((uint64_t)num << OL_SCALE_SHIFT) + (uint64_t)den / 2) / (uint64_t)den);
}

static int32_t gcd32(int32_t a, int32_t b)
{
    while (b) {
        int32_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

void ol_scale_dpi(ol_scale *scale, int dpi, int zoom_percent)
{
    int32_t num = (int32_t)dpi * (int32_t)zoom_percent, den = OL_UNITS_PER_INCH * 100, g;
    if (num <= 0) num = 72 * 100;
    g = gcd32(num, den);
    ol_scale_set(scale, num / g, den / g);
}

void ol_scale_fit(ol_scale *scale, ol_unit length, int32_t pixels)
{
    int32_t g;
    if (length <= 0 || pixels <= 0) {
        ol_scale_set(scale, 0, 0);
        return;
    }
    g = gcd32(pixels, length);
    ol_scale_set(scale, pixels / g, length / g);
}

/* round(v * num / den): one 32 x 32 -> 64 multiply (a single muls.l on a
 * 68020 to 68040) and a shift */
static int32_t scale_units(const ol_scale *scale, ol_unit v)
{
    return (int32_t)(((int64_t)v * (int64_t)(int32_t)scale->mul +
                      ((int64_t)1 << (OL_SCALE_SHIFT - 1))) >> OL_SCALE_SHIFT);
}

int32_t ol_scale_x(const ol_scale *scale, ol_unit x)
{
    return scale->origin_x + scale_units(scale, x);
}

int32_t ol_scale_y(const ol_scale *scale, ol_unit y)
{
    return scale->origin_y + scale_units(scale, y);
}

static ol_unit unscale(const ol_scale *scale, int32_t px)
{
    int64_t n = (int64_t)px * scale->den;
    return (ol_unit)(n >= 0 ? (n + scale->num / 2) / scale->num
                            : -((-n + scale->num / 2) / scale->num));
}

ol_unit ol_unscale_x(const ol_scale *scale, int32_t px)
{
    return unscale(scale, px - scale->origin_x);
}

ol_unit ol_unscale_y(const ol_scale *scale, int32_t py)
{
    return unscale(scale, py - scale->origin_y);
}

int32_t ol_scale_length(const ol_scale *scale, ol_unit length)
{
    int32_t px = scale_units(scale, length);
    return length > 0 && px < 1 ? 1 : px;
}

ol_pixel_rect ol_scale_rect(const ol_scale *scale, ol_rect r)
{
    ol_pixel_rect p;
    int32_t x2 = scale_units(scale, r.x + r.width), y2 = scale_units(scale, r.y + r.height);
    p.x = scale_units(scale, r.x);
    p.y = scale_units(scale, r.y);
    p.width = x2 - p.x;
    p.height = y2 - p.y;
    p.x += scale->origin_x;
    p.y += scale->origin_y;
    return p;
}

static int pixel_rects_cross(const ol_pixel_rect *a, const ol_pixel_rect *b)
{
    return a->x < b->x + b->width && b->x < a->x + a->width &&
           a->y < b->y + b->height && b->y < a->y + a->height;
}

size_t ol_draw_page(const ol_document *doc, int page, const ol_scale *scale,
                    const ol_pixel_rect *clip, ol_draw_fn draw, void *userdata)
{
    size_t first, count, i, drawn = 0;
    ol_unit top = 0, bottom = 0;
    int culled = 0;

    if (!scale || !draw || !ol_display_page_range(doc, page, &first, &count)) return 0;
    if (clip) {
        /* the clip's band in units, a pixel wider each way: ops outside it
         * are passed over without being converted */
        top = ol_unscale_y(scale, clip->y - 1);
        bottom = ol_unscale_y(scale, clip->y + clip->height + 1);
        culled = 1;
    }
    for (i = first; i < first + count; ++i) {
        const ol_display_op *op = &doc->ops[i];
        ol_device_op d;
        ol_unit t, b;
        memset(&d, 0, sizeof(d));
        d.op = op;
        switch (op->type) {
        case OL_OP_TEXT:
            t = op->u.text.bounds.y;
            b = t + op->u.text.bounds.height;
            if (culled && (b < top || t > bottom)) continue;
            d.box = ol_scale_rect(scale, op->u.text.bounds);
            d.baseline = ol_scale_y(scale, op->u.text.baseline);
            d.font_pixels = ol_scale_length(scale, op->u.text.style.font_size);
            break;
        case OL_OP_FILL_RECT:
            t = op->u.fill_rect.bounds.y;
            b = t + op->u.fill_rect.bounds.height;
            if (culled && (b < top || t > bottom)) continue;
            d.box = ol_scale_rect(scale, op->u.fill_rect.bounds);
            break;
        case OL_OP_IMAGE:
            t = op->u.image.bounds.y;
            b = t + op->u.image.bounds.height;
            if (culled && (b < top || t > bottom)) continue;
            d.box = ol_scale_rect(scale, op->u.image.bounds);
            break;
        case OL_OP_SHAPE:
            t = op->u.fill_rect.bounds.y;
            b = t + op->u.fill_rect.bounds.height;
            if (culled && (b < top || t > bottom)) continue;
            d.box = ol_scale_rect(scale, op->u.fill_rect.bounds);
            if (op->graphic) {
                d.thickness = ol_scale_length(scale, op->graphic->shape.stroke_width);
            }
            break;
        case OL_OP_RULE:
            t = op->u.rule.y1 < op->u.rule.y2 ? op->u.rule.y1 : op->u.rule.y2;
            b = (op->u.rule.y1 < op->u.rule.y2 ? op->u.rule.y2 : op->u.rule.y1) + op->u.rule.thickness;
            if (culled && (b < top || t > bottom)) continue;
            d.x1 = ol_scale_x(scale, op->u.rule.x1);
            d.y1 = ol_scale_y(scale, op->u.rule.y1);
            d.x2 = ol_scale_x(scale, op->u.rule.x2);
            d.y2 = ol_scale_y(scale, op->u.rule.y2);
            d.thickness = ol_scale_length(scale, op->u.rule.thickness);
            d.box.x = d.x1 < d.x2 ? d.x1 : d.x2;
            d.box.y = d.y1 < d.y2 ? d.y1 : d.y2;
            d.box.width = (d.x1 < d.x2 ? d.x2 - d.x1 : d.x1 - d.x2) + d.thickness;
            d.box.height = (d.y1 < d.y2 ? d.y2 - d.y1 : d.y1 - d.y2) + d.thickness;
            break;
        default:
            continue;
        }
        if (op->clip.height) {
            if (rect_empty(op->clip)) continue;
            d.clip = ol_scale_rect(scale, op->clip);
            if (!pixel_rects_cross(&d.box, &d.clip)) continue;
        }
        if (clip && !pixel_rects_cross(&d.box, clip)) continue;
        draw(userdata, &d);
        ++drawn;
    }
    return drawn;
}
