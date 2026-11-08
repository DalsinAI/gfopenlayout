/* GFOpenLayout 0.8: rows, widths, heights, grow and gap (OL_BOX_ROW and the
 * box style's width, height, grow, gap and align), for programs' windows.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); return 20; \
} } while (0)

/* every character 8 CSS px wide; a line 16 px */
static int measure(void *userdata, const char *utf8, size_t length,
                   const ol_style *style, ol_text_metrics *m)
{
    (void)userdata;
    (void)utf8;
    (void)style;
    m->width = (ol_unit)length * OL_CSSPX(8);
    m->ascent = OL_CSSPX(12);
    m->descent = OL_CSSPX(4);
    return 1;
}

static ol_style text_style(void)
{
    ol_style s;
    ol_style_init(&s);
    s.font_size = OL_CSSPX(16);
    s.line_height = OL_CSSPX(16);
    return s;
}

static ol_node *add(ol_document *doc, ol_node *parent, ol_role role, const char *text,
                    uint32_t flags, ol_unit width, ol_unit height, int grow)
{
    ol_style s = text_style();
    ol_box_style b;
    ol_node *n = ol_node_append(doc, parent, role, text);
    if (!n) return NULL;
    s.display = OL_DISPLAY_BLOCK;
    ol_node_set_style(n, &s);
    ol_box_style_init(&b);
    b.flags = flags;
    b.width = width;
    b.height = height;
    b.grow = grow;
    ol_node_set_box(n, &b);
    return n;
}

static void set_box(ol_node *n, ol_unit gap, uint32_t align)
{
    ol_box_style b = *ol_node_box(n);
    b.gap = gap;
    b.align = align;
    ol_node_set_box(n, &b);
}

int main(void)
{
    ol_document *doc = ol_document_new();
    ol_node *root, *row, *a, *b, *c, *col, *top, *list, *bottom, *tall;
    ol_rect r;
    ol_style s;
    ol_box_style old;

    /* blocks in blocks, far deeper than the stack: left out past the limit */
    {
        ol_document *deep = ol_document_new();
        ol_node *n;
        int i;
        CHECK(deep);
        n = ol_document_root(deep);
        for (i = 0; i < 200000; ++i) CHECK(n = ol_node_append(deep, n, OL_ROLE_BLOCK, NULL));
        CHECK(ol_layout(deep, OL_CSSPX(400), measure, NULL));
        ol_document_free(deep);
    }

    CHECK(doc);
    root = ol_document_root(doc);

    /* a toolbar: two buttons as wide as their text, a gap, a field that takes the rest */
    row = add(doc, root, OL_ROLE_BLOCK, NULL, OL_BOX_ROW, 0, 0, 0);
    set_box(row, OL_CSSPX(4), OL_CROSS_STRETCH);
    a = add(doc, row, OL_ROLE_BUTTON, "Open", 0, 0, 0, 0);
    b = add(doc, row, OL_ROLE_BUTTON, "Save", 0, 0, 0, 0);
    c = add(doc, row, OL_ROLE_INPUT, "x", 0, 0, 0, 1);
    CHECK(a && b && c);
    CHECK(ol_layout(doc, OL_CSSPX(400), measure, NULL));
    r = ol_node_bounds(a);
    CHECK(r.x == 0 && r.width == OL_CSSPX(32) && r.height == OL_CSSPX(16));
    r = ol_node_bounds(b);
    CHECK(r.x == OL_CSSPX(36) && r.width == OL_CSSPX(32));
    r = ol_node_bounds(c);
    CHECK(r.x == OL_CSSPX(72) && r.width == OL_CSSPX(400 - 72));
    r = ol_node_bounds(row);
    CHECK(r.height == OL_CSSPX(16));
    CHECK(ol_hit_test(doc, OL_CSSPX(40), OL_CSSPX(8)) == b);
    CHECK(ol_hit_test(doc, OL_CSSPX(390), OL_CSSPX(8)) == c);

    /* two that grow share the room 1:3; a width is kept */
    {
        ol_box_style bb = *ol_node_box(a);
        bb.grow = 1;
        ol_node_set_box(a, &bb);
        bb = *ol_node_box(c);
        bb.grow = 3;
        ol_node_set_box(c, &bb);
        bb = *ol_node_box(b);
        bb.width = OL_CSSPX(50);
        ol_node_set_box(b, &bb);
    }
    CHECK(ol_layout(doc, OL_CSSPX(400), measure, NULL));
    /* left: 400 - (32 + 50 + 8) - 8 = 302 px; a gets a quarter (75.5), c the rest */
    CHECK(ol_node_bounds(a).width == OL_CSSPX(32) + OL_CSSPX(302) / 4);
    CHECK(ol_node_bounds(b).width == OL_CSSPX(50));
    CHECK(ol_node_bounds(c).width == OL_CSSPX(8) + OL_CSSPX(302) - OL_CSSPX(302) / 4);
    CHECK(ol_node_bounds(c).x + ol_node_bounds(c).width == OL_CSSPX(400));

    /* too little room: those without a width give it up, in proportion */
    CHECK(ol_layout(doc, OL_CSSPX(80), measure, NULL));
    CHECK(ol_node_bounds(b).width == OL_CSSPX(50));
    CHECK(ol_node_bounds(a).width + ol_node_bounds(c).width == OL_CSSPX(80 - 50 - 8));

    ol_document_free(doc);

    /* stretch, centre and end across a row */
    doc = ol_document_new();
    root = ol_document_root(doc);
    row = add(doc, root, OL_ROLE_BLOCK, NULL, OL_BOX_ROW, 0, 0, 0);
    tall = add(doc, row, OL_ROLE_BLOCK, "one\ntwo\nthree", 0, 0, 0, 0);
    a = add(doc, row, OL_ROLE_BUTTON, "OK", 0, 0, 0, 0);
    s = text_style();
    s.display = OL_DISPLAY_BLOCK;
    s.background = 0xff336699u;
    ol_node_set_style(a, &s);
    CHECK(ol_layout(doc, OL_CSSPX(300), measure, NULL));
    CHECK(ol_node_bounds(tall).height == OL_CSSPX(48));
    CHECK(ol_node_bounds(a).height == OL_CSSPX(48));      /* stretched */
    {
        size_t i, fills = 0;
        for (i = 0; i < ol_display_count(doc); ++i) {
            const ol_display_op *op = ol_display_get(doc, i);
            if (op->type == OL_OP_FILL_RECT && op->u.fill_rect.node_id == ol_node_id(a)) {
                CHECK(op->u.fill_rect.bounds.height == OL_CSSPX(48));
                ++fills;
            }
        }
        CHECK(fills == 1);                                  /* the first pass's taken back */
    }
    set_box(row, 0, OL_CROSS_CENTRE);
    CHECK(ol_layout(doc, OL_CSSPX(300), measure, NULL));
    CHECK(ol_node_bounds(a).height == OL_CSSPX(16));
    CHECK(ol_node_bounds(a).y == OL_CSSPX(16));
    set_box(row, 0, OL_CROSS_END);
    CHECK(ol_layout(doc, OL_CSSPX(300), measure, NULL));
    CHECK(ol_node_bounds(a).y == OL_CSSPX(32));
    /* a text op moves with its box */
    {
        size_t i;
        for (i = 0; i < ol_display_count(doc); ++i) {
            const ol_display_op *op = ol_display_get(doc, i);
            if (op->type == OL_OP_TEXT && op->u.text.node_id == ol_node_id(a))
                CHECK(op->u.text.bounds.y == OL_CSSPX(32));
        }
    }
    /* nothing grows: the row's text_align puts them at its end */
    set_box(row, 0, OL_CROSS_START);
    s = text_style();
    s.display = OL_DISPLAY_BLOCK;
    s.text_align = OL_ALIGN_END;
    ol_node_set_style(row, &s);
    CHECK(ol_layout(doc, OL_CSSPX(300), measure, NULL));
    CHECK(ol_node_bounds(a).x + ol_node_bounds(a).width == OL_CSSPX(300));
    ol_document_free(doc);

    /* a window: a fixed height, a list that grows to fill what the rows leave */
    doc = ol_document_new();
    root = ol_document_root(doc);
    col = add(doc, root, OL_ROLE_BLOCK, NULL, 0, 0, OL_CSSPX(200), 0);
    set_box(col, OL_CSSPX(6), OL_CROSS_STRETCH);
    top = add(doc, col, OL_ROLE_BLOCK, "Folders", 0, 0, 0, 0);
    list = add(doc, col, OL_ROLE_LIST, "item", 0, 0, 0, 1);
    bottom = add(doc, col, OL_ROLE_BLOCK, NULL, OL_BOX_ROW, 0, 0, 0);
    a = add(doc, bottom, OL_ROLE_BUTTON, "OK", 0, 0, 0, 1);
    b = add(doc, bottom, OL_ROLE_BUTTON, "Cancel", 0, 0, 0, 1);
    CHECK(top && list && a && b);
    CHECK(ol_layout(doc, OL_CSSPX(300), measure, NULL));
    CHECK(ol_node_bounds(col).height == OL_CSSPX(200));
    CHECK(ol_node_bounds(top).y == 0);
    CHECK(ol_node_bounds(list).y == OL_CSSPX(22));
    CHECK(ol_node_bounds(list).height == OL_CSSPX(200 - 16 - 16 - 12));
    CHECK(ol_node_bounds(bottom).y == OL_CSSPX(200 - 16));
    /* the two buttons share the row equally (grow 1 each, after their text) */
    CHECK(ol_node_bounds(a).width + ol_node_bounds(b).width == OL_CSSPX(300));
    CHECK(ol_node_bounds(b).width - ol_node_bounds(a).width == OL_CSSPX(32));
    CHECK(ol_document_content_height(doc) == OL_CSSPX(200));

    /* a box style from gfopenlayout.library 7 (0.7's size) leaves 0.8's fields unset */
    ol_box_style_init(&old);
    old.size = (uint32_t)OL_BOX_STYLE_SIZE_0_7;
    old.width = OL_CSSPX(99);
    ol_node_set_box(a, &old);
    CHECK(ol_node_box(a)->width == 0 && ol_node_box(a)->grow == 0);
    ol_document_free(doc);

    printf("OPENLAYOUT ROWS PASS version=%s\n", OL_VERSION);
    return 0;
}
