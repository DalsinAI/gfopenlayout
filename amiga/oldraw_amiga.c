/* GFOpenLayout drawing's Amiga target: oldraw_ops on a RastPort. The only place wave 1's drawing reaches
 * graphics.library, layers.library, cybergraphics.library and icon.library. Wave 1 draws each call as it comes
 * (RectFill, FillPixelArray, WritePixelArray a block at a time, ClipBlit, Text, DrawIconStateA); the caches and the AC090 and GFOpenGPU hooks
 * come in behind these same calls in wave 2.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "oldraw_internal.h"

#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <graphics/regions.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <workbench/icon.h>
#include <workbench/workbench.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/diskfont.h>
#include <proto/icon.h>
#include <inline/macros.h>

#include <stdlib.h>
#include <string.h>

/* cybergraphics.library through its vectors, as olui_amiga.c calls it; open while any target is */
static struct Library *OldrawCgxBase;
static int cgx_users;
#define CGX_FILL(rp, x, y, w, h, argb) LP6(0x96, ULONG, FillPixelArray, struct RastPort *, (rp), a1, UWORD, (x), d0, \
    UWORD, (y), d1, UWORD, (w), d2, UWORD, (h), d3, ULONG, (argb), d4, , OldrawCgxBase)
#define CGX_INVERT(rp, x, y, w, h) LP5(0x90, ULONG, InvertPixelArray, struct RastPort *, (rp), a1, UWORD, (x), d0, \
    UWORD, (y), d1, UWORD, (w), d2, UWORD, (h), d3, , OldrawCgxBase)
#define CGX_WRITE(src, sx, sy, mod, rp, x, y, w, h, fmt) LP10(0x7e, ULONG, WritePixelArray, APTR, (src), a0, \
    UWORD, (sx), d0, UWORD, (sy), d1, UWORD, (mod), d2, struct RastPort *, (rp), a1, UWORD, (x), d3, UWORD, (y), d4, \
    UWORD, (w), d5, UWORD, (h), d6, UBYTE, (fmt), d7, , OldrawCgxBase)
#define CGX_MAPATTR(bm, a) LP2(0x60, ULONG, GetCyberMapAttr, struct BitMap *, (bm), a0, ULONG, (a), d0, , OldrawCgxBase)
#define CYBRMATTR_DEPTH 0x80000007UL
#define CYBRMATTR_ISCYBERGFX 0x80000008UL
#define RECTFMT_ARGB 2

#define MAX_PENS 64

typedef struct {
    struct RastPort *rp;
    struct Screen *scr;
    struct ColorMap *cm;
    int direct;                 /* true colour through cybergraphics */
    UWORD role_pen[OLDRAW_PEN_COUNT];
    struct { ULONG rgb; LONG pen; int owned; } pens[MAX_PENS];
    int npens;
    struct TextFont *font;      /* opened by family and size; NULL: rp's */
    char family[64];
    int px;
    struct Layer_Info *li;      /* a layerless RastPort's: oldraw's own layer on its BitMap, so it can be cut */
    struct Layer *own;
    oldraw *d;
} amiga_target;

static ULONG pen_for_rgb(amiga_target *t, ULONG rgb)
{
    int i;
    LONG pen;
    ULONG r = ((rgb >> 16) & 255) * 0x01010101UL, g = ((rgb >> 8) & 255) * 0x01010101UL, b = (rgb & 255) * 0x01010101UL;
    for (i = 0; i < t->npens; ++i)
        if (t->pens[i].rgb == rgb) return (ULONG)t->pens[i].pen;
    int owned = 0;
    pen = -1;
    if (t->cm && t->npens < MAX_PENS) pen = ObtainBestPen(t->cm, r, g, b, OBP_Precision, PRECISION_GUI, TAG_DONE);
    if (pen >= 0) owned = 1;            /* only a pen ObtainBestPen gave is released again */
    if (pen < 0 && t->cm) pen = FindColor(t->cm, r, g, b, -1);
    if (pen < 0) pen = 1;
    if (t->npens < MAX_PENS) {
        t->pens[t->npens].rgb = rgb;
        t->pens[t->npens].pen = pen;
        t->pens[t->npens].owned = owned;
        ++t->npens;
    }
    return (ULONG)pen;
}

static ULONG pen_of(amiga_target *t, oldraw_colour c)
{
    if ((c >> 24) == 0xFEu) return t->role_pen[(c & 0xFFu) < OLDRAW_PEN_COUNT ? (c & 0xFFu) : OLDRAW_PEN_TEXT];
    return pen_for_rgb(t, c & 0xFFFFFFUL);
}

static void a_fill(void *tp, int x, int y, int w, int h, oldraw_colour c)
{
    amiga_target *t = (amiga_target *)tp;
    if (t->direct) {
        CGX_FILL(t->rp, (UWORD)x, (UWORD)y, (UWORD)w, (UWORD)h, oldraw_resolve(t->d, c) & 0xFFFFFFUL);
        return;
    }
    SetAPen(t->rp, (UBYTE)pen_of(t, c));
    SetDrMd(t->rp, JAM1);
    RectFill(t->rp, x, y, x + w - 1, y + h - 1);
}

static void a_invert(void *tp, int x, int y, int w, int h)
{
    amiga_target *t = (amiga_target *)tp;
    if (t->direct) {
        CGX_INVERT(t->rp, (UWORD)x, (UWORD)y, (UWORD)w, (UWORD)h);
        return;
    }
    {
        ULONG mask = t->rp->Mask;
        SetDrMd(t->rp, COMPLEMENT);
        SetWriteMask(t->rp, 0xFF);
        RectFill(t->rp, x, y, x + w - 1, y + h - 1);
        SetWriteMask(t->rp, mask);
        SetDrMd(t->rp, JAM1);
    }
}

/* the clip rectangle as the layer's clip region while text or an icon draws (they draw whole). The layer's own
 * region (OLUI's, say) is kept: ours is cut to it, and it goes back after. t->rp always has a layer (a layerless
 * RastPort gets one of oldraw's own in oldraw_new_rastport). */
typedef struct { struct Region *reg, *old; int on; } clip_state;

static void clip_on(amiga_target *t, const oldraw_rect *c, clip_state *cs)
{
    struct Rectangle r;
    cs->on = 0;
    if (!c || !t->rp->Layer) return;
    cs->reg = NewRegion();
    if (!cs->reg) return;
    r.MinX = (WORD)c->x;
    r.MinY = (WORD)c->y;
    r.MaxX = (WORD)(c->x + c->w - 1);
    r.MaxY = (WORD)(c->y + c->h - 1);
    if (!OrRectRegion(cs->reg, &r)) { DisposeRegion(cs->reg); return; }
    cs->old = t->rp->Layer->ClipRegion;
    if (cs->old && !AndRegionRegion(cs->old, cs->reg)) { DisposeRegion(cs->reg); return; }
    InstallClipRegion(t->rp->Layer, cs->reg);
    cs->on = 1;
}

static void clip_off(amiga_target *t, clip_state *cs)
{
    if (!cs->on) return;
    InstallClipRegion(t->rp->Layer, cs->old);
    DisposeRegion(cs->reg);
    cs->on = 0;
}

static void a_image(void *tp, const oldraw_image *img_c, int sx, int sy, int x, int y, int w, int h, unsigned flags)
{
    amiga_target *t = (amiga_target *)tp;
    oldraw_image *img = (oldraw_image *)img_c;      /* its selected and ghosted pixels are kept in it */
    const oldraw_rect *b;
    const uint32_t *px;
    int i, j, n;
    if (img->kind == OLDRAW_IMG_ICON) {
        /* icon.library draws the whole icon, so always through a clip region: without one, DrawIconState was 3 to 4
         * times slower in GFLayoutBench (icon_blit_diskobject 1683 and 1932 icons/s with it, 422 and 535 without) */
        oldraw_rect c;
        clip_state cs;
        c.x = x;
        c.y = y;
        c.w = w;
        c.h = h;
        clip_on(t, &c, &cs);
        /* GetIconRectangle's corner need not be 0,0: the picture's 0,0 is drawn at its MinX, MinY */
        DrawIconState(t->rp, (struct DiskObject *)img->icon, NULL, x - sx - img->ox, y - sy - img->oy,
                      (flags & OLDRAW_SELECTED) ? IDS_SELECTED : (flags & OLDRAW_GHOSTED) ? IDS_DISABLED : IDS_NORMAL,
                      ICONDRAWA_Frameless, TRUE, ICONDRAWA_EraseBackground, FALSE, TAG_DONE);
        clip_off(t, &cs);
        return;
    }
    px = oldraw_image_variant(img, flags);
    if (!px) return;
    n = oldraw_image_blocks(img, &b);
    SetDrMd(t->rp, JAM1);
    for (i = 0; i < n; ++i) {
        /* each block of drawn pixels that crosses what is asked for: one blit (true colour) */
        int bx = (int)b[i].x, by = (int)b[i].y, bw = (int)b[i].w, bh = (int)b[i].h;
        if (bx < sx) { bw -= sx - bx; bx = sx; }
        if (by < sy) { bh -= sy - by; by = sy; }
        if (bx + bw > sx + w) bw = sx + w - bx;
        if (by + bh > sy + h) bh = sy + h - by;
        if (bw <= 0 || bh <= 0) continue;
        if (t->direct) {
            CGX_WRITE((APTR)px, (UWORD)bx, (UWORD)by, (UWORD)(img->w * 4), t->rp, (UWORD)(x + bx - sx),
                      (UWORD)(y + by - sy), (UWORD)bw, (UWORD)bh, RECTFMT_ARGB);
            continue;
        }
        /* a palette: runs of one colour along each row of the block */
        for (j = by; j < by + bh; ++j) {
            const uint32_t *row = px + (size_t)j * (size_t)img->w;
            int k = bx;
            while (k < bx + bw) {
                uint32_t rgb = row[k] & 0xFFFFFFUL;
                int from = k;
                while (k < bx + bw && (row[k] & 0xFFFFFFUL) == rgb) ++k;
                SetAPen(t->rp, (UBYTE)pen_for_rgb(t, rgb));
                RectFill(t->rp, x + from - sx, y + j - sy, x + k - 1 - sx, y + j - sy);
            }
        }
    }
}

/* the font for f: rp's own, or one opened by its family and size and kept */
static struct TextFont *font_for(amiga_target *t, const oldraw_font *f)
{
    struct TextAttr ta;
    if (!f || !f->family) return NULL;
    if (t->font && t->px == f->px && !strcmp(t->family, f->family)) return t->font;
    if (t->font) { CloseFont(t->font); t->font = NULL; }
    strncpy(t->family, f->family, sizeof t->family - 1);
    t->family[sizeof t->family - 1] = 0;
    t->px = f->px;
    ta.ta_Name = (STRPTR)t->family;
    ta.ta_YSize = (UWORD)(f->px > 0 ? f->px : (t->scr && t->scr->Font) ? t->scr->Font->ta_YSize : 8);
    ta.ta_Style = 0;
    ta.ta_Flags = 0;
    t->font = OpenDiskFont(&ta);
    return t->font;
}

/* utf8 as Latin-1 for Text() (what isn't Latin-1: "?"); its length */
static int latin1(const char *s, size_t n, char *out, int max)
{
    size_t i = 0;
    int k = 0;
    while (i < n && k < max) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) { out[k++] = (char)c; ++i; continue; }
        if ((c & 0xE0u) == 0xC0u && i + 1 < n && ((unsigned char)s[i + 1] & 0xC0u) == 0x80u) {
            unsigned v = ((c & 0x1Fu) << 6) | ((unsigned char)s[i + 1] & 0x3Fu);
            out[k++] = (char)(v < 0x100 ? v : '?');
            i += 2;
            continue;
        }
        out[k++] = '?';
        ++i;
        while (i < n && ((unsigned char)s[i] & 0xC0u) == 0x80u) ++i;
    }
    return k;
}

static void with_font(amiga_target *t, const oldraw_font *f, unsigned style, struct TextFont **old)
{
    struct TextFont *tf = font_for(t, f);
    ULONG want = (ULONG)(((style & OLDRAW_BOLD) ? FSF_BOLD : 0) | ((style & OLDRAW_ITALIC) ? FSF_ITALIC : 0) |
                         ((style & OLDRAW_UNDERLINE) ? FSF_UNDERLINED : 0));
    *old = t->rp->Font;
    if (tf && t->rp->Font != tf) SetFont(t->rp, tf);
    if (want) SetSoftStyle(t->rp, want, 0xFF);      /* plain text (the most) leaves the style alone */
}

static void font_back(amiga_target *t, struct TextFont *old)
{
    if (t->rp->AlgoStyle) SetSoftStyle(t->rp, 0, 0xFF);
    if (old && t->rp->Font != old) SetFont(t->rp, old);
}

static int a_measure(void *tp, const char *s, size_t n, const oldraw_font *f, oldraw_text_size *size)
{
    amiga_target *t = (amiga_target *)tp;
    char buf[256];
    struct TextFont *old;
    int k = latin1(s, n, buf, (int)sizeof buf);
    with_font(t, f, f ? f->style : 0, &old);     /* oldraw_text_measure gives the call's style in f->style */
    size->width = (int)TextLength(t->rp, (STRPTR)buf, (ULONG)k);
    size->ascent = t->rp->TxBaseline;
    size->descent = t->rp->TxHeight - t->rp->TxBaseline;
    font_back(t, old);
    return 1;
}

static int a_text(void *tp, int x, int y, const char *s, size_t n, const oldraw_font *f, oldraw_colour c,
                  unsigned style, const oldraw_rect *clip)
{
    amiga_target *t = (amiga_target *)tp;
    char buf[256];
    struct TextFont *old;
    clip_state cs;
    int k = latin1(s, n, buf, (int)sizeof buf);
    clip_on(t, clip, &cs);
    with_font(t, f, style | (f ? f->style : 0), &old);
    SetDrMd(t->rp, JAM1);
    if (style & (OLDRAW_SHADOW | OLDRAW_OUTLINE)) {
        static const signed char sh[][2] = { {1, 1} }, ol[][2] = { {-1, 0}, {1, 0}, {0, -1}, {0, 1} };
        int i, m = (style & OLDRAW_OUTLINE) ? 4 : 1;
        SetAPen(t->rp, (UBYTE)pen_of(t, OLDRAW_PEN(OLDRAW_PEN_SHADOW)));
        for (i = 0; i < m; ++i) {
            const signed char *o = (style & OLDRAW_OUTLINE) ? ol[i] : sh[0];
            Move(t->rp, x + o[0], y + o[1]);
            Text(t->rp, (STRPTR)buf, (ULONG)k);
        }
    }
    SetAPen(t->rp, (UBYTE)pen_of(t, c));      /* on true colour too: Text() draws with a pen */
    Move(t->rp, x, y);
    Text(t->rp, (STRPTR)buf, (ULONG)k);
    font_back(t, old);
    clip_off(t, &cs);
    return 1;
}

static void a_flush(void *tp) { (void)tp; }

/* any part of x,y w by h hidden with no backing store: in a simple-refresh layer, under a layer in front of it
 * (Layer's front list and bounds are public; its ClipRects' own fields are not) */
static int hidden_in(struct Layer *l, int x, int y, int w, int h)
{
    struct Layer *f;
    LONG x0, y0, x1, y1;
    if (!l || !(l->Flags & LAYERSIMPLE)) return 0;
    x0 = l->bounds.MinX + x;
    y0 = l->bounds.MinY + y;
    x1 = x0 + w - 1;
    y1 = y0 + h - 1;
    for (f = l->front; f; f = f->front)
        if (f->bounds.MinX <= x1 && f->bounds.MaxX >= x0 && f->bounds.MinY <= y1 && f->bounds.MaxY >= y0) return 1;
    return 0;
}

/* ClipBlit copies overlapping areas of one RastPort the right way round (BltBitMap's own order). A source that is
 * partly hidden in a simple-refresh window isn't copied: oldraw damages the destination instead. */
static int a_copy(void *tp, int sx, int sy, int dx, int dy, int w, int h)
{
    amiga_target *t = (amiga_target *)tp;
    struct Layer *l = t->rp->Layer;
    int hidden;
    /* layers.library locks every layer an arrangement moves, so with ours locked its front list stays put while we
     * look and copy (and the layer lock nests, as ClipBlit takes it too) */
    if (l) LockLayer(0, l);
    hidden = hidden_in(l, sx, sy, w, h);
    if (!hidden) ClipBlit(t->rp, sx, sy, t->rp, dx, dy, w, h, 0xC0);
    if (l) UnlockLayer(l);
    return hidden;
}

static void a_image_free(void *tp, oldraw_image *img) { (void)tp; (void)img; }

static const oldraw_ops amiga_ops = { a_fill, a_invert, a_image, a_text, a_measure, a_flush, a_image_free, a_copy };

static void read_pens(amiga_target *t)
{
    struct DrawInfo *dri = t->scr ? GetScreenDrawInfo(t->scr) : NULL;
    int i;
    for (i = 0; i < OLDRAW_PEN_COUNT; ++i) {
        ULONG rgb[3];
        UWORD pen = (UWORD)(dri && i < dri->dri_NumPens ? dri->dri_Pens[i] : (i == OLDRAW_PEN_SHINE ? 2 : 1));
        t->role_pen[i] = pen;
        if (t->cm) {
            GetRGB32(t->cm, pen, 1, rgb);
            t->d->pens[i] = 0xFF000000UL | ((rgb[0] >> 24) << 16) | ((rgb[1] >> 24) << 8) | (rgb[2] >> 24);
        }
    }
    if (dri) FreeScreenDrawInfo(t->scr, dri);
}

static void release_pens(amiga_target *t)
{
    int i;
    for (i = 0; i < t->npens; ++i)
        if (t->pens[i].owned && t->cm) ReleasePen(t->cm, (ULONG)t->pens[i].pen);
    t->npens = 0;
}

static void free_target(oldraw *d)
{
    amiga_target *t = (amiga_target *)d->target;
    if (!t) return;
    release_pens(t);
    if (t->font) CloseFont(t->font);
    if (t->own) DeleteLayer(0, t->own);
    if (t->li) DisposeLayerInfo(t->li);
    if (--cgx_users == 0 && OldrawCgxBase) { CloseLibrary(OldrawCgxBase); OldrawCgxBase = NULL; }
    free(t);
    d->target = NULL;
}

oldraw *oldraw_new_rastport(struct RastPort *rp, struct Screen *scr, int width, int height)
{
    amiga_target *t;
    oldraw *d;
    if (!rp) return NULL;
    t = (amiga_target *)calloc(1, sizeof *t);
    if (!t) return NULL;
    d = oldraw_new(&amiga_ops, t, width, height, NULL);
    if (!d) { free(t); return NULL; }
    if (!scr) {
        /* the default public screen's pens (it stays open while Workbench runs) */
        scr = LockPubScreen(NULL);
        if (scr) UnlockPubScreen(NULL, scr);
    }
    if (!rp->Layer) {
        /* a BitMap's RastPort: drawn through a layer of our own over it, which clip regions can cut. Neither making
         * the layer nor deleting it may touch the BitMap (its owner drew there already and draws there after us):
         * no backfill for the layer, and none for the LayerInfo, whose hook fills what DeleteLayer uncovers.
         * Without these, a slot window's bitmap (GFOpenTitle's) came out cleared to pen 0. */
        if (!rp->BitMap || width <= 0 || height <= 0 || !(t->li = NewLayerInfo()) ||
            (InstallLayerInfoHook(t->li, LAYERS_NOBACKFILL), 0) ||
            !(t->own = CreateUpfrontHookLayer(t->li, rp->BitMap, 0, 0, width - 1, height - 1, LAYERSIMPLE,
                                              LAYERS_NOBACKFILL, NULL))) {
            if (t->li) DisposeLayerInfo(t->li);
            free(t);
            d->target = NULL;
            oldraw_free(d);
            return NULL;
        }
        if (rp->Font) SetFont(t->own->rp, rp->Font);
        SetAPen(t->own->rp, rp->FgPen);
        rp = t->own->rp;
    }
    t->rp = rp;
    t->scr = scr;
    t->cm = scr ? scr->ViewPort.ColorMap : NULL;
    t->d = d;
    ++cgx_users;
    if (!OldrawCgxBase) OldrawCgxBase = OpenLibrary((CONST_STRPTR)"cybergraphics.library", 41);
    if (OldrawCgxBase && rp->BitMap && CGX_MAPATTR(rp->BitMap, CYBRMATTR_ISCYBERGFX) &&
        CGX_MAPATTR(rp->BitMap, CYBRMATTR_DEPTH) > 8)
        t->direct = 1;
    d->free_target = free_target;
    read_pens(t);
    return d;
}

void oldraw_pens_changed(oldraw *d)
{
    amiga_target *t;
    if (!d || d->ops != &amiga_ops) return;
    t = (amiga_target *)d->target;
    release_pens(t);
    read_pens(t);
    if (t->font) { CloseFont(t->font); t->font = NULL; }   /* opened again, as Font prefs now say, when next used */
    t->family[0] = 0;
    oldraw_theme_changed(d);
}

oldraw_image *oldraw_image_icon(oldraw *d, struct DiskObject *icon)
{
    oldraw_image *img;
    struct Rectangle r;
    amiga_target *t;
    if (!d || !icon || d->ops != &amiga_ops) return NULL;
    t = (amiga_target *)d->target;
    if (!GetIconRectangle(t->rp, icon, NULL, &r, ICONDRAWA_Frameless, TRUE, TAG_DONE)) return NULL;
    img = (oldraw_image *)calloc(1, sizeof *img);
    if (!img) return NULL;
    img->kind = OLDRAW_IMG_ICON;
    img->icon = icon;
    img->w = r.MaxX - r.MinX + 1;
    img->h = r.MaxY - r.MinY + 1;
    img->ox = r.MinX;
    img->oy = r.MinY;
    return img;
}
