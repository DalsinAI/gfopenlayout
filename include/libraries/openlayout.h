#ifndef LIBRARIES_OPENLAYOUT_H
#define LIBRARIES_OPENLAYOUT_H

#include <exec/types.h>
#include <openlayout.h>

#define OPENLAYOUTLIB_NAME "openlayout.library"
#define OPENLAYOUTLIB_VERSION 2             /* the calls up to OL_NodeValue */
#define OPENLAYOUTLIB_REVISION 0
#define OPENLAYOUTLIB_DOCUMENT_VERSION 3    /* pages, tables, frames, caret: OL_BoxStyleInit on */
#define OPENLAYOUTLIB_TABSTOP_VERSION 4     /* right, centred and decimal tab stops, leaders: OL_NodeSetTabStops */
#define OPENLAYOUTLIB_ANCHOR_VERSION 5      /* frames anchored to their pages: ol_box_style's anchor, whose size
                                               the program sets (OL_BoxStyleInit stops before it) */

struct OLNodeAppend {
    ol_document *document;
    ol_node *parent;
    ULONG role;
    CONST_STRPTR text;
};

struct OLNodeString {
    ol_node *node;
    CONST_STRPTR text;
};

struct OLNodeStyle {
    ol_node *node;
    const ol_style *style;
};

struct OLNodeImage {
    ol_node *node;
    LONG image_id;
    ol_unit width;
    ol_unit height;
};

struct OLLayoutRequest {
    ol_document *document;
    ol_unit viewport_width;
    ol_measure_text_fn measure;
    APTR measure_userdata;
};

struct OLFindRoleName {
    const ol_document *document;
    ULONG role;
    CONST_STRPTR name;
};

struct OLHitAction {
    const ol_document *document;
    ol_unit x;
    ol_unit y;
    ULONG action;
};

struct OLNodeBounds {
    const ol_node *node;
    ol_rect *bounds;
};

/* version 3 */

struct OLNodeBox {
    ol_node *node;
    const ol_box_style *box;
};

struct OLNodeColumns {
    ol_node *node;
    LONG count;
    const ol_unit *widths;
};

struct OLLayoutPagesRequest {
    ol_document *document;
    const ol_page *page;
    ol_measure_text_fn measure;
    APTR measure_userdata;
};

struct OLPageRange {
    const ol_document *document;
    LONG page;
    ULONG first;        /* set by OL_DisplayPageRange */
    ULONG count;
};

struct OLHitTestPage {
    const ol_document *document;
    LONG page;
    ol_unit x;
    ol_unit y;
};

struct OLHitText {
    const ol_document *document;
    LONG page;
    ol_unit x;
    ol_unit y;
    ol_measure_text_fn measure;
    APTR measure_userdata;
    ol_text_position *position;     /* set by OL_HitText */
};

struct OLDrawPage {
    const ol_document *document;
    LONG page;
    const ol_scale *scale;
    const ol_pixel_rect *clip;      /* NULL: the whole page */
    ol_draw_fn draw;
    APTR userdata;
};

struct OLCaretBox {
    const ol_document *document;
    const ol_text_position *position;
    ol_measure_text_fn measure;
    APTR measure_userdata;
    LONG page;          /* set by OL_CaretBox, with box */
    ol_rect *box;
};

struct OLNodeTabs {
    ol_node *node;
    LONG count;
    const ol_unit *stops;
};

/* version 4 */
struct OLNodeTabStops {
    ol_node *node;
    LONG count;
    const ol_tab_stop *stops;
};

#endif
