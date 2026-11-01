#ifndef INLINE_OPENLAYOUT_H
#define INLINE_OPENLAYOUT_H
#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#endif
#ifndef OPENLAYOUT_BASE_NAME
#define OPENLAYOUT_BASE_NAME OpenLayoutBase
#endif

#define OL_Version() LP0(0x1e, ULONG, OL_Version, , OPENLAYOUT_BASE_NAME)
#define OL_VersionString() LP0(0x24, CONST_STRPTR, OL_VersionString, , OPENLAYOUT_BASE_NAME)
#define OL_DocumentNew() LP0(0x2a, ol_document *, OL_DocumentNew, , OPENLAYOUT_BASE_NAME)
#define OL_DocumentFree(doc) LP1NR(0x30, OL_DocumentFree, ol_document *, doc, a0, , OPENLAYOUT_BASE_NAME)
#define OL_DocumentRoot(doc) LP1(0x36, ol_node *, OL_DocumentRoot, ol_document *, doc, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeAppend(req) LP1(0x3c, ol_node *, OL_NodeAppend, struct OLNodeAppend *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeID(node) LP1(0x42, ULONG, OL_NodeID, const ol_node *, node, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeSetText(req) LP1(0x48, BOOL, OL_NodeSetText, struct OLNodeString *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeSetName(req) LP1(0x4e, BOOL, OL_NodeSetName, struct OLNodeString *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeSetValue(req) LP1(0x54, BOOL, OL_NodeSetValue, struct OLNodeString *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeSetHref(req) LP1(0x5a, BOOL, OL_NodeSetHref, struct OLNodeString *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeSetStyle(req) LP1NR(0x60, OL_NodeSetStyle, struct OLNodeStyle *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeSetActions(node,actions) LP2NR(0x66, OL_NodeSetActions, ol_node *, node, a0, ULONG, actions, d0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeActions(node) LP1(0x6c, ULONG, OL_NodeActions, const ol_node *, node, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeSetImage(req) LP1NR(0x72, OL_NodeSetImage, struct OLNodeImage *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeHref(node) LP1(0x78, CONST_STRPTR, OL_NodeHref, const ol_node *, node, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeBounds(req) LP1(0x7e, BOOL, OL_NodeBounds, struct OLNodeBounds *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_FindRoleName(req) LP1(0x84, const ol_node *, OL_FindRoleName, struct OLFindRoleName *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_HitAction(req) LP1(0x8a, const ol_node *, OL_HitAction, struct OLHitAction *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_Layout(req) LP1(0x90, BOOL, OL_Layout, struct OLLayoutRequest *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_DocumentContentHeight(doc) LP1(0x96, LONG, OL_DocumentContentHeight, const ol_document *, doc, a0, , OPENLAYOUT_BASE_NAME)
#define OL_DocumentGeneration(doc) LP1(0x9c, ULONG, OL_DocumentGeneration, const ol_document *, doc, a0, , OPENLAYOUT_BASE_NAME)
#define OL_DisplayCount(doc) LP1(0xa2, ULONG, OL_DisplayCount, const ol_document *, doc, a0, , OPENLAYOUT_BASE_NAME)
#define OL_DisplayGet(doc,index) LP2(0xa8, const ol_display_op *, OL_DisplayGet, const ol_document *, doc, a0, ULONG, index, d0, , OPENLAYOUT_BASE_NAME)
#define OL_StyleInit(style) LP1NR(0xae, OL_StyleInit, ol_style *, style, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeByID(doc,id) LP2(0xb4, const ol_node *, OL_NodeByID, const ol_document *, doc, a0, ULONG, id, d0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeRole(node) LP1(0xba, ULONG, OL_NodeRole, const ol_node *, node, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeText(node) LP1(0xc0, CONST_STRPTR, OL_NodeText, const ol_node *, node, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeName(node) LP1(0xc6, CONST_STRPTR, OL_NodeName, const ol_node *, node, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeValue(node) LP1(0xcc, CONST_STRPTR, OL_NodeValue, const ol_node *, node, a0, , OPENLAYOUT_BASE_NAME)
#define OL_BoxStyleInit(box) LP1NR(0xd2, OL_BoxStyleInit, ol_box_style *, box, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeSetBox(req) LP1NR(0xd8, OL_NodeSetBox, struct OLNodeBox *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeSetColumns(req) LP1(0xde, BOOL, OL_NodeSetColumns, struct OLNodeColumns *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_LayoutPages(req) LP1(0xe4, BOOL, OL_LayoutPages, struct OLLayoutPagesRequest *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_PageCount(doc) LP1(0xea, LONG, OL_PageCount, const ol_document *, doc, a0, , OPENLAYOUT_BASE_NAME)
#define OL_DisplayPageRange(req) LP1(0xf0, BOOL, OL_DisplayPageRange, struct OLPageRange *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_HitTestPage(req) LP1(0xf6, const ol_node *, OL_HitTestPage, struct OLHitTestPage *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_HitText(req) LP1(0xfc, BOOL, OL_HitText, struct OLHitText *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_CaretBox(req) LP1(0x102, BOOL, OL_CaretBox, struct OLCaretBox *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeBox(node) LP1(0x108, const ol_box_style *, OL_NodeBox, const ol_node *, node, a0, , OPENLAYOUT_BASE_NAME)
#define OL_ScaleSet(scale,num,den) LP3NR(0x10e, OL_ScaleSet, ol_scale *, scale, a0, LONG, num, d0, LONG, den, d1, , OPENLAYOUT_BASE_NAME)
#define OL_ScaleDPI(scale,dpi,zoom_percent) LP3NR(0x114, OL_ScaleDPI, ol_scale *, scale, a0, LONG, dpi, d0, LONG, zoom_percent, d1, , OPENLAYOUT_BASE_NAME)
#define OL_ScaleFit(scale,length,pixels) LP3NR(0x11a, OL_ScaleFit, ol_scale *, scale, a0, LONG, length, d0, LONG, pixels, d1, , OPENLAYOUT_BASE_NAME)
#define OL_UnscaleX(scale,px) LP2(0x120, LONG, OL_UnscaleX, const ol_scale *, scale, a0, LONG, px, d0, , OPENLAYOUT_BASE_NAME)
#define OL_UnscaleY(scale,py) LP2(0x126, LONG, OL_UnscaleY, const ol_scale *, scale, a0, LONG, py, d0, , OPENLAYOUT_BASE_NAME)
#define OL_DrawPage(req) LP1(0x12c, ULONG, OL_DrawPage, struct OLDrawPage *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_NodeSetTabs(req) LP1(0x132, BOOL, OL_NodeSetTabs, struct OLNodeTabs *, req, a0, , OPENLAYOUT_BASE_NAME)
/* version 4 */
#define OL_NodeSetTabStops(req) LP1(0x138, BOOL, OL_NodeSetTabStops, struct OLNodeTabStops *, req, a0, , OPENLAYOUT_BASE_NAME)
/* version 6 */
#define OL_DocumentReserve(req) LP1(0x13e, BOOL, OL_DocumentReserve, struct OLDocumentReserve *, req, a0, , OPENLAYOUT_BASE_NAME)
#define OL_DocumentNewReserved(ops) LP1(0x144, ol_document *, OL_DocumentNewReserved, ULONG, ops, d0, , OPENLAYOUT_BASE_NAME)
#define OL_FramePlace(req) LP1(0x14a, BOOL, OL_FramePlace, struct OLFramePlace *, req, a0, , OPENLAYOUT_BASE_NAME)

#endif
