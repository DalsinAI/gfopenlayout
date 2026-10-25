#include "openlayout.h"

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

    ol_style style;
    ol_rect bounds;
    uint32_t actions;
    uint32_t states;

    int image_id;
    ol_unit image_width;
    ol_unit image_height;

    ol_box_style box;
    ol_unit *columns;
    int column_count;
    ol_unit *tabs;
    unsigned char *tab_info;  /* each stop's ol_tab_align | ol_tab_leader << 4 */
    int tab_count;
};

/* A stretch of the galley a page may not end inside: a line, a table row,
 * a frame, a block kept together. */
typedef struct {
    ol_unit top;
    ol_unit bottom;
} ol_span;

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

    /* collected while laying out, for pages */
    ol_span *keeps;
    size_t keep_count;
    size_t keep_capacity;
    ol_unit *breaks;
    size_t break_count;
    size_t break_capacity;

    /* pages: one after ol_layout, page 0 being the whole galley */
    int paged;
    ol_page page;
    int page_count;
    ol_unit *page_start;      /* galley y of each page's first content */
    ol_unit *page_end;        /* and of its end */
    size_t *page_first;       /* each page's first display op */
    size_t *page_ops;         /* and how many */
    int page_capacity;
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
    free(node->text);
    free(node->name);
    free(node->value);
    free(node->href);
    free(node->columns);
    free(node->tabs);
    free(node->tab_info);
    free(node);
}

void ol_document_free(ol_document *doc)
{
    size_t i;
    if (!doc) return;
    for (i = 0; i < doc->node_count; ++i) free_node(doc->nodes[i]);
    free(doc->nodes);
    free(doc->ops);
    free(doc->keeps);
    free(doc->breaks);
    free(doc->page_start);
    free(doc->page_end);
    free(doc->page_first);
    free(doc->page_ops);
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

const ol_node *ol_hit_test(const ol_document *doc, ol_unit x, ol_unit y)
{
    const ol_node *best = NULL;
    int best_depth = -1;
    size_t i;
    if (!doc) return NULL;
    for (i = 0; i < doc->node_count; ++i) {
        const ol_node *n = doc->nodes[i];
        int d;
        if (n->style.display == OL_DISPLAY_NONE || !point_in_rect(n->bounds, x, y))
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
    }
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
    if (block) {
        flow->tabs = block->tabs;
        flow->tab_info = block->tab_info;
        flow->tab_count = block->tab_count;
    }
    flow->cursor_x = x0 + indent;
    flow->content_right = flow->cursor_x;
    flow->line_top = top;
    flow->align = align;
    flow->line_first_op = doc->op_count;
}

static int flow_newline(ol_inline_flow *flow, int last)
{
    if (!finish_line(flow, last)) return 0;
    flow->line_top += flow->line_height > 0 ? flow->line_height : OL_CSSPX(20);
    flow->cursor_x = flow->x0;
    flow->content_right = flow->x0;
    flow->line_height = 0;
    flow->ascent = 0;
    flow->descent = 0;
    flow->has_content = 0;
    flow->line_aligned_tab = 0;
    flow->line_first_op = flow->doc->op_count;
    return 1;
}

/* Ends the flow; *bottom is below its last line. */
static int flush_flow(ol_inline_flow *flow, ol_unit *bottom)
{
    if (!finish_line(flow, 1)) return 0;
    *bottom = flow->line_top + (flow->has_content ? flow->line_height : 0);
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

    /* a word wraps when it does not fit; its trailing spaces may hang */
    if (flow->has_content && word_length && !piece_fits(flow, word_width))
        if (!flow_newline(flow, 0)) return 0;

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
    to = next_tab(flow, &info);
    align = (int)(info & 15u);
    if (align == OL_TAB_LEFT) {
        if (to > flow->right && flow->has_content) {
            /* the line ends here as at a line break: not widened to the edge (a justified contents line
             * whose stop is past the edge was spread across the line before its page number) */
            if (!flow_newline(flow, 1)) return 0;
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

    if (flow->has_content && !piece_fits(flow, w))
        if (!flow_newline(flow, 0)) return 0;

    op = append_op(flow->doc, OL_OP_IMAGE);
    if (!op) return 0;
    op->u.image.node_id = node->id;
    op->u.image.bounds.x = flow->cursor_x;
    op->u.image.bounds.y = flow->line_top;
    op->u.image.bounds.width = w;
    op->u.image.bounds.height = h;
    op->u.image.image_id = node->image_id;

    if (h > flow->ascent) flow->ascent = h;
    flow->cursor_x += w;
    flow->content_right = flow->cursor_x;
    flow->has_content = 1;
    return 1;
}

static int layout_inline_node(ol_inline_flow *flow, ol_node *node)
{
    ol_node *child;
    if (!node || node->style.display == OL_DISPLAY_NONE) return 1;

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

static int add_borders(ol_document *doc, const ol_node *node)
{
    const ol_box_style *b = &node->box;
    ol_rect r = node->bounds;
    ol_unit x2 = r.x + r.width, y2 = r.y + r.height;
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

static int is_frame(const ol_node *node)
{
    return (node->box.flags & OL_BOX_ABSOLUTE) != 0;
}

static int layout_frame(ol_document *doc, ol_node *node, ol_unit origin_x, ol_unit origin_y,
                        ol_measure_text_fn measure, void *measure_userdata)
{
    ol_unit bottom;
    ol_unit w = node->box.frame.width > 0 ? node->box.frame.width : OL_CSSPX(1);
    return layout_block(doc, node, origin_x + node->box.frame.x, origin_y + node->box.frame.y,
                        w, measure, measure_userdata, &bottom);
}

/* A table's rows, as a grid: each cell at its column, every cell in a row
 * as tall as the row's tallest, its background and borders to match. */
static int layout_table(ol_document *doc, ol_node *table, ol_unit x, ol_unit y,
                        ol_unit width, ol_measure_text_fn measure,
                        void *measure_userdata, ol_unit *bottom_out)
{
    ol_unit widths[OL_MAX_COLUMNS], col_x[OL_MAX_COLUMNS + 1];
    ol_node *cells[OL_MAX_COLUMNS];
    size_t cell_first_op[OL_MAX_COLUMNS];
    int ncols = table->column_count, i;
    ol_node *row, *cell;

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

    if (ncols > 0) {
        if (table->column_count) {
            int64_t sum = 0;
            int unset = 0;
            ol_unit left;
            for (i = 0; i < ncols; ++i) {
                if (table->columns[i] > 0) sum += table->columns[i];
                else ++unset;
            }
            for (i = 0; i < ncols; ++i)
                widths[i] = table->columns[i] > 0
                    ? (sum > width ? (ol_unit)((int64_t)table->columns[i] * width / sum)
                                   : table->columns[i])
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
        size_t row_bg = (size_t)-1;
        int col = 0, ncells = 0, k;

        if (row->style.display == OL_DISPLAY_NONE) continue;
        if (row->role != OL_ROLE_ROW || ncols <= 0) {
            /* a caption or other block between the rows */
            if (!layout_block(doc, row, x, y, width, measure, measure_userdata, &y)) return 0;
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
        for (k = 0; k < ncells; ++k) {
            ol_node *c = cells[k];
            ol_unit h = row_bottom - c->style.margin_bottom - c->bounds.y;
            if (h > c->bounds.height) {
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
        y = row_bottom;
    }
    *bottom_out = y;
    return 1;
}

static int layout_block(ol_document *doc, ol_node *node,
                        ol_unit x, ol_unit y, ol_unit width,
                        ol_measure_text_fn measure, void *measure_userdata,
                        ol_unit *bottom_out)
{
    ol_unit box_x, box_y, inner_x, inner_w, current_y, content_bottom, empty_top;
    ol_unit ml = node ? node->style.margin_left : 0, mr = node ? node->style.margin_right : 0;
    ol_unit mt = node ? node->style.margin_top : 0, mb = node ? node->style.margin_bottom : 0;
    ol_unit keep_with_next = 0;
    int has_keep_with_next = 0;
    ol_node *child;
    ol_inline_flow flow;
    size_t background_index = (size_t)-1;

    if (!node || node->style.display == OL_DISPLAY_NONE) {
        *bottom_out = y;
        return 1;
    }
    if (is_frame(node)) ml = mr = mt = mb = 0;   /* a frame is where it is put */
    if ((node->box.flags & OL_BOX_PAGE_BREAK_BEFORE) && !add_break(doc, y)) return 0;

    box_x = x + ml;
    box_y = y + mt;
    inner_x = box_x + node->style.padding_left;
    inner_w = width - ml - mr - node->style.padding_left - node->style.padding_right;
    if (inner_w < OL_CSSPX(1)) inner_w = OL_CSSPX(1);

    memset(&node->bounds, 0, sizeof(node->bounds));

    if ((node->style.background >> 24) != 0) {
        ol_display_op *bg = append_op(doc, OL_OP_FILL_RECT);
        if (!bg) return 0;
        background_index = doc->op_count - 1;
        bg->u.fill_rect.node_id = node->id;
        bg->u.fill_rect.argb = node->style.background;
    }

    current_y = empty_top = box_y + node->style.padding_top;
    start_flow(&flow, doc, node, measure, measure_userdata, inner_x, inner_x + inner_w,
               current_y, node->style.text_align, node->box.text_indent);

    if (node->role == OL_ROLE_TABLE) {
        if (!layout_table(doc, node, inner_x, current_y, inner_w, measure,
                          measure_userdata, &current_y))
            return 0;
        flow.line_top = current_y;
    } else {
        if (node->text && node->text[0]) {
            if (!layout_inline_text(&flow, node)) return 0;
        }
        for (child = node->first_child; child; child = child->next_sibling) {
            if (child->style.display == OL_DISPLAY_NONE || is_frame(child)) continue;
            if (child->style.display == OL_DISPLAY_INLINE) {
                if (!layout_inline_node(&flow, child)) return 0;
            } else {
                ol_unit child_bottom;
                size_t keeps_before;
                if (!flush_flow(&flow, &current_y)) return 0;
                keeps_before = doc->keep_count;
                if (!layout_block(doc, child, inner_x, current_y, inner_w,
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
                start_flow(&flow, doc, node, measure, measure_userdata, inner_x, inner_x + inner_w,
                           current_y, node->style.text_align, 0);
            }
        }
    }

    if (!flush_flow(&flow, &content_bottom)) return 0;
    if (content_bottom < current_y) content_bottom = current_y;
    if ((node->box.flags & OL_BOX_EMPTY_LINE) && content_bottom <= empty_top) {
        content_bottom = empty_top + (node->style.line_height > 0 ? node->style.line_height
                                                                  : OL_CSSPX(20));
        if (!add_keep(doc, empty_top, content_bottom)) return 0;
    }
    content_bottom += node->style.padding_bottom;

    node->bounds.x = box_x;
    node->bounds.y = box_y;
    node->bounds.width = width - ml - mr;
    if (node->bounds.width < 0) node->bounds.width = 0;
    node->bounds.height = content_bottom - box_y;
    if (is_frame(node) && node->box.frame.height > 0) node->bounds.height = node->box.frame.height;
    if (node->bounds.height < 0) node->bounds.height = 0;

    if (background_index != (size_t)-1)
        doc->ops[background_index].u.fill_rect.bounds = node->bounds;
    /* a cell's borders come when its row's height is known */
    if (node->role != OL_ROLE_CELL && !add_borders(doc, node)) return 0;
    if ((node->box.flags & (OL_BOX_KEEP_TOGETHER | OL_BOX_ABSOLUTE)) &&
        !add_keep(doc, node->bounds.y, node->bounds.y + node->bounds.height))
        return 0;

    /* frames last, above the flow they stand in */
    for (child = node->first_child; child; child = child->next_sibling)
        if (child->style.display != OL_DISPLAY_NONE && is_frame(child) &&
            !layout_frame(doc, child, inner_x, box_y + node->style.padding_top,
                          measure, measure_userdata))
            return 0;

    *bottom_out = is_frame(node) ? node->bounds.y + node->bounds.height
                                 : content_bottom + mb;
    return 1;
}

/* The document's children, one under another, as the galley. */
static int layout_galley(ol_document *doc, ol_unit width,
                         ol_measure_text_fn measure, void *measure_userdata)
{
    ol_node *child, *root = doc->root;
    ol_unit y = 0, keep_with_next = 0;
    int has_keep_with_next = 0;

    for (child = root->first_child; child; child = child->next_sibling) {
        size_t keeps_before = doc->keep_count;
        if (child->style.display == OL_DISPLAY_NONE || is_frame(child)) continue;
        if (child->style.display == OL_DISPLAY_INLINE) {
            ol_inline_flow flow;
            start_flow(&flow, doc, NULL, measure, measure_userdata, 0, width, y,
                       child->style.text_align, 0);
            if (!layout_inline_node(&flow, child)) return 0;
            if (!flush_flow(&flow, &y)) return 0;
        } else {
            ol_unit bottom;
            if (!layout_block(doc, child, 0, y, width, measure, measure_userdata, &bottom))
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
            !layout_frame(doc, child, 0, 0, measure, measure_userdata))
            return 0;

    inline_bounds(doc);
    root->bounds.x = 0;
    root->bounds.y = 0;
    root->bounds.width = width;
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
    doc->paged = 0;
    doc->page_count = 0;
    for (i = 0; i < doc->node_count; ++i)
        memset(&doc->nodes[i]->bounds, 0, sizeof(doc->nodes[i]->bounds));
}

/* ---- Pages ----------------------------------------------------------------- */

static int add_page(ol_document *doc, ol_unit start, ol_unit end)
{
    if (doc->page_count == doc->page_capacity) {
        int cap = doc->page_capacity ? doc->page_capacity * 2 : 8;
        ol_unit *s = (ol_unit *)realloc(doc->page_start, (size_t)cap * sizeof(*s));
        ol_unit *e;
        size_t *f, *n;
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
        doc->page_capacity = cap;
    }
    doc->page_start[doc->page_count] = start;
    doc->page_end[doc->page_count] = end;
    doc->page_first[doc->page_count] = 0;
    doc->page_ops[doc->page_count] = 0;
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

static int cut_pages(ol_document *doc, ol_unit body_height)
{
    ol_unit total = doc->content_height, start = 0;
    size_t b = 0;

    if (doc->keep_count && doc->keeps[doc->keep_count - 1].bottom > total)
        total = doc->keeps[doc->keep_count - 1].bottom;
    for (;;) {
        ol_unit limit = start + body_height, end = limit;
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
    ol_unit dy = doc->page.margin_top - doc->page_start[p];
    ol_unit dx = doc->page.margin_left;
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
    case OL_OP_FILL_RECT: {
        ol_rect *r = &op->u.fill_rect.bounds;
        ol_unit t = r->y, b = r->y + r->height;
        if (split) {
            if (t < lo && p > 0) t = lo;
            if (b > hi && p + 1 < doc->page_count) b = hi;
            if (b <= t) return 0;
        }
        r->x += dx;
        r->y = t + dy;
        r->height = b - t;
        break;
    }
    case OL_OP_RULE:
        if (split) {
            ol_unit *a = op->u.rule.y1 < op->u.rule.y2 ? &op->u.rule.y1 : &op->u.rule.y2;
            ol_unit *z = a == &op->u.rule.y1 ? &op->u.rule.y2 : &op->u.rule.y1;
            if (*a < lo && p > 0) *a = lo;
            if (*z > hi && p + 1 < doc->page_count) *z = hi;
            if (*z <= *a) return 0;
        }
        op->u.rule.x1 += dx;
        op->u.rule.x2 += dx;
        op->u.rule.y1 += dy;
        op->u.rule.y2 += dy;
        break;
    }
    return 1;
}

/* The galley's ops sorted onto their pages, each page's together. */
static int sort_ops_into_pages(ol_document *doc)
{
    size_t i, total = 0;
    size_t *next;
    ol_display_op *ops;
    int p;

    for (i = 0; i < doc->op_count; ++i) {
        int first, last, split;
        op_pages(doc, &doc->ops[i], &first, &last, &split);
        for (p = first; p <= last; ++p) {
            ol_display_op tmp = doc->ops[i];
            if (place_op(doc, &tmp, p, split)) {
                ++doc->page_ops[p];
                ++total;
            }
        }
    }
    ops = (ol_display_op *)malloc((total ? total : 1) * sizeof(*ops));
    next = (size_t *)malloc((size_t)doc->page_count * sizeof(*next));
    if (!ops || !next) {
        free(ops);
        free(next);
        return 0;
    }
    for (p = 0, i = 0; p < doc->page_count; ++p) {
        doc->page_first[p] = i;
        next[p] = i;
        i += doc->page_ops[p];
    }
    for (i = 0; i < doc->op_count; ++i) {
        int first, last, split;
        op_pages(doc, &doc->ops[i], &first, &last, &split);
        for (p = first; p <= last; ++p) {
            ol_display_op tmp = doc->ops[i];
            if (place_op(doc, &tmp, p, split)) ops[next[p]++] = tmp;
        }
    }
    free(next);
    free(doc->ops);
    doc->ops = ops;
    doc->op_count = total;
    doc->op_capacity = total ? total : 1;
    return 1;
}

int ol_layout(ol_document *doc, ol_unit viewport_width,
              ol_measure_text_fn measure, void *measure_userdata)
{
    if (!doc || viewport_width <= 0) return 0;
    reset_layout(doc);
    doc->viewport_width = viewport_width;
    if (!layout_galley(doc, viewport_width, measure, measure_userdata)) return 0;
    if (!add_page(doc, 0, doc->content_height)) return 0;
    doc->page_ops[0] = doc->op_count;
    ++doc->generation;
    return 1;
}

int ol_layout_pages(ol_document *doc, const ol_page *page,
                    ol_measure_text_fn measure, void *measure_userdata)
{
    ol_unit body_w, body_h;
    if (!doc || !page) return 0;
    body_w = page->width - page->margin_left - page->margin_right;
    body_h = page->height - page->margin_top - page->margin_bottom;
    if (body_w <= 0 || body_h <= 0) return 0;
    reset_layout(doc);
    doc->viewport_width = body_w;
    if (!layout_galley(doc, body_w, measure, measure_userdata)) return 0;
    sort_keeps_and_breaks(doc);
    doc->page = *page;
    if (!cut_pages(doc, body_h)) return 0;
    if (!sort_ops_into_pages(doc)) return 0;
    doc->paged = 1;
    ++doc->generation;
    return 1;
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

const ol_node *ol_hit_test_page(const ol_document *doc, int page,
                                ol_unit x, ol_unit y)
{
    ol_unit gy;
    if (!doc || page < 0 || page >= doc->page_count) return NULL;
    if (!doc->paged) return ol_hit_test(doc, x, y);
    gy = y - doc->page.margin_top + doc->page_start[page];
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
        if (op->type != OL_OP_TEXT) continue;
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
        ol_unit gx = node->bounds.x + node->style.padding_left + node->box.text_indent;
        ol_unit gy = node->bounds.y + node->style.padding_top;
        int p = 0;
        if (doc->paged) {
            p = page_holding(doc, gy);
            gx += doc->page.margin_left;
            gy += doc->page.margin_top - doc->page_start[p];
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
        if (clip && !pixel_rects_cross(&d.box, clip)) continue;
        draw(userdata, &d);
        ++drawn;
    }
    return drawn;
}
