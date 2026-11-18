#ifndef LIBRARIES_OPENLAYOUT_H
#define LIBRARIES_OPENLAYOUT_H

#include <exec/types.h>
#include <gfopenlayout.h>
#include <gfopenlayout_ui.h>
#include <gfopenlayout_ui_amiga.h>

#define OPENLAYOUTLIB_NAME "gfopenlayout.library"
#define OPENLAYOUTLIB_VERSION 2             /* the calls up to OL_NodeValue */
#define OPENLAYOUTLIB_REVISION 0
#define OPENLAYOUTLIB_EXACT_REVISION 1      /* with version 7: OL_BOX_EXACT_LINES laid out (7.0 ignores the flag) */
#define OPENLAYOUTLIB_DOCUMENT_VERSION 3    /* pages, tables, frames, caret: OL_BoxStyleInit on */
#define OPENLAYOUTLIB_TABSTOP_VERSION 4     /* right, centred and decimal tab stops, leaders: OL_NodeSetTabStops */
#define OPENLAYOUTLIB_RESERVE_VERSION 6     /* the display list's room at once: OL_DocumentReserve, OL_DocumentNewReserved */
#define OPENLAYOUTLIB_ANCHOR_VERSION 5      /* frames anchored to their pages: ol_box_style's anchor, whose size
                                               the program sets (OL_BoxStyleInit stops before it) */
#define OPENLAYOUTLIB_WRAP_VERSION 7        /* text round anchored frames: ol_box_style's wrap and wrap_gap, with
                                               the size the program sets (OL_BoxStyleInit stops before them);
                                               where a frame went: OL_FramePlace */
#define OPENLAYOUTLIB_ROW_VERSION 8         /* programs' windows: OL_BOX_ROW and ol_box_style's width, height, grow,
                                               gap and align, with the size the program sets (OL_BoxStyleInit
                                               stops before them) */
#define OPENLAYOUTLIB_WEB_VERSION 9         /* web boxes: OL_BOX_SIDE_BORDERS, OL_BOX_AUTO_LEFT and _RIGHT,
                                               OL_BOX_REPEAT_HEADER, OL_DISPLAY_INLINE_BLOCK and ol_box_style's
                                               border_widths, border_argbs, min_width, max_width,
                                               width_permille, max_width_permille and valign, with the size the
                                               program sets (OL_BoxStyleInit stops before them) */
#define OPENLAYOUTLIB_UI_VERSION 10         /* one library: GFCSS in documents (OL_NodeSetTag to
                                               OL_DocumentAddImport), GFOpenLayout UI (OLUI_New to
                                               OLUI_FreeCss) and its Intuition glue (OLUI_AmigaIdcmp to
                                               OLUI_AmigaEvent), the C API's calls by their library
                                               names (olui_set_text is OLUI_SetText). Callbacks
                                               (measure, draw, handlers) are the program's C
                                               functions, called with the C calling convention:
                                               a program built with -fbaserel gives them __saveds.
                                               libgfopenlayout_link.a is the C API through the
                                               library, for a program written to it. */
#define OPENLAYOUTLIB_DESKTOP_VERSION 11    /* what a desktop and its drawers need (GFOpenLayout UI
                                               0.4, OpenBench): elements made, placed and taken
                                               away (OLUI_Add, OLUI_Remove, OLUI_SetStyle,
                                               OLUI_SetPlace), position and z-index, OLUI_Hit,
                                               and frames as wide as their content
                                               (OL_BOX_FIT_CONTENT). Through
                                               libgfopenlayout_link.a a version 11 call on
                                               version 10 does nothing and returns 0. */
#define OPENLAYOUTLIB_TOOLKIT_VERSION 12    /* what a toolkit over the UI needs (GFOpenLayout UI
                                               0.5, the wxWidgets port): OLUI_Repaint (a
                                               program's picture drawn again, all or part) and
                                               OLUI_Popup (the pop-up open over the window).
                                               Through libgfopenlayout_link.a a version 12 call
                                               on version 11 does nothing and returns 0. */
#define OPENLAYOUTLIB_DRAWING_VERSION 13    /* document vector shapes, image crop metadata and
                                               stateless box-local clipping: OL_ShapeStyleInit,
                                               OL_NodeSetShape and OL_NodeSetImageCrop. Existing
                                               display-op fields and all older vectors retain
                                               their offsets. */
#define OPENLAYOUTLIB_PATH_VERSION 14       /* allocation-free primitive flattening and shape
                                               rotation: OL_ShapePath. The shape style extension
                                               is appended after every version 13 field. */
#define OPENLAYOUTLIB_COLUMNS_VERSION 15    /* bounded newspaper columns inside absolute text
                                               frames: ol_box_style's text_columns and
                                               text_column_gap, appended after every 0.14 field */
#define OPENLAYOUTLIB_EXCLUSIONS_VERSION 16 /* copied, box-local exclusion rectangles: text in a
                                               bounded frame can reuse the version-7 wrap fitter
                                               around Publisher/Writer objects without drawing or
                                               affecting sibling flows */
#define OPENLAYOUTLIB_CONTOUR_VERSION 17    /* conservative horizontal exclusion bands derived
                                               from node-owned closed shape geometry */
#define OPENLAYOUTLIB_GRID_VERSION 18       /* grids: OL_BOX_GRID and ol_box_style's grid_columns,
                                               appended after every 0.17 field; GFOpenLayout
                                               UI's <grid columns=n> and grid-columns (MUI's
                                               column groups, GTK's tables); the same size for
                                               a box's children: OL_BOX_SAME_WIDTH and
                                               OL_BOX_SAME_HEIGHT, UI's same-size (MUI's
                                               SameSize) */
#define OPENLAYOUTLIB_GRADIENT_VERSION 20   /* gradient fills: ol_shape_style's gradient,
                                               fill2_argb and gradient_angle_tenths, appended
                                               after every 0.14 field. Older libraries keep the
                                               shorter style, so a renderer checks
                                               OL_SHAPE_STYLE_HAS_GRADIENT and fills plain. */
#define OPENLAYOUTLIB_ROW_SPAN_VERSION 21   /* tables: ol_box_style's row_span (a cell down more
                                               than one row), appended after every 0.20 field,
                                               and a row's box.height as its least height */
#define OPENLAYOUTLIB_HATCH_VERSION 22      /* hatching and shadows: ol_shape_style's hatch,
                                               hatch_argb, hatch_angle_tenths, hatch_distance,
                                               shadow_argb, shadow_dx and shadow_dy, appended
                                               after every 0.20 field. Older libraries keep the
                                               shorter style, so a renderer checks
                                               OL_SHAPE_STYLE_HAS_HATCH and draws neither. */
#define OPENLAYOUTLIB_TEXT_CAPS_VERSION 23  /* OLUI_AMIGA_TEXT_CAPS to OLUI_AmigaTextWidth: the
                                               height of a face's capitals in size->ascent, for
                                               text centred by its capitals. An older library
                                               answers the ascender's whatever the flag. */
#define OPENLAYOUTLIB_CORE_API_VERSION 24   /* the document calls by their C prototypes
                                               (OLAPI_DocumentNew to OLAPI_UnscaleY, after every
                                               version 19 vector): a program that compiled
                                               GFOpenLayout's source in links
                                               libgfopenlayout_link.a and calls ol_layout and the
                                               rest as before. Through it, an older library stops
                                               the program at its first call with a line saying
                                               which it needs. */
#define OPENLAYOUTLIB_DRAW_API_VERSION 25   /* GFOpenLayout drawing (gfopenlayout_draw.h) by its C
                                               prototypes (OLDraw_NewRastport to OLDraw_EllipseHas,
                                               after every version 24 vector), with its Amiga
                                               target: a program that compiled draw/oldraw.c and
                                               amiga/oldraw_amiga.c in links libgfopenlayout_link.a
                                               and calls oldraw_new_rastport and the rest as
                                               before. Through it, an older library stops the
                                               program at its first drawing call with a line
                                               saying which it needs. */

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

/* version 6 */

struct OLDocumentReserve {
    ol_document *document;
    ULONG ops;
};

/* version 7 */

struct OLFramePlace {
    const ol_document *document;
    const ol_node *frame;
    LONG page;          /* set by OL_FramePlace, with rect */
    ol_rect *rect;
};


#endif
