/* GFOpenLayout's documents styled through GFCSS: nodes as elements (a tag,
 * attributes, states), a document's stylesheets and viewport, and every
 * tagged node's ol_style and ol_box_style worked out from them before a
 * layout. Nodes with no tag keep what was set on them, so documents with
 * no stylesheets are laid out as before.
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "gfopenlayout.h"
#include "gfopenlayout_css.h"
#include "openlayout_css.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name, *value;
} ocss_attr;

/* A node's element */
typedef struct {
    char *tag;
    ocss_attr *attrs;
    int nattrs, cap;
    uint32_t state;
    /* worked out by each styling: */
    int32_t index, count;       /* among its parent element's elements */
    ol_node *parent_el, *prev_el, *next_el;
    ol_node *gen[2];            /* its ::before and ::after text */
    int generated;              /* it is one of those */
} ocss_node;

/* A document's sheets */
typedef struct {
    gfcss_engine *e;
    gfcss_sheet **sheets;
    int nsheets, cap;
    int media, viewport_set;
    ol_unit vw, vh;
    int stale;
    size_t styled_nodes;        /* how many nodes it had when it was styled */
    ol_unit styled_width;
    gfcss_computed *stack;      /* the elements' styles down to the one being styled */
    uint32_t *deco;             /* and the text decoration each passes on */
    size_t depth_cap;
    gfcss_computed pseudo;
} ocss_doc;

#define MAX_DEPTH 512

static char *dup0(const char *s)
{
    size_t n;
    char *d;
    if (!s) return NULL;
    n = strlen(s);
    d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

static int name_eq(const char *a, const char *b)
{
    while (*a && *b) {
        int x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

void ol_css_node_free(void *css)
{
    ocss_node *n = (ocss_node *)css;
    int i;
    if (!n) return;
    for (i = 0; i < n->nattrs; ++i) {
        free(n->attrs[i].name);
        free(n->attrs[i].value);
    }
    free(n->attrs);
    free(n->tag);
    free(n);
}

void ol_css_document_free(void *css)
{
    ocss_doc *d = (ocss_doc *)css;
    if (!d) return;
    gfcss_free(d->e);
    free(d->sheets);
    free(d->stack);
    free(d->deco);
    free(d);
}

static ocss_doc *doc_css(ol_document *doc, int make)
{
    ocss_doc *d = (ocss_doc *)ol_css_document_data(doc);
    if (d || !make || !doc) return d;
    d = (ocss_doc *)calloc(1, sizeof(*d));
    if (!d) return NULL;
    if (!(d->e = gfcss_new())) {
        free(d);
        return NULL;
    }
    d->stale = 1;
    ol_css_set_document_data(doc, d);
    return d;
}

static void touch(const ol_node *node)
{
    ocss_doc *d = (ocss_doc *)ol_css_document_data(ol_css_node_document(node));
    if (d) d->stale = 1;
}

static ocss_node *node_css(ol_node *node, int make)
{
    ocss_node *n = (ocss_node *)ol_css_node_data(node);
    if (n || !make || !node) return n;
    n = (ocss_node *)calloc(1, sizeof(*n));
    if (n) ol_css_set_node_data(node, n);
    return n;
}

/* ---- Nodes as elements ------------------------------------------------------ */

int ol_node_set_tag(ol_node *node, const char *tag)
{
    ocss_node *n;
    char *t = NULL;
    size_t i;
    if (!node) return 0;
    if (!(n = node_css(node, 1))) return 0;
    if (tag && tag[0]) {
        if (!(t = dup0(tag))) return 0;
        for (i = 0; t[i]; ++i)
            if (t[i] >= 'A' && t[i] <= 'Z') t[i] = (char)(t[i] + 32);
    }
    free(n->tag);
    n->tag = t;
    touch(node);
    return 1;
}

const char *ol_node_tag(const ol_node *node)
{
    const ocss_node *n = (const ocss_node *)ol_css_node_data(node);
    return n ? n->tag : NULL;
}

int ol_node_set_attr(ol_node *node, const char *name, const char *value)
{
    ocss_node *n;
    int i;
    char *v;
    if (!node || !name || !name[0]) return 0;
    if (!(n = node_css(node, 1))) return 0;
    for (i = 0; i < n->nattrs; ++i)
        if (name_eq(n->attrs[i].name, name)) break;
    touch(node);
    if (!value) {
        if (i < n->nattrs) {
            free(n->attrs[i].name);
            free(n->attrs[i].value);
            n->attrs[i] = n->attrs[--n->nattrs];
        }
        return 1;
    }
    if (!(v = dup0(value))) return 0;
    if (i < n->nattrs) {
        free(n->attrs[i].value);
        n->attrs[i].value = v;
        return 1;
    }
    if (n->nattrs == n->cap) {
        int cap = n->cap ? n->cap * 2 : 4;
        ocss_attr *a = (ocss_attr *)realloc(n->attrs, (size_t)cap * sizeof(*a));
        if (!a) {
            free(v);
            return 0;
        }
        n->attrs = a;
        n->cap = cap;
    }
    n->attrs[n->nattrs].name = dup0(name);
    if (!n->attrs[n->nattrs].name) {
        free(v);
        return 0;
    }
    n->attrs[n->nattrs++].value = v;
    return 1;
}

const char *ol_node_attr(const ol_node *node, const char *name)
{
    const ocss_node *n = (const ocss_node *)ol_css_node_data(node);
    int i;
    if (!n || !name) return NULL;
    for (i = 0; i < n->nattrs; ++i)
        if (name_eq(n->attrs[i].name, name)) return n->attrs[i].value;
    return NULL;
}

void ol_node_set_css_state(ol_node *node, uint32_t states)
{
    ocss_node *n = node_css(node, 1);
    if (!n || n->state == states) return;
    n->state = states;
    touch(node);
}

uint32_t ol_node_css_state(const ol_node *node)
{
    const ocss_node *n = (const ocss_node *)ol_css_node_data(node);
    return n ? n->state : 0;
}

int ol_node_is_generated(const ol_node *node)
{
    const ocss_node *n = (const ocss_node *)ol_css_node_data(node);
    return n ? n->generated : 0;
}

static int is_element(const ol_node *node)
{
    const ocss_node *n = (const ocss_node *)ol_css_node_data(node);
    return n && n->tag && !n->generated;
}

/* ---- The document's sheets ----------------------------------------------------- */

int ol_document_add_stylesheet(ol_document *doc, const char *css, size_t length, int origin)
{
    ocss_doc *d = doc_css(doc, 1);
    gfcss_sheet *s;
    if (!d) return 0;
    if (d->nsheets == d->cap) {
        int cap = d->cap ? d->cap * 2 : 4;
        gfcss_sheet **p = (gfcss_sheet **)realloc(d->sheets, (size_t)cap * sizeof(*p));
        if (!p) return 0;
        d->sheets = p;
        d->cap = cap;
    }
    if (!(s = gfcss_sheet_new(d->e, origin))) return 0;
    d->sheets[d->nsheets++] = s;
    d->stale = 1;
    return gfcss_sheet_parse(s, css, length);
}

void ol_document_set_viewport(ol_document *doc, ol_unit width, ol_unit height, int media)
{
    ocss_doc *d = doc_css(doc, 1);
    if (!d) return;
    d->vw = width;
    d->vh = height;
    d->media = media;
    d->viewport_set = 1;
    d->stale = 1;
}

/* the index'th import of a sheet (counting down index as it goes) */
static int find_import(gfcss_sheet *s, int *index, gfcss_sheet **owner, int *at)
{
    int n = gfcss_sheet_import_count(s);
    if (*index < n) {
        *owner = s;
        *at = *index;
        return 1;
    }
    *index -= n;
    return 0;
}

int ol_document_import_count(const ol_document *doc)
{
    const ocss_doc *d = (const ocss_doc *)ol_css_document_data(doc);
    int i, n = 0;
    if (!d) return 0;
    for (i = 0; i < d->nsheets; ++i) n += gfcss_sheet_import_count(d->sheets[i]);
    return n;
}

static int locate(const ol_document *doc, int index, gfcss_sheet **owner, int *at)
{
    const ocss_doc *d = (const ocss_doc *)ol_css_document_data(doc);
    int i;
    if (!d || index < 0) return 0;
    for (i = 0; i < d->nsheets; ++i)
        if (find_import(d->sheets[i], &index, owner, at)) return 1;
    return 0;
}

const char *ol_document_import_url(const ol_document *doc, int index)
{
    gfcss_sheet *owner;
    int at;
    if (!locate(doc, index, &owner, &at)) return NULL;
    return gfcss_sheet_import_url(owner, at);
}

int ol_document_add_import(ol_document *doc, int index, const char *css, size_t length)
{
    gfcss_sheet *owner, *s;
    int at;
    ocss_doc *d = doc_css(doc, 0);
    if (!d || !locate(doc, index, &owner, &at)) return 0;
    if (!(s = gfcss_sheet_import(owner, at))) return 0;
    d->stale = 1;
    return gfcss_sheet_parse(s, css, length);
}

/* ---- Styling ---------------------------------------------------------------------- */

static void el_info(void *ud, const void *el, gfcss_info *in)
{
    const ol_node *node = (const ol_node *)el;
    const ocss_node *n = (const ocss_node *)ol_css_node_data(node);
    const char *text = ol_node_text(node);
    (void)ud;
    in->tag = n->tag;
    in->id = ol_node_attr(node, "id");
    in->classes = ol_node_attr(node, "class");
    in->style = ol_node_attr(node, "style");
    in->state = n->state;
    in->index = n->index;
    in->count = n->count;
    in->empty = !ol_node_first_child(node) && (!text || !text[0]);
}

static const void *el_parent(void *ud, const void *el)
{
    (void)ud;
    return ((const ocss_node *)ol_css_node_data((const ol_node *)el))->parent_el;
}

static const void *el_prev(void *ud, const void *el)
{
    (void)ud;
    return ((const ocss_node *)ol_css_node_data((const ol_node *)el))->prev_el;
}

static const void *el_next(void *ud, const void *el)
{
    (void)ud;
    return ((const ocss_node *)ol_css_node_data((const ol_node *)el))->next_el;
}

static const char *el_attr(void *ud, const void *el, const char *name)
{
    (void)ud;
    return ol_node_attr((const ol_node *)el, name);
}

static const gfcss_element_ops node_ops = { el_info, el_parent, el_prev, el_next, el_attr };

typedef struct {
    ol_document *doc;
    ocss_doc *d;
} walk;

static int room(ocss_doc *d, size_t depth)
{
    size_t cap;
    gfcss_computed *s;
    uint32_t *dc;
    if (depth < d->depth_cap) return 1;
    cap = d->depth_cap ? d->depth_cap * 2 : 16;
    while (cap <= depth) cap *= 2;
    s = (gfcss_computed *)realloc(d->stack, cap * sizeof(*s));
    if (!s) return 0;
    d->stack = s;
    dc = (uint32_t *)realloc(d->deco, cap * sizeof(*dc));
    if (!dc) return 0;
    d->deco = dc;
    d->depth_cap = cap;
    return 1;
}

static uint32_t deco_flags(uint32_t deco)
{
    uint32_t f = 0;
    if (deco & GFCSS_UNDERLINE) f |= OL_TEXT_UNDERLINE;
    if (deco & GFCSS_LINE_THROUGH) f |= OL_TEXT_STRIKE;
    return f;
}

/* style and box from cs onto node, keeping what CSS doesn't say: a font
 * number, page breaks, frames, columns spanned */
static void set_styles(ol_node *node, const gfcss_computed *cs, ol_unit cw, uint32_t deco, ol_style *out_st,
                       ol_box_style *out_box)
{
    ol_style st;
    ol_box_style box, was = *ol_node_box(node);
    uint32_t keep;
    gfcss_to_ol(cs, cw, &st, &box);
    st.text_flags |= deco_flags(deco) | (ol_node_style(node)->text_flags & 0xffff0000u);
    keep = was.flags & ~(uint32_t)(OL_BOX_ROW | (1u << 7) | (1u << 8) | (1u << 9));
    box.flags |= keep;
    box.frame = was.frame;
    box.column_span = was.column_span;
    box.anchor = was.anchor;
    box.wrap = was.wrap;
    memcpy(box.wrap_gap, was.wrap_gap, sizeof(box.wrap_gap));
    ol_node_set_style(node, &st);
    ol_node_set_box(node, &box);
    if (out_st) *out_st = st;
    if (out_box) *out_box = box;
}

/* the ::before (which 0) or ::after (1) of an element: made, changed or emptied */
static int generated(walk *w, ol_node *node, ocss_node *n, size_t depth, int which, ol_unit cw)
{
    ocss_doc *d = w->d;
    gfcss_computed *ps = &d->pseudo;
    ol_node *g = n->gen[which];
    int has;
    gfcss_computed_init(ps);
    has = gfcss_compute(d->e, &node_ops, w, node, which ? GFCSS_PSEUDO_AFTER : GFCSS_PSEUDO_BEFORE,
                        &d->stack[depth], ps);
    if (!has && !g) return 1;
    if (!g) {
        ocss_node *gn;
        g = ol_node_append(w->doc, node, OL_ROLE_TEXT, "");
        if (!g || !(gn = node_css(g, 1))) return 0;
        gn->generated = 1;
        n->gen[which] = g;
    }
    ol_css_move_child(g, !which);
    if (has) {
        size_t len = gfcss_text_copy(&ps->content, NULL, 0);
        char *text = (char *)malloc(len + 1);
        int ok;
        if (!text) return 0;
        gfcss_text_copy(&ps->content, text, len + 1);
        ok = ol_node_set_text(g, text);
        free(text);
        if (!ok) return 0;
        if (ps->display != GFCSS_DISPLAY_BLOCK) ps->display = GFCSS_DISPLAY_INLINE;
        set_styles(g, ps, cw, d->deco[depth] | ps->text_decoration, NULL, NULL);
    } else {
        ol_style st;
        if (!ol_node_set_text(g, "")) return 0;
        st = *ol_node_style(g);
        st.display = OL_DISPLAY_NONE;
        ol_node_set_style(g, &st);
    }
    return 1;
}

static int is_flex(int32_t display)
{
    return display == GFCSS_DISPLAY_FLEX || display == GFCSS_DISPLAY_INLINE_FLEX;
}

/* parent's children styled; pd: the nearest element's place in the stack (0: none) */
static int style_children(walk *w, ol_node *parent, ol_node *parent_el, size_t pd, ol_unit cw)
{
    ocss_doc *d = w->d;
    ol_node *c, *prev = NULL;
    int32_t count = 0, index = 0;
    for (c = ol_css_first_child(parent); c; c = ol_css_next_sibling(c))
        if (is_element(c)) ++count;
    for (c = ol_css_first_child(parent); c; c = ol_css_next_sibling(c)) {
        ocss_node *n;
        if (!is_element(c)) continue;
        n = (ocss_node *)ol_css_node_data(c);
        n->index = ++index;
        n->count = count;
        n->parent_el = parent_el;
        n->prev_el = prev;
        n->next_el = NULL;
        if (prev) ((ocss_node *)ol_css_node_data(prev))->next_el = c;
        prev = c;
    }
    for (c = ol_css_first_child(parent); c; c = ol_css_next_sibling(c)) {
        ocss_node *n = (ocss_node *)ol_css_node_data(c);
        if (n && n->generated) continue;
        if (n && n->tag) {
            size_t depth = pd + 1;
            gfcss_computed *cs;
            ol_style st;
            ol_box_style box;
            ol_unit inner;
            if (depth >= MAX_DEPTH) continue;
            if (!room(d, depth + 1)) return 0;
            cs = &d->stack[depth];
            gfcss_computed_init(cs);
            if (!gfcss_compute(d->e, &node_ops, w, c, GFCSS_PSEUDO_NONE, pd ? &d->stack[pd] : NULL, cs)) return 0;
            /* a flex container's children are blocks, as CSS says */
            if (pd && is_flex(d->stack[pd].display)) {
                if (cs->display == GFCSS_DISPLAY_INLINE || cs->display == GFCSS_DISPLAY_INLINE_BLOCK)
                    cs->display = GFCSS_DISPLAY_BLOCK;
                else if (cs->display == GFCSS_DISPLAY_INLINE_FLEX) cs->display = GFCSS_DISPLAY_FLEX;
                else if (cs->display == GFCSS_DISPLAY_INLINE_TABLE) cs->display = GFCSS_DISPLAY_TABLE;
            }
            d->deco[depth] = (pd ? d->deco[pd] : 0) | cs->text_decoration;
            set_styles(c, cs, cw, d->deco[depth], &st, &box);
            if (!generated(w, c, n, depth, 0, cw) || !generated(w, c, n, depth, 1, cw)) return 0;
            /* what its children's percentages are of */
            if (st.display == OL_DISPLAY_INLINE) {
                inner = cw;
            } else {
                ol_unit borders = box.border_width > 0 && (box.border_sides & (OL_BORDER_LEFT | OL_BORDER_RIGHT))
                                ? box.border_width * (((box.border_sides & OL_BORDER_LEFT) != 0) +
                                                      ((box.border_sides & OL_BORDER_RIGHT) != 0)) : 0;
                inner = box.width > 0 ? box.width : cw - st.margin_left - st.margin_right;
                inner -= st.padding_left + st.padding_right + borders;
                if (inner < 0) inner = 0;
            }
            if (!style_children(w, c, c, depth, inner)) return 0;
        } else {
            /* no tag: a run of text in an element takes the element's inherited style */
            if (pd && ol_node_role(c) == OL_ROLE_TEXT) {
                gfcss_computed *cs;
                if (!room(d, pd + 2)) return 0;
                cs = &d->stack[pd + 1];
                gfcss_inherit(d->e, &d->stack[pd], cs);
                cs->display = GFCSS_DISPLAY_INLINE;
                set_styles(c, cs, cw, d->deco[pd], NULL, NULL);
            }
            if (!style_children(w, c, parent_el, pd, cw)) return 0;
        }
    }
    return 1;
}

int ol_document_apply_styles(ol_document *doc, ol_unit containing_width)
{
    ocss_doc *d = doc_css(doc, 0);
    walk w;
    if (!doc) return 0;
    if (!d) return 1;
    gfcss_set_media(d->e, d->media, d->viewport_set ? d->vw : containing_width, d->viewport_set ? d->vh : 0);
    if (!room(d, 2)) return 0;
    w.doc = doc;
    w.d = d;
    d->deco[0] = 0;
    if (!style_children(&w, ol_document_root(doc), NULL, 0, containing_width)) return 0;
    d->stale = 0;
    d->styled_nodes = ol_css_node_count(doc);
    d->styled_width = containing_width;
    return 1;
}

int ol_css_restyle(ol_document *doc, ol_unit width)
{
    ocss_doc *d = doc_css(doc, 0);
    if (!d || !d->nsheets) return 1;
    if (!d->stale && d->styled_nodes == ol_css_node_count(doc) && d->styled_width == width) return 1;
    return ol_document_apply_styles(doc, width);
}
