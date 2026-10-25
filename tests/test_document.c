/*
 * OpenLayout 0.2's document layout: alignment and justification, shared
 * baselines, font numbers, grid tables, frames, pages, and the caret and
 * character hit-testing an editor needs.
 * MIT, Copyright (c) 2026 AmigaChrome Team.
 */
#include "openlayout.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(x) do { if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); ++failures; \
} } while (0)

/* 8 px a character, or 10 px in font 1; ascent three quarters of the size */
static int measure(void *userdata, const char *utf8, size_t length,
                   const ol_style *style, ol_text_metrics *m)
{
    size_t i, glyphs = 0;
    ol_unit per = OL_TEXT_FONT_OF(style->text_flags) == 1 ? OL_CSSPX(10) : OL_CSSPX(8);
    (void)userdata;
    for (i = 0; i < length; ++i)
        if (((unsigned char)utf8[i] & 0xc0u) != 0x80u) ++glyphs;
    m->width = (ol_unit)glyphs * per;
    m->ascent = style->font_size * 3 / 4;
    m->descent = style->font_size - m->ascent;
    return 1;
}

static ol_node *block(ol_document *doc, ol_node *parent, ol_role role, ol_text_align align)
{
    ol_style s;
    ol_node *n = ol_node_append(doc, parent, role, NULL);
    ol_style_init(&s);
    s.display = OL_DISPLAY_BLOCK;
    s.text_align = align;
    ol_node_set_style(n, &s);
    return n;
}

static ol_node *text(ol_document *doc, ol_node *parent, const char *utf8, ol_unit size, unsigned font)
{
    ol_style s;
    ol_node *n = ol_node_append(doc, parent, OL_ROLE_TEXT, utf8);
    ol_style_init(&s);
    s.display = OL_DISPLAY_INLINE;
    s.font_size = size;
    s.line_height = size + size / 4;
    s.text_flags = OL_TEXT_FONT(font);
    ol_node_set_style(n, &s);
    return n;
}

/* the text ops of one node, in order */
static size_t text_ops(const ol_document *doc, ol_id id, const ol_display_op **out, size_t max)
{
    size_t i, n = 0;
    for (i = 0; i < ol_display_count(doc); ++i) {
        const ol_display_op *op = ol_display_get(doc, i);
        if (op->type == OL_OP_TEXT && op->u.text.node_id == id && n < max) out[n++] = op;
    }
    return n;
}

static void test_alignment(void)
{
    ol_document *doc = ol_document_new();
    ol_node *p, *t;
    const ol_display_op *ops[16];
    size_t n;
    /* 120 px wide: "aaa bbb ccc dddd" is 16 characters, 128 px */
    p = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_JUSTIFY);
    t = text(doc, p, "aaa bbb ccc dddd eee", OL_CSSPX(16), 0);
    CHECK(ol_layout(doc, OL_CSSPX(120), measure, NULL));
    n = text_ops(doc, ol_node_id(t), ops, 16);
    CHECK(n == 5);
    /* line 1 is aaa bbb ccc (88 px without its last space), widened to 120 */
    CHECK(ops[0]->u.text.bounds.x == 0);
    CHECK(ops[2]->u.text.bounds.x + OL_CSSPX(24) == OL_CSSPX(120));
    CHECK(ops[1]->u.text.bounds.x > OL_CSSPX(32));
    /* line 2 is the last: not widened */
    CHECK(ops[3]->u.text.bounds.y > ops[0]->u.text.bounds.y);
    CHECK(ops[3]->u.text.bounds.x == 0);
    CHECK(ops[4]->u.text.bounds.x == OL_CSSPX(40));
    ol_document_free(doc);

    doc = ol_document_new();
    p = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_END);
    t = text(doc, p, "ab cd", OL_CSSPX(16), 0);
    CHECK(ol_layout(doc, OL_CSSPX(100), measure, NULL));
    n = text_ops(doc, ol_node_id(t), ops, 16);
    CHECK(n == 2 && ops[1]->u.text.bounds.x + OL_CSSPX(16) == OL_CSSPX(100));
    ol_document_free(doc);

    doc = ol_document_new();
    p = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_CENTER);
    t = text(doc, p, "abcd", OL_CSSPX(16), 0);
    CHECK(ol_layout(doc, OL_CSSPX(100), measure, NULL));
    n = text_ops(doc, ol_node_id(t), ops, 16);
    CHECK(n == 1 && ops[0]->u.text.bounds.x == OL_CSSPX(34));
    ol_document_free(doc);
}

static void test_baseline_and_fonts(void)
{
    ol_document *doc = ol_document_new();
    ol_node *p, *small, *big, *wide;
    const ol_display_op *a[4], *b[4], *c[4];
    p = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    small = text(doc, p, "small ", OL_CSSPX(12), 0);
    big = text(doc, p, "BIG ", OL_CSSPX(32), 0);
    wide = text(doc, p, "wide", OL_CSSPX(12), 1);
    CHECK(ol_layout(doc, OL_CSSPX(400), measure, NULL));
    CHECK(text_ops(doc, ol_node_id(small), a, 4) == 1);
    CHECK(text_ops(doc, ol_node_id(big), b, 4) == 1);
    CHECK(text_ops(doc, ol_node_id(wide), c, 4) == 1);
    /* one line, one baseline, as tall as its tallest */
    CHECK(a[0]->u.text.baseline == b[0]->u.text.baseline);
    CHECK(c[0]->u.text.baseline == b[0]->u.text.baseline);
    CHECK(a[0]->u.text.bounds.height == OL_CSSPX(40));
    CHECK(a[0]->u.text.baseline == OL_CSSPX(4) + OL_CSSPX(24));
    /* the font number reaches the measure callback and the renderer */
    CHECK(OL_TEXT_FONT_OF(c[0]->u.text.style.text_flags) == 1);
    CHECK(c[0]->u.text.bounds.width == OL_CSSPX(40));
    CHECK(c[0]->u.text.bounds.x == OL_CSSPX(48 + 32));
    ol_document_free(doc);
}

static void test_table(void)
{
    ol_document *doc = ol_document_new();
    ol_node *table, *r1, *r2, *c11, *c12, *c21, *t;
    ol_unit widths[2] = { OL_CSSPX(100), OL_CSSPX(200) };
    ol_box_style box;
    ol_style s;
    size_t i, rules = 0;
    ol_rect b11, b12, b21;

    table = block(doc, NULL, OL_ROLE_TABLE, OL_ALIGN_START);
    CHECK(ol_node_set_columns(table, 2, widths));
    r1 = block(doc, table, OL_ROLE_ROW, OL_ALIGN_START);
    c11 = block(doc, r1, OL_ROLE_CELL, OL_ALIGN_START);
    text(doc, c11, "one two three four five six", OL_CSSPX(16), 0);
    c12 = block(doc, r1, OL_ROLE_CELL, OL_ALIGN_START);
    ol_style_init(&s);
    s.background = 0xffeeeeeeu;
    ol_node_set_style(c12, &s);
    text(doc, c12, "x", OL_CSSPX(16), 0);
    ol_box_style_init(&box);
    box.border_width = OL_CSSPX(1);
    ol_node_set_box(c11, &box);
    ol_node_set_box(c12, &box);
    r2 = block(doc, table, OL_ROLE_ROW, OL_ALIGN_START);
    c21 = block(doc, r2, OL_ROLE_CELL, OL_ALIGN_START);
    box.column_span = 2;
    box.border_width = 0;
    ol_node_set_box(c21, &box);
    t = text(doc, c21, "spans both", OL_CSSPX(16), 0);
    CHECK(ol_layout(doc, OL_CSSPX(400), measure, NULL));

    b11 = ol_node_bounds(c11);
    b12 = ol_node_bounds(c12);
    b21 = ol_node_bounds(c21);
    CHECK(b11.x == 0 && b11.width == OL_CSSPX(100));
    CHECK(b12.x == OL_CSSPX(100) && b12.width == OL_CSSPX(200));
    /* "one two three four five six" wraps to 3 lines in 100 px; x stays one
     * line but its cell is as tall as the row */
    CHECK(b11.height == OL_CSSPX(60));
    CHECK(b12.height == b11.height);
    CHECK(b21.y == b11.y + b11.height && b21.width == OL_CSSPX(300));
    CHECK(ol_node_bounds(r1).height == OL_CSSPX(60));
    for (i = 0; i < ol_display_count(doc); ++i) {
        const ol_display_op *op = ol_display_get(doc, i);
        if (op->type == OL_OP_RULE) ++rules;
        if (op->type == OL_OP_FILL_RECT) CHECK(op->u.fill_rect.bounds.height == OL_CSSPX(60));
    }
    CHECK(rules == 8);
    CHECK(ol_node_bounds(t).x == 0);
    ol_document_free(doc);
}

static void test_frames(void)
{
    ol_document *doc = ol_document_new();
    ol_node *p1, *frame, *p2, *ft;
    ol_box_style box;
    ol_rect fb;
    p1 = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    text(doc, p1, "first", OL_CSSPX(16), 0);
    frame = block(doc, NULL, OL_ROLE_BLOCK, OL_ALIGN_START);
    ol_box_style_init(&box);
    box.flags = OL_BOX_ABSOLUTE;
    box.frame.x = OL_CSSPX(50);
    box.frame.y = OL_CSSPX(60);
    box.frame.width = OL_CSSPX(100);
    box.frame.height = OL_CSSPX(40);
    ol_node_set_box(frame, &box);
    ft = text(doc, frame, "in the frame", OL_CSSPX(16), 0);
    p2 = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    text(doc, p2, "second", OL_CSSPX(16), 0);
    CHECK(ol_layout(doc, OL_CSSPX(400), measure, NULL));
    fb = ol_node_bounds(frame);
    CHECK(fb.x == OL_CSSPX(50) && fb.y == OL_CSSPX(60));
    CHECK(fb.width == OL_CSSPX(100) && fb.height == OL_CSSPX(40));
    /* out of the flow: the second paragraph follows the first */
    CHECK(ol_node_bounds(p2).y == OL_CSSPX(20));
    CHECK(ol_document_content_height(doc) == OL_CSSPX(40));
    CHECK(ol_node_bounds(ft).x == OL_CSSPX(50));
    CHECK(ol_hit_test(doc, OL_CSSPX(60), OL_CSSPX(65)) == ft);
    ol_document_free(doc);
}

/* A frame anchored to its page, at x, y from anchor's corners, w x h. */
static ol_node *anchored(ol_document *doc, ol_node *parent, uint32_t anchor, ol_unit x, ol_unit y,
                         ol_unit w, ol_unit h)
{
    ol_node *f = block(doc, parent, OL_ROLE_BLOCK, OL_ALIGN_START);
    ol_box_style box;
    ol_box_style_init(&box);
    box.flags = OL_BOX_ABSOLUTE;
    box.frame.x = x;
    box.frame.y = y;
    box.frame.width = w;
    box.frame.height = h;
    box.anchor = anchor;
    ol_node_set_box(f, &box);
    return f;
}

static ol_node *picture(ol_document *doc, ol_node *parent, int id, ol_unit w, ol_unit h)
{
    ol_node *n = ol_node_append(doc, parent, OL_ROLE_IMAGE, NULL);
    ol_node_set_image(n, id, w, h);
    return n;
}

static const ol_display_op *first_text(const ol_document *doc, const ol_node *n)
{
    const ol_display_op *op[1];
    return text_ops(doc, ol_node_id(n), op, 1) ? op[0] : NULL;
}

/* the first op of a type on a page, after its first from */
static const ol_display_op *page_op(const ol_document *doc, int page, ol_op_type type, size_t from)
{
    size_t first, count, i;
    if (!ol_display_page_range(doc, page, &first, &count)) return NULL;
    for (i = first + from; i < first + count; ++i)
        if (ol_display_get(doc, i)->type == type) return ol_display_get(doc, i);
    return NULL;
}

/* 0.3: frames anchored to their pages, behind and in front of the text */
static void test_anchored_frames(void)
{
    ol_document *doc = ol_document_new();
    ol_page page;
    ol_node *cover, *after, *second, *f1, *f2, *f3, *f4, *t2, *t3, *last = NULL;
    ol_style s;
    ol_box_style box;
    const ol_display_op *op, *ops[4];
    size_t first, count, i;
    ol_text_position pos;
    char line[48];

    memset(&page, 0, sizeof(page));
    page.width = OL_CSSPX(400);
    page.height = OL_CSSPX(310);       /* 270 px of body: 13 and a half lines */
    page.margin_top = page.margin_bottom = OL_CSSPX(20);
    page.margin_left = page.margin_right = OL_CSSPX(20);

    /* the cover: a whole-page picture behind the text, a title box in front */
    cover = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    text(doc, cover, "cover", OL_CSSPX(16), 0);
    f1 = anchored(doc, cover, OL_ANCHOR_X(OL_ANCHOR_PAGE) | OL_ANCHOR_Y(OL_ANCHOR_PAGE) | OL_ANCHOR_BEHIND,
                  0, 0, OL_CSSPX(400), OL_CSSPX(300));
    picture(doc, f1, 7, OL_CSSPX(400), OL_CSSPX(300));
    f2 = anchored(doc, cover, OL_ANCHOR_X(OL_ANCHOR_MARGIN) | OL_ANCHOR_Y(OL_ANCHOR_MARGIN),
                  OL_CSSPX(10), OL_CSSPX(30), OL_CSSPX(200), OL_CSSPX(60));
    s = *ol_node_style(f2);
    s.background = 0xff336699u;
    s.padding_left = OL_CSSPX(4);
    ol_node_set_style(f2, &s);
    t2 = text(doc, f2, "Title text", OL_CSSPX(16), 0);
    /* the frames move nothing: the next paragraph follows the cover's line */
    after = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    text(doc, after, "after the cover", OL_CSSPX(16), 0);
    for (i = 0; i < 13; ++i) {
        ol_node *p = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
        snprintf(line, sizeof line, "filler %u", (unsigned)i);
        text(doc, p, line, OL_CSSPX(16), 0);
    }
    /* page 3: frames from the paragraph, one in front, one behind from the page's edge */
    second = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    s = *ol_node_style(second);
    s.margin_left = OL_CSSPX(30);
    ol_node_set_style(second, &s);
    ol_box_style_init(&box);
    box.flags = OL_BOX_PAGE_BREAK_BEFORE;
    ol_node_set_box(second, &box);
    text(doc, second, "second page", OL_CSSPX(16), 0);
    f3 = anchored(doc, second, OL_ANCHOR_ON, OL_CSSPX(5), OL_CSSPX(7), OL_CSSPX(50), OL_CSSPX(20));
    t3 = text(doc, f3, "pb", OL_CSSPX(16), 0);
    f4 = anchored(doc, second, OL_ANCHOR_X(OL_ANCHOR_PAGE) | OL_ANCHOR_BEHIND,
                  OL_CSSPX(100), -OL_CSSPX(10), OL_CSSPX(30), OL_CSSPX(30));
    picture(doc, f4, 9, OL_CSSPX(30), OL_CSSPX(30));
    /* enough lines for a third page */
    for (i = 0; i < 14; ++i) {
        ol_node *p = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
        snprintf(line, sizeof line, "line %u", (unsigned)i);
        last = text(doc, p, line, OL_CSSPX(16), 0);
    }

    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    CHECK(ol_page_count(doc) == 4);
    /* the frames moved no page break: no line of the body crosses a page's bottom margin */
    for (i = 0; i < ol_display_count(doc); ++i) {
        op = ol_display_get(doc, i);
        if (op->type == OL_OP_TEXT && op->u.text.node_id != ol_node_id(t2))
            CHECK(op->u.text.bounds.y + op->u.text.bounds.height <= OL_CSSPX(290));
    }

    /* page 1: the picture first, at the page's corner, as big as the page */
    CHECK(ol_display_page_range(doc, 0, &first, &count));
    op = ol_display_get(doc, first);
    CHECK(op->type == OL_OP_IMAGE && op->u.image.image_id == 7 && op->page == 0);
    CHECK(op->u.image.bounds.x == 0 && op->u.image.bounds.y == 0);
    CHECK(op->u.image.bounds.width == OL_CSSPX(400) && op->u.image.bounds.height == OL_CSSPX(300));
    /* the body's text where it would be without the frames */
    op = first_text(doc, ol_node_by_id(doc, ol_node_id(after) + 1));
    CHECK(op && op->page == 0 && op->u.text.bounds.x == OL_CSSPX(20) && op->u.text.bounds.y == OL_CSSPX(40));
    /* the title box last, its fill and text from the margins */
    op = ol_display_get(doc, first + count - 1);
    CHECK(op->type == OL_OP_TEXT && op->u.text.node_id == ol_node_id(t2));
    CHECK(text_ops(doc, ol_node_id(t2), ops, 4) == 2 && ops[0]->u.text.bounds.x == OL_CSSPX(34) &&
          ops[0]->u.text.bounds.y == OL_CSSPX(50) && ops[0]->page == 0);
    op = page_op(doc, 0, OL_OP_FILL_RECT, 0);
    CHECK(op && op->u.fill_rect.argb == 0xff336699u && op->u.fill_rect.bounds.x == OL_CSSPX(30) &&
          op->u.fill_rect.bounds.y == OL_CSSPX(50) && op->u.fill_rect.bounds.width == OL_CSSPX(200) &&
          op->u.fill_rect.bounds.height == OL_CSSPX(60));

    /* page 3: behind from the page's left and the paragraph's top; in front from the paragraph's corner */
    CHECK(ol_display_page_range(doc, 2, &first, &count));
    op = ol_display_get(doc, first);
    CHECK(op->type == OL_OP_IMAGE && op->u.image.image_id == 9 && op->page == 2);
    CHECK(op->u.image.bounds.x == OL_CSSPX(100) && op->u.image.bounds.y == OL_CSSPX(10));
    CHECK(text_ops(doc, ol_node_id(t3), ops, 4) == 1 && ops[0]->page == 2 &&
          ops[0]->u.text.bounds.x == OL_CSSPX(55) && ops[0]->u.text.bounds.y == OL_CSSPX(27));
    op = ol_display_get(doc, first + count - 1);
    CHECK(op->type == OL_OP_TEXT && op->u.text.node_id == ol_node_id(t3));
    op = first_text(doc, last);
    CHECK(op && op->page == 3);

    /* the pointer finds the body under a frame */
    CHECK(ol_hit_test_page(doc, 0, OL_CSSPX(40), OL_CSSPX(60)) != t2);
    CHECK(ol_hit_text(doc, 0, OL_CSSPX(40), OL_CSSPX(60), measure, NULL, &pos));
    CHECK(pos.node_id != ol_node_id(t2));

    /* without pages: the frames in the galley, from its corner */
    CHECK(ol_layout(doc, OL_CSSPX(360), measure, NULL));
    op = ol_display_get(doc, 0);
    CHECK(op->type == OL_OP_IMAGE && op->u.image.image_id == 7 && op->u.image.bounds.x == 0);
    CHECK(text_ops(doc, ol_node_id(t2), ops, 4) == 2 && ops[0]->u.text.bounds.x == OL_CSSPX(14) &&
          ops[0]->u.text.bounds.y == OL_CSSPX(30));
    CHECK(ol_display_get(doc, ol_display_count(doc) - 1)->u.text.node_id == ol_node_id(t3));

    /* a program that knows 0.2's ol_box_style: its frames are not anchored */
    ol_box_style_init(&box);
    box.flags = OL_BOX_ABSOLUTE;
    box.anchor = OL_ANCHOR_BEHIND;
    box.size = (uint32_t)((char *)&box.anchor - (char *)&box);
    ol_node_set_box(f4, &box);
    CHECK(ol_node_box(f4)->anchor == 0);
    ol_document_free(doc);
}

/* An A5-ish page: 400 x 300 px, 20 px margins: 260 px of body, 13 lines. */
static void test_pages(void)
{
    ol_document *doc = ol_document_new();
    ol_page page;
    ol_node *paras[40], *heading = NULL, *broken = NULL;
    ol_box_style box;
    size_t first, count, i;
    int p, pages;
    char line[64];

    memset(&page, 0, sizeof(page));
    page.width = OL_CSSPX(400);
    page.height = OL_CSSPX(300);
    page.margin_top = page.margin_bottom = OL_CSSPX(20);
    page.margin_left = page.margin_right = OL_CSSPX(20);

    for (i = 0; i < 30; ++i) {
        ol_style s;
        paras[i] = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
        ol_style_init(&s);
        s.margin_top = OL_CSSPX(10);
        s.line_height = OL_CSSPX(20);
        ol_node_set_style(paras[i], &s);
        /* each paragraph two lines in 360 px */
        snprintf(line, sizeof line, "paragraph %02u words words words words words words", (unsigned)i);
        text(doc, paras[i], line, OL_CSSPX(16), 0);
    }
    /* paragraph 20 starts a page; paragraph 9 keeps with the next */
    ol_box_style_init(&box);
    box.flags = OL_BOX_PAGE_BREAK_BEFORE;
    broken = paras[20];
    ol_node_set_box(broken, &box);
    box.flags = OL_BOX_KEEP_WITH_NEXT;
    heading = paras[9];
    ol_node_set_box(heading, &box);

    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    pages = ol_page_count(doc);
    CHECK(pages >= 4);
    for (p = 0; p < pages; ++p) {
        ol_unit top_most = OL_CSSPX(1000);
        CHECK(ol_display_page_range(doc, p, &first, &count));
        CHECK(count > 0);
        for (i = first; i < first + count; ++i) {
            const ol_display_op *op = ol_display_get(doc, i);
            CHECK(op->page == p);
            if (op->type != OL_OP_TEXT) continue;
            /* every line inside the page's body */
            CHECK(op->u.text.bounds.y >= page.margin_top);
            CHECK(op->u.text.bounds.y + op->u.text.bounds.height <= page.height - page.margin_bottom);
            if (op->u.text.bounds.y < top_most) top_most = op->u.text.bounds.y;
        }
        /* no page after the first starts with the space between paragraphs */
        CHECK(p == 0 || top_most == page.margin_top);
    }
    /* the forced break: paragraph 20's first line is the top of a page */
    for (i = 0; i < ol_display_count(doc); ++i) {
        const ol_display_op *op = ol_display_get(doc, i);
        const ol_node *n = op->type == OL_OP_TEXT ? ol_node_parent(ol_node_by_id(doc, op->u.text.node_id)) : NULL;
        if (n == broken && op->u.text.text_offset == 0) {
            CHECK(op->u.text.bounds.y == page.margin_top);
            CHECK(op->page > 0);
        }
    }
    /* keep with next: paragraph 9's last line and paragraph 10's first on one page */
    {
        int last9 = -1, first10 = -1;
        for (i = 0; i < ol_display_count(doc); ++i) {
            const ol_display_op *op = ol_display_get(doc, i);
            const ol_node *n;
            if (op->type != OL_OP_TEXT) continue;
            n = ol_node_parent(ol_node_by_id(doc, op->u.text.node_id));
            if (n == paras[9]) last9 = op->page;
            if (n == paras[10] && first10 < 0) first10 = op->page;
        }
        CHECK(last9 >= 0 && last9 == first10);
    }
    /* hit-testing on a page */
    CHECK(ol_hit_test_page(doc, 1, OL_CSSPX(30), page.margin_top + OL_CSSPX(5)) != NULL);
    CHECK(ol_hit_test_page(doc, 1, OL_CSSPX(30), OL_CSSPX(5)) == NULL);
    ol_document_free(doc);
}

static void test_page_backgrounds_and_rows(void)
{
    ol_document *doc = ol_document_new();
    ol_page page;
    ol_node *quote, *table;
    ol_style s;
    size_t i, fills = 0;
    int p;

    memset(&page, 0, sizeof(page));
    page.width = OL_CSSPX(200);
    page.height = OL_CSSPX(140);
    page.margin_top = page.margin_bottom = OL_CSSPX(20);   /* 100 px: 5 lines */

    /* a shaded block of 8 lines crosses a page: its background on both */
    quote = block(doc, NULL, OL_ROLE_BLOCK, OL_ALIGN_START);
    ol_style_init(&s);
    s.background = 0xffddddddu;
    ol_node_set_style(quote, &s);
    text(doc, quote, "a\nb\nc\nd\ne\nf\ng\nh", OL_CSSPX(16), 0);
    /* a table whose rows are 3 lines: never cut through a row */
    table = block(doc, NULL, OL_ROLE_TABLE, OL_ALIGN_START);
    for (i = 0; i < 4; ++i) {
        ol_node *row = block(doc, table, OL_ROLE_ROW, OL_ALIGN_START);
        ol_node *cell = block(doc, row, OL_ROLE_CELL, OL_ALIGN_START);
        text(doc, cell, "x\ny\nz", OL_CSSPX(16), 0);
    }
    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    for (i = 0; i < ol_display_count(doc); ++i) {
        const ol_display_op *op = ol_display_get(doc, i);
        if (op->type == OL_OP_FILL_RECT && op->u.fill_rect.node_id == ol_node_id(quote)) {
            ++fills;
            CHECK(op->u.fill_rect.bounds.y >= page.margin_top);
            CHECK(op->u.fill_rect.bounds.y + op->u.fill_rect.bounds.height <= page.height - page.margin_bottom);
        }
    }
    CHECK(fills == 2);
    for (p = 0; p < ol_page_count(doc); ++p) {
        size_t first, count;
        int xs = 0, zs = 0;
        ol_display_page_range(doc, p, &first, &count);
        for (i = first; i < first + count; ++i) {
            const ol_display_op *op = ol_display_get(doc, i);
            const char *t;
            if (op->type != OL_OP_TEXT) continue;
            t = ol_node_text(ol_node_by_id(doc, op->u.text.node_id)) + op->u.text.text_offset;
            if (t[0] == 'x') ++xs;
            if (t[0] == 'z') ++zs;
        }
        CHECK(xs == zs);     /* every row whole on one page */
    }
    ol_document_free(doc);
}

static void test_editing(void)
{
    ol_document *doc = ol_document_new();
    ol_page page;
    ol_node *p, *t, *empty;
    ol_box_style box;
    ol_text_position pos, back;
    ol_rect caret;
    int pg;
    size_t k;
    const char *s = "Caret and hit tests, character by character \xc3\xa9t\xc3\xa9.";

    memset(&page, 0, sizeof(page));
    page.width = OL_CSSPX(300);
    page.height = OL_CSSPX(400);
    page.margin_top = page.margin_left = OL_CSSPX(30);

    p = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_JUSTIFY);
    t = text(doc, p, s, OL_CSSPX(16), 0);
    empty = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    ol_box_style_init(&box);
    box.flags = OL_BOX_EMPTY_LINE;
    ol_node_set_box(empty, &box);
    CHECK(ol_layout_pages(doc, &page, measure, NULL));

    /* every character boundary: its caret, then the character at the caret */
    for (k = 0; k <= strlen(s); ++k) {
        if (k < strlen(s) && ((unsigned char)s[k] & 0xc0u) == 0x80u) continue;
        pos.node_id = ol_node_id(t);
        pos.offset = k;
        CHECK(ol_caret_box(doc, &pos, measure, NULL, &pg, &caret));
        CHECK(pg == 0);
        CHECK(caret.x >= page.margin_left && caret.x <= page.width);
        CHECK(ol_hit_text(doc, pg, caret.x, caret.y + caret.height / 2, measure, NULL, &back));
        CHECK(back.node_id == pos.node_id);
        /* a line's end and the next line's start are the same place in the text */
        CHECK(back.offset == k || (k > 0 && s[k - 1] == ' ' && back.offset == k - 1));
    }
    /* a click right of a line's end: the end of that line */
    CHECK(ol_hit_text(doc, 0, OL_CSSPX(299), page.margin_top + OL_CSSPX(5), measure, NULL, &back));
    CHECK(back.node_id == ol_node_id(t) && back.offset > 0 && back.offset < strlen(s));
    /* the empty paragraph: one line high, the caret at its start */
    CHECK(ol_node_bounds(empty).height == OL_CSSPX(20));
    pos.node_id = ol_node_id(empty);
    pos.offset = 0;
    CHECK(ol_caret_box(doc, &pos, measure, NULL, &pg, &caret));
    CHECK(caret.x == page.margin_left && caret.height == OL_CSSPX(20));
    ol_document_free(doc);
}

static void test_box_versions(void)
{
    ol_document *doc = ol_document_new();
    ol_node *n = block(doc, NULL, OL_ROLE_BLOCK, OL_ALIGN_START);
    ol_box_style box;
    ol_box_style_init(&box);
    box.flags = OL_BOX_KEEP_TOGETHER;
    box.text_indent = OL_CSSPX(7);
    box.border_width = OL_CSSPX(2);
    /* a program built when ol_box_style ended after text_indent */
    box.size = (uint32_t)((char *)&box.frame - (char *)&box);
    ol_node_set_box(n, &box);
    CHECK(ol_node_box(n)->flags == OL_BOX_KEEP_TOGETHER);
    CHECK(ol_node_box(n)->text_indent == OL_CSSPX(7));
    CHECK(ol_node_box(n)->border_width == 0);
    CHECK(ol_node_box(n)->size == sizeof(ol_box_style));
    ol_document_free(doc);
}

struct draw_count {
    size_t text, fill, other;
    int32_t min_y, max_y;
};

static void count_draw(void *userdata, const ol_device_op *d)
{
    struct draw_count *c = (struct draw_count *)userdata;
    if (d->op->type == OL_OP_TEXT) ++c->text;
    else if (d->op->type == OL_OP_FILL_RECT) ++c->fill;
    else ++c->other;
    if (d->box.y < c->min_y) c->min_y = d->box.y;
    if (d->box.y + d->box.height > c->max_y) c->max_y = d->box.y + d->box.height;
}

static void test_scale(void)
{
    ol_scale sc;
    ol_rect r;
    ol_pixel_rect a, b;
    int i;
    ol_document *doc;
    ol_page page;
    struct draw_count all, part;
    ol_pixel_rect clip;

    /* 72 dpi at 100%: a point a pixel */
    memset(&sc, 0, sizeof(sc));
    ol_scale_dpi(&sc, 72, 100);
    CHECK(ol_scale_x(&sc, OL_POINT(100)) == 100);
    CHECK(ol_scale_length(&sc, OL_POINT(12)) == 12);
    CHECK(ol_scale_length(&sc, 1) == 1);          /* a hairline stays visible */
    /* an A4 page, 595 points wide, fitted to 300 pixels */
    ol_scale_fit(&sc, OL_POINT(595), 300);
    CHECK(ol_scale_x(&sc, OL_POINT(595)) == 300);
    CHECK(ol_scale_x(&sc, 0) == 0);
    sc.origin_x = 40;
    sc.origin_y = -1000;
    CHECK(ol_scale_x(&sc, OL_POINT(595)) == 340);
    /* and back, within a pixel */
    for (i = 0; i < 300; i += 7) {
        ol_unit u = ol_unscale_x(&sc, 40 + i);
        CHECK(ol_scale_x(&sc, u) == 40 + i);
    }
    /* boxes that touch in units touch in pixels, at an awkward scale */
    ol_scale_dpi(&sc, 97, 133);
    sc.origin_x = sc.origin_y = 0;
    r.y = 0;
    r.height = OL_POINT(10);
    for (i = 0; i < 50; ++i) {
        r.x = (ol_unit)i * 1237;
        r.width = 1237;
        a = ol_scale_rect(&sc, r);
        r.x += 1237;
        b = ol_scale_rect(&sc, r);
        CHECK(a.x + a.width == b.x);
    }
    /* a whole galley 100 A4 pages long scales without overflowing */
    ol_scale_dpi(&sc, 300, 400);
    CHECK(ol_scale_y(&sc, OL_POINT(842) * 100) == 842 * 100 * 300 * 4 / 72);

    /* drawing a page: only what crosses the clip */
    doc = ol_document_new();
    for (i = 0; i < 12; ++i) {
        ol_node *p = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
        text(doc, p, "a line of text", OL_CSSPX(16), 0);
    }
    memset(&page, 0, sizeof(page));
    page.width = OL_CSSPX(400);
    page.height = OL_CSSPX(600);
    page.margin_top = OL_CSSPX(40);
    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    ol_scale_fit(&sc, page.width, 200);          /* half size */
    sc.origin_x = 10;
    sc.origin_y = 20;
    memset(&all, 0, sizeof(all));
    all.min_y = 1 << 30;
    CHECK(ol_draw_page(doc, 0, &sc, NULL, count_draw, &all) == 48);
    CHECK(all.text == 48 && all.min_y == 20 + 20);   /* 4 words a line */
    clip.x = 0;
    clip.y = 40;
    clip.width = 300;
    clip.height = 30;                            /* the first three lines */
    memset(&part, 0, sizeof(part));
    part.min_y = 1 << 30;
    CHECK(ol_draw_page(doc, 0, &sc, &clip, count_draw, &part) == 12);
    CHECK(part.max_y <= 70);
    ol_document_free(doc);
}

/* the first text op of a node */
static const ol_display_op *first_op(const ol_document *doc, const ol_node *n)
{
    const ol_display_op *op[1];
    return text_ops(doc, ol_node_id(n), op, 1) ? op[0] : NULL;
}

static void test_tabs(void)
{
    ol_document *doc = ol_document_new();
    ol_node *list, *bullet, *tab, *item, *stops, *line, *lead, *lead_x, *just, *a, *bb, *gg;
    ol_box_style box;
    ol_style s;
    ol_unit at[2];
    ol_text_position pos;
    ol_rect caret;
    int pg = -1;
    const ol_display_op *op;

    /* a list item: the bullet hangs, its tab goes to the left edge, and the
     * lines under it start there too */
    list = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    s = *ol_node_style(list);
    s.margin_left = OL_CSSPX(24);
    ol_node_set_style(list, &s);
    ol_box_style_init(&box);
    box.text_indent = -OL_CSSPX(24);
    ol_node_set_box(list, &box);
    bullet = text(doc, list, "*", OL_CSSPX(16), 0);
    tab = text(doc, list, "\t", OL_CSSPX(16), 0);
    item = text(doc, list, "Libraries, devices and fonts are only noted", OL_CSSPX(16), 0);

    /* stops given out of order, then the half inch ones after the last */
    stops = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    at[0] = OL_CSSPX(100);
    at[1] = OL_CSSPX(40);
    CHECK(ol_node_set_tabs(stops, 2, at));
    CHECK(!ol_node_set_tabs(stops, OL_MAX_TABS + 1, at));
    line = text(doc, stops, "ab\tc\td\te", OL_CSSPX(16), 0);

    /* a tab at a line's start is kept */
    lead = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    lead_x = text(doc, lead, "\tx", OL_CSSPX(16), 0);

    /* justification leaves what is before a tab where it is */
    just = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_JUSTIFY);
    a = text(doc, just, "a", OL_CSSPX(16), 0);
    text(doc, just, "\t", OL_CSSPX(16), 0);
    bb = text(doc, just, "bb cc dd ee ff ", OL_CSSPX(16), 0);
    gg = text(doc, just, "gg hh ii", OL_CSSPX(16), 0);

    CHECK(ol_layout(doc, OL_CSSPX(200), measure, NULL));

    op = first_op(doc, bullet);
    CHECK(op && op->u.text.bounds.x == 0);
    op = first_op(doc, tab);
    CHECK(op && op->u.text.bounds.x == OL_CSSPX(8) && op->u.text.bounds.width == OL_CSSPX(16));
    {
        const ol_display_op *ops[8];
        size_t n = text_ops(doc, ol_node_id(item), ops, 8);
        CHECK(n >= 2 && ops[0]->u.text.bounds.x == OL_CSSPX(24));
        CHECK(n >= 2 && ops[n - 1]->u.text.bounds.y > ops[0]->u.text.bounds.y);
        while (n > 1 && ops[n - 1]->u.text.bounds.y == ops[n - 2]->u.text.bounds.y) --n;
        CHECK(n >= 2 && ops[n - 1]->u.text.bounds.x == OL_CSSPX(24));   /* the next line's first */
    }

    {
        const ol_display_op *ops[8];
        size_t n = text_ops(doc, ol_node_id(line), ops, 8);
        CHECK(n == 7);
        if (n == 7) {
            CHECK(ops[0]->u.text.bounds.x == 0);                         /* ab */
            CHECK(ops[2]->u.text.bounds.x == OL_CSSPX(40));              /* c */
            CHECK(ops[4]->u.text.bounds.x == OL_CSSPX(100));             /* d */
            CHECK(ops[6]->u.text.bounds.x == OL_CSSPX(144));             /* e: 3 x 48 */
        }
    }

    {
        const ol_display_op *ops[4];
        size_t n = text_ops(doc, ol_node_id(lead_x), ops, 4);
        CHECK(n == 2 && ops[0]->u.text.bounds.x == 0 && ops[1]->u.text.bounds.x == OL_CSSPX(48));
    }

    op = first_op(doc, a);
    CHECK(op && op->u.text.bounds.x == 0);
    op = first_op(doc, bb);
    CHECK(op && op->u.text.bounds.x == OL_CSSPX(48));
    {
        const ol_display_op *ops[4];
        size_t n = text_ops(doc, ol_node_id(gg), ops, 4);
        CHECK(n == 3 && ops[0]->u.text.bounds.x + OL_CSSPX(16) == OL_CSSPX(200));
    }

    /* the caret after a tab is at the stop */
    pos.node_id = ol_node_id(tab);
    pos.offset = 1;
    CHECK(ol_caret_box(doc, &pos, measure, NULL, &pg, &caret));
    CHECK(caret.x == OL_CSSPX(24));
    ol_document_free(doc);
}

int main(void)
{
    test_alignment();
    test_baseline_and_fonts();
    test_table();
    test_frames();
    test_anchored_frames();
    test_pages();
    test_page_backgrounds_and_rows();
    test_editing();
    test_box_versions();
    test_scale();
    test_tabs();
    if (failures) {
        printf("OPENLAYOUT DOCUMENT FAIL (%d)\n", failures);
        return 20;
    }
    printf("OPENLAYOUT DOCUMENT PASS version=%s\n", OL_VERSION);
    return 0;
}
