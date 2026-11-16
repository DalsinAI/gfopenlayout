/* GFOpenLayout's ROM core, step 1 (Workbench build plan row 10): olc_version, olc_init, regions (section 3 of
 * include/gfopenlayout_core.h) and surfaces in CPU memory (section 2).
 *
 * Rules kept here:
 *   - olc_init probes nothing and allocates nothing.
 *   - A surface is never in Chip RAM. On the Amiga its pixels are MEMF_FAST; with no Fast RAM there is no surface.
 *   - A region call that runs out of memory leaves the region as it was: each builds its answer in new memory
 *     and swaps it in only when it is whole.
 *   - Surface damage can't be lost to a full memory: a surface keeps room for one box, and when a box can't be
 *     added its damage becomes the whole surface.
 *   - No floating point, no locks: a region or surface belongs to its caller, who holds whatever lock covers it.
 *
 * Not yet (later steps, one PR each): OLC_MEM_RTG and the like argument (they need GFOpenRTG, attached after
 * boot), olc_surface_rastport (it answers NULL until the openintuition hooks step), compose, the hooks and text.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "olc_core_internal.h"

#include <string.h>

#ifdef OLC_AMIGA
#include <exec/memory.h>
#include <proto/exec.h>
#else
#include <stdlib.h>
#endif

/* ---- memory ---- */

#ifdef OLC_TEST_HOOKS
/* Host tests only: the allocation number that fails (1: the next one), 0 for none */
long olc_test_fail_at;
#endif

void *olc__alloc(size_t bytes, int fast)
{
    if (!bytes) return NULL;
#ifdef OLC_TEST_HOOKS
    if (olc_test_fail_at > 0 && --olc_test_fail_at == 0) return NULL;
#endif
#ifdef OLC_AMIGA
    return AllocVec((ULONG)bytes, (fast ? MEMF_FAST : MEMF_ANY) | MEMF_CLEAR);
#else
    (void)fast;
    return calloc(1, bytes);
#endif
}

void olc__free(void *p)
{
    if (!p) return;
#ifdef OLC_AMIGA
    FreeVec(p);
#else
    free(p);
#endif
}

/* ---- start ---- */

static int olc_inited;

uint32_t olc_version(void)
{
    return ((uint32_t)OLC_VERSION_MAJOR << 16) | (uint32_t)OLC_VERSION_MINOR;
}

int olc_init(void)
{
    olc_inited = 1;
    return 1;
}

int olc__ready(void)
{
    return olc_inited;
}

/* ---- boxes ---- */

static int box_empty(const olc_box *b)
{
    return b->x1 < b->x0 || b->y1 < b->y0;
}

static int box_cut(const olc_box *a, const olc_box *b, olc_box *out)
{
    out->x0 = a->x0 > b->x0 ? a->x0 : b->x0;
    out->y0 = a->y0 > b->y0 ? a->y0 : b->y0;
    out->x1 = a->x1 < b->x1 ? a->x1 : b->x1;
    out->y1 = a->y1 < b->y1 ? a->y1 : b->y1;
    return !box_empty(out);
}

/* a less b: at most four boxes, none overlapping, into out; their count */
static int box_less(const olc_box *a, const olc_box *b, olc_box *out)
{
    olc_box in;
    int n = 0;
    if (!box_cut(a, b, &in)) {
        out[0] = *a;
        return 1;
    }
    if (a->y0 < in.y0) {                                    /* above */
        out[n].x0 = a->x0; out[n].x1 = a->x1; out[n].y0 = a->y0; out[n].y1 = in.y0 - 1; n++;
    }
    if (in.y1 < a->y1) {                                    /* below */
        out[n].x0 = a->x0; out[n].x1 = a->x1; out[n].y0 = in.y1 + 1; out[n].y1 = a->y1; n++;
    }
    if (a->x0 < in.x0) {                                    /* left, the cut's rows only */
        out[n].x0 = a->x0; out[n].x1 = in.x0 - 1; out[n].y0 = in.y0; out[n].y1 = in.y1; n++;
    }
    if (in.x1 < a->x1) {                                    /* right */
        out[n].x0 = in.x1 + 1; out[n].x1 = a->x1; out[n].y0 = in.y0; out[n].y1 = in.y1; n++;
    }
    return n;
}

/* ---- regions ---- */

/* Room for at least want boxes in a new array holding r's; 0, or -1 with r as it was */
static int reg_room(olc_region *r, int32_t want)
{
    olc_box *nb;
    int32_t cap;
    if (want <= r->cap) return 0;
    cap = r->cap ? r->cap : 4;
    while (cap < want) {
        if (cap > (int32_t)0x3FFFFFFF / (int32_t)sizeof(olc_box)) return -1;
        cap *= 2;
    }
    nb = (olc_box *)olc__alloc((size_t)cap * sizeof *nb, 0);
    if (!nb) return -1;
    if (r->n) memcpy(nb, r->b, (size_t)r->n * sizeof *nb);
    olc__free(r->b);
    r->b = nb;
    r->cap = cap;
    return 0;
}

/* Joins boxes that share a whole edge, so a region drawn into piece by piece doesn't grow without end. Only ever
 * makes fewer boxes; never allocates. */
static void reg_join(olc_region *r)
{
    int joined = 1;
    while (joined) {
        int32_t i, j;
        joined = 0;
        for (i = 0; i < r->n; i++) {
            for (j = i + 1; j < r->n; j++) {
                olc_box *a = &r->b[i], *b = &r->b[j];
                int both = 0;
                if (a->x0 == b->x0 && a->x1 == b->x1 && (a->y1 + 1 == b->y0 || b->y1 + 1 == a->y0)) {
                    if (b->y0 < a->y0) a->y0 = b->y0;
                    if (b->y1 > a->y1) a->y1 = b->y1;
                    both = 1;
                } else if (a->y0 == b->y0 && a->y1 == b->y1 && (a->x1 + 1 == b->x0 || b->x1 + 1 == a->x0)) {
                    if (b->x0 < a->x0) a->x0 = b->x0;
                    if (b->x1 > a->x1) a->x1 = b->x1;
                    both = 1;
                }
                if (both) {
                    r->b[j] = r->b[--r->n];
                    joined = 1;
                    j = i;              /* a grew: look at every other box again */
                }
            }
        }
    }
}

static void reg_swap(olc_region *a, olc_region *b)
{
    olc_region t = *a;
    *a = *b;
    *b = t;
}

static void reg_drop(olc_region *r)
{
    olc__free(r->b);
    r->b = NULL;
    r->n = r->cap = 0;
}

olc_region *olc_region_new(void)
{
    return (olc_region *)olc__alloc(sizeof(olc_region), 0);
}

void olc_region_free(olc_region *r)
{
    if (!r) return;
    reg_drop(r);
    olc__free(r);
}

void olc_region_clear(olc_region *r)
{
    if (r) r->n = 0;
}

/* b's parts that no box of r covers, into out (fresh); 0, or -1 */
static int reg_outside(const olc_region *r, const olc_box *b, olc_region *out)
{
    olc_region next = { NULL, 0, 0 };
    int32_t i, k;
    out->b = NULL;
    out->n = out->cap = 0;
    if (reg_room(out, 4) < 0) return -1;
    out->b[out->n++] = *b;
    for (i = 0; i < r->n && out->n; i++) {
        next.n = 0;
        if (reg_room(&next, out->n * 4) < 0) {
            reg_drop(&next);
            reg_drop(out);
            return -1;
        }
        for (k = 0; k < out->n; k++) next.n += box_less(&out->b[k], &r->b[i], next.b + next.n);
        reg_swap(out, &next);
    }
    reg_drop(&next);
    return 0;
}

int olc_region_or_box(olc_region *r, const olc_box *b)
{
    olc_region add, all;
    if (!r || !b) return -1;
    if (box_empty(b)) return 0;
    if (reg_outside(r, b, &add) < 0) return -1;
    if (!add.n) {
        reg_drop(&add);
        return 0;
    }
    all.b = NULL;
    all.n = all.cap = 0;
    if (reg_room(&all, r->n + add.n) < 0) {
        reg_drop(&add);
        return -1;
    }
    if (r->n) memcpy(all.b, r->b, (size_t)r->n * sizeof *all.b);
    memcpy(all.b + r->n, add.b, (size_t)add.n * sizeof *all.b);
    all.n = r->n + add.n;
    reg_drop(&add);
    reg_join(&all);
    reg_swap(r, &all);
    reg_drop(&all);
    return 0;
}

int olc_region_and_box(olc_region *r, const olc_box *b)
{
    int32_t i, n = 0;
    if (!r || !b) return -1;
    /* Cutting never makes more boxes, so it is done in place and can't fail */
    for (i = 0; i < r->n; i++) {
        olc_box c;
        if (box_cut(&r->b[i], b, &c)) r->b[n++] = c;
    }
    r->n = n;
    return 0;
}

int olc_region_sub_box(olc_region *r, const olc_box *b)
{
    olc_region left = { NULL, 0, 0 };
    int32_t i;
    if (!r || !b) return -1;
    if (box_empty(b) || !r->n) return 0;
    if (reg_room(&left, r->n * 4) < 0) return -1;
    for (i = 0; i < r->n; i++) left.n += box_less(&r->b[i], b, left.b + left.n);
    reg_join(&left);
    reg_swap(r, &left);
    reg_drop(&left);
    return 0;
}

int olc_region_copy(olc_region *to, const olc_region *from)
{
    olc_region c = { NULL, 0, 0 };
    if (!to || !from) return -1;
    if (to == from) return 0;
    if (!from->n) {
        to->n = 0;
        return 0;
    }
    if (reg_room(&c, from->n) < 0) return -1;
    memcpy(c.b, from->b, (size_t)from->n * sizeof *c.b);
    c.n = from->n;
    reg_swap(to, &c);
    reg_drop(&c);
    return 0;
}

int olc_region_or(olc_region *r, const olc_region *other)
{
    olc_region work = { NULL, 0, 0 };
    int32_t i;
    if (!r || !other) return -1;
    if (r == other || !other->n) return 0;
    if (olc_region_copy(&work, r) < 0) return -1;
    for (i = 0; i < other->n; i++) {
        if (olc_region_or_box(&work, &other->b[i]) < 0) {
            reg_drop(&work);
            return -1;
        }
    }
    reg_swap(r, &work);
    reg_drop(&work);
    return 0;
}

int32_t olc_region_count(const olc_region *r)
{
    return r ? r->n : 0;
}

int olc_region_box(const olc_region *r, int32_t i, olc_box *b)
{
    if (!r || !b || i < 0 || i >= r->n) return 0;
    *b = r->b[i];
    return 1;
}

int olc_region_bounds(const olc_region *r, olc_box *b)
{
    int32_t i;
    if (!r || !b || !r->n) return 0;
    *b = r->b[0];
    for (i = 1; i < r->n; i++) {
        const olc_box *e = &r->b[i];
        if (e->x0 < b->x0) b->x0 = e->x0;
        if (e->y0 < b->y0) b->y0 = e->y0;
        if (e->x1 > b->x1) b->x1 = e->x1;
        if (e->y1 > b->y1) b->y1 = e->y1;
    }
    return 1;
}

/* ---- surfaces ---- */

static int32_t fmt_bytes(uint32_t fmt)
{
    switch (fmt) {
    case OLC_FMT_CLUT8: return 1;
    case OLC_FMT_RGB16: return 2;
    case OLC_FMT_XRGB32:
    case OLC_FMT_ARGB32: return 4;
    default: return 0;
    }
}

/* Rows start on 16 bytes, as GFOpenRTG's bitmaps and the GPU's uploads like them */
#define OLC_ROW_ALIGN 16
#define OLC_MAX_SIDE 16384

olc_surface *olc_surface_new(int32_t w, int32_t h, uint32_t fmt, uint32_t where, void *like)
{
    olc_surface *s;
    int32_t bpp = fmt_bytes(fmt), bpr;
    uint32_t got;
    (void)like;                 /* only the RTG step looks at the screen's bitmap */
    if (!bpp || w <= 0 || h <= 0 || w > OLC_MAX_SIDE || h > OLC_MAX_SIDE) return NULL;
    switch (where) {
    case OLC_MEM_ANY:
    case OLC_MEM_FAST: got = OLC_MEM_FAST; break;
#ifndef OLC_AMIGA
    case OLC_MEM_HOST: got = OLC_MEM_HOST; break;
#endif
    default: return NULL;       /* RTG memory (and the host's, on the Amiga) come with later steps */
    }
    bpr = (w * bpp + OLC_ROW_ALIGN - 1) & ~(int32_t)(OLC_ROW_ALIGN - 1);
    s = (olc_surface *)olc__alloc(sizeof *s, 0);
    if (!s) return NULL;
    /* room for one damage box, so damage is never lost (olc_surface_damage) */
    if (reg_room(&s->damage, 1) < 0 ||
        !(s->pixels = olc__alloc((size_t)bpr * (size_t)h, 1))) {
        reg_drop(&s->damage);
        olc__free(s);
        return NULL;
    }
    s->w = w;
    s->h = h;
    s->bpr = bpr;
    s->fmt = fmt;
    s->where = got;
    return s;
}

void olc_surface_free(olc_surface *s)
{
    if (!s) return;
    olc__free(s->pixels);
    reg_drop(&s->damage);
    olc__free(s);
}

void *olc_surface_pixels(olc_surface *s, int32_t *bytes_per_row)
{
    if (!s) return NULL;
    if (bytes_per_row) *bytes_per_row = s->bpr;
    return s->pixels;
}

void olc_surface_size(const olc_surface *s, int32_t *w, int32_t *h, uint32_t *fmt)
{
    if (w) *w = s ? s->w : 0;
    if (h) *h = s ? s->h : 0;
    if (fmt) *fmt = s ? s->fmt : 0;
}

void olc_surface_damage(olc_surface *s, const olc_box *b)
{
    olc_box all, c;
    if (!s || !b) return;
    all.x0 = 0;
    all.y0 = 0;
    all.x1 = s->w - 1;
    all.y1 = s->h - 1;
    if (!box_cut(b, &all, &c)) return;
    if (olc_region_or_box(&s->damage, &c) < 0) {
        s->damage.b[0] = all;   /* the room kept since olc_surface_new */
        s->damage.n = 1;
    }
}

void *olc_surface_rastport(olc_surface *s)
{
    (void)s;
    return NULL;                /* the openintuition hooks step gives surfaces a RastPort */
}

const olc_region *olc__surface_damage(const olc_surface *s)
{
    return s ? &s->damage : NULL;
}

void olc__surface_damage_clear(olc_surface *s)
{
    if (s) s->damage.n = 0;
}
