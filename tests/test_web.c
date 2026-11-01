/* GFOpenLayout 0.9: web boxes. Per-side borders, widths in thousandths with
 * their least and most, auto side margins, inline-blocks, cells' vertical
 * alignment and table headers repeated on the pages a table goes on to.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(x) do { ++checks; if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); ++failures; \
} } while (0)

#define PX(v) OL_CSSPX(v)

/* every character 8 CSS px wide; ascent 12, descent 4 */
static int measure(void *userdata, const char *utf8, size_t length,
                   const ol_style *style, ol_text_metrics *m)
{
    (void)userdata;
    (void)utf8;
    (void)style;
    m->width = (ol_unit)length * PX(8);
    m->ascent = PX(12);
    m->descent = PX(4);
    return 1;
}

/* a block (or other display) whose own text is text, lines 16 px */
static ol_node *add(ol_document *doc, ol_node *parent, ol_role role, ol_display display, const char *text)
{
    ol_style s;
    ol_node *n = ol_node_append(doc, parent, role, text);
    if (!n) return NULL;
    ol_style_init(&s);
    s.display = display;
    s.font_size = PX(16);
    s.line_height = PX(16);
    ol_node_set_style(n, &s);
    return n;
}

static ol_node *block(ol_document *doc, ol_node *parent, const char *text)
{
    return add(doc, parent, OL_ROLE_BLOCK, OL_DISPLAY_BLOCK, text);
}

static ol_node *span(ol_document *doc, ol_node *parent, const char *text)
{
    return add(doc, parent, OL_ROLE_TEXT, OL_DISPLAY_INLINE, text);
}

static void set_style(ol_node *n, ol_unit padding, ol_unit margin_lr, uint32_t background, ol_text_align align)
{
    ol_style s = *ol_node_style(n);
    s.padding_top = s.padding_right = s.padding_bottom = s.padding_left = padding;
    s.margin_left = s.margin_right = margin_lr;
    s.background = background;
    s.text_align = align;
    ol_node_set_style(n, &s);
}

static ol_box_style box_of(const ol_node *n)
{
    return *ol_node_box(n);
}

/* the nth op (from 0) of a type for a node; NULL when there is none */
static const ol_display_op *nth_op(const ol_document *doc, const ol_node *n, ol_op_type type, int nth)
{
    size_t i;
    for (i = 0; i < ol_display_count(doc); ++i) {
        const ol_display_op *op = ol_display_get(doc, i);
        ol_id id = op->type == OL_OP_TEXT ? op->u.text.node_id
                 : op->type == OL_OP_FILL_RECT ? op->u.fill_rect.node_id
                 : op->type == OL_OP_IMAGE ? op->u.image.node_id : op->u.rule.node_id;
        if (op->type == type && id == ol_node_id(n) && nth-- == 0) return op;
    }
    return NULL;
}

static int count_ops(const ol_document *doc, const ol_node *n, ol_op_type type)
{
    int k = 0;
    while (nth_op(doc, n, type, k)) ++k;
    return k;
}

static int rule_is(const ol_display_op *op, ol_unit x1, ol_unit y1, ol_unit x2, ol_unit y2, ol_unit t, uint32_t argb)
{
    return op && op->u.rule.x1 == x1 && op->u.rule.y1 == y1 && op->u.rule.x2 == x2 && op->u.rule.y2 == y2 &&
           op->u.rule.thickness == t && op->u.rule.argb == argb;
}

static int rect_is(ol_rect r, ol_unit x, ol_unit y, ol_unit w, ol_unit h)
{
    return r.x == x && r.y == y && r.width == w && r.height == h;
}

static void draw_rect(void *userdata, const ol_device_op *d)
{
    ol_pixel_rect *out = (ol_pixel_rect *)userdata;
    if (d->op->type == OL_OP_RULE && d->op->u.rule.argb == 0xff000001u) *out = d->box;
}

static void test_side_borders(void)
{
    ol_document *doc = ol_document_new();
    ol_node *b, *t, *row, *c, *old;
    ol_box_style box;
    ol_scale sc;
    ol_pixel_rect top_box;

    /* 2, 3, 4 and 5 px borders round 10 px of padding */
    b = block(doc, NULL, "ab");
    set_style(b, PX(10), 0, 0, OL_ALIGN_START);
    box = box_of(b);
    box.flags |= OL_BOX_SIDE_BORDERS;
    box.border_widths[0] = PX(2);
    box.border_widths[1] = PX(3);
    box.border_widths[2] = PX(4);
    box.border_widths[3] = PX(5);
    box.border_argbs[0] = 0xff000001u;
    box.border_argbs[1] = 0xff000002u;
    box.border_argbs[2] = 0xff000003u;
    box.border_argbs[3] = 0xff000004u;
    ol_node_set_box(b, &box);
    t = block(doc, NULL, "next");
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    /* they take room: 2 + 10 + 16 + 10 + 4 high, the text inside them and the padding */
    CHECK(rect_is(ol_node_bounds(b), 0, 0, PX(400), PX(42)));
    CHECK(nth_op(doc, b, OL_OP_TEXT, 0)->u.text.bounds.x == PX(15));
    CHECK(nth_op(doc, b, OL_OP_TEXT, 0)->u.text.bounds.y == PX(12));
    CHECK(ol_node_bounds(t).y == PX(42));
    /* a rule each, inside the box: each covers its side's strip */
    CHECK(count_ops(doc, b, OL_OP_RULE) == 4);
    CHECK(rule_is(nth_op(doc, b, OL_OP_RULE, 0), 0, 0, PX(398), 0, PX(2), 0xff000001u));
    CHECK(rule_is(nth_op(doc, b, OL_OP_RULE, 1), 0, PX(38), PX(396), PX(38), PX(4), 0xff000003u));
    CHECK(rule_is(nth_op(doc, b, OL_OP_RULE, 2), 0, 0, 0, PX(37), PX(5), 0xff000004u));
    CHECK(rule_is(nth_op(doc, b, OL_OP_RULE, 3), PX(397), 0, PX(397), PX(39), PX(3), 0xff000002u));
    /* drawn a pixel a CSS pixel: the top one is the box's top 2 px, edge to edge */
    ol_scale_set(&sc, 1, OL_UNITS_PER_CSSPX);
    sc.origin_x = sc.origin_y = 0;
    memset(&top_box, 0, sizeof(top_box));
    CHECK(ol_draw_page(doc, 0, &sc, NULL, draw_rect, &top_box) > 0);
    CHECK(top_box.x == 0 && top_box.y == 0 && top_box.width == 400 && top_box.height == 2);

    /* a side 0 wide has none, and takes no room */
    box.border_widths[0] = 0;
    ol_node_set_box(b, &box);
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    CHECK(count_ops(doc, b, OL_OP_RULE) == 3);
    CHECK(ol_node_bounds(b).height == PX(40));
    CHECK(nth_op(doc, b, OL_OP_TEXT, 0)->u.text.bounds.y == PX(10));
    ol_document_free(doc);

    /* the single border is as it was: on the edges, taking no room */
    doc = ol_document_new();
    old = block(doc, NULL, "ab");
    set_style(old, PX(10), 0, 0, OL_ALIGN_START);
    box = box_of(old);
    box.border_width = PX(1);
    box.border_argb = 0xff00ff00u;
    box.border_widths[0] = PX(9);           /* not used without OL_BOX_SIDE_BORDERS */
    ol_node_set_box(old, &box);
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    CHECK(rect_is(ol_node_bounds(old), 0, 0, PX(400), PX(36)));
    CHECK(nth_op(doc, old, OL_OP_TEXT, 0)->u.text.bounds.x == PX(10));
    CHECK(count_ops(doc, old, OL_OP_RULE) == 4);
    CHECK(rule_is(nth_op(doc, old, OL_OP_RULE, 0), 0, 0, PX(400), 0, PX(1), 0xff00ff00u));
    CHECK(rule_is(nth_op(doc, old, OL_OP_RULE, 3), PX(400), 0, PX(400), PX(36), PX(1), 0xff00ff00u));
    ol_document_free(doc);

    /* in a row, a child as wide as its content and its side borders; a cell's borders round the row's height */
    doc = ol_document_new();
    row = block(doc, NULL, NULL);
    box = box_of(row);
    box.flags |= OL_BOX_ROW;
    box.align = OL_CROSS_START;
    ol_node_set_box(row, &box);
    c = block(doc, row, "ab");
    box = box_of(c);
    box.flags |= OL_BOX_SIDE_BORDERS;
    box.border_widths[1] = PX(3);
    box.border_widths[3] = PX(5);
    ol_node_set_box(c, &box);
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    CHECK(ol_node_bounds(c).width == PX(16 + 8));
    CHECK(nth_op(doc, c, OL_OP_TEXT, 0)->u.text.bounds.x == PX(5));
    ol_document_free(doc);

    doc = ol_document_new();
    {
        ol_node *table = add(doc, NULL, OL_ROLE_TABLE, OL_DISPLAY_BLOCK, NULL);
        ol_node *r = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
        ol_node *c1 = add(doc, r, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "a\nb\nc");
        ol_node *c2 = add(doc, r, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "x");
        box = box_of(c2);
        box.flags |= OL_BOX_SIDE_BORDERS;
        box.border_widths[2] = PX(2);
        box.border_argbs[2] = 0xffff0000u;
        ol_node_set_box(c2, &box);
        CHECK(ol_layout(doc, PX(200), measure, NULL));
        CHECK(ol_node_bounds(c1).height == PX(48));
        /* x and its border, 18 px, in a row of 48: the border at the row's foot */
        CHECK(rect_is(ol_node_bounds(c2), PX(100), 0, PX(100), PX(48)));
        CHECK(ol_node_bounds(r).height == PX(48));
        CHECK(count_ops(doc, c2, OL_OP_RULE) == 1);
        CHECK(rule_is(nth_op(doc, c2, OL_OP_RULE, 0), PX(100), PX(46), PX(198), PX(46), PX(2), 0xffff0000u));
        /* taller than the others, it makes the row taller */
        ol_node_set_text(c2, "x\ny\nz");
        CHECK(ol_layout(doc, PX(200), measure, NULL));
        CHECK(ol_node_bounds(r).height == PX(50) && ol_node_bounds(c1).height == PX(50));
    }
    ol_document_free(doc);
}

static void test_widths(void)
{
    ol_document *doc = ol_document_new();
    ol_node *half, *outer, *quarter, *capped, *both, *least, *wide, *fixed;
    ol_box_style box;

    half = block(doc, NULL, "a");
    box = box_of(half);
    box.width_permille = 500;
    ol_node_set_box(half, &box);
    outer = block(doc, NULL, NULL);
    set_style(outer, PX(20), 0, 0, OL_ALIGN_START);
    quarter = block(doc, outer, "b");
    box = box_of(quarter);
    box.width_permille = 250;
    ol_node_set_box(quarter, &box);
    capped = block(doc, outer, "c");        /* half, at most 100 px */
    box = box_of(capped);
    box.width_permille = 500;
    box.max_width = PX(100);
    ol_node_set_box(capped, &box);
    both = block(doc, outer, "d");          /* at most 100 px and at most a fifth (72 px): the narrower */
    box = box_of(both);
    box.max_width = PX(100);
    box.max_width_permille = 200;
    ol_node_set_box(both, &box);
    least = block(doc, outer, "e");         /* at most 100 px, at least 150: the least wins */
    box = box_of(least);
    box.max_width = PX(100);
    box.min_width = PX(150);
    ol_node_set_box(least, &box);
    wide = block(doc, outer, "f");          /* at least 500 px in 360: wider than its block */
    box = box_of(wide);
    box.min_width = PX(500);
    ol_node_set_box(wide, &box);
    fixed = block(doc, outer, "g");         /* a width wins over thousandths */
    box = box_of(fixed);
    box.width = PX(120);
    box.width_permille = 500;
    ol_node_set_box(fixed, &box);
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    CHECK(rect_is(ol_node_bounds(half), 0, 0, PX(200), PX(16)));
    CHECK(ol_node_bounds(quarter).x == PX(20) && ol_node_bounds(quarter).width == PX(90));
    CHECK(ol_node_bounds(capped).width == PX(100));
    CHECK(ol_node_bounds(both).width == PX(72));
    CHECK(ol_node_bounds(least).width == PX(150));
    CHECK(ol_node_bounds(wide).width == PX(500));
    CHECK(ol_node_bounds(fixed).width == PX(120));
    /* thousandths of a width that is not a whole number of thousands, without rounding up */
    CHECK(ol_layout(doc, PX(401), measure, NULL));
    CHECK(ol_node_bounds(half).width == PX(401) / 2);
    ol_document_free(doc);

    /* a table's cells give their columns' widths; the others share what is left */
    doc = ol_document_new();
    {
        ol_node *table = add(doc, NULL, OL_ROLE_TABLE, OL_DISPLAY_BLOCK, NULL);
        ol_node *r1 = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
        ol_node *a = add(doc, r1, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "a");
        ol_node *b = add(doc, r1, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "b");
        ol_node *c = add(doc, r1, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "c");
        ol_node *d = add(doc, r1, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "d");
        ol_node *r2 = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
        ol_node *e = add(doc, r2, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "e");
        ol_node *f = add(doc, r2, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "f");
        box = box_of(a);
        box.width_permille = 250;           /* 100 px */
        ol_node_set_box(a, &box);
        box = box_of(b);
        box.width = PX(40);
        ol_node_set_box(b, &box);
        box = box_of(f);
        box.width = PX(60);                 /* the widest of column 2's */
        ol_node_set_box(f, &box);
        CHECK(ol_layout(doc, PX(400), measure, NULL));
        CHECK(ol_node_bounds(a).x == 0 && ol_node_bounds(a).width == PX(100));
        CHECK(ol_node_bounds(b).x == PX(100) && ol_node_bounds(b).width == PX(60));
        CHECK(ol_node_bounds(c).x == PX(160) && ol_node_bounds(c).width == PX(120));
        CHECK(ol_node_bounds(d).x == PX(280) && ol_node_bounds(d).width == PX(120));
        CHECK(ol_node_bounds(e).width == PX(100) && ol_node_bounds(f).width == PX(60));
        /* asking for more than the table has: scaled down to it */
        box = box_of(c);
        box.width = PX(440);
        ol_node_set_box(c, &box);
        CHECK(ol_layout(doc, PX(400), measure, NULL));
        /* 100 + 60 + 440 px in 400: each two thirds of what it asks; nothing left for the last */
        CHECK(ol_node_bounds(a).width == PX(100) * 2 / 3 && ol_node_bounds(b).width == PX(40));
        CHECK(ol_node_bounds(c).width == PX(440) * 2 / 3 && ol_node_bounds(d).width == 0);
    }
    ol_document_free(doc);

    /* a row's children: a quarter of the row, and one that grows, at most 200 px */
    doc = ol_document_new();
    {
        ol_node *row = block(doc, NULL, NULL), *a, *b;
        box = box_of(row);
        box.flags |= OL_BOX_ROW;
        ol_node_set_box(row, &box);
        a = block(doc, row, "a");
        box = box_of(a);
        box.width_permille = 250;
        ol_node_set_box(a, &box);
        b = block(doc, row, "b");
        box = box_of(b);
        box.grow = 1;
        ol_node_set_box(b, &box);
        CHECK(ol_layout(doc, PX(400), measure, NULL));
        CHECK(ol_node_bounds(a).width == PX(100));
        CHECK(ol_node_bounds(b).x == PX(100) && ol_node_bounds(b).width == PX(300));
        box.max_width = PX(200);
        ol_node_set_box(b, &box);
        CHECK(ol_layout(doc, PX(400), measure, NULL));
        CHECK(ol_node_bounds(b).width == PX(200));
        /* too little room: the quarter keeps its width, as a width would, and the other gives room up */
        box.max_width = 0;
        box.grow = 0;
        ol_node_set_box(b, &box);
        ol_node_set_text(b, "bbbbbbbb");
        CHECK(ol_layout(doc, PX(80), measure, NULL));
        CHECK(ol_node_bounds(a).width == PX(20));
        CHECK(ol_node_bounds(b).x == PX(20) && ol_node_bounds(b).width == PX(60));
    }
    ol_document_free(doc);
}

static void test_auto_margins(void)
{
    ol_document *doc = ol_document_new();
    ol_node *outer, *centre, *right, *left, *margined, *top;
    ol_box_style box;

    top = block(doc, NULL, "t");            /* in the galley too */
    box = box_of(top);
    box.width = PX(100);
    box.flags |= OL_BOX_AUTO_LEFT | OL_BOX_AUTO_RIGHT;
    ol_node_set_box(top, &box);
    outer = block(doc, NULL, NULL);
    set_style(outer, PX(20), 0, 0, OL_ALIGN_START);
    centre = block(doc, outer, "a");
    box = box_of(centre);
    box.width = PX(100);
    box.flags |= OL_BOX_AUTO_LEFT | OL_BOX_AUTO_RIGHT;
    ol_node_set_box(centre, &box);
    right = block(doc, outer, "b");
    box.flags = OL_BOX_AUTO_LEFT;
    ol_node_set_box(right, &box);
    left = block(doc, outer, "c");
    box.flags = OL_BOX_AUTO_RIGHT;
    ol_node_set_box(left, &box);
    margined = block(doc, outer, "d");      /* centred with its margins: 110 px in 360 */
    set_style(margined, 0, PX(5), 0, OL_ALIGN_START);
    box.flags = OL_BOX_AUTO_LEFT | OL_BOX_AUTO_RIGHT;
    box.width = 0;
    box.max_width_permille = 250;           /* 90 px */
    ol_node_set_box(margined, &box);
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    CHECK(ol_node_bounds(top).x == PX(150) && ol_node_bounds(top).width == PX(100));
    CHECK(ol_node_bounds(centre).x == PX(20 + 130) && ol_node_bounds(centre).width == PX(100));
    CHECK(nth_op(doc, centre, OL_OP_TEXT, 0)->u.text.bounds.x == PX(150));
    CHECK(ol_node_bounds(right).x == PX(20 + 260));
    CHECK(ol_node_bounds(left).x == PX(20));
    CHECK(ol_node_bounds(margined).x == PX(20 + 130 + 5) && ol_node_bounds(margined).width == PX(90));
    ol_document_free(doc);
}

static void test_inline_blocks(void)
{
    ol_document *doc = ol_document_new();
    ol_node *p, *a, *ib, *c, *inner, *ib2, *ib3;
    ol_box_style box;
    const ol_display_op *op;
    size_t i;

    /* "ab ", a box 100 px wide, " cd": on one line, the box's bottom on the baseline */
    p = block(doc, NULL, NULL);
    a = span(doc, p, "ab ");
    ib = add(doc, p, OL_ROLE_BLOCK, OL_DISPLAY_INLINE_BLOCK, "x");
    set_style(ib, 0, 0, 0xff112233u, OL_ALIGN_START);
    box = box_of(ib);
    box.width = PX(100);
    box.height = PX(30);
    ol_node_set_box(ib, &box);
    c = span(doc, p, " cd");
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    /* the line: 30 above the baseline, 4 below */
    CHECK(rect_is(ol_node_bounds(ib), PX(24), 0, PX(100), PX(30)));
    CHECK(ol_node_bounds(p).height == PX(34));
    op = nth_op(doc, a, OL_OP_TEXT, 0);
    CHECK(op->u.text.baseline == PX(30) && op->u.text.bounds.height == PX(34));
    CHECK(nth_op(doc, c, OL_OP_TEXT, 1)->u.text.bounds.x == PX(132));
    /* its own text and background where the box is */
    op = nth_op(doc, ib, OL_OP_TEXT, 0);
    CHECK(op->u.text.bounds.x == PX(24) && op->u.text.bounds.y == 0 && op->u.text.baseline == PX(12));
    CHECK(rect_is(nth_op(doc, ib, OL_OP_FILL_RECT, 0)->u.fill_rect.bounds, PX(24), 0, PX(100), PX(30)));
    for (i = 0; i < ol_display_count(doc); ++i)         /* no place holder left behind */
        CHECK(ol_display_get(doc, i)->type != OL_OP_IMAGE);
    CHECK(ol_hit_test(doc, PX(50), PX(10)) == ib);

    /* centred: the box moves with its line */
    set_style(p, 0, 0, 0, OL_ALIGN_CENTER);
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    CHECK(ol_node_bounds(ib).x == PX(24 + 126));
    CHECK(nth_op(doc, ib, OL_OP_TEXT, 0)->u.text.bounds.x == PX(150));

    /* too wide for what is left of the line: on the next one, as a word */
    set_style(p, 0, 0, 0, OL_ALIGN_START);
    CHECK(ol_layout(doc, PX(100), measure, NULL));
    CHECK(rect_is(ol_node_bounds(ib), 0, PX(16), PX(100), PX(30)));
    CHECK(nth_op(doc, ib, OL_OP_TEXT, 0)->u.text.bounds.y == PX(16));
    ol_document_free(doc);

    /* as wide as its content (with its padding), or the line; thousandths of the line */
    doc = ol_document_new();
    p = block(doc, NULL, NULL);
    ib = add(doc, p, OL_ROLE_BLOCK, OL_DISPLAY_INLINE_BLOCK, "hello");
    set_style(ib, PX(5), 0, 0, OL_ALIGN_START);
    ib2 = add(doc, p, OL_ROLE_BLOCK, OL_DISPLAY_INLINE_BLOCK, NULL);
    inner = block(doc, ib2, "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll");
    ib3 = add(doc, p, OL_ROLE_BLOCK, OL_DISPLAY_INLINE_BLOCK, "z");
    box = box_of(ib3);
    box.width_permille = 500;
    ol_node_set_box(ib3, &box);
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    CHECK(rect_is(ol_node_bounds(ib), 0, 0, PX(50), PX(26)));
    CHECK(nth_op(doc, ib, OL_OP_TEXT, 0)->u.text.bounds.x == PX(5));
    /* 59 characters, 472 px: the line's 400, so on a line of its own, its text wrapped in it */
    CHECK(ol_node_bounds(ib2).x == 0 && ol_node_bounds(ib2).y == PX(26) && ol_node_bounds(ib2).width == PX(400));
    CHECK(ol_node_bounds(ib2).height == PX(32));
    CHECK(rect_is(ol_node_bounds(inner), 0, PX(26), PX(400), PX(32)));
    CHECK(rect_is(ol_node_bounds(ib3), 0, PX(58), PX(200), PX(16)));
    CHECK(ol_document_content_height(doc) == PX(58 + 16));
    ol_document_free(doc);

    /* at the line's top, middle and bottom, beside a box 40 px tall on the baseline */
    {
        uint32_t v;
        ol_unit want[4] = { PX(30), 0, PX(17), PX(34) };
        for (v = OL_VALIGN_BASELINE; v <= OL_VALIGN_BOTTOM; ++v) {
            ol_node *tall, *small;
            doc = ol_document_new();
            p = block(doc, NULL, NULL);
            span(doc, p, "a ");
            tall = add(doc, p, OL_ROLE_BLOCK, OL_DISPLAY_INLINE_BLOCK, NULL);
            box = box_of(tall);
            box.width = PX(20);
            box.height = PX(40);
            ol_node_set_box(tall, &box);
            small = add(doc, p, OL_ROLE_BLOCK, OL_DISPLAY_INLINE_BLOCK, NULL);
            box.height = PX(10);
            box.valign = v;
            ol_node_set_box(small, &box);
            CHECK(ol_layout(doc, PX(400), measure, NULL));
            CHECK(ol_node_bounds(p).height == PX(44));
            CHECK(ol_node_bounds(tall).y == 0);
            CHECK(ol_node_bounds(small).x == PX(36) && ol_node_bounds(small).y == want[v]);
            ol_document_free(doc);
        }
        /* at the top of a line of text: the line as tall as it, the text in the line's middle as before */
        doc = ol_document_new();
        p = block(doc, NULL, NULL);
        a = span(doc, p, "a ");
        ib = add(doc, p, OL_ROLE_BLOCK, OL_DISPLAY_INLINE_BLOCK, NULL);
        box = box_of(ib);
        box.width = PX(20);
        box.height = PX(40);
        box.valign = OL_VALIGN_TOP;
        ol_node_set_box(ib, &box);
        CHECK(ol_layout(doc, PX(400), measure, NULL));
        CHECK(ol_node_bounds(p).height == PX(40));
        CHECK(ol_node_bounds(ib).y == 0);
        CHECK(nth_op(doc, a, OL_OP_TEXT, 0)->u.text.baseline == PX(12 + 12));
        ol_document_free(doc);
    }

    /* one inside another, and on pages: the box goes with its line */
    doc = ol_document_new();
    {
        ol_page page;
        ol_node *outer, *in;
        size_t first, count;
        int found = 0;
        memset(&page, 0, sizeof(page));
        page.width = PX(200);
        page.height = PX(100);
        page.margin_top = page.margin_left = PX(10);
        page.margin_bottom = PX(10);
        block(doc, NULL, "1\n2\n3\n4\n5");          /* 80 px: the box's line goes on page 2 */
        p = block(doc, NULL, NULL);
        outer = add(doc, p, OL_ROLE_BLOCK, OL_DISPLAY_INLINE_BLOCK, "o ");
        in = add(doc, outer, OL_ROLE_BLOCK, OL_DISPLAY_INLINE_BLOCK, "i");
        set_style(in, PX(2), 0, 0, OL_ALIGN_START);
        CHECK(ol_layout_pages(doc, &page, measure, NULL));
        CHECK(ol_page_count(doc) == 2);
        CHECK(rect_is(ol_node_bounds(outer), 0, PX(80), PX(28), PX(24)));
        CHECK(rect_is(ol_node_bounds(in), PX(16), PX(80), PX(12), PX(20)));
        CHECK(ol_display_page_range(doc, 1, &first, &count));
        for (i = first; i < first + count; ++i) {
            op = ol_display_get(doc, i);
            if (op->type == OL_OP_TEXT && op->u.text.node_id == ol_node_id(in)) {
                CHECK(op->u.text.bounds.x == PX(10 + 16 + 2) && op->u.text.bounds.y == PX(10 + 2));
                ++found;
            }
        }
        CHECK(found == 1);
    }
    ol_document_free(doc);
}

static void test_cell_valign(void)
{
    uint32_t v;
    for (v = OL_VALIGN_BASELINE; v <= OL_VALIGN_BOTTOM; ++v) {
        ol_document *doc = ol_document_new();
        ol_node *table = add(doc, NULL, OL_ROLE_TABLE, OL_DISPLAY_BLOCK, NULL);
        ol_node *r = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
        ol_node *c1 = add(doc, r, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "a\nb\nc\nd");
        ol_node *c2 = add(doc, r, OL_ROLE_CELL, OL_DISPLAY_BLOCK, NULL);
        ol_node *para = block(doc, c2, "x");
        ol_unit dy = v == OL_VALIGN_MIDDLE ? PX(22) : v == OL_VALIGN_BOTTOM ? PX(44) : 0;
        ol_box_style box;
        (void)c1;
        set_style(c2, PX(2), 0, 0xffeeeeeeu, OL_ALIGN_START);
        box = box_of(c2);
        box.valign = v;
        ol_node_set_box(c2, &box);
        CHECK(ol_layout(doc, PX(200), measure, NULL));
        /* the row is 64 px; the cell's content 16 px and its padding, 44 px less */
        CHECK(rect_is(ol_node_bounds(c2), PX(100), 0, PX(100), PX(64)));
        CHECK(rect_is(nth_op(doc, c2, OL_OP_FILL_RECT, 0)->u.fill_rect.bounds, PX(100), 0, PX(100), PX(64)));
        CHECK(rect_is(ol_node_bounds(para), PX(102), PX(2) + dy, PX(96), PX(16)));
        CHECK(nth_op(doc, para, OL_OP_TEXT, 0)->u.text.bounds.y == PX(2) + dy);
        CHECK(nth_op(doc, para, OL_OP_TEXT, 0)->u.text.baseline == PX(14) + dy);
        ol_document_free(doc);
    }
}

/* the texts of one page's text ops, joined, and how many of them start with c */
static int page_texts(const ol_document *doc, int page, char c, ol_unit *first_y)
{
    size_t first, count, i;
    int n = 0;
    if (!ol_display_page_range(doc, page, &first, &count)) return -1;
    for (i = first; i < first + count; ++i) {
        const ol_display_op *op = ol_display_get(doc, i);
        const char *t;
        if (op->type != OL_OP_TEXT) continue;
        t = ol_node_text(ol_node_by_id(doc, op->u.text.node_id)) + op->u.text.text_offset;
        if (t[0] == c) {
            if (!n && first_y) *first_y = op->u.text.bounds.y;
            ++n;
        }
    }
    return n;
}

static void test_repeat_header(void)
{
    ol_document *doc = ol_document_new();
    ol_page page;
    ol_node *table, *head, *hcell, *cells[10];
    ol_box_style box;
    ol_style s;
    size_t first, count, i;
    ol_unit y;
    int k, p;

    memset(&page, 0, sizeof(page));
    page.width = PX(200);
    page.height = PX(140);
    page.margin_top = page.margin_bottom = PX(20);   /* 100 px a page */
    page.margin_left = PX(30);

    table = add(doc, NULL, OL_ROLE_TABLE, OL_DISPLAY_BLOCK, NULL);
    s = *ol_node_style(table);
    s.background = 0xff0000ffu;
    ol_node_set_style(table, &s);
    head = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
    s = *ol_node_style(head);
    s.background = 0xffcccccc;
    ol_node_set_style(head, &s);
    box = box_of(head);
    box.flags |= OL_BOX_REPEAT_HEADER;
    ol_node_set_box(head, &box);
    hcell = add(doc, head, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "H");
    for (k = 0; k < 10; ++k) {
        ol_node *r = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
        cells[k] = add(doc, r, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "r");
    }
    /* without pages, nothing is repeated */
    CHECK(ol_layout(doc, PX(140), measure, NULL));
    CHECK(count_ops(doc, hcell, OL_OP_TEXT) == 1);

    /* page 1: the header and rows 0 to 4 (96 px); page 2: the header again, 16 px, then rows 5 to 9 */
    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    CHECK(ol_page_count(doc) == 2);
    CHECK(page_texts(doc, 0, 'H', &y) == 1 && y == PX(20));
    CHECK(page_texts(doc, 0, 'r', NULL) == 5);
    CHECK(page_texts(doc, 1, 'H', &y) == 1 && y == PX(20));
    CHECK(page_texts(doc, 1, 'r', &y) == 5 && y == PX(36));
    CHECK(ol_display_page_range(doc, 1, &first, &count));
    {
        size_t table_bg = (size_t)-1, head_bg = (size_t)-1, head_text = (size_t)-1, row_text = (size_t)-1;
        for (i = first; i < first + count; ++i) {
            const ol_display_op *op = ol_display_get(doc, i);
            CHECK(op->page == 1);
            if (op->type == OL_OP_FILL_RECT && op->u.fill_rect.node_id == ol_node_id(table)) {
                table_bg = i;
                /* up behind the header, down to the last row */
                CHECK(rect_is(op->u.fill_rect.bounds, PX(30), PX(20), PX(170), PX(96)));
            }
            if (op->type == OL_OP_FILL_RECT && op->u.fill_rect.node_id == ol_node_id(head)) {
                head_bg = i;
                CHECK(rect_is(op->u.fill_rect.bounds, PX(30), PX(20), PX(170), PX(16)));
            }
            if (op->type == OL_OP_TEXT && op->u.text.node_id == ol_node_id(hcell)) {
                head_text = i;
                CHECK(op->u.text.bounds.x == PX(30) && op->u.text.baseline == PX(32));
            }
            if (op->type == OL_OP_TEXT && op->u.text.node_id == ol_node_id(cells[5]) &&
                row_text == (size_t)-1)
                row_text = i;
        }
        /* drawn in order: the table's background, the header over it, then the rows */
        CHECK(table_bg != (size_t)-1 && head_bg != (size_t)-1 && head_text != (size_t)-1 && row_text != (size_t)-1);
        CHECK(table_bg < head_bg && head_bg < head_text && head_text < row_text);
    }
    /* the rows below the header are where they are drawn */
    CHECK(ol_hit_test_page(doc, 1, PX(40), PX(40)) == cells[5]);
    CHECK(ol_hit_test_page(doc, 1, PX(40), PX(30)) == NULL);
    CHECK(ol_hit_test_page(doc, 0, PX(40), PX(30)) == hcell);

    ol_document_free(doc);

    /* two header rows, both again */
    doc = ol_document_new();
    table = add(doc, NULL, OL_ROLE_TABLE, OL_DISPLAY_BLOCK, NULL);
    for (k = 0; k < 2; ++k) {
        head = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
        box = box_of(head);
        box.flags |= OL_BOX_REPEAT_HEADER;
        ol_node_set_box(head, &box);
        add(doc, head, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "H");
    }
    for (k = 0; k < 12; ++k) {
        ol_node *r = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
        add(doc, r, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "r");
        if (k == 5) {                       /* a row flagged after the header is not a header */
            box = box_of(r);
            box.flags |= OL_BOX_REPEAT_HEADER;
            ol_node_set_box(r, &box);
        }
    }
    /* 32 px of header, 4 rows a page after it: 4 + 4 + 4 */
    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    CHECK(ol_page_count(doc) == 3);
    for (p = 0; p < 3; ++p) {
        CHECK(page_texts(doc, p, 'H', &y) == 2 && y == PX(20));
        CHECK(page_texts(doc, p, 'r', &y) == 4 && y == PX(52));
    }
    ol_document_free(doc);

    /* never alone at a page's foot: the header goes over with the first row */
    doc = ol_document_new();
    block(doc, NULL, "1\n2\n3\n4\n5");      /* 80 px */
    table = add(doc, NULL, OL_ROLE_TABLE, OL_DISPLAY_BLOCK, NULL);
    head = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
    box = box_of(head);
    box.flags |= OL_BOX_REPEAT_HEADER;
    ol_node_set_box(head, &box);
    add(doc, head, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "H");
    for (k = 0; k < 3; ++k) {
        ol_node *r = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
        add(doc, r, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "r");
    }
    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    CHECK(ol_page_count(doc) == 2);
    CHECK(page_texts(doc, 0, 'H', NULL) == 0);
    CHECK(page_texts(doc, 1, 'H', &y) == 1 && y == PX(20));
    CHECK(page_texts(doc, 1, 'r', &y) == 3 && y == PX(36));
    ol_document_free(doc);

    /* a header taller than a page is never repeated */
    doc = ol_document_new();
    table = add(doc, NULL, OL_ROLE_TABLE, OL_DISPLAY_BLOCK, NULL);
    head = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
    box = box_of(head);
    box.flags |= OL_BOX_REPEAT_HEADER;
    ol_node_set_box(head, &box);
    add(doc, head, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "H\nH\nH\nH\nH\nH\nH");   /* 112 px */
    for (k = 0; k < 8; ++k) {
        ol_node *r = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
        add(doc, r, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "r");
    }
    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    {
        int hs = 0, rs = 0;
        for (p = 0; p < ol_page_count(doc); ++p) {
            hs += page_texts(doc, p, 'H', NULL);
            rs += page_texts(doc, p, 'r', NULL);
        }
        CHECK(hs == 7 && rs == 8);
    }
    ol_document_free(doc);
}

static unsigned long lcg = 4321;
static unsigned next_rand(unsigned n)
{
    lcg = lcg * 1103515245ul + 12345ul;
    return (unsigned)((lcg >> 8) % n);
}

/* random documents of tables with headers, inline-blocks, side borders and
 * frames behind and in front: every op, header copies too, on the page
 * whose range holds it, the ranges one after another and all of the list */
static void test_repeat_random(void)
{
    int round;
    for (round = 0; round < 40; ++round) {
        ol_document *doc = ol_document_new();
        ol_page page;
        int k, blocks = 2 + (int)next_rand(5), p;
        size_t total = 0;
        memset(&page, 0, sizeof(page));
        page.width = PX(300);
        page.height = PX(100 + 20 * (int)next_rand(5));
        page.margin_top = page.margin_bottom = PX(10);
        page.margin_left = PX(10 * (int)next_rand(3));
        for (k = 0; k < blocks; ++k) {
            unsigned what = next_rand(4);
            ol_box_style box;
            if (what == 0) {
                ol_node *table = add(doc, NULL, OL_ROLE_TABLE, OL_DISPLAY_BLOCK, NULL);
                int rows = 3 + (int)next_rand(20), h = 1 + (int)next_rand(2), r;
                ol_style s = *ol_node_style(table);
                s.background = 0xff8080ffu;
                ol_node_set_style(table, &s);
                for (r = 0; r < rows; ++r) {
                    ol_node *row = add(doc, table, OL_ROLE_ROW, OL_DISPLAY_BLOCK, NULL);
                    ol_node *c = add(doc, row, OL_ROLE_CELL, OL_DISPLAY_BLOCK, r < h ? "H" : next_rand(3) ? "r" : "r\nr");
                    add(doc, row, OL_ROLE_CELL, OL_DISPLAY_BLOCK, "s");
                    box = box_of(c);
                    box.flags |= OL_BOX_SIDE_BORDERS;
                    box.border_widths[2] = PX(1);
                    box.valign = next_rand(4);
                    ol_node_set_box(c, &box);
                    if (r < h) {
                        box = box_of(row);
                        box.flags |= OL_BOX_REPEAT_HEADER;
                        ol_node_set_box(row, &box);
                    }
                }
            } else if (what == 1) {
                ol_node *para = block(doc, NULL, "lorem ipsum dolor sit amet ");
                ol_node *ib = add(doc, para, OL_ROLE_BLOCK, OL_DISPLAY_INLINE_BLOCK, "box");
                span(doc, para, " consectetur adipiscing elit sed do eiusmod");
                box = box_of(ib);
                box.valign = next_rand(4);
                box.flags |= OL_BOX_SIDE_BORDERS;
                box.border_widths[0] = box.border_widths[3] = PX(2);
                ol_node_set_box(ib, &box);
            } else if (what == 2) {
                ol_node *para = block(doc, NULL, "a\nb\nc");
                ol_node *f = block(doc, para, "frame");
                box = box_of(f);
                box.flags |= OL_BOX_ABSOLUTE;
                box.frame.x = PX(20);
                box.frame.y = PX(10);
                box.frame.width = PX(60);
                box.anchor = OL_ANCHOR_X(OL_ANCHOR_BLOCK) | (next_rand(2) ? OL_ANCHOR_BEHIND : OL_ANCHOR_ON);
                ol_node_set_box(f, &box);
            } else {
                ol_node *b = block(doc, NULL, "one two three four five six seven");
                box = box_of(b);
                box.width_permille = 300 + next_rand(700);
                box.flags |= OL_BOX_AUTO_LEFT | (next_rand(2) ? OL_BOX_AUTO_RIGHT : 0);
                ol_node_set_box(b, &box);
            }
        }
        CHECK(ol_layout_pages(doc, &page, measure, NULL));
        for (p = 0; p < ol_page_count(doc); ++p) {
            size_t first, count, i;
            CHECK(ol_display_page_range(doc, p, &first, &count));
            CHECK(first == total);
            total += count;
            for (i = first; i < first + count; ++i) CHECK(ol_display_get(doc, i)->page == p);
        }
        CHECK(total == ol_display_count(doc));
        ol_document_free(doc);
    }
}

static void test_versions(void)
{
    ol_document *doc = ol_document_new();
    ol_node *n = block(doc, NULL, "a");
    ol_box_style old;
    /* a box style from gfopenlayout.library 8 (0.8's size) leaves 0.9's fields unset */
    ol_box_style_init(&old);
    old.size = (uint32_t)OL_BOX_STYLE_SIZE_0_8;
    old.width = PX(50);
    old.width_permille = 500;
    old.border_widths[0] = PX(3);
    old.valign = OL_VALIGN_BOTTOM;
    ol_node_set_box(n, &old);
    CHECK(ol_node_box(n)->width == PX(50));
    CHECK(ol_node_box(n)->width_permille == 0 && ol_node_box(n)->border_widths[0] == 0 &&
          ol_node_box(n)->valign == 0 && ol_node_box(n)->min_width == 0);
    CHECK(OL_BOX_STYLE_SIZE_0_8 == offsetof(ol_box_style, border_widths));
    CHECK(OL_HAS_WEB_BOXES == 1 && !strcmp(OL_VERSION, "0.9"));
    ol_document_free(doc);
}

int main(void)
{
    test_side_borders();
    test_widths();
    test_auto_margins();
    test_inline_blocks();
    test_cell_valign();
    test_repeat_header();
    test_repeat_random();
    test_versions();
    if (failures) {
        printf("OPENLAYOUT WEB FAIL (%d of %d)\n", failures, checks);
        return 20;
    }
    printf("OPENLAYOUT WEB PASS version=%s checks=%d\n", OL_VERSION, checks);
    return 0;
}
