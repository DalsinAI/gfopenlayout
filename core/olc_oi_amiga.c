/* GFOpenLayout's ROM core, step 3 (Workbench build plan row 10): olc_oi(), openintuition's drawing hooks.
 *
 * Dale, 16 November 2026: the core takes the pixel hooks, and Intuition keeps its own work. So the table's
 * entries for obtain_rp, release_rp, draw_image_state, limit_rect and limit_end are NULL: those need Intuition
 * and layers (GadgetInfo RastPorts, BOOPSI images, clip regions), and the shell keeps its own. deep is NULL too:
 * it answers from Intuition's Workbench screen, not from pixels.
 *
 * The twelve here are openintuition's shell's own (openintuition_lib.c and openintuition_glue.h at amicradle-guest
 * dcf77f8), moved as they are: the same graphics.library calls in the same order, with the same care for a
 * program's area pattern left in its RastPort. They never look inside env. On a composited screen the drawing is
 * marked by layers' graphics thunks, as any other drawing is, so nothing more is needed here.
 *
 * graphics.library is the core's own base (olc_gfx), opened by olc_oi(), which the shell calls once at its init.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "olc_core_internal.h"

#ifdef OLC_AMIGA
#include <exec/libraries.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
static struct GfxBase *olc_gfx;
#define GRAPHICS_BASE_NAME olc_gfx
#include <proto/graphics.h>

/* A font to draw or measure in: an open TextFont, or a TextAttr opened for the call (and closed after), or the
 * default */
static struct TextFont *use_font(void *font, int textfont, int *opened)
{
    *opened = 0;
    if (!font)
        return olc_gfx->DefaultFont;
    if (textfont)
        return (struct TextFont *)font;
    {
        struct TextFont *f = OpenFont((struct TextAttr *)font);
        if (f) {
            *opened = 1;
            return f;
        }
    }
    return olc_gfx->DefaultFont;
}

static void h_erase_rect(struct oi_env *e, void *rp, olc_oi_s32 x0, olc_oi_s32 y0, olc_oi_s32 x1, olc_oi_s32 y1)
{
    (void)e;
    if (x1 >= x0 && y1 >= y0)
        EraseRect((struct RastPort *)rp, x0, y0, x1, y1);
}

/* Solid, whatever area pattern the RastPort carries (a program's, in its window's RastPort) */
static void h_fill_rect(struct oi_env *e, void *rp, olc_oi_u32 pen, olc_oi_s32 x0, olc_oi_s32 y0, olc_oi_s32 x1,
                        olc_oi_s32 y1)
{
    struct RastPort *r = (struct RastPort *)rp;
    UWORD *op;
    BYTE os;
    (void)e;
    if (x1 < x0 || y1 < y0)
        return;
    op = r->AreaPtrn;
    os = r->AreaPtSz;
    r->AreaPtrn = NULL;
    r->AreaPtSz = 0;
    SetAPen(r, pen);
    SetDrMd(r, JAM1);
    RectFill(r, x0, y0, x1, y1);
    r->AreaPtrn = op;
    r->AreaPtSz = os;
}

static void h_text(struct oi_env *e, void *rp, olc_oi_u32 fpen, olc_oi_u32 bpen, olc_oi_u32 mode, void *font,
                   int textfont, olc_oi_s32 x, olc_oi_s32 y, const char *s, olc_oi_u32 len)
{
    struct RastPort *r = (struct RastPort *)rp;
    struct TextFont *old = r->Font, *f;
    int opened = 0;
    (void)e;
    f = font ? use_font(font, textfont, &opened) : r->Font;
    if (f && f != old)
        SetFont(r, f);
    SetAPen(r, fpen);
    SetBPen(r, bpen);
    SetDrMd(r, mode);
    Move(r, x, y + (r->Font ? r->Font->tf_Baseline : 6));
    Text(r, (CONST_STRPTR)s, len);
    if (f && f != old && old)
        SetFont(r, old);
    if (opened)
        CloseFont(f);
}

static olc_oi_s32 h_text_length(struct oi_env *e, void *font, int textfont, const char *s, olc_oi_u32 len)
{
    struct RastPort rp;
    int opened;
    struct TextFont *f = use_font(font, textfont, &opened);
    olc_oi_s32 n;
    (void)e;
    InitRastPort(&rp);
    if (f)
        SetFont(&rp, f);
    n = TextLength(&rp, (CONST_STRPTR)s, len);
    if (opened)
        CloseFont(f);
    return n;
}

static olc_oi_s32 h_font_height(struct oi_env *e, void *font, int textfont)
{
    int opened;
    struct TextFont *f = use_font(font, textfont, &opened);
    olc_oi_s32 h = f ? f->tf_YSize : 8;
    (void)e;
    if (opened)
        CloseFont(f);
    return h;
}

static void h_polyline(struct oi_env *e, void *rp, olc_oi_u32 fpen, olc_oi_u32 bpen, olc_oi_u32 mode, olc_oi_s32 x,
                       olc_oi_s32 y, const short *xy, olc_oi_u32 count)
{
    struct RastPort *r = (struct RastPort *)rp;
    olc_oi_u32 i;
    (void)e;
    if (!count)
        return;
    SetAPen(r, fpen);
    SetBPen(r, bpen);
    SetDrMd(r, mode);
    Move(r, x + xy[0], y + xy[1]);
    for (i = 1; i < count; i++)
        Draw(r, x + xy[2 * i], y + xy[2 * i + 1]);
    if (count == 1)
        Draw(r, x + xy[0], y + xy[1]);
}

/* A plain struct Image: a BitMap over its planes, PlanePick choosing which planes it has and PlaneOnOff filling
 * the rest (graphics takes a NULL plane as all 0s and -1 as all 1s) */
static void h_blit_image(struct oi_env *e, void *rp, void *image, olc_oi_s32 x, olc_oi_s32 y)
{
    struct Image *im = (struct Image *)image;
    struct BitMap bm;
    ULONG plane = 0, words, i;
    (void)e;
    if (im->Width <= 0 || im->Height <= 0)
        return;
    words = (ULONG)(im->Width + 15) >> 4;
    InitBitMap(&bm, 8, im->Width, im->Height);
    for (i = 0; i < 8; i++) {
        if (im->PlanePick & (1 << i))
            bm.Planes[i] = im->ImageData ? (PLANEPTR)((UWORD *)im->ImageData + words * im->Height * plane++) : NULL;
        else
            bm.Planes[i] = (im->PlaneOnOff & (1 << i)) ? (PLANEPTR)-1 : NULL;
    }
    BltBitMapRastPort(&bm, 0, 0, (struct RastPort *)rp, x, y, im->Width, im->Height, 0xC0);
}

static void h_fill_pattern(struct oi_env *e, void *rp, olc_oi_u32 fpen, olc_oi_u32 bpen, olc_oi_u32 mode,
                           const unsigned short *pattern, olc_oi_s32 patsize, olc_oi_s32 x0, olc_oi_s32 y0,
                           olc_oi_s32 x1, olc_oi_s32 y1)
{
    struct RastPort *r = (struct RastPort *)rp;
    UWORD *op = r->AreaPtrn;
    BYTE os = r->AreaPtSz;
    (void)e;
    if (x1 < x0 || y1 < y0)
        return;
    r->AreaPtrn = (UWORD *)pattern;
    r->AreaPtSz = (BYTE)patsize;
    SetAPen(r, fpen);
    SetBPen(r, bpen);
    SetDrMd(r, mode);
    RectFill(r, x0, y0, x1, y1);
    r->AreaPtrn = op;
    r->AreaPtSz = os;
}

static void h_set_font(struct oi_wenv *we, struct oi_RastPort *rp, void *font)
{
    (void)we;
    if (font)
        SetFont((struct RastPort *)rp, (struct TextFont *)font);
}

static void h_complement(struct oi_wenv *we, void *rp, olc_oi_s32 x0, olc_oi_s32 y0, olc_oi_s32 x1, olc_oi_s32 y1)
{
    struct RastPort *r = (struct RastPort *)rp;
    UBYTE mode = r->DrawMode;
    UWORD *op = r->AreaPtrn;
    BYTE os = r->AreaPtSz;
    (void)we;
    if (x1 < x0 || y1 < y0)
        return;
    r->AreaPtrn = NULL;                 /* every pixel, whatever pattern the program left (h_fill_rect) */
    r->AreaPtSz = 0;
    SetDrMd(r, COMPLEMENT);
    RectFill(r, x0, y0, x1, y1);
    SetDrMd(r, mode);
    r->AreaPtrn = op;
    r->AreaPtSz = os;
}

static void h_blit_bitmap(struct oi_wenv *we, void *rp, struct oi_BitMap *bm, olc_oi_s32 x, olc_oi_s32 y,
                          olc_oi_s32 w, olc_oi_s32 h)
{
    (void)we;
    BltBitMapRastPort((struct BitMap *)bm, 0, 0, (struct RastPort *)rp, x, y, w, h, 0xC0);
}

static void h_scroll_raster(struct oi_wenv *we, void *rp, olc_oi_s32 dx, olc_oi_s32 dy, olc_oi_s32 x0,
                            olc_oi_s32 y0, olc_oi_s32 x1, olc_oi_s32 y1)
{
    (void)we;
    ScrollRasterBF((struct RastPort *)rp, dx, dy, x0, y0, x1, y1);
}

static const olc_oi_hooks hooks = {
    NULL,                       /* obtain_rp: Intuition's */
    NULL,                       /* release_rp: Intuition's */
    NULL,                       /* draw_image_state: Intuition's (BOOPSI images) */
    h_erase_rect,
    h_fill_rect,
    h_text,
    h_text_length,
    h_font_height,
    h_polyline,
    h_blit_image,
    h_fill_pattern,
    NULL,                       /* deep: Intuition's (its Workbench screen) */
    NULL,                       /* limit_rect: Intuition's (layers' clip regions) */
    NULL,                       /* limit_end: Intuition's */
    h_set_font,
    h_complement,
    h_blit_bitmap,
    h_scroll_raster
};

/* The table, with graphics.library opened for it: NULL when graphics.library can't be had (never, in a ROM that
 * has Intuition). The shell calls it once, at its init. */
const olc_oi_hooks *olc_oi(void)
{
    if (!olc_gfx) {
        struct GfxBase *g = (struct GfxBase *)OpenLibrary((CONST_STRPTR)"graphics.library", 39);
        if (!g) return NULL;
        Forbid();
        if (!olc_gfx) {
            olc_gfx = g;
            g = NULL;
        }
        Permit();
        if (g) CloseLibrary((struct Library *)g);
    }
    return &hooks;
}
#else
typedef int olc_oi_amiga_unused;        /* the hooks are graphics.library's: Amiga only */
#endif
