/* openlayout.library 5.0: version 3 adds pages, tables, frames and the
 * caret (OpenLayout 0.2) after version 2's calls, which are unchanged;
 * version 4 adds tab stops that align and their leaders (OpenLayout 0.3);
 * version 5 places frames anchored to their pages (OpenLayout 0.4, the
 * box style's anchor), with no new calls. Programs built for 2, 3 or 4
 * keep working.
 * Copyright (c) 2026 AmigaChrome Team. MIT.
 */
#include <exec/types.h>
#include <exec/resident.h>
#include <exec/libraries.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>

#include "../include/libraries/openlayout.h"

#define REG(r, decl) register decl __asm(#r)
#define LIB_VERSION 5
#define LIB_REVISION 0

struct OpenLayoutLibBase {
    struct Library lib;
    BPTR seglist;
};

struct ExecBase *SysBase;

int start(void) { return -1; }

static const char lib_name[] = OPENLAYOUTLIB_NAME;
static const char lib_id[] =
    "openlayout.library 5.0 (25.10.2026) OpenLayout 0.4, AmigaChrome Team\r\n";
/* for Version and the file's version tag: lib_id has none */
static const char lib_ver[] __attribute__((used)) =
    "$VER: openlayout.library 5.0 (25.10.2026) OpenLayout 0.4, AmigaChrome Team";

static struct Library *lib_init(REG(d0, struct OpenLayoutLibBase *base),
                                REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys));
static struct Library *lib_open(REG(a6, struct OpenLayoutLibBase *base));
static BPTR lib_close(REG(a6, struct OpenLayoutLibBase *base));
static BPTR lib_expunge(REG(a6, struct OpenLayoutLibBase *base));
static ULONG lib_null(void);

static ULONG OL_Version(REG(a6, struct OpenLayoutLibBase *base));
static CONST_STRPTR OL_VersionString(REG(a6, struct OpenLayoutLibBase *base));
static ol_document *OL_DocumentNew(REG(a6, struct OpenLayoutLibBase *base));
static void OL_DocumentFree(REG(a0, ol_document *doc), REG(a6, struct OpenLayoutLibBase *base));
static ol_node *OL_DocumentRoot(REG(a0, ol_document *doc), REG(a6, struct OpenLayoutLibBase *base));
static ol_node *OL_NodeAppend(REG(a0, struct OLNodeAppend *r), REG(a6, struct OpenLayoutLibBase *base));
static ULONG OL_NodeID(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_NodeSetText(REG(a0, struct OLNodeString *r), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_NodeSetName(REG(a0, struct OLNodeString *r), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_NodeSetValue(REG(a0, struct OLNodeString *r), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_NodeSetHref(REG(a0, struct OLNodeString *r), REG(a6, struct OpenLayoutLibBase *base));
static void OL_NodeSetStyle(REG(a0, struct OLNodeStyle *r), REG(a6, struct OpenLayoutLibBase *base));
static void OL_NodeSetActions(REG(a0, ol_node *node), REG(d0, ULONG actions), REG(a6, struct OpenLayoutLibBase *base));
static ULONG OL_NodeActions(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base));
static void OL_NodeSetImage(REG(a0, struct OLNodeImage *r), REG(a6, struct OpenLayoutLibBase *base));
static CONST_STRPTR OL_NodeHref(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_NodeBounds(REG(a0, struct OLNodeBounds *r), REG(a6, struct OpenLayoutLibBase *base));
static const ol_node *OL_FindRoleName(REG(a0, struct OLFindRoleName *r), REG(a6, struct OpenLayoutLibBase *base));
static const ol_node *OL_HitAction(REG(a0, struct OLHitAction *r), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_Layout(REG(a0, struct OLLayoutRequest *r), REG(a6, struct OpenLayoutLibBase *base));
static LONG OL_DocumentContentHeight(REG(a0, const ol_document *doc), REG(a6, struct OpenLayoutLibBase *base));
static ULONG OL_DocumentGeneration(REG(a0, const ol_document *doc), REG(a6, struct OpenLayoutLibBase *base));
static ULONG OL_DisplayCount(REG(a0, const ol_document *doc), REG(a6, struct OpenLayoutLibBase *base));
static const ol_display_op *OL_DisplayGet(REG(a0, const ol_document *doc), REG(d0, ULONG index), REG(a6, struct OpenLayoutLibBase *base));
static void OL_StyleInit(REG(a0, ol_style *style), REG(a6, struct OpenLayoutLibBase *base));
static const ol_node *OL_NodeByID(REG(a0, const ol_document *doc), REG(d0, ULONG id), REG(a6, struct OpenLayoutLibBase *base));
static ULONG OL_NodeRole(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base));
static CONST_STRPTR OL_NodeText(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base));
static CONST_STRPTR OL_NodeName(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base));
static CONST_STRPTR OL_NodeValue(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base));
static void OL_BoxStyleInit(REG(a0, ol_box_style *box), REG(a6, struct OpenLayoutLibBase *base));
static void OL_NodeSetBox(REG(a0, struct OLNodeBox *r), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_NodeSetColumns(REG(a0, struct OLNodeColumns *r), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_LayoutPages(REG(a0, struct OLLayoutPagesRequest *r), REG(a6, struct OpenLayoutLibBase *base));
static LONG OL_PageCount(REG(a0, const ol_document *doc), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_DisplayPageRange(REG(a0, struct OLPageRange *r), REG(a6, struct OpenLayoutLibBase *base));
static const ol_node *OL_HitTestPage(REG(a0, struct OLHitTestPage *r), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_HitText(REG(a0, struct OLHitText *r), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_CaretBox(REG(a0, struct OLCaretBox *r), REG(a6, struct OpenLayoutLibBase *base));
static const ol_box_style *OL_NodeBox(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base));
static void OL_ScaleSet(REG(a0, ol_scale *scale), REG(d0, LONG num), REG(d1, LONG den), REG(a6, struct OpenLayoutLibBase *base));
static void OL_ScaleDPI(REG(a0, ol_scale *scale), REG(d0, LONG dpi), REG(d1, LONG zoom), REG(a6, struct OpenLayoutLibBase *base));
static void OL_ScaleFit(REG(a0, ol_scale *scale), REG(d0, LONG length), REG(d1, LONG pixels), REG(a6, struct OpenLayoutLibBase *base));
static LONG OL_UnscaleX(REG(a0, const ol_scale *scale), REG(d0, LONG px), REG(a6, struct OpenLayoutLibBase *base));
static LONG OL_UnscaleY(REG(a0, const ol_scale *scale), REG(d0, LONG py), REG(a6, struct OpenLayoutLibBase *base));
static ULONG OL_DrawPage(REG(a0, struct OLDrawPage *r), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_NodeSetTabs(REG(a0, struct OLNodeTabs *r), REG(a6, struct OpenLayoutLibBase *base));
static BOOL OL_NodeSetTabStops(REG(a0, struct OLNodeTabStops *r), REG(a6, struct OpenLayoutLibBase *base));

static const APTR lib_vectors[] = {
    (APTR)lib_open, (APTR)lib_close, (APTR)lib_expunge, (APTR)lib_null,
    (APTR)OL_Version, (APTR)OL_VersionString,
    (APTR)OL_DocumentNew, (APTR)OL_DocumentFree, (APTR)OL_DocumentRoot,
    (APTR)OL_NodeAppend, (APTR)OL_NodeID,
    (APTR)OL_NodeSetText, (APTR)OL_NodeSetName, (APTR)OL_NodeSetValue,
    (APTR)OL_NodeSetHref, (APTR)OL_NodeSetStyle, (APTR)OL_NodeSetActions,
    (APTR)OL_NodeActions, (APTR)OL_NodeSetImage, (APTR)OL_NodeHref,
    (APTR)OL_NodeBounds, (APTR)OL_FindRoleName, (APTR)OL_HitAction,
    (APTR)OL_Layout, (APTR)OL_DocumentContentHeight,
    (APTR)OL_DocumentGeneration, (APTR)OL_DisplayCount, (APTR)OL_DisplayGet,
    (APTR)OL_StyleInit, (APTR)OL_NodeByID, (APTR)OL_NodeRole,
    (APTR)OL_NodeText, (APTR)OL_NodeName, (APTR)OL_NodeValue,
    /* version 3 */
    (APTR)OL_BoxStyleInit, (APTR)OL_NodeSetBox, (APTR)OL_NodeSetColumns,
    (APTR)OL_LayoutPages, (APTR)OL_PageCount, (APTR)OL_DisplayPageRange,
    (APTR)OL_HitTestPage, (APTR)OL_HitText, (APTR)OL_CaretBox, (APTR)OL_NodeBox,
    (APTR)OL_ScaleSet, (APTR)OL_ScaleDPI, (APTR)OL_ScaleFit,
    (APTR)OL_UnscaleX, (APTR)OL_UnscaleY, (APTR)OL_DrawPage, (APTR)OL_NodeSetTabs,
    /* version 4 */
    (APTR)OL_NodeSetTabStops,
    (APTR)-1
};

static const struct {
    ULONG size;
    const APTR *vectors;
    APTR data;
    APTR init;
} lib_inittable = {
    sizeof(struct OpenLayoutLibBase), lib_vectors, NULL, (APTR)lib_init
};

const struct Resident lib_romtag = {
    RTC_MATCHWORD, (struct Resident *)&lib_romtag, (APTR)(&lib_romtag + 1),
    RTF_AUTOINIT, LIB_VERSION, NT_LIBRARY, 0,
    (char *)lib_name, (char *)lib_id, (APTR)&lib_inittable
};

static struct Library *lib_init(REG(d0, struct OpenLayoutLibBase *base),
                                REG(a0, BPTR seglist),
                                REG(a6, struct ExecBase *sys))
{
    SysBase = sys;
    base->seglist = seglist;
    base->lib.lib_Revision = LIB_REVISION;
    return &base->lib;
}

static struct Library *lib_open(REG(a6, struct OpenLayoutLibBase *base))
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return &base->lib;
}

static BPTR lib_close(REG(a6, struct OpenLayoutLibBase *base))
{
    base->lib.lib_OpenCnt--;
    if (base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP))
        return lib_expunge(base);
    return 0;
}

static BPTR lib_expunge(REG(a6, struct OpenLayoutLibBase *base))
{
    BPTR seglist;
    if (base->lib.lib_OpenCnt) {
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    seglist = base->seglist;
    Remove(&base->lib.lib_Node);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize,
            base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seglist;
}

static ULONG lib_null(void) { return 0; }

static ULONG OL_Version(REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return 1;
}

static CONST_STRPTR OL_VersionString(REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return (CONST_STRPTR)OL_VERSION;
}

static ol_document *OL_DocumentNew(REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_document_new();
}

static void OL_DocumentFree(REG(a0, ol_document *doc), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    ol_document_free(doc);
}

static ol_node *OL_DocumentRoot(REG(a0, ol_document *doc), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_document_root(doc);
}

static ol_node *OL_NodeAppend(REG(a0, struct OLNodeAppend *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    if (!r) return NULL;
    return ol_node_append(r->document, r->parent, (ol_role)r->role, (const char *)r->text);
}

static ULONG OL_NodeID(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return (ULONG)ol_node_id(node);
}

static BOOL OL_NodeSetText(REG(a0, struct OLNodeString *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return r && ol_node_set_text(r->node, (const char *)r->text) ? TRUE : FALSE;
}

static BOOL OL_NodeSetName(REG(a0, struct OLNodeString *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return r && ol_node_set_name(r->node, (const char *)r->text) ? TRUE : FALSE;
}

static BOOL OL_NodeSetValue(REG(a0, struct OLNodeString *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return r && ol_node_set_value(r->node, (const char *)r->text) ? TRUE : FALSE;
}

static BOOL OL_NodeSetHref(REG(a0, struct OLNodeString *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return r && ol_node_set_href(r->node, (const char *)r->text) ? TRUE : FALSE;
}

static void OL_NodeSetStyle(REG(a0, struct OLNodeStyle *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    if (r) ol_node_set_style(r->node, r->style);
}

static void OL_NodeSetActions(REG(a0, ol_node *node), REG(d0, ULONG actions), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    ol_node_set_actions(node, actions);
}

static ULONG OL_NodeActions(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return (ULONG)ol_node_actions(node);
}

static void OL_NodeSetImage(REG(a0, struct OLNodeImage *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    if (r) ol_node_set_image(r->node, r->image_id, r->width, r->height);
}

static CONST_STRPTR OL_NodeHref(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return (CONST_STRPTR)ol_node_href(node);
}

static BOOL OL_NodeBounds(REG(a0, struct OLNodeBounds *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    if (!r || !r->node || !r->bounds) return FALSE;
    *r->bounds = ol_node_bounds(r->node);
    return TRUE;
}

static const ol_node *OL_FindRoleName(REG(a0, struct OLFindRoleName *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    if (!r) return NULL;
    return ol_find_role_name(r->document, (ol_role)r->role, (const char *)r->name);
}

static const ol_node *OL_HitAction(REG(a0, struct OLHitAction *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    if (!r) return NULL;
    return ol_hit_action(r->document, r->x, r->y, r->action);
}

static BOOL OL_Layout(REG(a0, struct OLLayoutRequest *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    if (!r) return FALSE;
    return ol_layout(r->document, r->viewport_width,
                     r->measure, r->measure_userdata) ? TRUE : FALSE;
}

static LONG OL_DocumentContentHeight(REG(a0, const ol_document *doc), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return (LONG)ol_document_content_height(doc);
}

static ULONG OL_DocumentGeneration(REG(a0, const ol_document *doc), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return (ULONG)ol_document_generation(doc);
}

static ULONG OL_DisplayCount(REG(a0, const ol_document *doc), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return (ULONG)ol_display_count(doc);
}

static const ol_display_op *OL_DisplayGet(REG(a0, const ol_document *doc), REG(d0, ULONG index), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_display_get(doc, (size_t)index);
}

static void OL_StyleInit(REG(a0, ol_style *style), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    ol_style_init(style);
}

static const ol_node *OL_NodeByID(REG(a0, const ol_document *doc), REG(d0, ULONG id), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_node_by_id(doc, (ol_id)id);
}

static ULONG OL_NodeRole(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return (ULONG)ol_node_role(node);
}

static CONST_STRPTR OL_NodeText(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return (CONST_STRPTR)ol_node_text(node);
}

static CONST_STRPTR OL_NodeName(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return (CONST_STRPTR)ol_node_name(node);
}

static CONST_STRPTR OL_NodeValue(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return (CONST_STRPTR)ol_node_value(node);
}

/* ---- version 3 ------------------------------------------------------------ */

/* 5.0: only the box style programs built for 3 and 4 have, so this never writes past theirs; a program
 * that anchors a frame sets size to sizeof(ol_box_style) and anchor after it */
static void OL_BoxStyleInit(REG(a0, ol_box_style *box), REG(a6, struct OpenLayoutLibBase *base))
{
    ol_box_style full;
    (void)base;
    if (!box) return;
    ol_box_style_init(&full);
    full.size = (uint32_t)OL_BOX_STYLE_SIZE_0_3;
    CopyMem(&full, box, (ULONG)OL_BOX_STYLE_SIZE_0_3);
}

static void OL_NodeSetBox(REG(a0, struct OLNodeBox *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    if (r) ol_node_set_box(r->node, r->box);
}

static BOOL OL_NodeSetColumns(REG(a0, struct OLNodeColumns *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return r && ol_node_set_columns(r->node, (int)r->count, r->widths) ? TRUE : FALSE;
}

static BOOL OL_LayoutPages(REG(a0, struct OLLayoutPagesRequest *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return r && ol_layout_pages(r->document, r->page, r->measure, r->measure_userdata) ? TRUE : FALSE;
}

static LONG OL_PageCount(REG(a0, const ol_document *doc), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return (LONG)ol_page_count(doc);
}

static BOOL OL_DisplayPageRange(REG(a0, struct OLPageRange *r), REG(a6, struct OpenLayoutLibBase *base))
{
    size_t first, count;
    (void)base;
    if (!r || !ol_display_page_range(r->document, (int)r->page, &first, &count)) return FALSE;
    r->first = (ULONG)first;
    r->count = (ULONG)count;
    return TRUE;
}

static const ol_node *OL_HitTestPage(REG(a0, struct OLHitTestPage *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    if (!r) return NULL;
    return ol_hit_test_page(r->document, (int)r->page, r->x, r->y);
}

static BOOL OL_HitText(REG(a0, struct OLHitText *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return r && ol_hit_text(r->document, (int)r->page, r->x, r->y, r->measure,
                            r->measure_userdata, r->position) ? TRUE : FALSE;
}

static BOOL OL_CaretBox(REG(a0, struct OLCaretBox *r), REG(a6, struct OpenLayoutLibBase *base))
{
    int page = 0;
    (void)base;
    if (!r || !ol_caret_box(r->document, r->position, r->measure, r->measure_userdata,
                            &page, r->box))
        return FALSE;
    r->page = page;
    return TRUE;
}

static const ol_box_style *OL_NodeBox(REG(a0, const ol_node *node), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return ol_node_box(node);
}

static void OL_ScaleSet(REG(a0, ol_scale *scale), REG(d0, LONG num), REG(d1, LONG den), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    ol_scale_set(scale, num, den);
}

static void OL_ScaleDPI(REG(a0, ol_scale *scale), REG(d0, LONG dpi), REG(d1, LONG zoom), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    ol_scale_dpi(scale, (int)dpi, (int)zoom);
}

static void OL_ScaleFit(REG(a0, ol_scale *scale), REG(d0, LONG length), REG(d1, LONG pixels), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    ol_scale_fit(scale, length, pixels);
}

static LONG OL_UnscaleX(REG(a0, const ol_scale *scale), REG(d0, LONG px), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return scale ? ol_unscale_x(scale, px) : 0;
}

static LONG OL_UnscaleY(REG(a0, const ol_scale *scale), REG(d0, LONG py), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return scale ? ol_unscale_y(scale, py) : 0;
}

static ULONG OL_DrawPage(REG(a0, struct OLDrawPage *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    if (!r) return 0;
    return (ULONG)ol_draw_page(r->document, (int)r->page, r->scale, r->clip, r->draw, r->userdata);
}

static BOOL OL_NodeSetTabs(REG(a0, struct OLNodeTabs *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return r && ol_node_set_tabs(r->node, (int)r->count, r->stops) ? TRUE : FALSE;
}

/* ---- version 4 ------------------------------------------------------------ */

static BOOL OL_NodeSetTabStops(REG(a0, struct OLNodeTabStops *r), REG(a6, struct OpenLayoutLibBase *base))
{
    (void)base;
    return r && ol_node_set_tab_stops(r->node, (int)r->count, r->stops) ? TRUE : FALSE;
}
