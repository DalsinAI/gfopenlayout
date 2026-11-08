/*
 * GFOpenLayout 0.2's document layout: alignment and justification, shared
 * baselines, font numbers, grid tables, frames, pages, and the caret and
 * character hit-testing an editor needs.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "gfopenlayout.h"

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

/* 0.4: frames anchored to their pages, behind and in front of the text */
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

    /* a program that knows 0.3's ol_box_style (gfopenlayout.library 3 or 4): its frames are not anchored */
    ol_box_style_init(&box);
    box.flags = OL_BOX_ABSOLUTE;
    box.anchor = OL_ANCHOR_BEHIND;
    box.size = (uint32_t)OL_BOX_STYLE_SIZE_0_3;
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

/* Newspaper columns are virtual narrow pages grouped left-to-right on each
 * physical page.  Exercise the editor-facing coordinates as well as flow. */
static void test_page_columns(void)
{
    ol_document *doc = ol_document_new();
    ol_page page;
    ol_node *paras[24], *texts[24], *fblock, *fpage;
    const ol_display_op *second = NULL;
    ol_text_position pos;
    ol_rect caret;
    ol_unit column_width, second_x, gap_x;
    size_t first, count, i;
    int caret_page = -1;
    char line[64];

    memset(&page, 0, sizeof(page));
    page.width = OL_CSSPX(400);
    page.height = OL_CSSPX(300);
    page.margin_top = page.margin_bottom = OL_CSSPX(20);
    page.margin_left = page.margin_right = OL_CSSPX(20);
    column_width = OL_CSSPX(170); /* (360 - 20 gap) / 2 */
    second_x = page.margin_left + column_width + OL_CSSPX(20);
    gap_x = page.margin_left + column_width + OL_CSSPX(10);

    for (i = 0; i < 24; ++i) {
        ol_style s;
        paras[i] = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
        ol_style_init(&s);
        s.margin_top = OL_CSSPX(10);
        s.line_height = OL_CSSPX(20);
        ol_node_set_style(paras[i], &s);
        snprintf(line, sizeof line, "column paragraph %02u short words", (unsigned)i);
        texts[i] = text(doc, paras[i], line, OL_CSSPX(16), 0);
    }
    fblock = anchored(doc, paras[8], OL_ANCHOR_ON,
                      OL_CSSPX(5), OL_CSSPX(3), OL_CSSPX(20), OL_CSSPX(20));
    picture(doc, fblock, 31, OL_CSSPX(20), OL_CSSPX(20));
    fpage = anchored(doc, paras[8],
                     OL_ANCHOR_X(OL_ANCHOR_PAGE) | OL_ANCHOR_Y(OL_ANCHOR_PAGE) | OL_ANCHOR_BEHIND,
                     OL_CSSPX(7), OL_CSSPX(8), OL_CSSPX(18), OL_CSSPX(18));
    picture(doc, fpage, 32, OL_CSSPX(18), OL_CSSPX(18));

    CHECK(ol_layout_pages_columns(doc, &page, 2, OL_CSSPX(20), measure, NULL));
    CHECK(ol_page_count(doc) >= 2);
    CHECK(ol_display_page_range(doc, 0, &first, &count));
    CHECK(count > 0);
    for (i = first; i < first + count; ++i) {
        const ol_display_op *op = ol_display_get(doc, i);
        CHECK(op->page == 0);
        if (!second && op->type == OL_OP_TEXT && op->u.text.bounds.x >= second_x)
            second = op;
    }
    CHECK(second != NULL);
    if (second) {
        const ol_node *hit = ol_hit_test_page(doc, 0,
            second->u.text.bounds.x + OL_CSSPX(2),
            second->u.text.bounds.y + OL_CSSPX(2));
        CHECK(hit != NULL);
        CHECK(hit == ol_node_by_id(doc, second->u.text.node_id));
        pos.node_id = second->u.text.node_id;
        pos.offset = second->u.text.text_offset;
        CHECK(ol_caret_box(doc, &pos, measure, NULL, &caret_page, &caret));
        CHECK(caret_page == 0);
        CHECK(caret.x >= second_x);
        CHECK(caret.x == second->u.text.bounds.x);
    }
    {
        const ol_display_op *anchor_text = first_text(doc, texts[8]);
        ol_rect at;
        int fp = -1;
        CHECK(anchor_text != NULL);
        CHECK(ol_frame_place(doc, fblock, &fp, &at));
        CHECK(anchor_text && fp == anchor_text->page);
        CHECK(anchor_text && at.x == anchor_text->u.text.bounds.x + OL_CSSPX(5));
        CHECK(ol_frame_place(doc, fpage, &fp, &at));
        CHECK(anchor_text && fp == anchor_text->page);
        CHECK(at.x == OL_CSSPX(7) && at.y == OL_CSSPX(8));
        CHECK(ol_node_page_box(doc, ol_node_id(paras[8]), &fp, &at));
        CHECK(anchor_text && fp == anchor_text->page && at.x == anchor_text->u.text.bounds.x &&
              at.y == anchor_text->u.text.bounds.y);
        CHECK(ol_node_page_box(doc, ol_node_id(fpage), &fp, &at));
        CHECK(anchor_text && fp == anchor_text->page && at.x == OL_CSSPX(7) && at.y == OL_CSSPX(8));
        CHECK(!ol_node_page_box(doc, 1000000, &fp, &at));
    }
    /* The inter-column gap is not editable content. */
    CHECK(ol_hit_test_page(doc, 0, gap_x, page.margin_top + OL_CSSPX(5)) == NULL);
    ol_document_free(doc);
}

/* Variable section widths are real line-breaking widths, then their virtual
 * regions are grouped onto physical pages using each section's own columns. */
static void test_page_sections(void)
{
    ol_document *doc = ol_document_new();
    ol_page page;
    ol_page_section sections[2];
    ol_node *paras[24], *texts[24];
    const ol_display_op *ops[8];
    ol_rect box;
    int i, section_page = -1, saw_second_column = 0;
    ol_unit preceding_bottom = 0;
    size_t first, count, k, n;

    memset(&page, 0, sizeof(page));
    page.width = OL_CSSPX(400);
    page.height = OL_CSSPX(300);
    page.margin_top = page.margin_bottom = OL_CSSPX(20);
    page.margin_left = page.margin_right = OL_CSSPX(20);
    for (i = 0; i < 24; ++i) {
        ol_style style;
        paras[i] = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
        ol_style_init(&style);
        style.margin_top = OL_CSSPX(10);
        style.line_height = OL_CSSPX(20);
        ol_node_set_style(paras[i], &style);
        texts[i] = text(doc, paras[i], "wide words fit but narrow columns wrap", OL_CSSPX(16), 0);
    }
    sections[0].first_node = ol_node_id(paras[0]);
    sections[0].columns = 1;
    sections[0].gap = 0;
    sections[0].break_before = OL_SECTION_CONTINUOUS;
    sections[1].first_node = ol_node_id(paras[6]);
    sections[1].columns = 2;
    sections[1].gap = OL_CSSPX(20);
    sections[1].break_before = OL_SECTION_NEW_PAGE;

    CHECK(ol_layout_pages_sections(doc, &page, sections, 2, measure, NULL));
    n = text_ops(doc, ol_node_id(texts[0]), ops, 8);
    CHECK(n > 0 && ops[0]->u.text.bounds.y == ops[n - 1]->u.text.bounds.y);
    n = text_ops(doc, ol_node_id(texts[6]), ops, 8);
    CHECK(n > 1 && ops[0]->u.text.bounds.y < ops[n - 1]->u.text.bounds.y);
    CHECK(ol_node_page_box(doc, ol_node_id(paras[6]), &section_page, &box));
    CHECK(section_page > 0 && box.x == page.margin_left && box.y == page.margin_top + OL_CSSPX(10));
    CHECK(ol_display_page_range(doc, section_page, &first, &count));
    for (k = first; k < first + count; ++k) {
        const ol_display_op *op = ol_display_get(doc, k);
        CHECK(op->page == section_page);
        if (op->type == OL_OP_TEXT && op->u.text.bounds.x >= OL_CSSPX(210))
            saw_second_column = 1;
    }
    CHECK(saw_second_column);
    CHECK(ol_hit_test_page(doc, section_page, OL_CSSPX(200), OL_CSSPX(30)) == NULL);

    sections[1].break_before = OL_SECTION_CONTINUOUS;
    CHECK(ol_layout_pages_sections(doc, &page, sections, 2, measure, NULL));
    CHECK(ol_node_page_box(doc, ol_node_id(paras[6]), &section_page, &box));
    CHECK(section_page == 0 && box.y > page.margin_top);

    /* The final page of a multi-column section is balanced before a
     * continuous successor. Both preceding columns are occupied, and the
     * successor starts in a rectangular band below both of them. */
    sections[0].columns = 2;
    sections[0].gap = OL_CSSPX(20);
    CHECK(ol_layout_pages_sections(doc, &page, sections, 2, measure, NULL));
    saw_second_column = 0;
    for (i = 0; i < 6; ++i) {
        n = text_ops(doc, ol_node_id(texts[i]), ops, 8);
        for (k = 0; k < n; ++k) {
            ol_unit bottom = ops[k]->u.text.bounds.y + ops[k]->u.text.bounds.height;
            if (bottom > preceding_bottom) preceding_bottom = bottom;
            if (ops[k]->u.text.bounds.x >= OL_CSSPX(210)) saw_second_column = 1;
        }
    }
    CHECK(saw_second_column);
    CHECK(ol_node_page_box(doc, ol_node_id(paras[6]), &section_page, &box));
    CHECK(section_page == 0 && box.y >= preceding_bottom);
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

/* a paragraph of one text node with the given stops */
static ol_node *tab_para(ol_document *doc, const char *utf8, int n, const ol_tab_stop *stops,
                         ol_text_align align)
{
    ol_node *p = block(doc, NULL, OL_ROLE_PARAGRAPH, align);
    CHECK(ol_node_set_tab_stops(p, n, stops));
    return text(doc, p, utf8, OL_CSSPX(16), 0);
}

/* 0.3: right, centred and decimal stops, and leaders */
static void test_aligned_tabs(void)
{
    ol_document *doc = ol_document_new();
    ol_tab_stop st[3];
    ol_node *toc, *edge, *centre, *dec, *dec_none, *no_room, *two, *wrap, *just, *left_past, *p;
    const ol_display_op *ops[16];
    ol_text_position pos;
    ol_rect caret;
    int pg = -1;
    size_t n;

    memset(st, 0, sizeof st);
    /* a contents line: the page number at a right stop past the line's end, with dots */
    st[0].position = OL_CSSPX(200);
    st[0].align = OL_TAB_RIGHT;
    st[0].leader = OL_LEADER_DOT;
    toc = tab_para(doc, "Executive summary\t3", 1, st, OL_ALIGN_START);
    /* a right stop at the line's end */
    st[0].position = OL_CSSPX(180);
    st[0].leader = OL_LEADER_NONE;
    edge = tab_para(doc, "Scope\t12", 1, st, OL_ALIGN_START);
    /* centred */
    st[0].position = OL_CSSPX(100);
    st[0].align = OL_TAB_CENTER;
    st[0].leader = OL_LEADER_HYPHEN;
    centre = tab_para(doc, "ab\tcdef", 1, st, OL_ALIGN_START);
    /* decimal, then decimal with no point */
    st[0].position = OL_CSSPX(120);
    st[0].align = OL_TAB_DECIMAL;
    st[0].leader = OL_LEADER_UNDERSCORE;
    dec = tab_para(doc, "x\t12.50", 1, st, OL_ALIGN_START);
    dec_none = tab_para(doc, "x\t125", 1, st, OL_ALIGN_START);
    /* no room before the stop: the text stays after the tab */
    st[0].position = OL_CSSPX(60);
    st[0].align = OL_TAB_RIGHT;
    st[0].leader = OL_LEADER_NONE;
    no_room = tab_para(doc, "abcdef\tlongtext", 1, st, OL_ALIGN_START);
    /* a right stop, then a left one */
    st[0].position = OL_CSSPX(100);
    st[1].position = OL_CSSPX(150);
    st[1].align = OL_TAB_LEFT;
    two = tab_para(doc, "a\tb\tc", 2, st, OL_ALIGN_START);
    /* the text after a right stop wraps: what is on the line goes to the stop */
    st[0].position = OL_CSSPX(176);
    wrap = tab_para(doc, "a\tbbbb cccc dddd eeee ffff", 1, st, OL_ALIGN_START);
    /* a justified line with a right stop is not widened */
    st[0].position = OL_CSSPX(160);
    p = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_JUSTIFY);
    CHECK(ol_node_set_tab_stops(p, 1, st));
    just = text(doc, p, "aa bb\tcc\nnext", OL_CSSPX(16), 0);
    /* a justified line that a left stop past its end breaks is not widened */
    st[0].position = OL_CSSPX(400);
    st[0].align = OL_TAB_LEFT;
    p = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_JUSTIFY);
    CHECK(ol_node_set_tab_stops(p, 1, st));
    left_past = text(doc, p, "aa bb cc\t9", OL_CSSPX(16), 0);
    /* bad calls */
    CHECK(!ol_node_set_tab_stops(p, OL_MAX_TABS + 1, st));
    CHECK(!ol_node_set_tab_stops(p, 1, NULL));
    CHECK(ol_node_set_tab_stops(p, 0, NULL) && ol_node_set_tab_stops(p, 1, st));

    CHECK(ol_layout(doc, OL_CSSPX(180), measure, NULL));

    /* "Executive summary" is 136 px; the 3 ends at 200, on the same line */
    n = text_ops(doc, ol_node_id(toc), ops, 16);
    CHECK(n == 4);
    if (n == 4) {
        CHECK(ops[3]->u.text.bounds.x == OL_CSSPX(192));
        CHECK(ops[3]->u.text.bounds.y == ops[0]->u.text.bounds.y);
        CHECK(ops[2]->u.text.bounds.x == OL_CSSPX(136) && ops[2]->u.text.bounds.width == OL_CSSPX(56));
        CHECK(OL_TEXT_LEADER_OF(ops[2]->u.text.style.text_flags) == OL_LEADER_DOT);
        CHECK(OL_TEXT_LEADER_OF(ops[0]->u.text.style.text_flags) == OL_LEADER_NONE);
    }
    n = text_ops(doc, ol_node_id(edge), ops, 16);
    CHECK(n == 3 && ops[2]->u.text.bounds.x == OL_CSSPX(164) && ops[2]->u.text.bounds.y == ops[0]->u.text.bounds.y);
    CHECK(n == 3 && OL_TEXT_LEADER_OF(ops[1]->u.text.style.text_flags) == OL_LEADER_NONE);
    n = text_ops(doc, ol_node_id(centre), ops, 16);
    CHECK(n == 3 && ops[2]->u.text.bounds.x == OL_CSSPX(84) && ops[1]->u.text.bounds.width == OL_CSSPX(68));
    CHECK(n == 3 && OL_TEXT_LEADER_OF(ops[1]->u.text.style.text_flags) == OL_LEADER_HYPHEN);
    n = text_ops(doc, ol_node_id(dec), ops, 16);
    CHECK(n == 3 && ops[2]->u.text.bounds.x == OL_CSSPX(104));
    CHECK(n == 3 && OL_TEXT_LEADER_OF(ops[1]->u.text.style.text_flags) == OL_LEADER_UNDERSCORE);
    n = text_ops(doc, ol_node_id(dec_none), ops, 16);
    CHECK(n == 3 && ops[2]->u.text.bounds.x == OL_CSSPX(96));
    n = text_ops(doc, ol_node_id(no_room), ops, 16);
    CHECK(n == 3 && ops[2]->u.text.bounds.x == OL_CSSPX(48) && ops[1]->u.text.bounds.width == 0);
    n = text_ops(doc, ol_node_id(two), ops, 16);
    CHECK(n == 5 && ops[2]->u.text.bounds.x == OL_CSSPX(92) && ops[4]->u.text.bounds.x == OL_CSSPX(150));
    n = text_ops(doc, ol_node_id(wrap), ops, 16);
    CHECK(n == 7);
    if (n == 7) {
        /* "bbbb cccc dddd eeee" (152 px) has room after the tab at 8: it ends at 176; ffff wraps */
        CHECK(ops[2]->u.text.bounds.x == OL_CSSPX(24) && ops[5]->u.text.bounds.x == OL_CSSPX(144));
        CHECK(ops[5]->u.text.bounds.y == ops[0]->u.text.bounds.y);
        CHECK(ops[6]->u.text.bounds.y > ops[0]->u.text.bounds.y && ops[6]->u.text.bounds.x == 0);
    }
    n = text_ops(doc, ol_node_id(just), ops, 16);
    CHECK(n >= 4 && ops[0]->u.text.bounds.x == 0 && ops[1]->u.text.bounds.x == OL_CSSPX(24));
    CHECK(n >= 4 && ops[3]->u.text.bounds.x == OL_CSSPX(144));

    n = text_ops(doc, ol_node_id(left_past), ops, 16);
    CHECK(n == 5 && ops[1]->u.text.bounds.x == OL_CSSPX(24) && ops[2]->u.text.bounds.x == OL_CSSPX(48));
    CHECK(n == 5 && ops[3]->u.text.bounds.y > ops[0]->u.text.bounds.y);

    /* the caret after a right-aligned tab is where its text starts */
    pos.node_id = ol_node_id(toc);
    pos.offset = 18;
    CHECK(ol_caret_box(doc, &pos, measure, NULL, &pg, &caret));
    CHECK(caret.x == OL_CSSPX(192));
    ol_document_free(doc);
}

/* 0.5: the display list sorted into its pages where it is (and no second array): the same as 0.4's copy
 * (OL_CHECK_SORT, which make test builds with, compares every sort with it), on documents with what moves
 * ops about: shaded blocks and table rows crossing pages (on each page, cut), frames behind and in front
 * on their pages, empty pages; laid out with ol_document_reserve's or ol_document_new_reserved's room, with
 * too little, and with none. */
extern int ol_sort_checks, ol_sort_mismatches;

static unsigned long lcg = 12345;
static unsigned next_rand(unsigned n)
{
    lcg = lcg * 1103515245UL + 12345UL;
    return (unsigned)((lcg >> 16) % n);
}

static void test_sort_in_place(void)
{
    int round, checks = ol_sort_checks;
    for (round = 0; round < 40; ++round) {
        ol_document *doc = round % 4 == 3 ? ol_document_new_reserved(3000) : ol_document_new();
        ol_page page;
        ol_style s;
        ol_box_style box;
        int i, n = 20 + (int)next_rand(60), pages, p;
        size_t total = 0, first, count, k;
        memset(&page, 0, sizeof(page));
        page.width = OL_CSSPX(200);
        page.height = OL_CSSPX(100 + next_rand(100));
        page.margin_top = page.margin_bottom = OL_CSSPX(10);
        page.margin_left = page.margin_right = OL_CSSPX(10);
        for (i = 0; i < n; ++i) {
            unsigned what = next_rand(8);
            ol_node *b = block(doc, NULL, what == 6 ? OL_ROLE_TABLE : OL_ROLE_PARAGRAPH, OL_ALIGN_START);
            if (what == 6) {                            /* a table of shaded cells */
                int r;
                for (r = 0; r < 1 + (int)next_rand(4); ++r) {
                    ol_node *row = block(doc, b, OL_ROLE_ROW, OL_ALIGN_START);
                    ol_node *cell = block(doc, row, OL_ROLE_CELL, OL_ALIGN_START);
                    ol_style_init(&s);
                    s.display = OL_DISPLAY_BLOCK;
                    s.background = 0xffcceeffu;
                    ol_node_set_style(cell, &s);
                    ol_box_style_init(&box);
                    box.border_width = OL_CSSPX(1);
                    ol_node_set_box(cell, &box);
                    text(doc, cell, "cell\nrow", OL_CSSPX(16), 0);
                }
                continue;
            }
            if (what == 7) {                            /* shaded and empty: a fill no page gets */
                ol_style_init(&s);
                s.display = OL_DISPLAY_BLOCK;
                s.background = 0xffeeeeeeu;
                ol_node_set_style(b, &s);
                continue;
            }
            if (what <= 1) {                            /* shaded and bordered: crosses pages, cut on each */
                ol_style_init(&s);
                s.display = OL_DISPLAY_BLOCK;
                s.background = 0xffddddddu;
                ol_node_set_style(b, &s);
                ol_box_style_init(&box);
                box.border_width = OL_CSSPX(1);
                ol_node_set_box(b, &box);
                text(doc, b, "a\nb\nc\nd\ne\nf\ng\nh\ni\nj", OL_CSSPX(16), 0);
            } else {
                text(doc, b, what == 2 ? "one two three four five six seven eight nine ten" : "a line", OL_CSSPX(16),
                     what & 1);
            }
            if (what == 3 || what == 4) {               /* a frame on its page, behind or in front */
                ol_node *f = anchored(doc, b, OL_ANCHOR_X(OL_ANCHOR_PAGE) | OL_ANCHOR_Y(what == 3 ? OL_ANCHOR_PAGE
                                                                                           : OL_ANCHOR_BLOCK) |
                                                  (what == 3 ? OL_ANCHOR_BEHIND : 0u),
                                      OL_CSSPX(next_rand(50)), OL_CSSPX(next_rand(30)), OL_CSSPX(60), OL_CSSPX(40));
                ol_style_init(&s);
                s.display = OL_DISPLAY_BLOCK;
                s.background = 0xff336699u;
                ol_node_set_style(f, &s);
                text(doc, f, "box", OL_CSSPX(12), 0);
            }
            if (what == 5) {                            /* a page break */
                ol_box_style_init(&box);
                box.flags = OL_BOX_PAGE_BREAK_BEFORE;
                ol_node_set_box(b, &box);
            }
        }
        if (round % 3 == 1) CHECK(ol_document_reserve(doc, 4));       /* too little: it grows as before */
        if (round % 3 == 2) CHECK(ol_document_reserve(doc, 4000));    /* room enough */
        CHECK(ol_layout_pages(doc, &page, measure, NULL));
        pages = ol_page_count(doc);
        /* every page's ops are its own, and add up */
        for (p = 0; p < pages; ++p) {
            CHECK(ol_display_page_range(doc, p, &first, &count));
            for (k = first; k < first + count; ++k) CHECK(ol_display_get(doc, k)->page == p);
            total += count;
        }
        CHECK(total == ol_display_count(doc));
        /* and again on the same document: the same */
        CHECK(ol_layout_pages(doc, &page, measure, NULL) && ol_page_count(doc) == pages);
        ol_document_free(doc);
    }
    CHECK(ol_sort_checks >= checks + 80);
    CHECK(ol_sort_mismatches == 0);
}

/* ---- 0.6: text round frames ---------------------------------------------- */

extern int ol_wrap_layouts, ol_wrap_passes, ol_wrap_unsettled;

/* An anchored frame that the body wraps round. */
static ol_node *wrapped(ol_document *doc, ol_node *parent, uint32_t anchor, ol_unit x, ol_unit y,
                        ol_unit w, ol_unit h, uint32_t wrap, ol_unit gap)
{
    ol_node *f = anchored(doc, parent, anchor, x, y, w, h);
    ol_box_style box = *ol_node_box(f);
    box.wrap = wrap;
    box.wrap_gap[1] = box.wrap_gap[3] = gap;
    ol_node_set_box(f, &box);
    return f;
}

/* How many of the body's text ops on a page (those of the nodes in body) cross a rectangle in the
 * page's coordinates; a piece's trailing spaces, which may hang past a stretch's end, left out. */
static int crossing(const ol_document *doc, int page, ol_rect r, const ol_id *body, size_t nbody)
{
    size_t first, count, i, k;
    int n = 0;
    if (!ol_display_page_range(doc, page, &first, &count)) return 0;
    for (i = first; i < first + count; ++i) {
        const ol_display_op *op = ol_display_get(doc, i);
        const ol_node *node;
        const char *t;
        ol_unit w;
        size_t len;
        int mine = 0;
        if (op->type != OL_OP_TEXT) continue;
        for (k = 0; k < nbody; ++k) mine |= body[k] == op->u.text.node_id;
        if (!mine) continue;
        node = ol_node_by_id(doc, op->u.text.node_id);
        t = ol_node_text(node) + op->u.text.text_offset;
        len = op->u.text.text_length;
        w = op->u.text.bounds.width;
        while (len && t[len - 1] == ' ') {
            --len;
            w -= OL_TEXT_FONT_OF(op->u.text.style.text_flags) == 1 ? OL_CSSPX(10) : OL_CSSPX(8);
        }
        if (!len) continue;
        if (op->u.text.bounds.x < r.x + r.width && op->u.text.bounds.x + w > r.x &&
            op->u.text.bounds.y < r.y + r.height && op->u.text.bounds.y + op->u.text.bounds.height > r.y)
            ++n;
    }
    return n;
}

static ol_rect rect(ol_unit x, ol_unit y, ol_unit w, ol_unit h)
{
    ol_rect r;
    r.x = x;
    r.y = y;
    r.width = w;
    r.height = h;
    return r;
}

static void test_wrap_frames(void)
{
    ol_document *doc = ol_document_new();
    ol_page page;
    ol_node *p1, *p2, *p3, *tb, *cellp, *f1, *f2, *f3, *f4, *t4;
    ol_id body[8];
    ol_box_style box;
    const ol_display_op *op, *ops[64];
    size_t i, n;
    int left = 0, right = 0, under = 0, beside = 0, below = 0, layouts = ol_wrap_layouts, passes = ol_wrap_passes;
    int unsettled = ol_wrap_unsettled;
    static const char words[] = "lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod tempor "
                                "incididunt ut labore et dolore magna aliqua ut enim ad minim veniam quis "
                                "nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat";

    memset(&page, 0, sizeof(page));
    page.width = OL_CSSPX(400);
    page.height = OL_CSSPX(310);       /* 270 px of body: 13 and a half lines of 20 px */
    page.margin_top = page.margin_bottom = OL_CSSPX(20);
    page.margin_left = page.margin_right = OL_CSSPX(20);

    /* a picture at the top left of the body, square, 10 px from the text beside it */
    p1 = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    body[0] = ol_node_id(text(doc, p1, words, OL_CSSPX(16), 0));
    f1 = wrapped(doc, p1, OL_ANCHOR_X(OL_ANCHOR_MARGIN) | OL_ANCHOR_Y(OL_ANCHOR_MARGIN), 0, 0,
                 OL_CSSPX(100), OL_CSSPX(60), OL_WRAP_SQUARE, OL_CSSPX(10));
    picture(doc, f1, 1, OL_CSSPX(100), OL_CSSPX(60));
    /* one in the middle of the column, text on both sides, justified */
    p2 = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_JUSTIFY);
    body[1] = ol_node_id(text(doc, p2, words, OL_CSSPX(16), 0));
    f2 = wrapped(doc, p2, OL_ANCHOR_X(OL_ANCHOR_PAGE) | OL_ANCHOR_Y(OL_ANCHOR_BLOCK), OL_CSSPX(160), OL_CSSPX(20),
                 OL_CSSPX(80), OL_CSSPX(40), OL_WRAP_SQUARE, 0);
    /* a text box above and below which the text goes, in front; its own text does not wrap */
    p3 = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    body[2] = ol_node_id(text(doc, p3, words, OL_CSSPX(16), 0));
    f3 = wrapped(doc, p3, OL_ANCHOR_X(OL_ANCHOR_PAGE) | OL_ANCHOR_Y(OL_ANCHOR_BLOCK), OL_CSSPX(100), OL_CSSPX(10),
                 OL_CSSPX(100), OL_CSSPX(30), OL_WRAP_TOP_BOTTOM, 0);
    text(doc, f3, "box text", OL_CSSPX(16), 0);
    /* a table under a square frame: its cells do not wrap */
    tb = block(doc, NULL, OL_ROLE_TABLE, OL_ALIGN_START);
    {
        ol_node *row = block(doc, tb, OL_ROLE_ROW, OL_ALIGN_START);
        ol_node *cell = block(doc, row, OL_ROLE_CELL, OL_ALIGN_START);
        cellp = text(doc, cell, "cell", OL_CSSPX(16), 0);
    }
    f4 = wrapped(doc, tb, OL_ANCHOR_X(OL_ANCHOR_MARGIN) | OL_ANCHOR_Y(OL_ANCHOR_BLOCK), 0, 0,
                 OL_CSSPX(60), OL_CSSPX(20), OL_WRAP_SQUARE, 0);
    t4 = text(doc, f4, "f", OL_CSSPX(16), 0);
    (void)p3;
    (void)f2;

    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    CHECK(ol_wrap_layouts == layouts + 1 && ol_wrap_unsettled == unsettled);
    /* each frame's zone is worked out where its paragraph starts: one pass */
    CHECK(ol_wrap_passes - passes == 1);

    /* the picture: the first three lines start 10 px right of it, the fourth at the margin */
    n = text_ops(doc, body[0], ops, 64);
    CHECK(n > 10);
    for (i = 0; i < n; ++i) {
        op = ops[i];
        CHECK(op->page == 0);
        if (op->u.text.bounds.y < OL_CSSPX(80)) {
            CHECK(op->u.text.bounds.x >= OL_CSSPX(130));
            if (op->u.text.bounds.x == OL_CSSPX(130)) ++left;
        } else if (op->u.text.bounds.y == OL_CSSPX(80) && op->u.text.bounds.x == OL_CSSPX(20)) {
            ++right;
        }
    }
    CHECK(left == 3 && right == 1);
    CHECK(crossing(doc, 0, rect(OL_CSSPX(20), OL_CSSPX(20), OL_CSSPX(110), OL_CSSPX(60)), body, 3) == 0);

    /* the frame in the middle: lines beside it on both sides, none under it */
    n = text_ops(doc, body[1], ops, 64);
    {
        const ol_display_op *f = page_op(doc, 0, OL_OP_TEXT, 0);
        ol_unit fy = 0;
        (void)f;
        for (i = 0; i < ol_display_count(doc); ++i) {
            op = ol_display_get(doc, i);
            if (op->type == OL_OP_TEXT && op->u.text.node_id == body[1]) { fy = op->u.text.bounds.y; break; }
        }
        /* the paragraph's first line is 20 px above the frame */
        for (i = 0; i < n; ++i) {
            op = ops[i];
            if (op->u.text.bounds.y >= fy + OL_CSSPX(20) && op->u.text.bounds.y < fy + OL_CSSPX(60)) {
                if (op->u.text.bounds.x < OL_CSSPX(160)) ++beside;
                else if (op->u.text.bounds.x >= OL_CSSPX(240)) ++under;
                else ++below;                   /* none starts under the frame */
            }
        }
        CHECK(beside >= 2 && under >= 2 && below == 0);
        CHECK(crossing(doc, 0, rect(OL_CSSPX(160), fy + OL_CSSPX(20), OL_CSSPX(80), OL_CSSPX(40)), body, 3) == 0);
        /* justified: the left stretch's last piece ends at the frame (its spaces may hang) */
        {
            ol_unit end = 0;
            for (i = 0; i < n; ++i)
                if (ops[i]->u.text.bounds.y == fy + OL_CSSPX(20) && ops[i]->u.text.bounds.x < OL_CSSPX(160) &&
                    ops[i]->u.text.bounds.x + ops[i]->u.text.bounds.width > end)
                    end = ops[i]->u.text.bounds.x + ops[i]->u.text.bounds.width;
            CHECK(end >= OL_CSSPX(160) - OL_CSSPX(1) && end <= OL_CSSPX(168));
        }
    }

    /* the text box: nothing beside it; its own text inside it */
    {
        ol_unit fy = 0;
        int p = 0;
        for (i = 0; i < ol_display_count(doc); ++i) {
            op = ol_display_get(doc, i);
            if (op->type == OL_OP_TEXT && op->u.text.node_id == ol_node_id(ol_node_by_id(doc, ol_node_id(f3) + 1))) {
                fy = op->u.text.bounds.y;
                p = op->page;
                CHECK(op->u.text.bounds.x == OL_CSSPX(100));
                break;
            }
        }
        CHECK(fy > 0);
        CHECK(crossing(doc, p, rect(OL_CSSPX(20), fy, OL_CSSPX(360), OL_CSSPX(30)), body, 3) == 0);
    }

    /* where the frames went: the picture at the top left of the body, the box on its page */
    {
        ol_rect r;
        int pg = -1;
        CHECK(ol_frame_place(doc, f1, &pg, &r) && pg == 0 && r.x == OL_CSSPX(20) && r.y == OL_CSSPX(20) &&
              r.width == OL_CSSPX(100) && r.height == OL_CSSPX(60));
        CHECK(ol_frame_place(doc, f3, &pg, &r) && r.width == OL_CSSPX(100) && r.height == OL_CSSPX(30));
        CHECK(!ol_frame_place(doc, p1, &pg, &r) && !ol_frame_place(NULL, f1, &pg, &r));
    }

    /* the table's cell is where it would be; the frame over it too */
    op = first_text(doc, cellp);
    CHECK(op && op->u.text.bounds.x < OL_CSSPX(40));
    op = first_text(doc, t4);
    CHECK(op && op->u.text.bounds.x == OL_CSSPX(20));

    /* without wrap the same document has the picture's text at the margin */
    box = *ol_node_box(f1);
    box.wrap = OL_WRAP_NONE;
    ol_node_set_box(f1, &box);
    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    op = first_text(doc, ol_node_by_id(doc, body[0]));
    CHECK(op && op->u.text.bounds.x == OL_CSSPX(20) && op->u.text.bounds.y == OL_CSSPX(20));

    /* a program built for 0.5's ol_box_style (gfopenlayout.library 5 or 6): no wrap */
    box.wrap = OL_WRAP_SQUARE;
    box.size = (uint32_t)OL_BOX_STYLE_SIZE_0_5;
    ol_node_set_box(f1, &box);
    CHECK(ol_node_box(f1)->wrap == OL_WRAP_NONE && ol_node_box(f1)->anchor == box.anchor);

    /* without pages: round the frame where ol_layout puts it in the galley */
    box.size = sizeof(box);
    ol_node_set_box(f1, &box);
    CHECK(ol_layout(doc, OL_CSSPX(360), measure, NULL));
    op = first_text(doc, ol_node_by_id(doc, body[0]));
    CHECK(op && op->u.text.bounds.x == OL_CSSPX(110) && op->u.text.bounds.y == 0);
    ol_document_free(doc);
}

/* A frame on a later page, from its paragraph; a frame that pushes the text it is anchored in onto the next
 * page; an empty line under a frame. */
static void test_wrap_pages(void)
{
    ol_document *doc = ol_document_new();
    ol_page page;
    ol_node *b, *f, *empty, *lastp = NULL;
    ol_id body[40];
    size_t nb = 0, i;
    int unsettled = ol_wrap_unsettled, p;
    char line[64];
    const ol_display_op *op;

    memset(&page, 0, sizeof(page));
    page.width = OL_CSSPX(400);
    page.height = OL_CSSPX(310);
    page.margin_top = page.margin_bottom = OL_CSSPX(20);
    page.margin_left = page.margin_right = OL_CSSPX(20);
    for (i = 0; i < 20; ++i) {
        b = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
        snprintf(line, sizeof line, "line %02u with some more words to fill it up to the end", (unsigned)i);
        body[nb++] = ol_node_id(text(doc, b, line, OL_CSSPX(16), 0));
        if (i == 4) {                           /* a page break, and a picture at the new page's top left */
            ol_box_style box;
            ol_box_style_init(&box);
            box.flags = OL_BOX_PAGE_BREAK_BEFORE;
            ol_node_set_box(b, &box);
            f = wrapped(doc, b, OL_ANCHOR_X(OL_ANCHOR_MARGIN) | OL_ANCHOR_Y(OL_ANCHOR_BLOCK), 0, 0,
                        OL_CSSPX(80), OL_CSSPX(30), OL_WRAP_SQUARE, OL_CSSPX(10));
            picture(doc, f, 5, OL_CSSPX(80), OL_CSSPX(30));
        }
        if (i == 16) {                          /* on page 2: a picture at the right, from this paragraph */
            f = wrapped(doc, b, OL_ANCHOR_X(OL_ANCHOR_MARGIN) | OL_ANCHOR_Y(OL_ANCHOR_BLOCK), OL_CSSPX(260), 0,
                        OL_CSSPX(100), OL_CSSPX(100), OL_WRAP_SQUARE, OL_CSSPX(10));
            picture(doc, f, 3, OL_CSSPX(100), OL_CSSPX(100));
        }
        lastp = b;
    }
    /* an empty line (a word processor's) under a top and bottom frame anchored in it */
    empty = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    {
        ol_box_style box;
        ol_box_style_init(&box);
        box.flags = OL_BOX_EMPTY_LINE;
        ol_node_set_box(empty, &box);
    }
    f = wrapped(doc, empty, OL_ANCHOR_X(OL_ANCHOR_MARGIN) | OL_ANCHOR_Y(OL_ANCHOR_BLOCK), 0, 0,
                OL_CSSPX(50), OL_CSSPX(50), OL_WRAP_TOP_BOTTOM, 0);
    picture(doc, f, 4, OL_CSSPX(50), OL_CSSPX(50));
    b = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    body[nb++] = ol_node_id(text(doc, b, "after the empty line", OL_CSSPX(16), 0));
    (void)lastp;

    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    CHECK(ol_wrap_unsettled == unsettled);
    /* page 1 is as it was: every line at the margin */
    for (i = 0; i < ol_display_count(doc); ++i) {
        op = ol_display_get(doc, i);
        if (op->type == OL_OP_TEXT && op->page == 0 && op->u.text.text_offset == 0)
            CHECK(op->u.text.bounds.x == OL_CSSPX(20));
    }
    /* on every page, no body text under the frames */
    for (p = 0; p < ol_page_count(doc); ++p) {
        size_t first, count, k;
        CHECK(ol_display_page_range(doc, p, &first, &count));
        for (k = first; k < first + count; ++k) {
            op = ol_display_get(doc, k);
            if (op->type == OL_OP_IMAGE)
                CHECK(crossing(doc, p, op->u.image.bounds, body, nb) == 0);
        }
    }
    /* the picture's paragraph wraps: its lines end before the picture */
    op = first_text(doc, ol_node_by_id(doc, body[16]));
    CHECK(op && op->page == 2);
    /* after the break: the picture on the new page, the paragraph's first lines beside it */
    op = first_text(doc, ol_node_by_id(doc, body[4]));
    CHECK(op && op->page == 1 && op->u.text.bounds.x == OL_CSSPX(110) && op->u.text.bounds.y == OL_CSSPX(20));
    for (i = 0; i < ol_display_count(doc); ++i)
        if (ol_display_get(doc, i)->type == OL_OP_IMAGE && ol_display_get(doc, i)->u.image.image_id == 5)
            CHECK(ol_display_get(doc, i)->page == 1 && ol_display_get(doc, i)->u.image.bounds.y == OL_CSSPX(20));
    /* the paragraph after the empty line is below the frame in it */
    {
        const ol_display_op *pic = NULL, *after = first_text(doc, ol_node_by_id(doc, body[nb - 1]));
        for (i = 0; i < ol_display_count(doc); ++i)
            if (ol_display_get(doc, i)->type == OL_OP_IMAGE && ol_display_get(doc, i)->u.image.image_id == 4)
                pic = ol_display_get(doc, i);
        CHECK(pic && after && after->page == pic->page &&
              after->u.text.bounds.y >= pic->u.image.bounds.y + pic->u.image.bounds.height);
    }
    ol_document_free(doc);
}

/* Random documents with frames to wrap round: no body text under them, wherever the passes settled. The
 * first 30 are a word processor's usual: pictures and boxes placed from their paragraphs, below their
 * corners, and the cover's from the page; laid out in one pass, or two. The other 60 put many frames from
 * their pages anywhere, above their paragraphs too, on small pages. */
static void test_wrap_random(void)
{
    int round, settled = 0, usual_passes = 0, layouts = ol_wrap_layouts, passes0 = ol_wrap_passes;
    for (round = -30; round < 60; ++round) {
        ol_document *doc = ol_document_new();
        ol_page page;
        ol_id body[128];
        size_t nb = 0, first, count, k, f;
        int i, n = 10 + (int)next_rand(40), p, unsettled = ol_wrap_unsettled;
        ol_node *frames[64];
        size_t nf = 0;
        memset(&page, 0, sizeof(page));
        page.width = OL_CSSPX(200 + next_rand(200));
        page.height = OL_CSSPX(150 + next_rand(150));
        page.margin_top = page.margin_bottom = OL_CSSPX(10 + next_rand(20));
        page.margin_left = page.margin_right = OL_CSSPX(10 + next_rand(20));
        for (i = 0; i < n; ++i) {
            unsigned what = next_rand(6);
            ol_node *b = block(doc, NULL, OL_ROLE_PARAGRAPH, next_rand(3) == 0 ? OL_ALIGN_JUSTIFY
                                                              : next_rand(2) ? OL_ALIGN_START : OL_ALIGN_CENTER);
            body[nb++] = ol_node_id(text(doc, b, what == 0 ? "a short line" :
                                         "one two three four five six seven eight nine ten eleven twelve", OL_CSSPX(16), 0));
            if (what >= 3 && nf < 64) {
                uint32_t from_y = round < 0 ? (i == 0 ? OL_ANCHOR_PAGE : OL_ANCHOR_BLOCK)
                                : next_rand(2) ? OL_ANCHOR_BLOCK : OL_ANCHOR_MARGIN;
                ol_node *fr = wrapped(doc, b, OL_ANCHOR_X(next_rand(2) ? OL_ANCHOR_MARGIN : OL_ANCHOR_PAGE) |
                                                  OL_ANCHOR_Y(from_y),
                                      OL_CSSPX(next_rand(150)), OL_CSSPX(next_rand(100)),
                                      OL_CSSPX(20 + next_rand(100)), OL_CSSPX(10 + next_rand(80)),
                                      what == 5 ? OL_WRAP_TOP_BOTTOM : OL_WRAP_SQUARE, OL_CSSPX(next_rand(10)));
                picture(doc, fr, (int)nf, OL_CSSPX(10), OL_CSSPX(10));
                frames[nf++] = fr;
            }
        }
        {
            int before = ol_wrap_passes;
            CHECK(ol_layout_pages(doc, &page, measure, NULL));
            if (round < 0) {
                CHECK(ol_wrap_unsettled == unsettled && ol_wrap_passes - before <= 2);
                usual_passes += ol_wrap_passes - before;
            }
        }
        if (ol_wrap_unsettled == unsettled) {
            if (round >= 0) ++settled;
            /* each frame's place on its page: its picture's corner and its own size */
            for (p = 0; p < ol_page_count(doc); ++p) {
                CHECK(ol_display_page_range(doc, p, &first, &count));
                for (k = first; k < first + count; ++k) {
                    const ol_display_op *op = ol_display_get(doc, k);
                    if (op->type != OL_OP_IMAGE) continue;
                    f = (size_t)op->u.image.image_id;
                    if (f < nf) {
                        const ol_box_style *box = ol_node_box(frames[f]);
                        ol_rect r = rect(op->u.image.bounds.x - box->wrap_gap[3], op->u.image.bounds.y,
                                         box->frame.width + box->wrap_gap[1] + box->wrap_gap[3], box->frame.height);
                        int c = crossing(doc, p, r, body, nb);
                        if (c) printf("round %d page %d frame %u crossed by %d\n", round, p, (unsigned)f, c);
                        CHECK(c == 0);
                    }
                }
            }
        }
        /* and again: the same */
        {
            size_t ops = ol_display_count(doc);
            int pages = ol_page_count(doc);
            CHECK(ol_layout_pages(doc, &page, measure, NULL));
            CHECK(ol_display_count(doc) == ops && ol_page_count(doc) == pages);
        }
        ol_document_free(doc);
    }
    printf("OPENLAYOUT WRAP usual %d passes for 30, random %d of 60 settled, %d passes in %d layouts\n", usual_passes,
           settled, ol_wrap_passes - passes0, ol_wrap_layouts - layouts);
    CHECK(settled >= 50);
}

/* 0.7: a block's lines exactly its line height, though its text is taller (OL_BOX_EXACT_LINES); without it the
 * line height is the least a line is */
static void test_exact_lines(void)
{
    ol_document *doc = ol_document_new();
    ol_node *exact, *least, *te, *tl;
    ol_style s;
    ol_box_style box;
    const ol_display_op *ops[16];
    size_t n, i;
    exact = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    s = *ol_node_style(exact);
    s.line_height = OL_CSSPX(10);
    ol_node_set_style(exact, &s);
    ol_box_style_init(&box);
    box.flags = OL_BOX_EXACT_LINES;
    ol_node_set_box(exact, &box);
    te = text(doc, exact, "aaa bbb ccc ddd eee fff ggg hhh", OL_CSSPX(16), 0);
    least = block(doc, NULL, OL_ROLE_PARAGRAPH, OL_ALIGN_START);
    s = *ol_node_style(least);
    s.line_height = OL_CSSPX(30);
    ol_node_set_style(least, &s);
    tl = text(doc, least, "aaa bbb ccc ddd eee fff ggg hhh", OL_CSSPX(16), 0);
    s = *ol_node_style(tl);
    s.line_height = OL_CSSPX(30);                /* a line is at least its runs' line height */
    ol_node_set_style(tl, &s);
    /* the text's own line height under the exact one, so that only the flag makes them 10 px */
    s = *ol_node_style(te);
    s.line_height = OL_CSSPX(10);
    ol_node_set_style(te, &s);
    CHECK(ol_layout(doc, OL_CSSPX(120), measure, NULL));
    n = text_ops(doc, ol_node_id(te), ops, 16);
    CHECK(n == 8);
    /* 15 characters a line: two lines 10 px apart, the text standing on each line's bottom (4 px of descent) */
    for (i = 0; i < n; ++i) {
        ol_unit row = ops[i]->u.text.bounds.y / OL_CSSPX(10);
        CHECK(ops[i]->u.text.bounds.y == row * OL_CSSPX(10) && ops[i]->u.text.bounds.height == OL_CSSPX(10));
        CHECK(ops[i]->u.text.baseline == ops[i]->u.text.bounds.y + OL_CSSPX(10) - OL_CSSPX(4));
    }
    CHECK(ops[n - 1]->u.text.bounds.y == OL_CSSPX(10));
    /* at least 30 px: the second paragraph's two lines are 30 px, its text 16, after the first's 20 */
    n = text_ops(doc, ol_node_id(tl), ops, 16);
    CHECK(n == 8 && ops[0]->u.text.bounds.y == OL_CSSPX(20) && ops[0]->u.text.bounds.height == OL_CSSPX(30) &&
          ops[n - 1]->u.text.bounds.y == OL_CSSPX(50));
    ol_document_free(doc);
}

typedef struct {
    ol_op_type type;
    union {
        ol_text_op text;
        ol_fill_rect_op fill_rect;
        ol_image_op image;
        ol_rule_op rule;
    } u;
    int32_t page;
} ol_display_op_v12;

typedef struct {
    uint32_t size;
    uint32_t type;
    uint32_t fill_argb;
    uint32_t stroke_argb;
    ol_unit stroke_width;
    int32_t radius_permille;
    uint32_t flags;
    uint32_t stroke_style;
} ol_shape_style_v13;

typedef struct {
    int shapes;
    ol_id hidden_id;
    int hidden_drawn;
    ol_device_op shape;
} drawing_capture;

static void capture_drawing(void *userdata, const ol_device_op *op)
{
    drawing_capture *c = (drawing_capture *)userdata;
    if (op->op->type == OL_OP_SHAPE) {
        c->shape = *op;
        ++c->shapes;
    }
    if (op->op->type == OL_OP_FILL_RECT && op->op->u.fill_rect.node_id == c->hidden_id)
        ++c->hidden_drawn;
}

static void test_drawing_v14(void)
{
    ol_document *doc = ol_document_new();
    ol_node *frame = block(doc, NULL, OL_ROLE_BLOCK, OL_ALIGN_START);
    ol_node *inner = block(doc, frame, OL_ROLE_BLOCK, OL_ALIGN_START);
    ol_node *image = ol_node_append(doc, inner, OL_ROLE_IMAGE, NULL);
    ol_node *outside = block(doc, inner, OL_ROLE_BLOCK, OL_ALIGN_START);
    ol_box_style box_style, inner_style, outside_style;
    ol_style outside_paint;
    ol_shape_style shape_style;
    ol_point points[3] = {{500, 0}, {1000, 1000}, {0, 1000}};
    ol_image_crop crop = {100, 50, 200, 150};
    const ol_display_op *shape = NULL, *picture = NULL;
    drawing_capture capture;
    ol_scale scale;
    ol_page page;
    ol_rect path_bounds;
    ol_point path[OL_MAX_SHAPE_POINTS];
    int path_count, closed;
    size_t i;

    /* Version 13 only appends fields: every v12 field retains its offset. */
    CHECK(offsetof(ol_display_op_v12, type) == offsetof(ol_display_op, type));
    CHECK(offsetof(ol_display_op_v12, u) == offsetof(ol_display_op, u));
    CHECK(offsetof(ol_display_op_v12, page) == offsetof(ol_display_op, page));
    CHECK(sizeof(ol_display_op_v12) == offsetof(ol_display_op, graphic));
    /* Version 14 only appends rotation to the size-versioned shape style. */
    CHECK(sizeof(ol_shape_style_v13) == offsetof(ol_shape_style, rotation_tenths));

    ol_box_style_init(&box_style);
    box_style.flags = OL_BOX_ABSOLUTE | OL_BOX_CLIP_CONTENT;
    box_style.frame.x = OL_CSSPX(10);
    box_style.frame.y = OL_CSSPX(20);
    box_style.frame.width = OL_CSSPX(100);
    box_style.frame.height = OL_CSSPX(50);
    ol_node_set_box(frame, &box_style);
    ol_box_style_init(&inner_style);
    inner_style.flags = OL_BOX_CLIP_CONTENT;
    inner_style.width = OL_CSSPX(60);
    inner_style.height = OL_CSSPX(30);
    ol_node_set_box(inner, &inner_style);
    ol_box_style_init(&outside_style);
    outside_style.flags = OL_BOX_ABSOLUTE;
    outside_style.frame.x = OL_CSSPX(100);
    outside_style.frame.width = outside_style.frame.height = OL_CSSPX(10);
    ol_node_set_box(outside, &outside_style);
    outside_paint = *ol_node_style(outside);
    outside_paint.background = 0xffff0000u;
    ol_node_set_style(outside, &outside_paint);

    ol_shape_style_init(&shape_style);
    shape_style.type = OL_SHAPE_POLYGON;
    shape_style.fill_argb = 0xff336699u;
    shape_style.stroke_argb = 0xff102030u;
    shape_style.stroke_width = OL_CSSPX(2);
    shape_style.radius_permille = 200;
    shape_style.flags = OL_SHAPE_ARROW_END;
    shape_style.stroke_style = OL_STROKE_DASHED;
    shape_style.rotation_tenths = 900;
    CHECK(ol_node_set_shape(frame, &shape_style, 3, points));
    points[0].x = 7; /* the node owns a copy */

    ol_node_set_image(image, 42, OL_CSSPX(120), OL_CSSPX(80));
    CHECK(ol_node_set_image_crop(image, &crop));
    crop.left = 999;
    CHECK(!ol_node_set_image_crop(image, &crop));
    CHECK(ol_layout(doc, OL_CSSPX(200), measure, NULL));

    for (i = 0; i < ol_display_count(doc); ++i) {
        const ol_display_op *op = ol_display_get(doc, i);
        if (op->type == OL_OP_SHAPE) shape = op;
        if (op->type == OL_OP_IMAGE && op->u.image.image_id == 42) picture = op;
    }
    CHECK(shape && shape->u.fill_rect.node_id == ol_node_id(frame));
    CHECK(shape && shape->graphic && shape->graphic->point_count == 3);
    CHECK(shape && shape->graphic && shape->graphic->points[0].x == 500);
    CHECK(shape && shape->graphic && shape->graphic->shape.stroke_style == OL_STROKE_DASHED);
    CHECK(shape && shape->graphic && shape->graphic->shape.rotation_tenths == 900);
    CHECK(shape && shape->clip.height != 0);
    CHECK(shape && shape->clip.x == OL_CSSPX(10) && shape->clip.y == OL_CSSPX(20));
    CHECK(shape && shape->clip.width == OL_CSSPX(100) && shape->clip.height == OL_CSSPX(50));
    CHECK(picture && picture->graphic && picture->graphic->crop.left == 100);
    CHECK(picture && picture->graphic->crop.bottom == 150);
    CHECK(picture && picture->clip.height != 0);
    CHECK(picture && picture->clip.width == OL_CSSPX(60));
    CHECK(picture && picture->clip.height == OL_CSSPX(30));

    path_bounds.x = 10;
    path_bounds.y = 20;
    path_bounds.width = 100;
    path_bounds.height = 50;
    closed = 0;
    path_count = ol_shape_path(shape->graphic, &path_bounds, path,
                               OL_MAX_SHAPE_POINTS, &closed);
    CHECK(path_count == 3 && closed);
    CHECK(path[0].x == 35 && path[0].y == 45);
    CHECK(path[1].x == 85 && path[1].y == -5);
    CHECK(path[2].x == 85 && path[2].y == 95);
    CHECK(ol_shape_path(shape->graphic, &path_bounds, path, 2, &closed) == 0);
    {
        ol_graphic primitive = *shape->graphic;
        primitive.shape.type = OL_SHAPE_RECT;
        primitive.shape.rotation_tenths = -900;
        path_count = ol_shape_path(&primitive, &path_bounds, path,
                                   OL_MAX_SHAPE_POINTS, &closed);
        CHECK(path_count == 4 && closed);
        CHECK(path[0].x == 85 && path[0].y == -5);
        CHECK(path[1].x == 85 && path[1].y == 95);
        CHECK(path[2].x == 35 && path[2].y == 95);
        CHECK(path[3].x == 35 && path[3].y == -5);
        primitive.shape.type = OL_SHAPE_ELLIPSE;
        primitive.shape.rotation_tenths = 0;
        CHECK(ol_shape_path(&primitive, &path_bounds, path,
                            OL_MAX_SHAPE_POINTS, &closed) == 24 && closed);
        primitive.shape.type = OL_SHAPE_ROUND_RECT;
        primitive.shape.radius_permille = 200;
        CHECK(ol_shape_path(&primitive, &path_bounds, path,
                            OL_MAX_SHAPE_POINTS, &closed) == 28 && closed);
    }

    memset(&capture, 0, sizeof(capture));
    capture.hidden_id = ol_node_id(outside);
    memset(&scale, 0, sizeof(scale));
    ol_scale_set(&scale, 1, OL_UNITS_PER_CSSPX);
    CHECK(ol_draw_page(doc, 0, &scale, NULL, capture_drawing, &capture) > 0);
    CHECK(capture.shapes == 1);
    CHECK(capture.hidden_drawn == 0);
    CHECK(capture.shape.box.x == 10 && capture.shape.box.y == 20);
    CHECK(capture.shape.box.width == 100 && capture.shape.box.height == 50);
    CHECK(capture.shape.thickness == 2);
    CHECK(capture.shape.clip.x == 10 && capture.shape.clip.y == 20);
    CHECK(capture.shape.clip.width == 100 && capture.shape.clip.height == 50);

    memset(&page, 0, sizeof(page));
    page.width = page.height = OL_CSSPX(200);
    page.margin_top = page.margin_right = page.margin_bottom = page.margin_left = OL_CSSPX(5);
    CHECK(ol_layout_pages(doc, &page, measure, NULL));
    shape = NULL;
    for (i = 0; i < ol_display_count(doc); ++i)
        if (ol_display_get(doc, i)->type == OL_OP_SHAPE) shape = ol_display_get(doc, i);
    CHECK(shape && shape->page == 0);
    CHECK(shape && shape->u.fill_rect.bounds.x == OL_CSSPX(15));
    CHECK(shape && shape->u.fill_rect.bounds.y == OL_CSSPX(25));
    CHECK(shape && shape->clip.x == OL_CSSPX(15) && shape->clip.y == OL_CSSPX(25));
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
    test_page_columns();
    test_page_sections();
    test_page_backgrounds_and_rows();
    test_editing();
    test_box_versions();
    test_scale();
    test_tabs();
    test_aligned_tabs();
    test_sort_in_place();
    test_wrap_frames();
    test_wrap_pages();
    test_wrap_random();
    test_exact_lines();
    test_drawing_v14();
    if (failures) {
        printf("OPENLAYOUT DOCUMENT FAIL (%d)\n", failures);
        return 20;
    }
    printf("OPENLAYOUT DOCUMENT PASS version=%s\n", OL_VERSION);
    return 0;
}
