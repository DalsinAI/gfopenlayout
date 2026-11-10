/* GFOpenLayout 0.18: the same size for every child (OL_BOX_SAME_WIDTH and
 * OL_BOX_SAME_HEIGHT), for MUI's SameSize, SameWidth and SameHeight.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); return 20; \
} } while (0)
#define PX OL_CSSPX

/* every character 8 CSS px wide; a line 16 px */
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

static ol_node *add(ol_document *doc, ol_node *parent, const char *text, uint32_t flags,
                    ol_unit width, ol_unit height, int grow)
{
    ol_style s;
    ol_box_style b;
    ol_node *n = ol_node_append(doc, parent, OL_ROLE_BLOCK, text);
    if (!n) return NULL;
    ol_style_init(&s);
    s.font_size = PX(16);
    s.line_height = PX(16);
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

static void set_flags(ol_node *n, uint32_t add_flags, uint32_t align)
{
    ol_box_style b = *ol_node_box(n);
    b.flags |= add_flags;
    b.align = align;
    ol_node_set_box(n, &b);
}

int main(void)
{
    ol_document *doc;
    ol_node *root, *row, *g, *blk, *a, *b, *c, *after;
    int i;

    /* a row of buttons that keep their own width: each as wide as the widest, nothing grows */
    doc = ol_document_new();
    CHECK(doc);
    root = ol_document_root(doc);
    row = add(doc, root, NULL, OL_BOX_ROW, 0, 0, 0);
    set_flags(row, OL_BOX_SAME_WIDTH, OL_CROSS_START);
    {
        ol_box_style bs = *ol_node_box(row);
        bs.gap = PX(4);
        ol_node_set_box(row, &bs);
    }
    a = add(doc, row, "OK", 0, 0, 0, 0);            /* 16 */
    b = add(doc, row, "Cancel", 0, 0, 0, 0);        /* 48 */
    c = add(doc, row, "Use", 0, 0, 0, 0);           /* 24 */
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    CHECK(ol_node_bounds(a).width == PX(48));
    CHECK(ol_node_bounds(b).width == PX(48));
    CHECK(ol_node_bounds(c).width == PX(48));
    CHECK(ol_node_bounds(b).x == PX(52));
    CHECK(ol_node_bounds(c).x == PX(104));

    /* in a row of its own, a same-width row is as wide as n of the widest */
    doc = ol_document_new();
    root = ol_document_root(doc);
    row = add(doc, root, NULL, OL_BOX_ROW, 0, 0, 0);
    g = add(doc, row, NULL, OL_BOX_ROW, 0, 0, 0);
    set_flags(g, OL_BOX_SAME_WIDTH, OL_CROSS_STRETCH);
    add(doc, g, "a", 0, 0, 0, 0);
    add(doc, g, "bbbb", 0, 0, 0, 0);
    after = add(doc, row, "z", 0, 0, 0, 0);
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    CHECK(ol_node_bounds(g).width == PX(64));
    CHECK(ol_node_bounds(after).x == PX(64));

    /* same height in a row with align start: as tall as the row anyway */
    doc = ol_document_new();
    root = ol_document_root(doc);
    row = add(doc, root, NULL, OL_BOX_ROW, 0, 0, 0);
    set_flags(row, OL_BOX_SAME_HEIGHT, OL_CROSS_START);
    a = add(doc, row, "a", 0, 0, 0, 0);
    b = add(doc, row, "b", 0, 0, PX(40), 0);
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    CHECK(ol_node_bounds(a).height == PX(40));

    /* a grid with same width: every column as wide as the widest */
    doc = ol_document_new();
    root = ol_document_root(doc);
    g = add(doc, root, NULL, OL_BOX_GRID, 0, 0, 0);
    {
        ol_box_style bs = *ol_node_box(g);
        bs.grid_columns = 3;
        bs.flags |= OL_BOX_SAME_WIDTH | OL_BOX_SAME_HEIGHT;
        bs.align = OL_CROSS_START;
        ol_node_set_box(g, &bs);
    }
    a = add(doc, g, "a", 0, 0, 0, 0);                       /* 8 */
    b = add(doc, g, "bbbbb", 0, 0, 0, 0);                   /* 40 */
    c = add(doc, g, "cc", 0, 0, 0, 0);                      /* 16 */
    add(doc, g, "d", 0, 0, PX(30), 0);                      /* the tallest, in row 2 */
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    for (i = 0; i < 3; ++i) {
        ol_node *cell = i == 0 ? a : i == 1 ? b : c;
        CHECK(ol_node_bounds(cell).width == PX(40));       /* none grows: each the widest's 40 */
        CHECK(ol_node_bounds(cell).x == i * PX(40));
    }
    CHECK(ol_node_bounds(a).height == PX(30));              /* row 1 as tall as row 2 */
    CHECK(ol_node_bounds(c).height == PX(30));

    /* a block: every child as tall as the tallest */
    doc = ol_document_new();
    root = ol_document_root(doc);
    blk = add(doc, root, NULL, 0, 0, 0, 0);
    set_flags(blk, OL_BOX_SAME_HEIGHT, OL_CROSS_STRETCH);
    a = add(doc, blk, "one", 0, 0, 0, 0);
    b = add(doc, blk, "two", 0, 0, PX(50), 0);
    c = add(doc, blk, "three", 0, 0, 0, 0);
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    CHECK(ol_node_bounds(a).height == PX(50));
    CHECK(ol_node_bounds(c).height == PX(50));
    CHECK(ol_node_bounds(c).y == PX(100));
    CHECK(ol_node_bounds(blk).height == PX(150));

    /* neither flag: nothing changes */
    doc = ol_document_new();
    root = ol_document_root(doc);
    row = add(doc, root, NULL, OL_BOX_ROW, 0, 0, 0);
    set_flags(row, 0, OL_CROSS_START);
    a = add(doc, row, "OK", 0, 0, 0, 0);
    b = add(doc, row, "Cancel", 0, 0, PX(40), 0);
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    CHECK(ol_node_bounds(a).width == PX(16) && ol_node_bounds(a).height == PX(16));
    printf("test_samesize: ok\n");
    return 0;
}
