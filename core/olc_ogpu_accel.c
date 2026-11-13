/* GFOpenLayout core's GFOpenGPU compositor accelerator, row N1. It records
 * high-level compose operations, then writes one GFOpenGPU stream and makes
 * one Submit call. CPU surface pixels remain the master; there is no readback.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "olc_ogpu_accel.h"
#include <stdlib.h>
#include <string.h>

enum {
    OG_OP_SURFACE = 0x0001, OG_OP_TARGET = 0x0002, OG_OP_CLIP = 0x0003,
    OG_OP_FILL = 0x0010, OG_OP_COPY = 0x0012, OG_OP_COMPOSITE = 0x0020
};
enum { OG_FMT_CLUT8 = 1, OG_FMT_RGB565 = 2, OG_FMT_ARGB32 = 3, OG_FMT_XRGB32 = 10 };
enum { OG_COMP_SRCALPHA = 1 };
enum { B_FILL, B_COPY, B_COMPOSITE };

typedef struct og_screen {
    void *bitmap;
    int32_t w, h, bpr;
    uint32_t address, fmt;
} og_screen;

typedef struct og_surface {
    const void *pixels;
    int32_t bpr, w, h;
    uint32_t fmt, address, slot, last_fence;
} og_surface;

typedef struct og_op {
    int kind;
    uint32_t slot, colour, opacity;
    olc_box box;
    int32_t dx, dy;
} og_op;

typedef struct og_batch {
    struct olc_ogpu *g;
    og_screen screen;
    olc_box *clips;
    int32_t nclip, capclip;
    og_op *ops;
    int32_t nop, capop;
} og_batch;

struct olc_ogpu {
    olc_accel accel;
    olc_ogpu_io io;
    og_surface *surfaces;
    int32_t nsurfaces, capsurfaces;
    uint32_t next_slot;
    int detached;
};

typedef struct og_out {
    unsigned char *p;
    uint32_t n, cap;
    int failed;
} og_out;

static void *more(void *p, int32_t *cap, int32_t need, size_t each)
{
    int32_t n = *cap ? *cap : 8;
    void *q;
    if (need <= *cap) return p;
    while (n < need) {
        if (n > 0x3fffffff) return NULL;
        n *= 2;
    }
    q = realloc(p, (size_t)n * each);
    if (!q) return NULL;
    *cap = n;
    return q;
}

static uint32_t pack(int32_t x, int32_t y)
{
    return ((uint32_t)x & 0xffffu) << 16 | ((uint32_t)y & 0xffffu);
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

static int reserve(og_out *o, uint32_t add)
{
    unsigned char *p;
    uint32_t cap = o->cap ? o->cap : 128;
    if (o->failed || add > 0xffffffffu - o->n) return -1;
    if (o->n + add <= o->cap) return 0;
    while (cap < o->n + add) {
        if (cap > 0x7fffffffu) { o->failed = 1; return -1; }
        cap *= 2;
    }
    p = (unsigned char *)realloc(o->p, (size_t)cap * 4);
    if (!p) { o->failed = 1; return -1; }
    o->p = p; o->cap = cap;
    return 0;
}

static void command(og_out *o, uint32_t op, const uint32_t *a, uint32_t n)
{
    uint32_t i;
    if (reserve(o, n + 1)) return;
    put32(o->p + o->n++ * 4, op << 16 | (n + 1));
    for (i = 0; i < n; i++) put32(o->p + o->n++ * 4, a[i]);
}

static int og_format(uint32_t fmt)
{
    switch (fmt) {
    case OLC_FMT_CLUT8: return OG_FMT_CLUT8;
    case OLC_FMT_RGB16: return OG_FMT_RGB565;
    case OLC_FMT_XRGB32: return OG_FMT_XRGB32;
    case OLC_FMT_ARGB32: return OG_FMT_ARGB32;
    default: return 0;
    }
}

static uint32_t fill_colour(uint32_t fmt, olc_colour c, int *ok)
{
    uint32_t r, g, b;
    *ok = 1;
    if (fmt == OLC_FMT_CLUT8) {
        if (!OLC_IS_PEN(c)) { *ok = 0; return 0; }
        return c & 255u;
    }
    if (OLC_IS_PEN(c)) { *ok = 0; return 0; }
    if (fmt == OLC_FMT_RGB16) {
        r = c >> 16 & 255u; g = c >> 8 & 255u; b = c & 255u;
        return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3);
    }
    if (fmt == OLC_FMT_XRGB32) return c & 0x00ffffffu;
    return c;
}

static og_surface *surface_by_slot(struct olc_ogpu *g, uint32_t slot)
{
    int32_t i;
    for (i = 0; i < g->nsurfaces; i++) if (g->surfaces[i].slot == slot) return &g->surfaces[i];
    return NULL;
}

static void emit_surface(og_out *o, uint32_t physical, uint32_t address, int32_t bpr,
                         int32_t w, int32_t h, uint32_t fmt)
{
    uint32_t a[5];
    a[0] = physical; a[1] = address; a[2] = (uint32_t)bpr;
    a[3] = pack(w, h); a[4] = (uint32_t)og_format(fmt);
    command(o, OG_OP_SURFACE, a, 5);
}

static void *a_begin(void *ctx, void *bitmap, void *pixels, int32_t bpr, uint32_t fmt, int32_t w, int32_t h)
{
    struct olc_ogpu *g = (struct olc_ogpu *)ctx;
    og_batch *b;
    uint32_t address;
    if (!g || g->detached || !bitmap || !pixels || bpr <= 0 || w <= 0 || h <= 0 || !og_format(fmt)) return NULL;
    address = g->io.address(g->io.ctx, pixels);
    if (!address) return NULL;
    b = (og_batch *)calloc(1, sizeof *b);
    if (!b) return NULL;
    b->g = g;
    b->screen.bitmap = bitmap; b->screen.address = address; b->screen.bpr = bpr;
    b->screen.fmt = fmt; b->screen.w = w; b->screen.h = h;
    return b;
}

static int a_bind(void *ctx, void *batch, const void *pixels, int32_t bpr, int32_t w, int32_t h,
                  uint32_t fmt, uint32_t *slot)
{
    struct olc_ogpu *g = (struct olc_ogpu *)ctx;
    og_batch *b = (og_batch *)batch;
    og_surface *s = NULL;
    int32_t i;
    uint32_t address;
    if (!g || !b || b->g != g || !pixels || !slot || bpr <= 0 || w <= 0 || h <= 0 || !og_format(fmt)) return -1;
    for (i = 0; i < g->nsurfaces; i++) if (g->surfaces[i].pixels == pixels) s = &g->surfaces[i];
    if (s) {
        if (s->bpr != bpr || s->w != w || s->h != h || s->fmt != fmt) return -1;
        *slot = s->slot;
        return 0;
    }
    address = g->io.address(g->io.ctx, pixels);
    if (!address) return -1;
    {
        og_surface *grown = (og_surface *)more(g->surfaces, &g->capsurfaces, g->nsurfaces + 1, sizeof *g->surfaces);
        if (!grown) return -1;
        g->surfaces = grown;
    }
    s = &g->surfaces[g->nsurfaces++];
    memset(s, 0, sizeof *s);
    s->pixels = pixels; s->bpr = bpr; s->w = w; s->h = h; s->fmt = fmt; s->address = address;
    s->slot = ++g->next_slot;
    if (!s->slot) s->slot = ++g->next_slot;
    *slot = s->slot;
    return 0;
}

static int a_upload(void *ctx, void *batch, uint32_t slot, const olc_box *boxes, int32_t n)
{
    struct olc_ogpu *g = (struct olc_ogpu *)ctx;
    og_batch *b = (og_batch *)batch;
    /* GFOpenGPU SURFACE names the Amiga-visible CPU-master storage itself;
     * unlike a texture-cache back end there is no second copy to update.
     * GFOpenRTG's bitmap hazard tracking settles a prior reader before CPU
     * drawing changes these pixels. The damage still reaches clip(), below. */
    (void)boxes;
    return g && b && b->g == g && surface_by_slot(g, slot) && n >= 0 && (!n || boxes) ? 0 : -1;
}

static int a_clip(void *ctx, void *batch, const olc_box *boxes, int32_t n)
{
    struct olc_ogpu *g = (struct olc_ogpu *)ctx;
    og_batch *b = (og_batch *)batch;
    olc_box *p;
    if (!g || !b || b->g != g || n < 0 || (n && !boxes)) return -1;
    p = (olc_box *)more(b->clips, &b->capclip, b->nclip + n, sizeof *b->clips);
    if (!p && n) return -1;
    b->clips = p;
    if (n) memcpy(b->clips + b->nclip, boxes, (size_t)n * sizeof *boxes);
    b->nclip += n;
    return 0;
}

static og_op *new_op(struct olc_ogpu *g, og_batch *b, int kind, const olc_box *box)
{
    og_op *p;
    if (!g || !b || b->g != g || !box || box->x1 < box->x0 || box->y1 < box->y0) return NULL;
    p = (og_op *)more(b->ops, &b->capop, b->nop + 1, sizeof *b->ops);
    if (!p) return NULL;
    b->ops = p;
    p = &b->ops[b->nop++];
    memset(p, 0, sizeof *p);
    p->kind = kind; p->box = *box;
    return p;
}

static int a_fill(void *ctx, void *batch, const olc_box *box, olc_colour c)
{
    og_op *p = new_op((struct olc_ogpu *)ctx, (og_batch *)batch, B_FILL, box);
    if (!p) return -1;
    p->colour = c;
    return 0;
}

static int a_copy(void *ctx, void *batch, uint32_t slot, const olc_box *src, int32_t dx, int32_t dy)
{
    struct olc_ogpu *g = (struct olc_ogpu *)ctx;
    og_op *p;
    if (!g || !surface_by_slot(g, slot)) return -1;
    p = new_op(g, (og_batch *)batch, B_COPY, src);
    if (!p) return -1;
    p->slot = slot; p->dx = dx; p->dy = dy;
    return 0;
}

static int a_composite(void *ctx, void *batch, uint32_t slot, const olc_box *src, int32_t dx, int32_t dy,
                       uint32_t opacity)
{
    struct olc_ogpu *g = (struct olc_ogpu *)ctx;
    og_op *p;
    if (!g || !surface_by_slot(g, slot) || opacity > 255) return -1;
    p = new_op(g, (og_batch *)batch, B_COMPOSITE, src);
    if (!p) return -1;
    p->slot = slot; p->dx = dx; p->dy = dy; p->opacity = opacity;
    return 0;
}

static void batch_free(og_batch *b)
{
    if (!b) return;
    free(b->clips); free(b->ops); free(b);
}

static int a_submit(void *ctx, void *batch, uint32_t *fence)
{
    struct olc_ogpu *g = (struct olc_ogpu *)ctx;
    og_batch *b = (og_batch *)batch;
    og_out out;
    uint32_t a[7], f = 0;
    int32_t i, j;
    int r = -1;
    memset(&out, 0, sizeof out);
    if (!g || !b || b->g != g || !fence) { batch_free(b); return -1; }
    emit_surface(&out, 0, b->screen.address, b->screen.bpr, b->screen.w, b->screen.h, b->screen.fmt);
    a[0] = 0; command(&out, OG_OP_TARGET, a, 1);
    for (i = 0; i < b->nclip; i++) {
        int32_t cw = b->clips[i].x1 - b->clips[i].x0 + 1;
        int32_t ch = b->clips[i].y1 - b->clips[i].y0 + 1;
        if (cw <= 0 || ch <= 0) continue;
        a[0] = pack(b->clips[i].x0, b->clips[i].y0); a[1] = pack(cw, ch);
        command(&out, OG_OP_CLIP, a, 2);
        for (j = 0; j < b->nop; j++) {
            og_op *op = &b->ops[j];
            int32_t w = op->box.x1 - op->box.x0 + 1, h = op->box.y1 - op->box.y0 + 1;
            if (op->kind == B_FILL) {
                int ok;
                a[0] = pack(op->box.x0, op->box.y0); a[1] = pack(w, h);
                a[2] = fill_colour(b->screen.fmt, op->colour, &ok);
                if (!ok) { out.failed = 1; break; }
                command(&out, OG_OP_FILL, a, 3);
            } else {
                og_surface *s = surface_by_slot(g, op->slot);
                if (!s) { out.failed = 1; break; }
                emit_surface(&out, 1, s->address, s->bpr, s->w, s->h, s->fmt);
                a[0] = 1; a[1] = pack(op->box.x0, op->box.y0);
                if (op->kind == B_COPY) {
                    a[2] = pack(op->dx, op->dy); a[3] = pack(w, h);
                    command(&out, OG_OP_COPY, a, 4);
                } else {
                    a[2] = pack(w, h); a[3] = pack(op->dx, op->dy); a[4] = pack(w, h);
                    a[5] = op->opacity; a[6] = s->fmt == OLC_FMT_ARGB32 ? OG_COMP_SRCALPHA : 0;
                    command(&out, OG_OP_COMPOSITE, a, 7);
                }
            }
        }
        if (out.failed) break;
    }
    if (!out.failed && g->io.submit(g->io.ctx, out.p, out.n, &f) == 0 && f) {
        for (i = 0; i < b->nop; i++) if (b->ops[i].slot) {
            og_surface *s = surface_by_slot(g, b->ops[i].slot);
            if (s) s->last_fence = f;
        }
        *fence = f;
        r = 0;
    }
    free(out.p);
    batch_free(b);
    return r;
}

static void a_abort(void *ctx, void *batch)
{
    og_batch *b = (og_batch *)batch;
    if (b && b->g == (struct olc_ogpu *)ctx) batch_free(b);
}

static void a_retire(void *ctx, uint32_t slot)
{
    struct olc_ogpu *g = (struct olc_ogpu *)ctx;
    int32_t i;
    if (!g) return;
    for (i = 0; i < g->nsurfaces; i++) if (g->surfaces[i].slot == slot) {
        if (g->surfaces[i].last_fence) (void)g->io.wait(g->io.ctx, g->surfaces[i].last_fence);
        g->surfaces[i] = g->surfaces[--g->nsurfaces];
        return;
    }
}

static void a_detach(void *ctx)
{
    struct olc_ogpu *g = (struct olc_ogpu *)ctx;
    uint32_t newest = 0;
    int32_t i;
    if (!g || g->detached) return;
    for (i = 0; i < g->nsurfaces; i++)
        if ((int32_t)(g->surfaces[i].last_fence - newest) > 0) newest = g->surfaces[i].last_fence;
    if (newest) (void)g->io.wait(g->io.ctx, newest);
    g->nsurfaces = 0;
    g->detached = 1;
    if (g->io.close) g->io.close(g->io.ctx);
}

olc_ogpu *olc_ogpu_new(const olc_ogpu_io *io)
{
    struct olc_ogpu *g;
    if (!io || !io->address || !io->submit || !io->wait) return NULL;
    g = (struct olc_ogpu *)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->io = *io;
    g->accel.version = OLC_ACCEL_VERSION;
    g->accel.ctx = g;
    g->accel.begin = a_begin; g->accel.bind = a_bind; g->accel.upload = a_upload;
    g->accel.clip = a_clip; g->accel.fill = a_fill; g->accel.copy = a_copy;
    g->accel.composite = a_composite; g->accel.submit = a_submit; g->accel.abort = a_abort;
    g->accel.retire = a_retire; g->accel.detach = a_detach;
    return g;
}

void olc_ogpu_free(olc_ogpu *g)
{
    if (!g) return;
    a_detach(g);
    free(g->surfaces); free(g);
}

const olc_accel *olc_ogpu_ops(olc_ogpu *g)
{
    return g ? &g->accel : NULL;
}
