#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/gfopenlayout.h>

#include <stdio.h>
#include <string.h>

struct Library *OpenLayoutBase;

#define CHECK(x) do { if (!(x)) {     printf("LAYOUTLIB FAIL line=%d expr=%s\n", __LINE__, #x); rc = 20; goto done; } } while (0)

static ULONG drawn;

static void count_op(void *userdata, const ol_device_op *op)
{
    (void)op;
    ++*(ULONG *)userdata;
}

int main(void)
{
    int rc = 0;
    ol_document *doc = NULL;
    ol_node *root, *p, *text, *link;
    struct OLNodeAppend append;
    struct OLNodeStyle style_req;
    struct OLNodeString string_req;
    struct OLLayoutRequest layout_req;
    struct OLFindRoleName find_req;
    struct OLNodeBounds bounds_req;
    struct OLHitAction hit_req;
    ol_style style;
    ol_rect box;
    const ol_node *found, *hit;

    OpenLayoutBase = OpenLibrary((CONST_STRPTR)OPENLAYOUTLIB_NAME, OPENLAYOUTLIB_VERSION);
    CHECK(OpenLayoutBase != NULL);
    CHECK(OL_Version() == 1);
    CHECK(!strcmp((const char *)OL_VersionString(), OL_VERSION));
    CHECK(OpenLayoutBase->lib_Version >= OPENLAYOUTLIB_DOCUMENT_VERSION);

    doc = OL_DocumentNew();
    CHECK(doc != NULL);
    root = OL_DocumentRoot(doc);
    CHECK(root != NULL);

    append.document = doc;
    append.parent = root;
    append.role = OL_ROLE_PARAGRAPH;
    append.text = NULL;
    p = OL_NodeAppend(&append);
    CHECK(p != NULL);

    OL_StyleInit(&style);
    style.display = OL_DISPLAY_BLOCK;
    style.margin_bottom = OL_CSSPX(8);
    style_req.node = p;
    style_req.style = &style;
    OL_NodeSetStyle(&style_req);

    append.parent = p;
    append.role = OL_ROLE_TEXT;
    append.text = (CONST_STRPTR)"Resident GFOpenLayout can lay out ";
    text = OL_NodeAppend(&append);
    CHECK(text != NULL);
    style.display = OL_DISPLAY_INLINE;
    style_req.node = text;
    OL_NodeSetStyle(&style_req);

    append.role = OL_ROLE_LINK;
    append.text = (CONST_STRPTR)"this link";
    link = OL_NodeAppend(&append);
    CHECK(link != NULL);
    style_req.node = link;
    OL_NodeSetStyle(&style_req);

    string_req.node = link;
    string_req.text = (CONST_STRPTR)"example-link";
    CHECK(OL_NodeSetName(&string_req));
    string_req.text = (CONST_STRPTR)"https://example.com/";
    CHECK(OL_NodeSetHref(&string_req));
    OL_NodeSetActions(link, OL_ACTION_ACTIVATE);

    layout_req.document = doc;
    layout_req.viewport_width = OL_CSSPX(240);
    layout_req.measure = NULL;
    layout_req.measure_userdata = NULL;
    CHECK(OL_Layout(&layout_req));
    CHECK(OL_DocumentContentHeight(doc) > 0);
    CHECK(OL_DocumentGeneration(doc) == 1);
    CHECK(OL_DisplayCount(doc) >= 2);

    find_req.document = doc;
    find_req.role = OL_ROLE_LINK;
    find_req.name = (CONST_STRPTR)"example-link";
    found = OL_FindRoleName(&find_req);
    CHECK(found != NULL);
    CHECK(OL_NodeID(found) == OL_NodeID(link));
    CHECK(!strcmp((const char *)OL_NodeHref(found), "https://example.com/"));

    bounds_req.node = found;
    bounds_req.bounds = &box;
    CHECK(OL_NodeBounds(&bounds_req));
    CHECK(box.width > 0 && box.height > 0);

    hit_req.document = doc;
    hit_req.x = box.x + 1;
    hit_req.y = box.y + 1;
    hit_req.action = OL_ACTION_ACTIVATE;
    hit = OL_HitAction(&hit_req);
    CHECK(hit != NULL);
    CHECK(OL_NodeID(hit) == OL_NodeID(link));
    CHECK(OL_NodeByID(doc, OL_NodeID(link)) == link);
    CHECK(OL_NodeRole(link) == OL_ROLE_LINK);
    CHECK(!strcmp((const char *)OL_NodeText(link), "this link"));
    CHECK(!strcmp((const char *)OL_NodeName(link), "example-link"));

    /* version 3: pages, the caret and the character under a point */
    {
        ol_document *paged = OL_DocumentNew();
        struct OLLayoutPagesRequest pages_req;
        struct OLPageRange range_req;
        struct OLHitText hit_text_req;
        struct OLCaretBox caret_req;
        struct OLNodeBox box_req;
        ol_page page;
        ol_box_style bs;
        ol_text_position pos, back;
        ol_rect caret;
        ol_node *last_text = NULL;
        int i;

        CHECK(paged != NULL);
        append.document = paged;
        for (i = 0; i < 20; ++i) {
            append.parent = OL_DocumentRoot(paged);
            append.role = OL_ROLE_PARAGRAPH;
            append.text = NULL;
            p = OL_NodeAppend(&append);
            CHECK(p != NULL);
            OL_BoxStyleInit(&bs);
            bs.flags = i == 10 ? OL_BOX_PAGE_BREAK_BEFORE : 0;
            box_req.node = p;
            box_req.box = &bs;
            OL_NodeSetBox(&box_req);
            append.parent = p;
            append.role = OL_ROLE_TEXT;
            append.text = (CONST_STRPTR)"Every paragraph of this document is long enough to wrap twice.";
            last_text = OL_NodeAppend(&append);
            CHECK(last_text != NULL);
            OL_StyleInit(&style);
            style.display = OL_DISPLAY_INLINE;
            style_req.node = last_text;
            style_req.style = &style;
            OL_NodeSetStyle(&style_req);
        }
        memset(&page, 0, sizeof(page));
        page.width = OL_CSSPX(400);
        page.height = OL_CSSPX(300);
        page.margin_top = page.margin_bottom = page.margin_left = page.margin_right = OL_CSSPX(20);
        pages_req.document = paged;
        pages_req.page = &page;
        pages_req.measure = NULL;
        pages_req.measure_userdata = NULL;
        CHECK(OL_LayoutPages(&pages_req));
        CHECK(OL_PageCount(paged) >= 2);
        range_req.document = paged;
        range_req.page = 1;
        CHECK(OL_DisplayPageRange(&range_req));
        CHECK(range_req.count > 0);

        pos.node_id = OL_NodeID(last_text);
        pos.offset = 6;
        caret_req.document = paged;
        caret_req.position = &pos;
        caret_req.measure = NULL;
        caret_req.measure_userdata = NULL;
        caret_req.box = &caret;
        CHECK(OL_CaretBox(&caret_req));
        CHECK(caret_req.page == OL_PageCount(paged) - 1);
        hit_text_req.document = paged;
        hit_text_req.page = caret_req.page;
        hit_text_req.x = caret.x;
        hit_text_req.y = caret.y + caret.height / 2;
        hit_text_req.measure = NULL;
        hit_text_req.measure_userdata = NULL;
        hit_text_req.position = &back;
        CHECK(OL_HitText(&hit_text_req));
        CHECK(back.node_id == pos.node_id && back.offset == pos.offset);
        {
            struct OLDrawPage draw_req;
            ol_scale scale;
            memset(&scale, 0, sizeof(scale));
            OL_ScaleDPI(&scale, 72, 100);
            draw_req.document = paged;
            draw_req.page = 1;
            draw_req.scale = &scale;
            draw_req.clip = NULL;
            draw_req.draw = count_op;
            draw_req.userdata = &drawn;
            CHECK(OL_DrawPage(&draw_req) == range_req.count);
            CHECK(drawn == range_req.count);
            CHECK(OL_UnscaleX(&scale, 100) == OL_POINT(100));
        }
        {
            struct OLNodeTabs tabs_req;
            ol_unit stop = OL_CSSPX(40);
            tabs_req.node = p;
            tabs_req.count = 1;
            tabs_req.stops = &stop;
            CHECK(OL_NodeSetTabs(&tabs_req));
            tabs_req.count = OL_MAX_TABS + 1;
            CHECK(!OL_NodeSetTabs(&tabs_req));
        }
        if (OpenLayoutBase->lib_Version >= OPENLAYOUTLIB_TABSTOP_VERSION) {
            struct OLNodeTabStops stops_req;
            ol_tab_stop stop;
            stop.position = OL_CSSPX(40);
            stop.align = OL_TAB_RIGHT;
            stop.leader = OL_LEADER_DOT;
            stops_req.node = p;
            stops_req.count = 1;
            stops_req.stops = &stop;
            CHECK(OL_NodeSetTabStops(&stops_req));
            stops_req.count = OL_MAX_TABS + 1;
            CHECK(!OL_NodeSetTabStops(&stops_req));
        }
        if (OpenLayoutBase->lib_Version >= OPENLAYOUTLIB_ANCHOR_VERSION) {
            /* OL_BoxStyleInit fills a 4.0 program's box style and no more; a frame anchored to its page
             * behind the text then leaves the pages as they were */
            union { ol_box_style box; unsigned char raw[sizeof(ol_box_style)]; } u;
            const ol_box_style *back_box;
            LONG pages = OL_PageCount(paged);
            ol_node *frame, *frame_text;
            memset(u.raw, 0xA5, sizeof u.raw);
            OL_BoxStyleInit(&u.box);
            CHECK(u.box.size == OL_BOX_STYLE_SIZE_0_3);
            CHECK(u.raw[sizeof u.raw - 1] == 0xA5);
            u.box.size = sizeof(ol_box_style);
            u.box.flags = OL_BOX_ABSOLUTE;
            u.box.frame.x = 0;
            u.box.frame.y = 0;
            u.box.frame.width = page.width;
            u.box.frame.height = page.height;
            u.box.anchor = OL_ANCHOR_X(OL_ANCHOR_PAGE) | OL_ANCHOR_Y(OL_ANCHOR_PAGE) | OL_ANCHOR_BEHIND;
            append.parent = OL_DocumentRoot(paged);
            append.role = OL_ROLE_PARAGRAPH;
            append.text = NULL;
            frame = OL_NodeAppend(&append);
            CHECK(frame != NULL);
            box_req.node = frame;
            box_req.box = &u.box;
            OL_NodeSetBox(&box_req);
            back_box = OL_NodeBox(frame);
            CHECK(back_box != NULL && back_box->anchor == u.box.anchor);
            append.parent = frame;
            append.role = OL_ROLE_TEXT;
            append.text = (CONST_STRPTR)"A cover's title, over its picture.";
            frame_text = OL_NodeAppend(&append);
            CHECK(frame_text != NULL);
            style_req.node = frame_text;
            style_req.style = &style;
            OL_NodeSetStyle(&style_req);
            CHECK(OL_LayoutPages(&pages_req));
            CHECK(OL_PageCount(paged) == pages);
        }
        if (OpenLayoutBase->lib_Version >= OPENLAYOUTLIB_RESERVE_VERSION) {
            /* room for the display list at once; laid out again, the pages as they were */
            struct OLDocumentReserve reserve_req;
            LONG pages = OL_PageCount(paged);
            reserve_req.document = paged;
            reserve_req.ops = 2 * OL_DisplayCount(paged) + 16;
            CHECK(OL_DocumentReserve(&reserve_req));
            CHECK(OL_LayoutPages(&pages_req));
            CHECK(OL_PageCount(paged) == pages);
            reserve_req.document = NULL;
            CHECK(!OL_DocumentReserve(&reserve_req));
            {
                ol_document *roomy = OL_DocumentNewReserved(1000);
                CHECK(roomy != NULL);
                CHECK(OL_DisplayCount(roomy) == 0);
                OL_DocumentFree(roomy);
            }
        }
        if (OpenLayoutBase->lib_Version >= OPENLAYOUTLIB_WRAP_VERSION) {
            /* text round a frame: a last page whose paragraph starts beside a picture at its left margin,
             * 10 px from it; with a 0.5 program's box style, at the margin */
            ol_node *para, *frame, *words;
            ol_box_style wb;
            ULONG k, n;
            ol_unit x = -1;
            int pass;
            append.parent = OL_DocumentRoot(paged);
            append.role = OL_ROLE_PARAGRAPH;
            append.text = NULL;
            para = OL_NodeAppend(&append);
            CHECK(para != NULL);
            OL_BoxStyleInit(&bs);
            bs.flags = OL_BOX_PAGE_BREAK_BEFORE;
            box_req.node = para;
            box_req.box = &bs;
            OL_NodeSetBox(&box_req);
            append.parent = para;
            append.role = OL_ROLE_TEXT;
            append.text = (CONST_STRPTR)"These words go round the picture beside them, and then under it.";
            words = OL_NodeAppend(&append);
            CHECK(words != NULL);
            style_req.node = words;
            style_req.style = &style;
            OL_NodeSetStyle(&style_req);
            append.parent = para;
            append.role = OL_ROLE_BLOCK;
            append.text = NULL;
            frame = OL_NodeAppend(&append);
            CHECK(frame != NULL);
            for (pass = 0; pass < 2; ++pass) {
                memset(&wb, 0, sizeof(wb));
                OL_BoxStyleInit(&wb);
                wb.size = pass ? (ULONG)OL_BOX_STYLE_SIZE_0_5 : sizeof(ol_box_style);
                wb.flags = OL_BOX_ABSOLUTE;
                wb.frame.width = OL_CSSPX(100);
                wb.frame.height = OL_CSSPX(60);
                wb.anchor = OL_ANCHOR_X(OL_ANCHOR_MARGIN) | OL_ANCHOR_Y(OL_ANCHOR_BLOCK);
                wb.wrap = OL_WRAP_SQUARE;
                wb.wrap_gap[1] = OL_CSSPX(10);
                box_req.node = frame;
                box_req.box = &wb;
                OL_NodeSetBox(&box_req);
                CHECK(OL_LayoutPages(&pages_req));
                n = OL_DisplayCount(paged);
                x = -1;
                for (k = 0; k < n; ++k) {
                    const ol_display_op *op = OL_DisplayGet(paged, k);
                    if (op->type == OL_OP_TEXT && op->u.text.node_id == OL_NodeID(words) && op->u.text.text_offset == 0)
                        x = op->u.text.bounds.x;
                }
                CHECK(x == (pass ? page.margin_left : page.margin_left + OL_CSSPX(110)));
            }
            {   /* where the frame went: the last page, at the margin, 100 x 60 px */
                struct OLFramePlace place_req;
                ol_rect where;
                place_req.document = paged;
                place_req.frame = frame;
                place_req.rect = &where;
                CHECK(OL_FramePlace(&place_req));
                CHECK(place_req.page == OL_PageCount(paged) - 1 && where.x == page.margin_left &&
                      where.width == OL_CSSPX(100) && where.height == OL_CSSPX(60));
                place_req.frame = para;
                CHECK(!OL_FramePlace(&place_req));
            }
        }
        if (OpenLayoutBase->lib_Version > OPENLAYOUTLIB_WRAP_VERSION ||
            (OpenLayoutBase->lib_Version == OPENLAYOUTLIB_WRAP_VERSION &&
             OpenLayoutBase->lib_Revision >= OPENLAYOUTLIB_EXACT_REVISION)) {
            /* 7.1: a paragraph's lines exactly 8 px apart, though its 16 px text is taller */
            ol_document *ex = OL_DocumentNew();
            struct OLLayoutRequest ex_req;
            ol_node *para, *words;
            ol_box_style eb;
            ULONG k, n, lines = 0;
            CHECK(ex != NULL);
            append.document = ex;
            append.parent = OL_DocumentRoot(ex);
            append.role = OL_ROLE_PARAGRAPH;
            append.text = NULL;
            para = OL_NodeAppend(&append);
            OL_StyleInit(&style);
            style.line_height = OL_CSSPX(8);
            style_req.node = para;
            style_req.style = &style;
            OL_NodeSetStyle(&style_req);
            OL_BoxStyleInit(&eb);
            eb.flags = OL_BOX_EXACT_LINES;
            box_req.node = para;
            box_req.box = &eb;
            OL_NodeSetBox(&box_req);
            append.parent = para;
            append.role = OL_ROLE_TEXT;
            append.text = (CONST_STRPTR)"Lines exactly eight pixels apart, whatever the size of their text.";
            words = OL_NodeAppend(&append);
            OL_StyleInit(&style);
            style.display = OL_DISPLAY_INLINE;
            style.line_height = OL_CSSPX(8);
            style_req.node = words;
            OL_NodeSetStyle(&style_req);
            ex_req.document = ex;
            ex_req.viewport_width = OL_CSSPX(120);
            ex_req.measure = NULL;
            ex_req.measure_userdata = NULL;
            CHECK(OL_Layout(&ex_req));
            n = OL_DisplayCount(ex);
            for (k = 0; k < n; ++k) {
                const ol_display_op *op = OL_DisplayGet(ex, k);
                if (op->type != OL_OP_TEXT) continue;
                CHECK(op->u.text.bounds.height == OL_CSSPX(8) && op->u.text.bounds.y % OL_CSSPX(8) == 0);
                if (op->u.text.bounds.y / OL_CSSPX(8) + 1 > (ol_unit)lines) lines = (ULONG)(op->u.text.bounds.y / OL_CSSPX(8) + 1);
            }
            CHECK(lines >= 2 && OL_DocumentContentHeight(ex) == (LONG)lines * OL_CSSPX(8));
            OL_DocumentFree(ex);
            append.document = paged;
        }
        printf("LAYOUTLIB PAGES %ld\n", (long)OL_PageCount(paged));
        OL_DocumentFree(paged);
    }

    if (OpenLayoutBase->lib_Version >= OPENLAYOUTLIB_DRAWING_VERSION) {
        ol_node *graphic_node, *image_node;
        ol_shape_style shape_style;
        ol_point points[3] = {{500, 0}, {1000, 1000}, {0, 1000}};
        ol_image_crop crop = {100, 50, 200, 150};
        ol_box_style graphic_box;
        struct OLNodeBox box_req;
        struct OLNodeImage image_req;
        ULONG k, shapes = 0, images = 0;

        append.document = doc;
        append.parent = root;
        append.role = OL_ROLE_BLOCK;
        append.text = NULL;
        graphic_node = OL_NodeAppend(&append);
        CHECK(graphic_node != NULL);
        OL_BoxStyleInit(&graphic_box);
        graphic_box.size = sizeof(graphic_box);
        graphic_box.flags = OL_BOX_ABSOLUTE | OL_BOX_CLIP_CONTENT;
        graphic_box.frame.width = OL_CSSPX(80);
        graphic_box.frame.height = OL_CSSPX(40);
        box_req.node = graphic_node;
        box_req.box = &graphic_box;
        OL_NodeSetBox(&box_req);
        OL_ShapeStyleInit(&shape_style);
        shape_style.type = OL_SHAPE_POLYGON;
        shape_style.fill_argb = 0xff336699u;
        CHECK(OL_NodeSetShape(graphic_node, &shape_style, 3, points));

        append.parent = graphic_node;
        append.role = OL_ROLE_IMAGE;
        image_node = OL_NodeAppend(&append);
        CHECK(image_node != NULL);
        image_req.node = image_node;
        image_req.image_id = 77;
        image_req.width = OL_CSSPX(100);
        image_req.height = OL_CSSPX(50);
        OL_NodeSetImage(&image_req);
        CHECK(OL_NodeSetImageCrop(image_node, &crop));
        CHECK(OL_Layout(&layout_req));
        for (k = 0; k < OL_DisplayCount(doc); ++k) {
            const ol_display_op *op = OL_DisplayGet(doc, k);
            if (op->type == OL_OP_SHAPE && op->u.fill_rect.node_id == OL_NodeID(graphic_node)) {
                CHECK(op->graphic && op->graphic->point_count == 3);
                CHECK(op->clip.width == OL_CSSPX(80));
                ++shapes;
            }
            if (op->type == OL_OP_IMAGE && op->u.image.image_id == 77) {
                CHECK(op->graphic && op->graphic->crop.left == 100);
                CHECK(op->clip.height == OL_CSSPX(40));
                ++images;
            }
        }
        CHECK(shapes == 1 && images == 1);
    }

    printf("LAYOUTLIB PASS version=%s height=%ld ops=%lu link=%lu\n",
           (const char *)OL_VersionString(),
           (long)(OL_DocumentContentHeight(doc) / OL_UNITS_PER_CSSPX),
           (unsigned long)OL_DisplayCount(doc),
           (unsigned long)OL_NodeID(link));

done:
    if (doc) OL_DocumentFree(doc);
    if (OpenLayoutBase) CloseLibrary(OpenLayoutBase);
    return rc;
}
