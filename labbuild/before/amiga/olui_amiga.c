/* OpenLayout UI in an Intuition window (openlayout_ui_amiga.h).
 *
 * Everything is drawn into a buffer the size of the window's inside, a
 * simple layer on a bitmap like the window's, so clip regions cut text as
 * they cut boxes, and the buffer is copied to the window in one blit: no
 * flicker while a window is drawn again. graphics.library is OpenGfx's;
 * boxes on a true-colour screen go through cybergraphics.library's
 * FillPixelArray in their exact colours, row by row for gradients and
 * rounded corners (as OpenGadTools' look does); on a screen with a palette,
 * pens from ObtainBestPen, a gradient in four bands (THEME_SPEC.md 6).
 * Text is the screen's font at each size, through diskfont.library, its
 * UTF-8 turned into Latin-1 for the bitmap fonts. Integer maths only.
 *
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include "openlayout_ui_amiga.h"

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

/* cybergraphics.library through its vectors, as OpenGadTools' look_draw.c
 * calls it: FillPixelArray (-150) and GetCyberMapAttr (-96) */
static struct Library *CyberGfxBase;
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
    tf = OpenDiskFont(&ta);
    if (!tf) {
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

/* UTF-8 to Latin-1 for the bitmap fonts: what Latin-1 lacks is '?' */
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
        out[k++] = (char)(u < 0x100 ? u : '?');
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
        for (i = 0; i < h / 2 + 1; ++i)
            span(a, x + i, y + h / 4 + i, w - 2 * i, 1, op->argb | 0xff000000UL);
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

static void set_clip(olui_amiga *a, const olui_rect *r)
{
    struct Region *old;
    if (!r || r->w <= 0 || r->h <= 0) {
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
        if (OrRectRegion(a->region, &rect)) InstallClipRegion(a->layer, a->region);
    }
}

static void draw_op(void *userdata, const olui_draw_op *op)
{
    olui_amiga *a = (olui_amiga *)userdata;
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

static int inner_w(const struct Window *w) { return w->Width - w->BorderLeft - w->BorderRight; }
static int inner_h(const struct Window *w) { return w->Height - w->BorderTop - w->BorderBottom; }

static int make_buffer(olui_amiga *a)
{
    int w = inner_w(a->win), h = inner_h(a->win);
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

/* The buffer to the window, around the widgets the program draws itself;
 * then it draws them (olui_amiga_own). */
static void show(olui_amiga *a)
{
    struct Region *r;
    struct RegionRectangle *rr;
    struct Rectangle rect;
    int i, x0 = a->win->BorderLeft, y0 = a->win->BorderTop;
    int seen = 0;
    if (!a->bm) return;
    for (i = 0; i < a->nown; ++i) seen += a->own[i].seen;
    if (!seen || !(r = NewRegion())) {
        BltBitMapRastPort(a->bm, 0, 0, a->win->RPort, x0, y0, a->bw, a->bh, 0xc0);
    } else {
        rect.MinX = 0; rect.MinY = 0; rect.MaxX = (WORD)(a->bw - 1); rect.MaxY = (WORD)(a->bh - 1);
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
        if (a->own[i].seen)
            a->own[i].fn(a->own[i].data, a->own[i].el, x0 + a->own[i].box.x, y0 + a->own[i].box.y, a->own[i].box.w,
                         a->own[i].box.h);
}

static void draw_all(olui_amiga *a)
{
    int i;
    if (!make_buffer(a)) return;
    a->clipped = 0;
    for (i = 0; i < a->nown; ++i) a->own[i].seen = 0;
    olui_draw(a->ui, NULL, draw_op, a);
    set_clip(a, NULL);
    show(a);
}

/* ---- The public calls ---------------------------------------------------------------- */

ULONG olui_amiga_idcmp(void)
{
    return IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
           IDCMP_VANILLAKEY | IDCMP_RAWKEY;
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
        /* OpenPrefs Look's choice: "Graphite dark" on the first line */
        char *prefs = load_text("ENV:OpenGadTools/Look");
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
    draw_all(a);
    return 1;
}

void olui_amiga_draw(olui_amiga *a)
{
    if (a && olui_damaged(a->ui, NULL)) draw_all(a);
}

int olui_amiga_event(olui_amiga *a, struct IntuiMessage *m)
{
    int x, y, used = 0;
    unsigned shift;
    if (!a || !m) return OLUI_AMIGA_NONE;
    x = m->MouseX - a->win->BorderLeft;
    y = m->MouseY - a->win->BorderTop;
    shift = (m->Qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) ? OLUI_QUAL_SHIFT : 0;
    switch (m->Class) {
    case IDCMP_CLOSEWINDOW:
        return OLUI_AMIGA_CLOSE;
    case IDCMP_NEWSIZE:
        olui_amiga_layout(a);
        return OLUI_AMIGA_USED;
    case IDCMP_REFRESHWINDOW:
        BeginRefresh(a->win);
        show(a);
        EndRefresh(a->win, TRUE);
        return OLUI_AMIGA_USED;
    case IDCMP_MOUSEMOVE:
        used = olui_mouse(a->ui, x, y, OLUI_MOUSE_MOVE);
        break;
    case IDCMP_MOUSEBUTTONS:
        if (m->Code == SELECTDOWN) {
            int twice = DoubleClick(a->down_secs, a->down_micros, m->Seconds, m->Micros) &&
                        x - a->down_x < 4 && a->down_x - x < 4 && y - a->down_y < 4 && a->down_y - y < 4;
            a->down_secs = m->Seconds;
            a->down_micros = m->Micros;
            a->down_x = x;
            a->down_y = y;
            used = olui_mouse(a->ui, x, y, twice ? OLUI_MOUSE_DOUBLE : OLUI_MOUSE_DOWN);
        } else if (m->Code == SELECTUP) {
            used = olui_mouse(a->ui, x, y, OLUI_MOUSE_UP);
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
        used = olui_key(a->ui, code, shift);
        break;
    }
    case IDCMP_RAWKEY:
        switch (m->Code) {
        case RAW_UP: used = olui_key(a->ui, shift ? OLUI_KEY_PAGEUP : OLUI_KEY_UP, 0); break;
        case RAW_DOWN: used = olui_key(a->ui, shift ? OLUI_KEY_PAGEDOWN : OLUI_KEY_DOWN, 0); break;
        case RAW_LEFT: used = olui_key(a->ui, shift ? OLUI_KEY_HOME : OLUI_KEY_LEFT, 0); break;
        case RAW_RIGHT: used = olui_key(a->ui, shift ? OLUI_KEY_END : OLUI_KEY_RIGHT, 0); break;
        case RAW_WHEEL_UP: used = olui_mouse(a->ui, x, y, OLUI_WHEEL_UP); break;
        case RAW_WHEEL_DOWN: used = olui_mouse(a->ui, x, y, OLUI_WHEEL_DOWN); break;
        default: break;
        }
        break;
    default:
        break;
    }
    return used ? OLUI_AMIGA_USED : OLUI_AMIGA_NONE;
}
