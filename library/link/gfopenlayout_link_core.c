/* Made by tools/gen-library.py from the headers: don't edit. GFOpenLayout's document
 * calls (gfopenlayout.h) through gfopenlayout.library 24: a program links this
 * (libgfopenlayout_link.a) where it compiled src/openlayout.c in;
 * gfopenlayout_link_base.c opens the library for it. On an older library the first
 * call stops the program and says which it needs.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdlib.h>
#include <string.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/gfopenlayout.h>

static void ol_link_core_needs(void) __attribute__((noreturn));
static void ol_link_core_needs(void)
{
    static const char why[] = "This needs gfopenlayout.library 24 or later in LIBS:\n";
    struct Library *dos = OpenLibrary((CONST_STRPTR)"dos.library", 36);
    if (dos) {
        BPTR out = LP0(0x3c, BPTR, Output, , dos);     /* Output() */
        if (out) LP3(0x30, LONG, Write, BPTR, out, d1, APTR, (APTR)why, d2, LONG, (LONG)sizeof(why) - 1, d3, , dos);
        CloseLibrary(dos);
    }
    exit(RETURN_FAIL);
}

ol_document * ol_document_new(void)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_DocumentNew();
}

ol_document * ol_document_new_reserved(size_t ops)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_DocumentNewReserved(ops);
}

void ol_document_free(ol_document *doc)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    OLAPI_DocumentFree(doc);
}

const ol_node * ol_document_root_const(const ol_document *doc)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_DocumentRootConst(doc);
}

ol_node * ol_node_append(ol_document *doc, ol_node *parent, ol_role role, const char *utf8)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodeAppend(doc, parent, role, utf8);
}

const ol_node * ol_node_by_id(const ol_document *doc, ol_id id)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodeById(doc, id);
}

const ol_node * ol_node_parent(const ol_node *node)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodeParent(node);
}

ol_id ol_node_id(const ol_node *node)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodeId(node);
}

ol_role ol_node_role(const ol_node *node)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodeRole(node);
}

const char * ol_node_text(const ol_node *node)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodeText(node);
}

const char * ol_node_href(const ol_node *node)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodeHref(node);
}

int ol_node_set_href(ol_node *node, const char *utf8)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodeSetHref(node, utf8);
}

const ol_style * ol_node_style(const ol_node *node)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodeStyle(node);
}

void ol_node_set_style(ol_node *node, const ol_style *style)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    OLAPI_NodeSetStyle(node, style);
}

void ol_style_init(ol_style *style)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    OLAPI_StyleInit(style);
}

void ol_box_style_init_sized(ol_box_style *box, size_t size)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    OLAPI_BoxStyleInitSized(box, size);
}

const ol_box_style * ol_node_box(const ol_node *node)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodeBox(node);
}

void ol_node_set_box(ol_node *node, const ol_box_style *box)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    OLAPI_NodeSetBox(node, box);
}

int ol_node_set_columns(ol_node *node, int count, const ol_unit *widths)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodeSetColumns(node, count, widths);
}

void ol_node_set_image(ol_node *node, int image_id, ol_unit width, ol_unit height)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    OLAPI_NodeSetImage(node, image_id, width, height);
}

int ol_node_set_tab_stops(ol_node *node, int count, const ol_tab_stop *stops)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodeSetTabStops(node, count, stops);
}

void ol_node_bounds_to(const ol_node *node, ol_rect *out)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    OLAPI_NodeBoundsTo(node, out);
}

int ol_node_page_box(const ol_document *doc, ol_id node_id, int *page, ol_rect *box)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_NodePageBox(doc, node_id, page, box);
}

int ol_frame_place(const ol_document *doc, const ol_node *frame, int *page, ol_rect *rect)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_FramePlace(doc, frame, page, rect);
}

int ol_layout(ol_document *doc, ol_unit viewport_width, ol_measure_text_fn measure, void *measure_userdata)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_Layout(doc, viewport_width, measure, measure_userdata);
}

int ol_layout_pages(ol_document *doc, const ol_page *page, ol_measure_text_fn measure, void *measure_userdata)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_LayoutPages(doc, page, measure, measure_userdata);
}

int ol_layout_pages_columns(ol_document *doc, const ol_page *page, int count, ol_unit gap, ol_measure_text_fn measure, void *measure_userdata)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_LayoutPagesColumns(doc, page, count, gap, measure, measure_userdata);
}

int ol_layout_pages_notes(ol_document *doc, const ol_page *page, const ol_page_note *notes, int note_count, ol_unit separator, int *note_page, ol_measure_text_fn measure, void *measure_userdata)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_LayoutPagesNotes(doc, page, notes, note_count, separator, note_page, measure, measure_userdata);
}

int ol_layout_pages_sections(ol_document *doc, const ol_page *page, const ol_page_section *sections, int section_count, ol_measure_text_fn measure, void *measure_userdata)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_LayoutPagesSections(doc, page, sections, section_count, measure, measure_userdata);
}

int ol_page_count(const ol_document *doc)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_PageCount(doc);
}

size_t ol_display_count(const ol_document *doc)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_DisplayCount(doc);
}

const ol_display_op * ol_display_get(const ol_document *doc, size_t index)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_DisplayGet(doc, index);
}

int ol_display_page_range(const ol_document *doc, int page, size_t *first, size_t *count)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_DisplayPageRange(doc, page, first, count);
}

const ol_node * ol_hit_test_page(const ol_document *doc, int page, ol_unit x, ol_unit y)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_HitTestPage(doc, page, x, y);
}

int ol_hit_text(const ol_document *doc, int page, ol_unit x, ol_unit y, ol_measure_text_fn measure, void *measure_userdata, ol_text_position *pos)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_HitText(doc, page, x, y, measure, measure_userdata, pos);
}

int ol_caret_box(const ol_document *doc, const ol_text_position *pos, ol_measure_text_fn measure, void *measure_userdata, int *page, ol_rect *box)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_CaretBox(doc, pos, measure, measure_userdata, page, box);
}

size_t ol_draw_page(const ol_document *doc, int page, const ol_scale *scale, const ol_pixel_rect *clip, ol_draw_fn draw, void *userdata)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_DrawPage(doc, page, scale, clip, draw, userdata);
}

void ol_scale_set(ol_scale *scale, int32_t num, int32_t den)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    OLAPI_ScaleSet(scale, num, den);
}

void ol_scale_fit(ol_scale *scale, ol_unit length, int32_t pixels)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    OLAPI_ScaleFit(scale, length, pixels);
}

int32_t ol_scale_x(const ol_scale *scale, ol_unit x)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_ScaleX(scale, x);
}

int32_t ol_scale_y(const ol_scale *scale, ol_unit y)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_ScaleY(scale, y);
}

int32_t ol_scale_length(const ol_scale *scale, ol_unit length)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_ScaleLength(scale, length);
}

void ol_scale_rect_to(const ol_scale *scale, const ol_rect *r, ol_pixel_rect *out)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    OLAPI_ScaleRectTo(scale, r, out);
}

ol_unit ol_unscale_x(const ol_scale *scale, int32_t px)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_UnscaleX(scale, px);
}

ol_unit ol_unscale_y(const ol_scale *scale, int32_t py)
{
    if (OpenLayoutBase->lib_Version < 24) ol_link_core_needs();
    return OLAPI_UnscaleY(scale, py);
}

/* ---- the C calls the library carries in another shape ---- */
void ol_box_style_init(ol_box_style *box)
{
    if (!box) return;
    memset(box, 0, sizeof(*box));
    ol_box_style_init_sized(box, sizeof(*box));   /* this program's size, whatever the library's is */
}

ol_rect ol_node_bounds(const ol_node *node)
{
    ol_rect r = {0, 0, 0, 0};
    ol_node_bounds_to(node, &r);
    return r;
}

ol_pixel_rect ol_scale_rect(const ol_scale *scale, ol_rect r)
{
    ol_pixel_rect p = {0, 0, 0, 0};
    ol_scale_rect_to(scale, &r, &p);
    return p;
}
