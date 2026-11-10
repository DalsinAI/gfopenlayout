/* GFOpenLayout 0.18: grids (OL_BOX_GRID and the box style's grid_columns),
 * for MUI's column groups and GTK's tables.
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

static ol_node *grid(ol_document *doc, ol_node *parent, int columns, ol_unit gap, uint32_t align)
{
    ol_node *g = add(doc, parent, NULL, OL_BOX_GRID, 0, 0, 0);
    ol_box_style b;
    if (!g) return NULL;
    b = *ol_node_box(g);
    b.grid_columns = columns;
    b.gap = gap;
    b.align = align;
    ol_node_set_box(g, &b);
    return g;
}

int main(void)
{
    ol_document *doc = ol_document_new();
    ol_node *root, *g, *keys[16], *lab[3], *fld[3], *row, *small, *after, *hid;
    static const char *const names[16] = { "7", "8", "9", "/", "4", "5", "6", "*",
                                            "1", "2", "3", "-", "0", ".", "=", "+" };
    ol_rect r, r2;
    int i;

    CHECK(doc);
    root = ol_document_root(doc);

    /* a calculator's keys: 4 columns that all grow share 400 px less 3 gaps of 4 */
    g = grid(doc, root, 4, PX(4), OL_CROSS_STRETCH);
    CHECK(g);
    for (i = 0; i < 16; ++i) CHECK((keys[i] = add(doc, g, names[i], 0, 0, 0, 1)) != NULL);
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    for (i = 0; i < 16; ++i) {
        r = ol_node_bounds(keys[i]);
        CHECK(r.x == (i % 4) * (PX(97) + PX(4)));
        CHECK(r.width == PX(97));
        CHECK(r.y == (i / 4) * (PX(16) + PX(4)));
        CHECK(r.height == PX(16));
    }
    r = ol_node_bounds(g);
    CHECK(r.height == 4 * PX(16) + 3 * PX(4));

    /* a form: the labels' column as wide as the widest label, the fields' the rest */
    doc = ol_document_new();
    root = ol_document_root(doc);
    g = grid(doc, root, 2, PX(4), OL_CROSS_STRETCH);
    lab[0] = add(doc, g, "Name:", 0, 0, 0, 0);
    fld[0] = add(doc, g, "x", 0, 0, 0, 1);
    lab[1] = add(doc, g, "E-mail address:", 0, 0, 0, 0);
    fld[1] = add(doc, g, "x", 0, 0, PX(40), 1);        /* a taller field: its row is 40 high */
    lab[2] = add(doc, g, "Age:", 0, 0, 0, 0);
    fld[2] = add(doc, g, "x", 0, PX(30), 0, 0);        /* in the growing column, but 30 wide itself */
    CHECK(ol_layout(doc, PX(400), measure, NULL));
    for (i = 0; i < 3; ++i) {
        r = ol_node_bounds(lab[i]);
        r2 = ol_node_bounds(fld[i]);
        CHECK(r.x == 0 && r.width == PX(15 * 8));
        CHECK(r2.x == PX(124) && r2.width == PX(400 - 124));
        CHECK(r.y == r2.y && r.height == r2.height);   /* stretched to the row */
    }
    CHECK(ol_node_bounds(lab[1]).height == PX(40));
    CHECK(ol_node_bounds(lab[1]).y == PX(16 + 4));
    CHECK(ol_node_bounds(lab[2]).y == PX(16 + 4 + 40 + 4));

    /* align start: a short cell keeps its height; centre stands it in the middle */
    doc = ol_document_new();
    root = ol_document_root(doc);
    g = grid(doc, root, 2, 0, OL_CROSS_CENTRE);
    small = add(doc, g, "a", 0, 0, 0, 0);
    add(doc, g, "b", 0, 0, PX(40), 0);
    CHECK(ol_layout(doc, PX(200), measure, NULL));
    r = ol_node_bounds(small);
    CHECK(r.height == PX(16) && r.y == PX(12));

    /* in a row, a grid is as wide as its columns; hidden cells take no place */
    doc = ol_document_new();
    root = ol_document_root(doc);
    row = add(doc, root, NULL, OL_BOX_ROW, 0, 0, 0);
    g = grid(doc, row, 2, PX(2), OL_CROSS_STRETCH);
    add(doc, g, "abc", 0, 0, 0, 0);
    hid = add(doc, g, "hidden!", 0, 0, 0, 0);
    {
        ol_style s = *ol_node_style(hid);
        s.display = OL_DISPLAY_NONE;
        ol_node_set_style(hid, &s);
    }
    add(doc, g, "de", 0, 0, 0, 0);
    add(doc, g, "fghij", 0, 0, 0, 0);
    after = add(doc, row, "z", 0, 0, 0, 0);
    CHECK(ol_layout(doc, PX(300), measure, NULL));
    CHECK(ol_node_bounds(g).width == PX(40 + 2 + 16));    /* abc|de, fghij: columns 40 and 16 */
    CHECK(ol_node_bounds(after).x == PX(58));

    /* too little room: the columns give it up in proportion */
    doc = ol_document_new();
    root = ol_document_root(doc);
    g = grid(doc, root, 2, 0, OL_CROSS_STRETCH);
    lab[0] = add(doc, g, "aaaaaaaaaa", 0, 0, 0, 0);   /* 80 */
    fld[0] = add(doc, g, "bbbbbbbbbbbbbbbbbbbb", 0, 0, 0, 0);   /* 160 */
    CHECK(ol_layout(doc, PX(120), measure, NULL));
    CHECK(ol_node_bounds(lab[0]).width == PX(40));
    CHECK(ol_node_bounds(fld[0]).width == PX(80));
    CHECK(ol_node_bounds(fld[0]).x == PX(40));

    /* an old box size: no grid columns, so one column */
    {
        ol_box_style b;
        doc = ol_document_new();
        root = ol_document_root(doc);
        g = add(doc, root, NULL, 0, 0, 0, 0);
        ol_box_style_init(&b);
        b.size = OL_BOX_STYLE_SIZE_0_17;
        b.flags = OL_BOX_GRID;
        ol_node_set_box(g, &b);
        lab[0] = add(doc, g, "a", 0, 0, 0, 0);
        lab[1] = add(doc, g, "b", 0, 0, 0, 0);
        CHECK(ol_layout(doc, PX(100), measure, NULL));
        CHECK(ol_node_box(g)->grid_columns == 0);
        CHECK(ol_node_bounds(lab[1]).y == PX(16));
        CHECK(ol_node_bounds(lab[1]).x == 0);
    }
    printf("test_grid: ok\n");
    return 0;
}
