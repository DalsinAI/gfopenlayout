/* SPDX-License-Identifier: MIT */
/* GFOpenLayers' composited mode: what a drawing call drew, from its
 * registers as layers_comp.c's thunks save them (0-7 d0-d7, 8 a0, 9 a1).
 * Pure, so tests/m68k/test_comp_marks.sh runs it under qemu-m68k against
 * the drawing itself. Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * A mark is a rectangle from the layer's top left; the compositor composes
 * only marked rectangles, so a mark must hold every pixel the call can
 * change. MK_WHOLE (all of the layer) is for calls whose reach the
 * registers don't tell (SetRast, Flood, AreaEnd, DrawGList, ...).
 *
 * Draw and PolyDraw start at the pen's place, which the call moves: the
 * thunk calls ocm_pre before the call, and it keeps the place in a scratch
 * register the call does not take (Draw a0, PolyDraw d1), in the thunk's
 * own copy of the registers; ocm_bounds reads it back after the call. */
#ifndef OPENLAYERS_MARK_H
#define OPENLAYERS_MARK_H

#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include "layers_core.h"

enum {
    MK_WHOLE,       /* all of the layer */
    MK_XYXY,        /* x0 a, y0 b, x1 c, y1 d */
    MK_XYWH,        /* x a, y b, w c, h d */
    MK_TEXT,        /* the font's rows at the pen, all across */
    MK_LINE,        /* the pen's place (kept in d) to x a, y b */
    MK_POLY,        /* the pen's place (kept in d), then count a points at b */
    MK_ELLIPSE,     /* centre a, b; radii c, d */
    MK_EOL,         /* the text's rows (TxBaseline, TxHeight) from the pen to the right */
    MK_BELOW,       /* the text's rows and every row below them */
    MK_ROW          /* x a, y b, width c (unsigned), one row */
};
struct omark {
    UBYTE type, rp, a, b, c, d;
};
static const struct omark marks[] = {
    { MK_WHOLE, 9, 0, 0, 0, 0 },        /* 0: the RastPort in a1, all of its layer */
    { MK_XYXY, 9, 0, 1, 2, 3 },         /* 1: RectFill, BltPattern, EraseRect */
    { MK_XYXY, 9, 2, 3, 4, 5 },         /* 2: ScrollRaster, ScrollRasterBF */
    { MK_XYWH, 9, 2, 3, 4, 5 },         /* 3: BltTemplate, ClipBlit, BltBitMapRastPort, BltMaskBitMapRastPort, MovePixelArray */
    { MK_XYXY, 9, 0, 1, 0, 1 },         /* 4: WritePixel, WriteRGBPixel */
    { MK_XYXY, 8, 0, 1, 2, 3 },         /* 5: WritePixelArray8, WriteChunkyPixels */
    { MK_ROW, 8, 0, 1, 2, 0 },          /* 6: WritePixelLine8 (rp a0, x d0, y d1, width d2) */
    { MK_TEXT, 9, 0, 0, 0, 0 },         /* 7: Text: the rows of the font, all across */
    { MK_ELLIPSE, 9, 0, 1, 2, 3 },      /* 8: DrawEllipse (xc d0, yc d1, a d2, b d3) */
    { MK_XYWH, 9, 3, 4, 5, 6 },         /* 9: WritePixelArray, WriteLUTPixelArray, WritePixelArrayAlpha, ScalePixelArray */
    { MK_XYWH, 9, 0, 1, 2, 3 },         /* 10: InvertPixelArray, FillPixelArray, ORTG_FillPixels, ORTG_InvertPixels */
    { MK_LINE, 9, 0, 1, 0, 8 },         /* 11: Draw (x d0, y d1; the pen kept in a0) */
    { MK_POLY, 9, 0, 8, 0, 1 },         /* 12: PolyDraw (count d0, points a0; the pen kept in d1) */
    { MK_EOL, 9, 0, 0, 0, 0 },          /* 13: ClearEOL */
    { MK_BELOW, 9, 0, 0, 0, 0 },        /* 14: ClearScreen */
};

/* Before the call: the pen's place, for a line from it. */
static void ocm_pre(const struct omark *k, ULONG *regs)
{
    const struct RastPort *rp = (const struct RastPort *)regs[k->rp];
    if ((k->type == MK_LINE || k->type == MK_POLY) && rp)
        regs[k->d] = (ULONG)(UWORD)rp->cp_x << 16 | (UWORD)rp->cp_y;
}

static void ocm_grow(LONG *x0, LONG *y0, LONG *x1, LONG *y1, LONG x, LONG y)
{
    if (x < *x0) *x0 = x;
    if (x > *x1) *x1 = x;
    if (y < *y0) *y0 = y;
    if (y > *y1) *y1 = y;
}

/* After the call: what it drew into a layer w by h, scrolled by sx, sy, as
 * a rectangle from the layer's top left (its RastPort's coordinates less
 * the scroll), cut to the layer. 0: nothing there. */
static int ocm_bounds(const struct omark *k, const ULONG *regs, const struct RastPort *rp, LONG w, LONG h,
                      LONG sx, LONG sy, struct lc_rect *out)
{
    LONG x0 = sx, y0 = sy, x1 = sx + w - 1, y1 = sy + h - 1, t;
    /* WORD arguments: only the low words are the caller's */
    LONG a = (WORD)regs[k->a], b = (WORD)regs[k->b], c = (WORD)regs[k->c], d = (WORD)regs[k->d];
    switch (k->type) {
    case MK_TEXT:
        if (rp->Font) {
            y0 = rp->cp_y - rp->Font->tf_Baseline;
            y1 = y0 + rp->Font->tf_YSize - 1;
        }
        break;
    case MK_XYXY:
    case MK_XYWH:
        x0 = a;
        y0 = b;
        x1 = k->type == MK_XYXY ? c : a + c - 1;
        y1 = k->type == MK_XYXY ? d : b + d - 1;
        if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
        if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
        break;
    case MK_LINE:
    case MK_POLY: {
        ULONG pen = regs[k->d];
        x0 = x1 = (WORD)(pen >> 16);
        y0 = y1 = (WORD)pen;
        if (k->type == MK_LINE)
            ocm_grow(&x0, &y0, &x1, &y1, a, b);
        else {
            const WORD *p = (const WORD *)regs[k->b];
            for (t = 0; t < a && p; t++)
                ocm_grow(&x0, &y0, &x1, &y1, p[2 * t], p[2 * t + 1]);
        }
        break;
    }
    case MK_ELLIPSE:
        if (c < 0) c = -c;
        if (d < 0) d = -d;
        x0 = a - c;
        x1 = a + c;
        y0 = b - d;
        y1 = b + d;
        break;
    case MK_EOL:
        x0 = rp->cp_x;
        y0 = rp->cp_y - rp->TxBaseline;
        y1 = y0 + rp->TxHeight - 1;
        break;
    case MK_BELOW:
        y0 = rp->cp_y - rp->TxBaseline;
        break;
    case MK_ROW:
        x0 = a;
        y0 = y1 = b;
        x1 = a + (LONG)(UWORD)c - 1;
        break;
    default:
        break;
    }
    x0 -= sx; x1 -= sx;
    y0 -= sy; y1 -= sy;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= w) x1 = w - 1;
    if (y1 >= h) y1 = h - 1;
    if (x0 > x1 || y0 > y1) return 0;
    out->x0 = x0; out->y0 = y0; out->x1 = x1; out->y1 = y1;
    return 1;
}

#endif
