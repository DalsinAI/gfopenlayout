#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/openlayout.h>

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
    CHECK(!strcmp((const char *)OL_VersionString(), "0.2"));
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
    append.text = (CONST_STRPTR)"Resident OpenLayout can lay out ";
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
        printf("LAYOUTLIB PAGES %ld\n", (long)OL_PageCount(paged));
        OL_DocumentFree(paged);
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
