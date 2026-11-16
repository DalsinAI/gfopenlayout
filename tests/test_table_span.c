/*
 * GFOpenLayout 0.21's tables: a cell down more than one row (box.row_span)
 * and a row's least height (a row's box.height), for a slide's and a
 * document's merged cells and fixed row heights, and CSS's rowspan and a
 * row's height.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "gfopenlayout.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(x) do { if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); ++failures; \
} } while (0)

/* 8 px a character; ascent three quarters of the size */
static int measure(void *userdata, const char *utf8, size_t length,
                   const ol_style *style, ol_text_metrics *m)
{
    size_t i, glyphs = 0;
    (void)userdata;
    for (i = 0; i < length; ++i)
        if (((unsigned char)utf8[i] & 0xc0u) != 0x80u) ++glyphs;
    m->width = (ol_unit)glyphs * OL_CSSPX(8);
    m->ascent = style->font_size * 3 / 4;
    m->descent = style->font_size - m->ascent;
    return 1;
}

static ol_node *block(ol_document *doc, ol_node *parent, ol_role role, uint32_t background)
{
    ol_style s;
    ol_node *n = ol_node_append(doc, parent, role, NULL);
    ol_style_init(&s);
    s.display = OL_DISPLAY_BLOCK;
    s.background = background;
    ol_node_set_style(n, &s);
    return n;
}

/* a cell of 16 px text, 20 px a line */
static ol_node *cell(ol_document *doc, ol_node *row, const char *utf8, int rows, uint32_t background)
{
    ol_style s;
    ol_box_style box;
    ol_node *c = block(doc, row, OL_ROLE_CELL, background), *t;
    ol_box_style_init(&box);
    box.border_width = OL_CSSPX(1);
    box.row_span = rows;
    ol_node_set_box(c, &box);
    t = ol_node_append(doc, c, OL_ROLE_TEXT, utf8);
    ol_style_init(&s);
    s.display = OL_DISPLAY_INLINE;
    s.font_size = OL_CSSPX(16);
    s.line_height = OL_CSSPX(20);
    ol_node_set_style(t, &s);
    return c;
}

static void row_height(ol_node *row, ol_unit height)
{
    ol_box_style box;
    ol_box_style_init(&box);
    box.height = height;
    ol_node_set_box(row, &box);
}

/* the fill rects drawn for a node */
static size_t fills(const ol_document *doc, const ol_node *n, ol_rect *out, size_t max)
{
    size_t i, k = 0;
    for (i = 0; i < ol_display_count(doc); ++i) {
        const ol_display_op *op = ol_display_get(doc, i);
        if (op->type == OL_OP_FILL_RECT && op->u.fill_rect.node_id == ol_node_id(n) && k < max)
            out[k++] = op->u.fill_rect.bounds;
    }
    return k;
}

/* the place in the display list of a node's first fill */
static size_t fill_at(const ol_document *doc, const ol_node *n)
{
    size_t i;
    for (i = 0; i < ol_display_count(doc); ++i) {
        const ol_display_op *op = ol_display_get(doc, i);
        if (op->type == OL_OP_FILL_RECT && op->u.fill_rect.node_id == ol_node_id(n))
            return i;
    }
    return (size_t)-1;
}

/* three columns of 100 px: a cell down two rows, the rows after it skip
 * its column, the last row it crosses grows to hold it, and a row of a
 * least height */
static void test_row_span(void)
{
    ol_document *doc = ol_document_new();
    ol_unit widths[3] = { OL_CSSPX(100), OL_CSSPX(100), OL_CSSPX(100) };
    ol_node *table, *r1, *r2, *r3, *c11, *c12, *c13, *c22, *c23, *c31, *c32;
    ol_rect b, rf[4];
    size_t n, i;

    table = block(doc, NULL, OL_ROLE_TABLE, 0);
    CHECK(ol_node_set_columns(table, 3, widths));
    r1 = block(doc, table, OL_ROLE_ROW, 0);
    /* 6 words of 4, two a line in 100 px: 3 lines, 60 px */
    c11 = cell(doc, r1, "aaaa bbbb cccc dddd eeee ffff", 2, 0xffddeeffu);
    c12 = cell(doc, r1, "x", 0, 0);
    c13 = cell(doc, r1, "y", 1, 0);
    r2 = block(doc, table, OL_ROLE_ROW, 0xffeeeeeeu);
    c22 = cell(doc, r2, "p", 0, 0);
    c23 = cell(doc, r2, "q", 0, 0);
    r3 = block(doc, table, OL_ROLE_ROW, 0);
    row_height(r3, OL_CSSPX(50));
    c31 = cell(doc, r3, "z", 0, 0);
    c32 = cell(doc, r3, "w", 0, 0);  /* across two columns, down one row */
    {
        ol_box_style box = *ol_node_box(c32);
        box.column_span = 2;
        box.row_span = 0;
        ol_node_set_box(c32, &box);
    }
    CHECK(ol_layout(doc, OL_CSSPX(300), measure, NULL));

    /* the spanning cell: column 0, from row 1's top to row 2's foot */
    b = ol_node_bounds(c11);
    CHECK(b.x == 0 && b.y == 0 && b.width == OL_CSSPX(100));
    CHECK(b.height == OL_CSSPX(60));
    /* row 1 is its own cells' height; row 2 grows to what the span needs */
    CHECK(ol_node_bounds(r1).height == OL_CSSPX(20));
    CHECK(ol_node_bounds(r2).y == OL_CSSPX(20));
    CHECK(ol_node_bounds(r2).height == OL_CSSPX(40));
    CHECK(ol_node_bounds(c12).x == OL_CSSPX(100) && ol_node_bounds(c13).x == OL_CSSPX(200));
    /* row 2's cells skip column 0 */
    CHECK(ol_node_bounds(c22).x == OL_CSSPX(100) && ol_node_bounds(c22).width == OL_CSSPX(100));
    CHECK(ol_node_bounds(c23).x == OL_CSSPX(200));
    CHECK(ol_node_bounds(c22).y == OL_CSSPX(20) && ol_node_bounds(c22).height == OL_CSSPX(40));
    /* the spanning cell's fill is its whole height */
    n = fills(doc, c11, rf, 4);
    CHECK(n == 1 && rf[0].height == OL_CSSPX(60));
    /* row 2's own background leaves column 0, which the span fills, and
     * comes before the span's cells are finished */
    n = fills(doc, r2, rf, 4);
    CHECK(n >= 1);
    for (i = 0; i < n; ++i) {
        CHECK(rf[i].x >= OL_CSSPX(100));
        CHECK(rf[i].y == OL_CSSPX(20) && rf[i].height == OL_CSSPX(40));
    }
    CHECK(n == 1 && rf[0].width == OL_CSSPX(200));
    /* row 3: at least 50 px, its cells as tall */
    CHECK(ol_node_bounds(r3).y == OL_CSSPX(60));
    CHECK(ol_node_bounds(r3).height == OL_CSSPX(50));
    CHECK(ol_node_bounds(c31).height == OL_CSSPX(50));
    CHECK(ol_node_bounds(c32).x == OL_CSSPX(100) && ol_node_bounds(c32).width == OL_CSSPX(200));
    CHECK(ol_document_content_height(doc) == OL_CSSPX(110));
    ol_document_free(doc);
}

/* a span in the middle column, with the row's background either side;
 * a row taller than its least height keeps its content's */
static void test_row_span_middle(void)
{
    ol_document *doc = ol_document_new();
    ol_unit widths[3] = { OL_CSSPX(100), OL_CSSPX(100), OL_CSSPX(100) };
    ol_node *table, *r1, *r2, *c12, *c21, *c23;
    ol_rect rf[4];
    size_t n;

    table = block(doc, NULL, OL_ROLE_TABLE, 0);
    CHECK(ol_node_set_columns(table, 3, widths));
    r1 = block(doc, table, OL_ROLE_ROW, 0);
    row_height(r1, OL_CSSPX(10));
    cell(doc, r1, "aaaa bbbb cccc", 0, 0);  /* 2 lines: 40 px, over the least */
    c12 = cell(doc, r1, "m", 2, 0);
    cell(doc, r1, "n", 0, 0);
    r2 = block(doc, table, OL_ROLE_ROW, 0xff00ff00u);
    c21 = cell(doc, r2, "s", 0, 0);
    c23 = cell(doc, r2, "t", 0, 0);
    CHECK(ol_layout(doc, OL_CSSPX(300), measure, NULL));
    CHECK(ol_node_bounds(r1).height == OL_CSSPX(40));
    CHECK(ol_node_bounds(c12).height == OL_CSSPX(60));
    CHECK(ol_node_bounds(c21).x == 0 && ol_node_bounds(c23).x == OL_CSSPX(200));
    n = fills(doc, r2, rf, 4);
    CHECK(n == 2);
    CHECK(n == 2 && rf[0].x == 0 && rf[0].width == OL_CSSPX(100));
    CHECK(n == 2 && rf[1].x == OL_CSSPX(200) && rf[1].width == OL_CSSPX(100));
    /* the row's fills go under its cells */
    CHECK(fill_at(doc, r2) < ol_display_count(doc));
    ol_document_free(doc);
}

/* without the table's columns, a row's cells after a span from above go
 * in the columns the span leaves; a span past the last row ends with the
 * table */
static void test_row_span_auto_columns(void)
{
    ol_document *doc = ol_document_new();
    ol_node *table, *r1, *r2, *c11, *c12, *c22;

    table = block(doc, NULL, OL_ROLE_TABLE, 0);
    r1 = block(doc, table, OL_ROLE_ROW, 0);
    c11 = cell(doc, r1, "a", 5, 0);
    c12 = cell(doc, r1, "b", 0, 0);
    r2 = block(doc, table, OL_ROLE_ROW, 0);
    c22 = cell(doc, r2, "c", 0, 0);
    CHECK(ol_layout(doc, OL_CSSPX(200), measure, NULL));
    CHECK(ol_node_bounds(c22).x == ol_node_bounds(c12).x);
    CHECK(ol_node_bounds(c22).x > 0);
    CHECK(ol_node_bounds(c11).height == OL_CSSPX(40));
    CHECK(ol_node_bounds(r2).height == OL_CSSPX(20));
    ol_document_free(doc);
}

/* a box style of 0.20's size has no row span, as ol_node_set_box copies
 * only its size */
static void test_old_box_size(void)
{
    ol_document *doc = ol_document_new();
    ol_unit widths[2] = { OL_CSSPX(100), OL_CSSPX(100) };
    ol_node *table, *r1, *r2, *c11, *c21;
    ol_box_style box;

    CHECK(OL_BOX_STYLE_SIZE_0_20 == offsetof(ol_box_style, row_span));
    table = block(doc, NULL, OL_ROLE_TABLE, 0);
    CHECK(ol_node_set_columns(table, 2, widths));
    r1 = block(doc, table, OL_ROLE_ROW, 0);
    c11 = cell(doc, r1, "a", 0, 0);
    ol_box_style_init(&box);
    box.size = OL_BOX_STYLE_SIZE_0_20;
    box.row_span = 2;
    ol_node_set_box(c11, &box);
    CHECK(ol_node_box(c11)->row_span == 0);
    cell(doc, r1, "b", 0, 0);
    r2 = block(doc, table, OL_ROLE_ROW, 0);
    c21 = cell(doc, r2, "c", 0, 0);
    CHECK(ol_layout(doc, OL_CSSPX(200), measure, NULL));
    CHECK(ol_node_bounds(c21).x == 0);
    CHECK(ol_node_bounds(c11).height == OL_CSSPX(20));
    ol_document_free(doc);
}

int main(void)
{
    test_row_span();
    test_row_span_middle();
    test_row_span_auto_columns();
    test_old_box_size();
    if (failures) {
        printf("test_table_span: %d failed\n", failures);
        return 1;
    }
    printf("test_table_span: ok\n");
    return 0;
}
