/* GFOpenLayout UI in an Intuition window (gfopenlayout_ui_amiga.h).
 *
 * Everything is drawn into a buffer the size of the window's inside, a
 * simple layer on a bitmap like the window's, so clip regions cut text as
 * they cut boxes, and the buffer is copied to the window in one blit: no
 * flicker while a window is drawn again. graphics.library is GFOpenGfx's;
 * boxes on a true-colour screen go through cybergraphics.library's
 * FillPixelArray in their exact colours, row by row for gradients and
 * rounded corners (as GFOpenGadTools' look does); on a screen with a palette,
 * pens from ObtainBestPen, a gradient in four bands (THEME_SPEC.md 6).
 * Text is the screen's font at each size, through diskfont.library, its
 * UTF-8 turned into Latin-1 for the bitmap fonts. Integer maths only.
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout_ui_amiga.h"

#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/gfxmacros.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <graphics/regions.h>
#include <graphics/clip.h>
#include <graphics/layers.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/diskfont.h>
#include <inline/macros.h>

#include <stdlib.h>
#include <string.h>

/* cybergraphics.library through its vectors, as GFOpenGadTools' look_draw.c
 * calls it: FillPixelArray (-150) and GetCyberMapAttr (-96) */
static struct Library *CyberGfxBase;
/* keymap.library's MapRawKey (-42), for windows without VANILLAKEY, through
 * its own base so a program's KeymapBase is left alone */
static struct Library *OluiKeymapBase;
#define KM_MAPRAWKEY(ie, buf, len, km) LP4(0x2a, WORD, MapRawKey, struct InputEvent *, (ie), a0, STRPTR, (buf), a1, \
    LONG, (len), d1, struct KeyMap *, (km), a2, , OluiKeymapBase)
#define CGX_FILL(rp, x, y, w, h, argb) LP6(0x96, ULONG, FillPixelArray, struct RastPort *, (rp), a1, UWORD, (x), d0, \
    UWORD, (y), d1, UWORD, (w), d2, UWORD, (h), d3, ULONG, (argb), d4, , CyberGfxBase)
#define CGX_MAPATTR(bm, a) LP2(0x60, ULONG, GetCyberMapAttr, struct BitMap *, (bm), a0, ULONG, (a), d0, , CyberGfxBase)
#define CYBRMATTR_DEPTH 0x80000007UL
#define CYBRMATTR_ISCYBERGFX 0x80000008UL

#define MAX_PENS 96
#define MAX_FONTS 12
#define MAX_OWN 4
#define THEME_DIR "SYS:Prefs/Presets/Themes/"

/* rawkeys */
#define RAW_UP 0x4c
#define RAW_DOWN 0x4d
#define RAW_RIGHT 0x4e
#define RAW_LEFT 0x4f
#define RAW_HELP 0x5f
#define RAW_WHEEL_UP 0x7a
#define RAW_WHEEL_DOWN 0x7b

typedef struct {
    ULONG rgb;
    LONG pen;
} pen_slot;

typedef struct {
    int px;
    unsigned style;             /* OL_TEXT_BOLD | OL_TEXT_MONO */
    struct TextFont *tf;
} font_slot;

struct olui_amiga {
    olui *ui;
    struct Window *win;
    struct Screen *scr;
    int direct;                 /* a true-colour screen: FillPixelArray */
    /* the buffer */
    struct BitMap *bm;
    struct Layer_Info *li;
    struct Layer *layer;
    struct RastPort *rp;
    struct Region *region;
    int bw, bh;
    olui_rect clip;             /* the clip now; w 0: none */
    int clipped;
    olui_rect area;             /* drawing only this part (olui_damaged's): everything is cut to it */
    int limited;
    pen_slot pens[MAX_PENS];
    int npens;
    font_slot fonts[MAX_FONTS];
    int nfonts;
    struct RastPort mrp;        /* for measuring */
    char font_name[64];
    int zoom;
    ULONG down_secs, down_micros;
    int down_x, down_y;
    unsigned qual;
    /* the widgets the program draws itself, and where they were drawn (the buffer's pixels) */
    struct {
        const olui_el *el;
        olui_amiga_own_fn fn;
        void *data;
        olui_rect box;
        int seen;
    } own[MAX_OWN];
    int nown;
    char mono_name[64];
    olui_amiga_image_fn image_fn;   /* draws img's pictures (olui_amiga_images) */
    void *image_data;
    /* the part of the window olui has (olui_amiga_strip): w or h 0 to the inside's edge */
    int sx, sy, sw, sh;
    int strip;
    int inside;                 /* the pointer was last inside the strip */
    int held;                   /* the left button went down inside the strip and is still down */
    int keys;                   /* OLUI_AMIGA_KEYS_* */
};

/* ---- Colours ---------------------------------------------------------------- */

static LONG pen_for(olui_amiga *a, ULONG argb)
{
    ULONG rgb = argb & 0xffffffUL;
    int i;
    LONG pen;
    for (i = 0; i < a->npens; ++i)
        if (a->pens[i].rgb == rgb) return a->pens[i].pen;
    pen = ObtainBestPen(a->scr->ViewPort.ColorMap, ((rgb >> 16) & 255) * 0x01010101UL,
                        ((rgb >> 8) & 255) * 0x01010101UL, (rgb & 255) * 0x01010101UL,
                        OBP_Precision, PRECISION_GUI, TAG_DONE);
    if (pen < 0) pen = 1;
    else if (a->npens < MAX_PENS) {
        a->pens[a->npens].rgb = rgb;
        a->pens[a->npens].pen = pen;
        ++a->npens;
    } else {
        /* full: this colour stays the pen until the window closes (not kept to release) */
        ReleasePen(a->scr->ViewPort.ColorMap, pen);
    }
    return pen;
}

/* A span of a row in one colour, cut to the clip and the buffer */
static void span(olui_amiga *a, int x, int y, int w, int h, ULONG argb)
{
    if ((argb >> 24) < 0x80) return;    /* see-through: left out (no blending in the buffer) */
    if (a->clipped) {
        int x2 = x + w, y2 = y + h;
        if (x < a->clip.x) x = a->clip.x;
        if (y < a->clip.y) y = a->clip.y;
        if (x2 > a->clip.x + a->clip.w) x2 = a->clip.x + a->clip.w;
        if (y2 > a->clip.y + a->clip.h) y2 = a->clip.y + a->clip.h;
        w = x2 - x;
        h = y2 - y;
    }
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > a->bw) w = a->bw - x;
    if (y + h > a->bh) h = a->bh - y;
    if (w <= 0 || h <= 0) return;
    if (a->direct) {
        CGX_FILL(a->rp, (UWORD)x, (UWORD)y, (UWORD)w, (UWORD)h, argb & 0xffffffUL);
    } else {
        SetAPen(a->rp, (UBYTE)pen_for(a, argb));
        RectFill(a->rp, x, y, x + w - 1, y + h - 1);
    }
}

static ULONG mix(ULONG c0, ULONG c1, int t, int span1000)
{
    int k;
    ULONG out = 0xff000000UL;
    if (span1000 <= 0) return c1;
    for (k = 0; k < 24; k += 8) {
        int v0 = (int)((c0 >> k) & 255), v1 = (int)((c1 >> k) & 255);
        out |= (ULONG)(v0 + (v1 - v0) * t / span1000) << k;
    }
    return out;
}

/* The fill's colour t thousandths down (stops in olui_draw_op) */
static ULONG stop_colour(const olui_draw_op *op, int t)
{
    int i;
    if (op->stops == 1 || t <= op->stop_pos[0]) return op->stop_argb[0] | 0xff000000UL;
    for (i = 0; i < op->stops - 1; ++i)
        if (t <= op->stop_pos[i + 1])
            return mix(op->stop_argb[i], op->stop_argb[i + 1], t - op->stop_pos[i],
                       op->stop_pos[i + 1] - op->stop_pos[i]);
    return op->stop_argb[op->stops - 1] | 0xff000000UL;
}

static int isqrt(int v)
{
    int r = 0, b = 1 << 14;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* How far a row is cut in by a corner of radius r (row 0 the edge) */
static int corner_inset(int r, int row)
{
    int dy;
    if (r <= 0 || row >= r) return 0;
    dy = 2 * (r - row) - 1;
    return r - (isqrt(4 * r * r - dy * dy) + 1) / 2;
}

static void draw_box(olui_amiga *a, const olui_draw_op *op)
{
    int x = op->box.x, y = op->box.y, w = op->box.w, h = op->box.h;
    int r = op->radius, b = op->border, row;
    int ir, iw, ih;
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (b * 2 > w || b * 2 > h) b = 0;
    ir = r - b > 0 ? r - b : 0;
    iw = w - 2 * b;
    ih = h - 2 * b;
    for (row = 0; row < h; ++row) {
        int out = corner_inset(r, row < h - 1 - row ? row : h - 1 - row);
        int irow = row - b;
        ULONG fill = 0;
        if (op->stops) {
            int t = h > 1 ? row * 1000 / (h - 1) : 0;
            if (!a->direct) t = (t * 4 / 1001) * 250 + 125;     /* four bands on a palette */
            fill = stop_colour(op, t);
        }
        if (b && (irow < 0 || irow >= ih)) {
            span(a, x + out, y + row, w - 2 * out, 1, op->border_argb | 0xff000000UL);
        } else if (b) {
            int in = corner_inset(ir, irow < ih - 1 - irow ? irow : ih - 1 - irow);
            int ix = x + b + in, iwr = iw - 2 * in;
            span(a, x + out, y + row, ix - (x + out), 1, op->border_argb | 0xff000000UL);
            span(a, ix + iwr, y + row, (x + w - out) - (ix + iwr), 1, op->border_argb | 0xff000000UL);
            if (op->stops) span(a, ix, y + row, iwr, 1, fill);
        } else if (op->stops) {
            span(a, x + out, y + row, w - 2 * out, 1, fill);
        }
    }
}

/* ---- Fonts --------------------------------------------------------------------- */

static struct TextFont *font_for(olui_amiga *a, int px, unsigned style)
{
    struct TextAttr ta;
    struct TextFont *tf;
    int i;
    style &= OL_TEXT_BOLD | OL_TEXT_MONO;
    if (px < 6) px = 6;
    for (i = 0; i < a->nfonts; ++i)
        if (a->fonts[i].px == px && a->fonts[i].style == style) return a->fonts[i].tf;
    ta.ta_Name = (STRPTR)((style & OL_TEXT_MONO) ? a->mono_name : a->font_name);
    ta.ta_YSize = (UWORD)px;
    ta.ta_Style = (style & OL_TEXT_BOLD) ? FSF_BOLD : FS_NORMAL;
    ta.ta_Flags = 0;
    /* in gfopenlayout.library diskfont.library may not have opened */
    tf = DiskfontBase ? OpenDiskFont(&ta) : NULL;
    if (!tf && DiskfontBase) {
        ta.ta_Style = FS_NORMAL;
        tf = OpenDiskFont(&ta);
    }
    if (!tf) tf = OpenFont(a->scr->Font);
    if (!tf) return NULL;
    if (a->nfonts == MAX_FONTS) {
        /* the oldest goes */
        CloseFont(a->fonts[0].tf);
        memmove(&a->fonts[0], &a->fonts[1], sizeof(a->fonts[0]) * (MAX_FONTS - 1));
        --a->nfonts;
    }
    a->fonts[a->nfonts].px = px;
    a->fonts[a->nfonts].style = style;
    a->fonts[a->nfonts].tf = tf;
    ++a->nfonts;
    return tf;
}

/* UTF-8 to Latin-1 for the bitmap fonts: what Latin-1 lacks is '?', but a
 * password's bullet (U+2022) is the middle dot */
static size_t latin1(const char *s, size_t n, char *out, size_t room)
{
    size_t i = 0, k = 0;
    while (i < n && k + 1 < room) {
        unsigned char c = (unsigned char)s[i];
        unsigned long u;
        int more;
        if (c < 0x80) { u = c; more = 0; }
        else if ((c & 0xe0) == 0xc0) { u = c & 0x1f; more = 1; }
        else if ((c & 0xf0) == 0xe0) { u = c & 0x0f; more = 2; }
        else if ((c & 0xf8) == 0xf0) { u = c & 0x07; more = 3; }
        else { u = '?'; more = 0; }
        ++i;
        while (more-- && i < n && ((unsigned char)s[i] & 0xc0) == 0x80) u = (u << 6) | ((unsigned char)s[i++] & 0x3f);
        out[k++] = (char)(u < 0x100 ? u : u == 0x2022 ? 0xb7 : '?');
    }
    out[k] = 0;
    return k;
}

static void use_font(olui_amiga *a, struct RastPort *rp, struct TextFont *tf, unsigned flags)
{
    ULONG want = 0;
    SetFont(rp, tf);
    if ((flags & OL_TEXT_BOLD) && !(tf->tf_Style & FSF_BOLD)) want |= FSF_BOLD;
    if (flags & OL_TEXT_ITALIC) want |= FSF_ITALIC;
    if (flags & OL_TEXT_UNDERLINE) want |= FSF_UNDERLINED;
    SetSoftStyle(rp, want, FSF_BOLD | FSF_ITALIC | FSF_UNDERLINED);
    (void)a;
}

static int px_of(const olui_amiga *a, ol_unit u)
{
    return (int)(((long long)u * a->zoom + OL_CSSPX(100) / 2) / OL_CSSPX(100));
}

static ol_unit unit_of(const olui_amiga *a, int px)
{
    return (ol_unit)((long long)px * OL_CSSPX(100) / a->zoom);
}

static int measure(void *userdata, const char *utf8, size_t length, const ol_style *style, ol_text_metrics *m)
{
    olui_amiga *a = (olui_amiga *)userdata;
    struct TextFont *tf = font_for(a, px_of(a, style->font_size), style->text_flags);
    char buf[256];
    long width = 0;
    if (!tf) return 0;
    use_font(a, &a->mrp, tf, style->text_flags);
    while (length) {
        size_t take = length, n;
        /* in pieces at character boundaries */
        if (take > 200) {
            take = 200;
            while (take < length && ((unsigned char)utf8[take] & 0xc0) == 0x80) ++take;
        }
        n = latin1(utf8, take, buf, sizeof(buf));
        width += TextLength(&a->mrp, (STRPTR)buf, (UWORD)n);
        if ((style->text_flags & OL_TEXT_BOLD) && !(tf->tf_Style & FSF_BOLD))
            width += (long)n * (tf->tf_BoldSmear ? tf->tf_BoldSmear : 1);
        utf8 += take;
        length -= take;
    }
    m->width = unit_of(a, (int)width);
    m->ascent = unit_of(a, tf->tf_Baseline + 1);
    m->descent = unit_of(a, tf->tf_YSize - tf->tf_Baseline - 1);
    return 1;
}

static void draw_text(olui_amiga *a, const olui_draw_op *op)
{
    struct TextFont *tf = font_for(a, op->font_pixels, op->text_flags);
    char buf[256];
    const char *s = op->text;
    size_t left = op->length;
    int x = op->box.x;
    if (!tf) return;
    use_font(a, a->rp, tf, op->text_flags);
    SetAPen(a->rp, (UBYTE)pen_for(a, op->argb));
    SetDrMd(a->rp, JAM1);
    while (left) {
        size_t take = left, n;
        if (take > 200) {
            take = 200;
            while (take < left && ((unsigned char)s[take] & 0xc0) == 0x80) ++take;
        }
        n = latin1(s, take, buf, sizeof(buf));
        Move(a->rp, x, op->baseline);
        Text(a->rp, (STRPTR)buf, (UWORD)n);
        x += TextLength(a->rp, (STRPTR)buf, (UWORD)n);
        s += take;
        left -= take;
    }
}

static void line(olui_amiga *a, int x0, int y0, int x1, int y1)
{
    Move(a->rp, x0, y0);
    Draw(a->rp, x1, y1);
}

static void draw_mark(olui_amiga *a, const olui_draw_op *op)
{
    int x = op->box.x, y = op->box.y, w = op->box.w, h = op->box.h, i;
    SetAPen(a->rp, (UBYTE)pen_for(a, op->argb));
    switch (op->mark) {
    case OLUI_MARK_TICK: {
        int x0 = x + w / 5, y0 = y + h / 2, x1 = x + w * 2 / 5, y1 = y + h - 1 - h / 5, x2 = x + w - 1 - w / 6, y2 = y + h / 5;
        for (i = 0; i < 2; ++i) {
            line(a, x0, y0 + i, x1, y1 + i);
            line(a, x1, y1 + i, x2, y2 + i);
        }
        break;
    }
    case OLUI_MARK_DOT: {
        olui_draw_op dot = *op;
        int in = w / 4;
        dot.kind = OLUI_DRAW_BOX;
        dot.box.x += in;
        dot.box.y += in;
        dot.box.w -= 2 * in;
        dot.box.h -= 2 * in;
        dot.radius = dot.box.w / 2;
        dot.stops = 1;
        dot.stop_argb[0] = op->argb;
        dot.stop_pos[0] = 0;
        dot.border = 0;
        draw_box(a, &dot);
        break;
    }
    case OLUI_MARK_OPEN:            /* pointing down */
    case OLUI_MARK_DROPDOWN:
    case OLUI_MARK_SPIN_DOWN:
        for (i = 0; i < h / 2 + 1; ++i)
            span(a, x + i, y + h / 4 + i, w - 2 * i, 1, op->argb | 0xff000000UL);
        break;
    case OLUI_MARK_SPIN_UP:         /* pointing up */
        for (i = 0; i < h / 2 + 1; ++i)
            span(a, x + i, y + h - 1 - h / 4 - i, w - 2 * i, 1, op->argb | 0xff000000UL);
        break;
    case OLUI_MARK_CLOSED:          /* pointing right */
        for (i = 0; i < h; ++i) {
            int d = i < h / 2 ? i : h - 1 - i;
            span(a, x + w / 4, y + i, d + 1, 1, op->argb | 0xff000000UL);
        }
        break;
    case OLUI_MARK_CARET:
        span(a, x, y, w > 0 ? w : 1, h, op->argb | 0xff000000UL);
        break;
    default:
        break;
    }
}

static int overlaps(const olui_rect *p, const olui_rect *q)
{
    return p->x < q->x + q->w && q->x < p->x + p->w && p->y < q->y + q->h && q->y < p->y + p->h;
}

static void set_clip(olui_amiga *a, const olui_rect *r)
{
    struct Region *old;
    olui_rect c;
    if (r && (r->w <= 0 || r->h <= 0)) r = NULL;
    if (a->limited) {
        /* drawing a part: the clip is cut to it, and no clip is the part */
        c = a->area;
        if (r) {
            int x1 = r->x + r->w < c.x + c.w ? r->x + r->w : c.x + c.w;
            int y1 = r->y + r->h < c.y + c.h ? r->y + r->h : c.y + c.h;
            if (r->x > c.x) c.x = r->x;
            if (r->y > c.y) c.y = r->y;
            c.w = x1 > c.x ? x1 - c.x : 0;
            c.h = y1 > c.y ? y1 - c.y : 0;
        }
        r = &c;
    } else if (!r) {
        a->clipped = 0;
        old = InstallClipRegion(a->layer, NULL);
        (void)old;
        return;
    }
    a->clip = *r;
    a->clipped = 1;
    if (a->region) {
        struct Rectangle rect;
        ClearRegion(a->region);
        rect.MinX = (WORD)r->x;
        rect.MinY = (WORD)r->y;
        rect.MaxX = (WORD)(r->x + r->w - 1);
        rect.MaxY = (WORD)(r->y + r->h - 1);
        /* cut to nothing (a clip outside the part): the region stays empty */
        if (r->w <= 0 || r->h <= 0 || OrRectRegion(a->region, &rect)) InstallClipRegion(a->layer, a->region);
    }
}

static void draw_op(void *userdata, const olui_draw_op *op)
{
    olui_amiga *a = (olui_amiga *)userdata;
    /* 0.4: what doesn't show (opacity 0) isn't drawn; the rest is drawn whole: this glue doesn't blend */
    if (op->kind != OLUI_DRAW_CLIP && op->alpha <= 0) return;
    switch (op->kind) {
    case OLUI_DRAW_BOX: draw_box(a, op); break;
    case OLUI_DRAW_TEXT: draw_text(a, op); break;
    case OLUI_DRAW_MARK: draw_mark(a, op); break;
    case OLUI_DRAW_CLIP: set_clip(a, op->clip.w > 0 ? &op->clip : NULL); break;
    case OLUI_DRAW_IMAGE:
        if (a->image_fn) a->image_fn(a->image_data, a->rp, op->el, op->image, op->box.x, op->box.y, op->box.w, op->box.h);
        break;
    default: break;
    }
    if (op->kind == OLUI_DRAW_BOX && op->el) {
        int i;
        for (i = 0; i < a->nown; ++i)
            if (a->own[i].el == op->el) { a->own[i].box = op->box; a->own[i].seen = 1; }
    }
}

/* ---- The buffer ------------------------------------------------------------------ */

static void free_buffer(olui_amiga *a)
{
    if (a->layer) {
        InstallClipRegion(a->layer, NULL);
        DeleteLayer(0, a->layer);
    }
    if (a->li) DisposeLayerInfo(a->li);
    if (a->bm) {
        WaitBlit();
        FreeBitMap(a->bm);
    }
    a->layer = NULL;
    a->li = NULL;
    a->bm = NULL;
    a->rp = NULL;
    a->bw = a->bh = 0;
}

/* The size of what olui has: the window's inside, or the strip of it */
static int inner_w(const olui_amiga *a)
{
    const struct Window *w = a->win;
    int all = w->Width - w->BorderLeft - w->BorderRight - a->sx;
    return a->strip && a->sw > 0 && a->sw < all ? a->sw : all;
}

static int inner_h(const olui_amiga *a)
{
    const struct Window *w = a->win;
    int all = w->Height - w->BorderTop - w->BorderBottom - a->sy;
    return a->strip && a->sh > 0 && a->sh < all ? a->sh : all;
}

static int make_buffer(olui_amiga *a)
{
    int w = inner_w(a), h = inner_h(a);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (a->bm && a->bw == w && a->bh == h) return 1;
    free_buffer(a);
    a->bm = AllocBitMap((ULONG)w, (ULONG)h, GetBitMapAttr(a->win->RPort->BitMap, BMA_DEPTH), BMF_MINPLANES,
                        a->win->RPort->BitMap);
    if (!a->bm) return 0;
    if (!(a->li = NewLayerInfo())) return 0;
    a->layer = CreateUpfrontLayer(a->li, a->bm, 0, 0, w - 1, h - 1, LAYERSIMPLE, NULL);
    if (!a->layer) return 0;
    a->rp = a->layer->rp;
    a->bw = w;
    a->bh = h;
    a->direct = CyberGfxBase && CGX_MAPATTR(a->bm, CYBRMATTR_ISCYBERGFX) && CGX_MAPATTR(a->bm, CYBRMATTR_DEPTH) > 8;
    return 1;
}

/* The buffer to the window (area of it; NULL: all), around the widgets the
 * program draws itself; then it draws those in area (olui_amiga_own). */
static void show(olui_amiga *a, const olui_rect *area)
{
    struct Region *r;
    struct RegionRectangle *rr;
    struct Rectangle rect;
    olui_rect all;
    int i, x0 = a->win->BorderLeft + a->sx, y0 = a->win->BorderTop + a->sy;
    int seen = 0;
    if (!a->bm) return;
    if (!area) {
        all.x = all.y = 0;
        all.w = a->bw;
        all.h = a->bh;
        area = &all;
    }
    for (i = 0; i < a->nown; ++i) seen += a->own[i].seen && overlaps(&a->own[i].box, area);
    if (!seen || !(r = NewRegion())) {
        BltBitMapRastPort(a->bm, area->x, area->y, a->win->RPort, x0 + area->x, y0 + area->y, area->w, area->h, 0xc0);
    } else {
        rect.MinX = (WORD)area->x; rect.MinY = (WORD)area->y;
        rect.MaxX = (WORD)(area->x + area->w - 1); rect.MaxY = (WORD)(area->y + area->h - 1);
        OrRectRegion(r, &rect);
        for (i = 0; i < a->nown; ++i) {
            const olui_rect *b = &a->own[i].box;
            if (!a->own[i].seen) continue;
            rect.MinX = (WORD)b->x; rect.MinY = (WORD)b->y;
            rect.MaxX = (WORD)(b->x + b->w - 1); rect.MaxY = (WORD)(b->y + b->h - 1);
            ClearRectRegion(r, &rect);
        }
        for (rr = r->RegionRectangle; rr; rr = rr->Next) {
            int x = r->bounds.MinX + rr->bounds.MinX, y = r->bounds.MinY + rr->bounds.MinY;
            BltBitMapRastPort(a->bm, x, y, a->win->RPort, x0 + x, y0 + y, rr->bounds.MaxX - rr->bounds.MinX + 1,
                              rr->bounds.MaxY - rr->bounds.MinY + 1, 0xc0);
        }
        DisposeRegion(r);
    }
    for (i = 0; i < a->nown; ++i)
        if (a->own[i].seen && overlaps(&a->own[i].box, area))
            a->own[i].fn(a->own[i].data, a->own[i].el, x0 + a->own[i].box.x, y0 + a->own[i].box.y, a->own[i].box.w,
                         a->own[i].box.h);
}

/* Draws area of the window (olui_damaged's; NULL: all of it) into the
 * buffer, cut to it, and copies that to the window */
static void draw_area(olui_amiga *a, const olui_rect *area)
{
    int i, w = inner_w(a), h = inner_h(a);
    olui_rect part;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    /* a new buffer, or no region to cut text to: all of it */
    if (!a->bm || a->bw != w || a->bh != h || !a->region) area = NULL;
    if (!make_buffer(a)) return;
    if (area) {
        part = *area;
        if (part.x < 0) { part.w += part.x; part.x = 0; }
        if (part.y < 0) { part.h += part.y; part.y = 0; }
        if (part.x + part.w > a->bw) part.w = a->bw - part.x;
        if (part.y + part.h > a->bh) part.h = a->bh - part.y;
        /* none of it in the window (not laid out at its size), or all of it: all of it */
        area = part.w <= 0 || part.h <= 0 || (part.w == a->bw && part.h == a->bh) ? NULL : &part;
    }
    a->clipped = 0;
    a->limited = area != NULL;
    if (area) a->area = *area;
    /* those that are drawn again say where they are again */
    for (i = 0; i < a->nown; ++i)
        if (!area || overlaps(&a->own[i].box, area)) a->own[i].seen = 0;
    if (area) set_clip(a, NULL);
    olui_draw(a->ui, area, draw_op, a);
    a->limited = 0;
    set_clip(a, NULL);
    show(a, area);
}

/* ---- The public calls ---------------------------------------------------------------- */

ULONG olui_amiga_idcmp(void)
{
    return IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
           IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_INTUITICKS;
}

olui_amiga *olui_amiga_new(olui *ui, struct Window *win)
{
    olui_amiga *a;
    if (!ui || !win) return NULL;
    a = (olui_amiga *)AllocVec(sizeof(*a), MEMF_ANY | MEMF_CLEAR);
    if (!a) return NULL;
    a->ui = ui;
    a->win = win;
    a->scr = win->WScreen;
    a->zoom = 100;
    a->region = NewRegion();
    if (!CyberGfxBase) CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 40);
    InitRastPort(&a->mrp);
    strncpy(a->font_name, (const char *)a->scr->Font->ta_Name, sizeof(a->font_name) - 1);
    strcpy(a->mono_name, "topaz.font");
    olui_set_measure(ui, measure, a);
    return a;
}

void olui_amiga_free(olui_amiga *a)
{
    int i;
    if (!a) return;
    olui_set_measure(a->ui, NULL, NULL);
    free_buffer(a);
    if (a->region) DisposeRegion(a->region);
    for (i = 0; i < a->nfonts; ++i) CloseFont(a->fonts[i].tf);
    for (i = 0; i < a->npens; ++i) ReleasePen(a->scr->ViewPort.ColorMap, a->pens[i].pen);
    FreeVec(a);
    /* cybergraphics.library stays open with the program: the next window may want it */
}

int olui_amiga_strip(olui_amiga *a, int x, int y, int w, int h)
{
    if (!a) return 0;
    a->strip = x > 0 || y > 0 || w > 0 || h > 0;
    a->sx = x > 0 ? x : 0;
    a->sy = y > 0 ? y : 0;
    a->sw = w > 0 ? w : 0;
    a->sh = h > 0 ? h : 0;
    a->inside = a->held = 0;
    /* laid out already: again at the strip's size, so the old buffer isn't drawn or hit */
    if (a->bm) olui_amiga_layout(a);
    return 1;
}

int olui_amiga_keys(olui_amiga *a, int keys)
{
    if (!a) return 0;
    a->keys = keys;
    return 1;
}

int olui_amiga_own(olui_amiga *a, const olui_el *el, olui_amiga_own_fn fn, void *userdata)
{
    int i;
    if (!a || !el) return 0;
    for (i = 0; i < a->nown && a->own[i].el != el; ++i) ;
    if (!fn) {
        if (i < a->nown) { memmove(&a->own[i], &a->own[i + 1], sizeof(a->own[0]) * (size_t)(a->nown - i - 1)); --a->nown; }
        return 1;
    }
    if (i == MAX_OWN) return 0;
    if (i == a->nown) ++a->nown;
    a->own[i].el = el;
    a->own[i].fn = fn;
    a->own[i].data = userdata;
    a->own[i].seen = 0;
    return 1;
}

void olui_amiga_images(olui_amiga *a, olui_amiga_image_fn fn, void *userdata)
{
    if (!a) return;
    a->image_fn = fn;
    a->image_data = userdata;
}

void olui_amiga_fonts(olui_amiga *a, const char *font, const char *mono)
{
    if (!a) return;
    while (a->nfonts > 0) CloseFont(a->fonts[--a->nfonts].tf);
    if (font && font[0]) { strncpy(a->font_name, font, sizeof(a->font_name) - 1); a->font_name[sizeof(a->font_name) - 1] = 0; }
    if (mono && mono[0]) { strncpy(a->mono_name, mono, sizeof(a->mono_name) - 1); a->mono_name[sizeof(a->mono_name) - 1] = 0; }
}

static char *load_text(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *s = NULL;
    LONG n = 0, cap = 0, got;
    if (!fh) return NULL;
    for (;;) {
        if (n + 1024 >= cap) {
            char *p = (char *)realloc(s, (size_t)(cap = cap ? cap * 2 : 4096));
            if (!p) { free(s); s = NULL; break; }
            s = p;
        }
        got = Read(fh, s + n, cap - n - 1);
        if (got <= 0) break;
        n += got;
    }
    Close(fh);
    if (s) s[n] = 0;
    return s;
}

int olui_amiga_theme(olui_amiga *a, const char *path)
{
    char name[80] = "Open", file[200], err[96];
    char *text, *css;
    int dark = 0;
    if (!a) return 0;
    if (!path) {
        /* GFOpenPrefs Look's choice: "Graphite dark" on the first line */
        char *prefs = load_text("ENV:GFOpenGadTools/Look");
        if (prefs) {
            char *nl = strchr(prefs, '\n'), *sp;
            if (nl) *nl = 0;
            sp = strrchr(prefs, ' ');
            if (sp && (!strcmp(sp + 1, "dark") || !strcmp(sp + 1, "light") || !strcmp(sp + 1, "auto"))) {
                dark = !strcmp(sp + 1, "dark");
                *sp = 0;
            }
            if (prefs[0]) {
                strncpy(name, prefs, sizeof(name) - 1);
                name[sizeof(name) - 1] = 0;
            }
            free(prefs);
        }
        strcpy(file, THEME_DIR);
        strncat(file, name, sizeof(file) - strlen(file) - 7);
        if (!strstr(name, ".theme")) strcat(file, ".theme");
        path = file;
    }
    if (!(text = load_text(path))) return 0;
    css = olui_theme_css(text, dark, err, sizeof(err));
    free(text);
    if (!css) return 0;
    olui_add_css(a->ui, css);
    free(css);
    return 1;
}

int olui_amiga_layout(olui_amiga *a)
{
    if (!a || !make_buffer(a)) return 0;
    if (!olui_layout(a->ui, a->bw, a->bh)) return 0;
    draw_area(a, NULL);
    return 1;
}

void olui_amiga_draw(olui_amiga *a)
{
    olui_rect r;
    if (a && olui_damaged(a->ui, &r)) draw_area(a, &r);
}

/* The character of a RAWKEY through the keymap (dead keys too), as
 * VANILLAKEY would have given it, to olui */
static int raw_char(olui_amiga *a, struct IntuiMessage *m, unsigned qual)
{
    struct InputEvent ie;
    UBYTE buf[4];
    unsigned code;
    if (!OluiKeymapBase && !(OluiKeymapBase = OpenLibrary((STRPTR)"keymap.library", 37))) return 0;
    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = m->Code;
    ie.ie_Qualifier = m->Qualifier;
    ie.ie_EventAddress = m->IAddress ? *(APTR *)m->IAddress : NULL;
    if (KM_MAPRAWKEY(&ie, (STRPTR)buf, (LONG)sizeof(buf), NULL) != 1) return 0;
    code = buf[0];
    switch (code) {
    case 8: code = OLUI_KEY_BACKSPACE; break;
    case 9: code = OLUI_KEY_TAB; break;
    case 10: case 13: code = OLUI_KEY_RETURN; break;
    case 27: code = OLUI_KEY_ESCAPE; break;
    case 127: code = OLUI_KEY_DELETE; break;
    default: if (code < 32) return 0; break;
    }
    return olui_key(a->ui, code, qual);
}

int olui_amiga_event(olui_amiga *a, struct IntuiMessage *m)
{
    int x, y, used = 0, alt;
    unsigned shift, command;
    if (!a || !m) return OLUI_AMIGA_NONE;
    x = m->MouseX - a->win->BorderLeft - a->sx;
    y = m->MouseY - a->win->BorderTop - a->sy;
    shift = (m->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? OLUI_QUAL_SHIFT : 0;
    command = (m->Qualifier & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) ? OLUI_QUAL_COMMAND : 0;
    alt = (m->Qualifier & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) != 0;
    if (a->strip) {
        /* olui has a strip of the window: the rest is the program's to draw and refresh */
        int in = x >= 0 && y >= 0 && x < a->bw && y < a->bh;
        switch (m->Class) {
        case IDCMP_NEWSIZE:
            olui_amiga_layout(a);
            return OLUI_AMIGA_NONE;
        case IDCMP_REFRESHWINDOW:
            /* the strip now, the program's BeginRefresh and EndRefresh after */
            show(a, NULL);
            return OLUI_AMIGA_NONE;
        case IDCMP_MOUSEMOVE:
            if (!in && !a->held) {
                /* leaving takes the hover off; the program has the move */
                if (a->inside) olui_mouse(a->ui, -1, -1, OLUI_MOUSE_MOVE);
                a->inside = 0;
                return OLUI_AMIGA_NONE;
            }
            a->inside = in;
            break;
        case IDCMP_MOUSEBUTTONS:
            /* a press outside is the program's; the button's release goes where its press went */
            if (m->Code == SELECTDOWN) {
                if (!in) {
                    /* a press in the program's part takes the keys from olui's widgets */
                    if (a->keys == OLUI_AMIGA_KEYS_FOCUS && olui_focused(a->ui)) {
                        /* and the focus ring or cursor goes now, not at the next draw */
                        olui_focus(a->ui, NULL);
                        olui_amiga_draw(a);
                    }
                    return OLUI_AMIGA_NONE;
                }
                a->held = 1;
            } else if (m->Code == SELECTUP) {
                if (!a->held) return OLUI_AMIGA_NONE;
                a->held = 0;
            } else if (!in) {
                return OLUI_AMIGA_NONE;
            }
            break;
        case IDCMP_RAWKEY:
            if ((m->Code == RAW_WHEEL_UP || m->Code == RAW_WHEEL_DOWN) && !in) return OLUI_AMIGA_NONE;
            break;
        default:
            break;
        }
    }
    if ((m->Class == IDCMP_VANILLAKEY || (m->Class == IDCMP_RAWKEY && m->Code != RAW_WHEEL_UP &&
                                          m->Code != RAW_WHEEL_DOWN)) &&
        a->keys == OLUI_AMIGA_KEYS_FOCUS && !olui_focused(a->ui))
        return OLUI_AMIGA_NONE;
    switch (m->Class) {
    case IDCMP_CLOSEWINDOW:
        return OLUI_AMIGA_CLOSE;
    case IDCMP_NEWSIZE:
        olui_amiga_layout(a);
        return OLUI_AMIGA_USED;
    case IDCMP_REFRESHWINDOW:
        BeginRefresh(a->win);
        show(a, NULL);
        EndRefresh(a->win, TRUE);
        return OLUI_AMIGA_USED;
    case IDCMP_MOUSEMOVE:
        used = olui_mouse_qual(a->ui, x, y, OLUI_MOUSE_MOVE, shift | command);
        break;
    case IDCMP_MOUSEBUTTONS:
        if (m->Code == SELECTDOWN) {
            int twice = DoubleClick(a->down_secs, a->down_micros, m->Seconds, m->Micros) &&
                        x - a->down_x < 4 && a->down_x - x < 4 && y - a->down_y < 4 && a->down_y - y < 4;
            a->down_secs = m->Seconds;
            a->down_micros = m->Micros;
            a->down_x = x;
            a->down_y = y;
            /* 0.4: with the qualifiers: Shift with a click adds to a selection */
            used = olui_mouse_qual(a->ui, x, y, twice ? OLUI_MOUSE_DOUBLE : OLUI_MOUSE_DOWN, shift | command);
        } else if (m->Code == SELECTUP) {
            used = olui_mouse_qual(a->ui, x, y, OLUI_MOUSE_UP, shift | command);
        } else if (m->Code == MENUDOWN) {
            /* 0.4: the right button, in a window with WFLG_RMBTRAP: a context menu */
            used = olui_mouse_qual(a->ui, x, y, OLUI_MOUSE_MENU, shift | command);
        }
        break;
    case IDCMP_VANILLAKEY: {
        unsigned code = m->Code;
        switch (code) {
        case 8: code = OLUI_KEY_BACKSPACE; break;
        case 9: code = OLUI_KEY_TAB; break;
        case 10: case 13: code = OLUI_KEY_RETURN; break;
        case 27: code = OLUI_KEY_ESCAPE; break;
        case 127: code = OLUI_KEY_DELETE; break;
        default: break;                         /* Latin-1 is Unicode's first 256 */
        }
        used = olui_key(a->ui, code, shift | command);
        break;
    }
    case IDCMP_INTUITICKS:
        /* about ten a second while the window is active: tooltips, a held arrow button */
        used = olui_tick(a->ui, 100);
        break;
    case IDCMP_RAWKEY:
        switch (m->Code) {
        /* Shift goes with the arrow (a textarea's selection; elsewhere olui makes it a page or an
         * end, as before); Alt with an arrow is a page or an end everywhere */
        case RAW_UP: used = olui_key(a->ui, alt ? OLUI_KEY_PAGEUP : OLUI_KEY_UP, alt ? 0u : shift); break;
        case RAW_DOWN: used = olui_key(a->ui, alt ? OLUI_KEY_PAGEDOWN : OLUI_KEY_DOWN, alt ? 0u : shift); break;
        case RAW_LEFT: used = olui_key(a->ui, alt ? OLUI_KEY_HOME : OLUI_KEY_LEFT, alt ? 0u : shift); break;
        case RAW_RIGHT: used = olui_key(a->ui, alt ? OLUI_KEY_END : OLUI_KEY_RIGHT, alt ? 0u : shift); break;
        case RAW_WHEEL_UP: used = olui_mouse(a->ui, x, y, OLUI_WHEEL_UP); break;
        case RAW_WHEEL_DOWN: used = olui_mouse(a->ui, x, y, OLUI_WHEEL_DOWN); break;
        default:
            /* a window without VANILLAKEY: the keymap gives the character */
            if (!(a->win->IDCMPFlags & IDCMP_VANILLAKEY) && !(m->Code & IECODE_UP_PREFIX))
                used = raw_char(a, m, shift | command);
            break;
        }
        break;
    default:
        break;
    }
    return used ? OLUI_AMIGA_USED : OLUI_AMIGA_NONE;
}
