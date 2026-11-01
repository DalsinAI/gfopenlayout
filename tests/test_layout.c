#include "gfopenlayout.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); return 20; \
} } while (0)

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

static size_t count_type(const ol_document *doc, ol_op_type type)
{
    size_t i, n = 0;
    for (i = 0; i < ol_display_count(doc); ++i) {
        const ol_display_op *op = ol_display_get(doc, i);
        if (op && op->type == type) ++n;
    }
    return n;
}

int main(void)
{
    ol_document *doc;
    ol_node *root, *heading, *heading_text, *para, *t1, *link, *t2, *image;
    ol_style s;
    ol_id link_id;
    ol_rect link_box;
    ol_unit wide_height, narrow_height;
    const ol_node *hit, *found;

    CHECK(OL_POINT(1) == 80);
    CHECK(OL_CSSPX(1) == 60);
    CHECK(OL_TWIP(1) == 4);
    CHECK(OL_UNITS_PER_INCH == 5760);

    doc = ol_document_new();
    CHECK(doc != NULL);
    root = ol_document_root(doc);
    CHECK(root != NULL);

    heading = ol_node_append(doc, root, OL_ROLE_HEADING, NULL);
    CHECK(heading != NULL);
    ol_style_init(&s);
    s.display = OL_DISPLAY_BLOCK;
    s.font_size = OL_CSSPX(24);
    s.line_height = OL_CSSPX(30);
    s.margin_bottom = OL_CSSPX(8);
    s.foreground = 0xff202020u;
    ol_node_set_style(heading, &s);
    CHECK(ol_node_set_name(heading, "page-title"));

    heading_text = ol_node_append(doc, heading, OL_ROLE_TEXT, "GFOpenLayout first light");
    CHECK(heading_text != NULL);
    s.display = OL_DISPLAY_INLINE;
    ol_node_set_style(heading_text, &s);

    para = ol_node_append(doc, root, OL_ROLE_PARAGRAPH, NULL);
    CHECK(para != NULL);
    ol_style_init(&s);
    s.display = OL_DISPLAY_BLOCK;
    s.line_height = OL_CSSPX(20);
    s.margin_bottom = OL_CSSPX(10);
    s.padding_left = OL_CSSPX(4);
    s.padding_right = OL_CSSPX(4);
    s.background = 0x10000000u;
    ol_node_set_style(para, &s);

    t1 = ol_node_append(doc, para, OL_ROLE_TEXT,
        "A tiny semantic layout engine can render a useful page and keep ");
    CHECK(t1 != NULL);
    s.display = OL_DISPLAY_INLINE;
    s.background = 0;
    ol_node_set_style(t1, &s);

    link = ol_node_append(doc, para, OL_ROLE_LINK, "this link");
    CHECK(link != NULL);
    s.text_flags = OL_TEXT_UNDERLINE;
    s.foreground = 0xff0000c0u;
    ol_node_set_style(link, &s);
    CHECK(ol_node_set_name(link, "example-link"));
    CHECK(ol_node_set_href(link, "https://example.com/"));
    link_id = ol_node_id(link);

    t2 = ol_node_append(doc, para, OL_ROLE_TEXT,
        " without needing WebCore.");
    CHECK(t2 != NULL);
    s.text_flags = 0;
    s.foreground = 0xff000000u;
    ol_node_set_style(t2, &s);

    image = ol_node_append(doc, root, OL_ROLE_IMAGE, NULL);
    CHECK(image != NULL);
    ol_style_init(&s);
    s.display = OL_DISPLAY_INLINE;
    ol_node_set_style(image, &s);
    ol_node_set_image(image, 7, OL_CSSPX(64), OL_CSSPX(32));

    CHECK(ol_layout(doc, OL_CSSPX(420), measure, NULL));
    CHECK(ol_document_generation(doc) == 1);
    wide_height = ol_document_content_height(doc);
    CHECK(wide_height > 0);
    CHECK(ol_display_count(doc) >= 5);
    CHECK(count_type(doc, OL_OP_TEXT) >= 4);
    CHECK(count_type(doc, OL_OP_FILL_RECT) == 1);
    CHECK(count_type(doc, OL_OP_IMAGE) == 1);

    found = ol_find_role_name(doc, OL_ROLE_LINK, "example-link");
    CHECK(found != NULL);
    CHECK(ol_node_id(found) == link_id);
    CHECK(strcmp(ol_node_href(found), "https://example.com/") == 0);
    CHECK(ol_node_actions(found) & OL_ACTION_ACTIVATE);

    link_box = ol_node_bounds(found);
    CHECK(link_box.width > 0 && link_box.height > 0);
    hit = ol_hit_test(doc, link_box.x + 1, link_box.y + 1);
    CHECK(hit != NULL);
    CHECK(ol_node_id(hit) == link_id);
    hit = ol_hit_action(doc, link_box.x + 1, link_box.y + 1, OL_ACTION_ACTIVATE);
    CHECK(hit != NULL);
    CHECK(ol_node_id(hit) == link_id);

    CHECK(ol_layout(doc, OL_CSSPX(180), measure, NULL));
    CHECK(ol_document_generation(doc) == 2);
    narrow_height = ol_document_content_height(doc);
    CHECK(narrow_height > wide_height);
    CHECK(ol_node_id(ol_find_role_name(doc, OL_ROLE_LINK, "example-link")) == link_id);

    printf("OPENLAYOUT PASS version=%s nodes_link_id=%lu wide_px=%ld narrow_px=%ld ops=%lu\n",
           OL_VERSION,
           (unsigned long)link_id,
           (long)(wide_height / OL_UNITS_PER_CSSPX),
           (long)(narrow_height / OL_UNITS_PER_CSSPX),
           (unsigned long)ol_display_count(doc));

    ol_document_free(doc);
    return 0;
}
