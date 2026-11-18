/*
 * The document calls a program makes through gfopenlayout.library 24
 * (library/link/gfopenlayout_link_core.c): each of them once, with what it
 * answers. On the host this links the source itself, so the same checks say
 * what the library must answer; build-library.sh links it with
 * libgfopenlayout_link.a as LayoutCoreTestLib, which calls the library.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "gfopenlayout.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(x) do { ++checks; if (!(x)) { \
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

static void draw(void *userdata, const ol_device_op *op)
{
    unsigned *seen = (unsigned *)userdata;
    if (op && op->op) ++*seen;
}

static ol_node *para(ol_document *doc, ol_node *parent)
{
    ol_style s;
    ol_node *n = ol_node_append(doc, parent, OL_ROLE_PARAGRAPH, NULL);
    ol_style_init(&s);
    s.display = OL_DISPLAY_BLOCK;
    s.margin_top = OL_CSSPX(10);
    s.line_height = OL_CSSPX(20);
    ol_node_set_style(n, &s);
    return n;
}

static ol_node *text(ol_document *doc, ol_node *parent, ol_role role, const char *utf8)
{
    ol_style s;
    ol_node *n = ol_node_append(doc, parent, role, utf8);
    ol_style_init(&s);
    s.display = OL_DISPLAY_INLINE;
    s.font_size = OL_CSSPX(16);
    s.line_height = OL_CSSPX(20);
    ol_node_set_style(n, &s);
    return n;
}

int main(void)
{
    ol_document *doc = ol_document_new_reserved(64), *other = ol_document_new();
    const ol_node *root;
    ol_node *paras[24], *texts[24], *link, *frame, *image, *shape, *table, *row, *cell;
    ol_page page;
    ol_page_note notes[1];
    ol_page_section sections[2];
    ol_tab_stop stops[1];
    ol_unit widths[2];
    ol_box_style box;
    ol_shape_style shape_style;
    ol_image_crop crop = {100, 50, 200, 150};
    ol_scale scale;
    ol_rect at, r;
    ol_pixel_rect px, px2;
    ol_text_position pos;
    const ol_display_op *first_text = NULL, *first_shape = NULL;
    size_t first = 0, count = 0, k;
    int note_page[1], pg = -1, i, plain_pages;
    int32_t one;
    unsigned seen = 0;
    char line[64];

    CHECK(doc != NULL && other != NULL);
    if (!doc) return 20;
    ol_document_free(other);
    root = ol_document_root_const(doc);
    CHECK(root != NULL && ol_node_role(root) == OL_ROLE_DOCUMENT);

    /* ---- the tree and what a node keeps ---- */
    for (i = 0; i < 24; ++i) {
        paras[i] = para(doc, NULL);
        sprintf(line, "paragraph %02d words words words words words words", i);
        texts[i] = text(doc, paras[i], OL_ROLE_TEXT, line);
    }
    CHECK(ol_node_first_child(root) == paras[0]);
    CHECK(ol_node_next_sibling(paras[0]) == paras[1]);
    CHECK(ol_node_parent(paras[3]) == root && ol_node_parent(texts[3]) == paras[3]);
    CHECK(ol_node_parent(root) == NULL);
    CHECK(ol_node_id(paras[3]) != 0 && ol_node_by_id(doc, ol_node_id(paras[3])) == paras[3]);
    CHECK(ol_node_by_id(doc, 1000000) == NULL);
    CHECK(ol_node_role(texts[0]) == OL_ROLE_TEXT);
    CHECK(ol_node_text(texts[2]) && !strncmp(ol_node_text(texts[2]), "paragraph 02", 12));
    CHECK(ol_node_style(paras[0]) && ol_node_style(paras[0])->line_height == OL_CSSPX(20));
    CHECK(ol_node_style(texts[0])->font_size == OL_CSSPX(16));
    link = text(doc, paras[0], OL_ROLE_LINK, " link");
    CHECK(ol_node_set_href(link, "https://example.org/"));
    CHECK(ol_node_href(link) && !strcmp(ol_node_href(link), "https://example.org/"));
    stops[0].position = OL_CSSPX(100);
    stops[0].align = OL_TAB_RIGHT;
    stops[0].leader = 0;
    CHECK(ol_node_set_tab_stops(paras[1], 1, stops));

    /* ---- box styles: this program's size, a shorter older one, a longer later one ---- */
    memset(&box, 0xaa, sizeof box);
    ol_box_style_init(&box);
    CHECK(box.size == sizeof box && box.flags == 0 && box.border_sides == OL_BORDER_ALL);
    {
        union { ol_box_style b; unsigned char bytes[sizeof(ol_box_style) + 16]; } u;
        memset(&u, 0xaa, sizeof u);
        ol_box_style_init_sized(&u.b, OL_BOX_STYLE_SIZE_0_3);
        CHECK(u.b.size == OL_BOX_STYLE_SIZE_0_3 && u.b.border_sides == OL_BORDER_ALL);
        CHECK(u.bytes[OL_BOX_STYLE_SIZE_0_3] == 0xaa);
        memset(&u, 0xaa, sizeof u);
        ol_box_style_init_sized(&u.b, sizeof u);
        CHECK(u.b.size == sizeof(ol_box_style));
        CHECK(u.bytes[sizeof(ol_box_style)] == 0 && u.bytes[sizeof u - 1] == 0);
    }
    frame = ol_node_append(doc, paras[8], OL_ROLE_BLOCK, NULL);
    box.flags = OL_BOX_ABSOLUTE;
    box.frame.x = OL_CSSPX(5);
    box.frame.y = OL_CSSPX(3);
    box.frame.width = OL_CSSPX(40);
    box.frame.height = OL_CSSPX(30);
    box.anchor = OL_ANCHOR_ON;
    ol_node_set_box(frame, &box);
    CHECK(ol_node_box(frame) && ol_node_box(frame)->frame.width == OL_CSSPX(40));
    CHECK(ol_node_box(frame)->flags == OL_BOX_ABSOLUTE);
    image = ol_node_append(doc, frame, OL_ROLE_IMAGE, NULL);
    ol_node_set_image(image, 31, OL_CSSPX(20), OL_CSSPX(20));
    CHECK(ol_node_set_image_crop(image, &crop));

    shape = ol_node_append(doc, paras[2], OL_ROLE_BLOCK, NULL);
    ol_box_style_init(&box);
    box.width = OL_CSSPX(100);
    box.height = OL_CSSPX(60);
    ol_node_set_box(shape, &box);
    ol_shape_style_init(&shape_style);
    shape_style.type = OL_SHAPE_ELLIPSE;
    CHECK(ol_node_set_shape(shape, &shape_style, 0, NULL));
    {
        ol_rect bounds;
        ol_unit gap[4];
        ol_exclusion contour[8], ex;
        int bands;
        bounds.x = bounds.y = 0;
        bounds.width = OL_CSSPX(100);
        bounds.height = OL_CSSPX(60);
        gap[0] = gap[2] = 0;
        gap[1] = gap[3] = OL_CSSPX(2);
        bands = ol_node_shape_exclusions(shape, &bounds, gap, 8, contour, 8);
        CHECK(bands > 4 && bands <= 8);
        memset(&ex, 0, sizeof ex);
        ex.bounds = bounds;
        ex.wrap = OL_WRAP_SQUARE;
        CHECK(ol_node_set_exclusions(frame, 1, &ex));
        CHECK(ol_node_set_exclusions(frame, 0, NULL));
    }

    table = ol_node_append(doc, NULL, OL_ROLE_TABLE, NULL);
    widths[0] = OL_CSSPX(120);
    widths[1] = OL_CSSPX(200);
    CHECK(ol_node_set_columns(table, 2, widths));
    row = ol_node_append(doc, table, OL_ROLE_ROW, NULL);
    cell = ol_node_append(doc, row, OL_ROLE_CELL, NULL);
    text(doc, para(doc, cell), OL_ROLE_TEXT, "left");
    cell = ol_node_append(doc, row, OL_ROLE_CELL, NULL);
    text(doc, para(doc, cell), OL_ROLE_TEXT, "right");

    /* ---- one long page ---- */
    CHECK(ol_layout(doc, OL_CSSPX(400), measure, NULL));
    r = ol_node_bounds(paras[0]);
    CHECK(r.width > 0 && r.height >= OL_CSSPX(20));
    memset(&at, 0, sizeof at);
    ol_node_bounds_to(paras[0], &at);
    CHECK(!memcmp(&at, &r, sizeof r));
    CHECK(ol_node_bounds(paras[1]).y > r.y);
    count = ol_display_count(doc);
    CHECK(count >= 24);
    CHECK(ol_display_get(doc, 0) != NULL && ol_display_get(doc, count) == NULL);

    /* ---- pages ---- */
    page.width = OL_CSSPX(400);
    page.height = OL_CSSPX(300);
    page.margin_top = page.margin_bottom = OL_CSSPX(20);
    page.margin_left = page.margin_right = OL_CSSPX(20);
    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    plain_pages = ol_page_count(doc);
    CHECK(plain_pages >= 2);
    CHECK(ol_display_page_range(doc, 0, &first, &count) && count > 0);
    CHECK(!ol_display_page_range(doc, plain_pages + 5, &first, &count));
    CHECK(ol_node_page_box(doc, ol_node_id(paras[0]), &pg, &at) && pg == 0);
    CHECK(at.x >= page.margin_left && at.y >= page.margin_top);
    CHECK(!ol_node_page_box(doc, 1000000, &pg, &at));
    CHECK(ol_frame_place(doc, frame, &pg, &at) && at.width == OL_CSSPX(40));
    ol_display_page_range(doc, 0, &first, &count);
    for (k = first; k < first + count; ++k) {
        const ol_display_op *op = ol_display_get(doc, k);
        if (op && op->type == OL_OP_TEXT && !first_text) first_text = op;
        if (op && op->type == OL_OP_SHAPE && op->graphic && !first_shape) first_shape = op;
    }
    CHECK(first_text != NULL && first_shape != NULL);
    if (first_text) {
        ol_unit x = first_text->u.text.bounds.x + OL_CSSPX(12);
        ol_unit y = first_text->u.text.bounds.y + first_text->u.text.bounds.height / 2;
        ol_rect caret;
        CHECK(ol_hit_test_page(doc, 0, x, y) != NULL);
        CHECK(ol_hit_test_page(doc, plain_pages + 5, x, y) == NULL);
        memset(&pos, 0, sizeof pos);
        CHECK(ol_hit_text(doc, 0, x, y, measure, NULL, &pos));
        CHECK(pos.node_id != 0 && pos.offset >= 1 && pos.offset <= 2);
        pg = -1;
        CHECK(ol_caret_box(doc, &pos, measure, NULL, &pg, &caret));
        CHECK(pg == 0 && caret.height > 0 && caret.x >= first_text->u.text.bounds.x);
    }
    if (first_shape) {
        ol_point points[64];
        int closed = -1, n;
        n = ol_shape_path(first_shape->graphic, &first_shape->u.fill_rect.bounds, points, 64, &closed);
        CHECK(n > 4 && n <= 64 && closed == 1);
    }

    /* ---- footnotes, columns, sections ---- */
    notes[0].anchor = ol_node_id(texts[1]);
    notes[0].height = OL_CSSPX(40);
    note_page[0] = -1;
    CHECK(ol_layout_pages_notes(doc, &page, notes, 1, OL_CSSPX(10), note_page, measure, NULL));
    CHECK(note_page[0] == 0 && ol_page_count(doc) >= plain_pages);
    CHECK(ol_layout_pages_columns(doc, &page, 2, OL_CSSPX(20), measure, NULL));
    CHECK(ol_page_count(doc) >= 2);
    sections[0].first_node = ol_node_id(paras[0]);
    sections[0].columns = 1;
    sections[0].gap = 0;
    sections[0].break_before = OL_SECTION_CONTINUOUS;
    sections[1].first_node = ol_node_id(paras[6]);
    sections[1].columns = 2;
    sections[1].gap = OL_CSSPX(20);
    sections[1].break_before = OL_SECTION_NEW_PAGE;
    CHECK(ol_layout_pages_sections(doc, &page, sections, 2, measure, NULL));
    CHECK(ol_node_page_box(doc, ol_node_id(paras[6]), &pg, &at) && pg > 0);

    /* ---- pixels ---- */
    memset(&scale, 0, sizeof scale);
    ol_scale_set(&scale, 1, OL_UNITS_PER_CSSPX);         /* a pixel a CSS pixel */
    one = ol_scale_x(&scale, OL_CSSPX(10));
    CHECK(one == 10 && ol_scale_y(&scale, OL_CSSPX(10)) == one);
    ol_scale_set(&scale, 2, OL_UNITS_PER_CSSPX);
    CHECK(ol_scale_x(&scale, OL_CSSPX(10)) == 2 * one && ol_scale_y(&scale, OL_CSSPX(10)) == 2 * one);
    CHECK(ol_scale_length(&scale, OL_CSSPX(10)) == 2 * one && ol_scale_length(&scale, 0) == 0);
    CHECK(ol_scale_length(&scale, 1) == 1);
    CHECK(ol_unscale_x(&scale, 2 * one) == OL_CSSPX(10) && ol_unscale_y(&scale, 2 * one) == OL_CSSPX(10));
    r.x = OL_CSSPX(10);
    r.y = OL_CSSPX(20);
    r.width = OL_CSSPX(30);
    r.height = OL_CSSPX(40);
    px = ol_scale_rect(&scale, r);
    CHECK(px.x == 2 * one && px.y == 4 * one && px.width == 6 * one && px.height == 8 * one);
    memset(&px2, 0, sizeof px2);
    ol_scale_rect_to(&scale, &r, &px2);
    CHECK(!memcmp(&px, &px2, sizeof px));
    ol_scale_fit(&scale, OL_CSSPX(400), 200);
    CHECK(ol_scale_length(&scale, OL_CSSPX(400)) == 200);
    k = ol_draw_page(doc, 0, &scale, NULL, draw, &seen);
    CHECK(k > 0 && seen == (unsigned)k);

    ol_document_free(doc);
    if (failures) {
        printf("LINKCORE FAIL %d of %d\n", failures, checks);
        return 20;
    }
    printf("LINKCORE PASS %d checks\n", checks);
    return 0;
}
