/* GFOpenLayout's ROM core, step 2 (Workbench build plan row 10): scenes, layers and compose (section 4 of
 * include/gfopenlayout_core.h), on the CPU and through an attached back end (olc_accel, row N1).
 *
 * How a compose goes:
 *   - The damage is the caller's region, or else what changed since the last compose: the scene's own (layers
 *     made, freed, moved, restacked or given a new look) and each shown layer's surface damage, moved to the
 *     screen. It is cut to the screen.
 *   - With a back end attached: one begin, bind and upload for each layer the damage reaches, one clip with the
 *     damage, then for the damage's bounds a fill and each layer back to front as a copy (opaque) or a composite
 *     (see-through), then one submit. When any call answers no, the core aborts the batch and the CPU does it.
 *   - On the CPU: each damage box is filled, then each layer's part of it is copied or blended, back to front.
 *   - It never waits. A box that can't be added (out of memory) makes the next compose do the whole screen.
 *
 * Locks: the scene's lock is number 5 of the lock order. Every call that changes the scene takes it; the back
 * end's retire runs outside it. On the host there is no lock (the tests are one task).
 *
 * The screen's pixels come from GFOpenRTG on the Amiga (core/olc_screen_amiga.c).
 *
 * Not yet: the layer shadow (OLC_LOOK_SHADOW is kept, not drawn), a layer whose lock is busy (answer 1: needs
 * the layer locks of the hooks step), pens on deep screens (a pen there is black). MIT, Copyright (c) 2026
 * AmiCradle Team. */
#include "olc_core_internal.h"

#include <string.h>

#ifdef OLC_AMIGA
#include <exec/semaphores.h>
#include <proto/exec.h>
#define LOCK_T struct SignalSemaphore
#define LOCK_INIT(l) InitSemaphore(l)
#define LOCK(l) ObtainSemaphore(l)
#define UNLOCK(l) ReleaseSemaphore(l)
#else
#define LOCK_T int
#define LOCK_INIT(l) (*(l) = 0)
#define LOCK(l) ((void)(l))
#define UNLOCK(l) ((void)(l))
#endif

struct olc_layer {
    struct olc_layer *front, *back;     /* nearer the viewer, further away */
    olc_scene *sc;
    olc_box at;
    olc_surface *s;
    uint32_t opacity, shadow, hidden;
};

struct olc_scene {
    LOCK_T lock;
    void *screen;
    int32_t w, h, bpr;
    uint32_t mode, fmt;
    uint32_t seq;                       /* composes so far */
    void *pixels;
    olc_layer *frontmost, *backmost;
    olc_region pending;                 /* the scene's own changes, screen coordinates */
    int all;                            /* compose the whole screen next time */
};

/* ---- the screen lookup and the back end ---- */

#ifdef OLC_AMIGA
static olc_screen_lookup lookup = olc__amiga_screen_lookup;
#else
static olc_screen_lookup lookup;
#endif
static const olc_accel *accel;
static uint32_t accel_gen;

void olc__set_screen_lookup(olc_screen_lookup fn)
{
    lookup = fn;
}

int olc_accel_attach(const olc_accel *ops)
{
    if (!ops || ops->version != OLC_ACCEL_VERSION || accel) return -1;
    accel = ops;
    if (!++accel_gen) accel_gen = 1;
    return 0;
}

void olc_accel_detach(void)
{
    const olc_accel *a = accel;
    if (!a) return;
    accel = NULL;                       /* every surface's slot is stale from here (gen) */
    if (!++accel_gen) accel_gen = 1;
    if (a->detach) a->detach(a->ctx);
}

/* Called by olc_surface_free, outside every lock: the back end lets the surface's slot go */
void olc__surface_retire(olc_surface *s)
{
    if (s && s->gen && accel && s->bound_to == accel && s->gen == accel_gen && accel->retire)
        accel->retire(accel->ctx, s->slot);
    if (s) s->gen = 0;
}

/* ---- boxes and regions ---- */

static int box_cut(const olc_box *a, const olc_box *b, olc_box *out)
{
    out->x0 = a->x0 > b->x0 ? a->x0 : b->x0;
    out->y0 = a->y0 > b->y0 ? a->y0 : b->y0;
    out->x1 = a->x1 < b->x1 ? a->x1 : b->x1;
    out->y1 = a->y1 < b->y1 ? a->y1 : b->y1;
    return out->x1 >= out->x0 && out->y1 >= out->y0;
}

static void screen_box(const olc_scene *sc, olc_box *b)
{
    b->x0 = 0;
    b->y0 = 0;
    b->x1 = sc->w - 1;
    b->y1 = sc->h - 1;
}

/* Something on screen changed at b: into pending, or the whole screen when that can't be had */
static void mark(olc_scene *sc, const olc_box *b)
{
    if (sc->mode == OLC_SCENE_DIRECT || sc->all) return;
    if (olc_region_or_box(&sc->pending, b) < 0) sc->all = 1;
}

/* ---- scenes ---- */

static int bind_screen(olc_scene *sc, void *screen, int32_t w, int32_t h)
{
    void *px = NULL;
    int32_t bpr = 0;
    uint32_t fmt = 0;
    if (!lookup || lookup(screen, &px, &bpr, &fmt) < 0 || !px || bpr <= 0) return -1;
    if (fmt != OLC_FMT_CLUT8 && fmt != OLC_FMT_RGB16 && fmt != OLC_FMT_XRGB32 && fmt != OLC_FMT_ARGB32) return -1;
    sc->pixels = px;
    sc->bpr = bpr;
    sc->fmt = fmt == OLC_FMT_ARGB32 ? OLC_FMT_XRGB32 : fmt;     /* the screen's alpha byte is never shown */
    sc->screen = screen;
    sc->w = w;
    sc->h = h;
    return 0;
}

olc_scene *olc_scene_new(void *screen, int32_t w, int32_t h, uint32_t mode)
{
    olc_scene *sc;
    if (w <= 0 || h <= 0 || (mode != OLC_SCENE_DIRECT && mode != OLC_SCENE_COMPOSITED)) return NULL;
    sc = (olc_scene *)olc__alloc(sizeof *sc, 0);
    if (!sc) return NULL;
    LOCK_INIT(&sc->lock);
    sc->screen = screen;
    sc->w = w;
    sc->h = h;
    sc->mode = mode;
    if (mode == OLC_SCENE_COMPOSITED && bind_screen(sc, screen, w, h) < 0) {
        olc__free(sc);
        return NULL;            /* the caller asks again for a direct one */
    }
    return sc;
}

int olc_scene_rebind(olc_scene *sc, void *screen, int32_t w, int32_t h)
{
    olc_scene keep;
    if (!sc || w <= 0 || h <= 0) return -1;
    if (sc->mode == OLC_SCENE_DIRECT) {
        sc->screen = screen;
        sc->w = w;
        sc->h = h;
        return 0;
    }
    keep = *sc;
    if (bind_screen(sc, screen, w, h) < 0) {
        sc->screen = keep.screen;
        sc->w = keep.w;
        sc->h = keep.h;
        return -1;
    }
    LOCK(&sc->lock);
    sc->all = 1;                /* a new bitmap holds nothing of ours yet */
    UNLOCK(&sc->lock);
    return 0;
}

void olc_scene_free(olc_scene *sc)
{
    if (!sc) return;
    while (sc->frontmost) olc_layer_free(sc->frontmost);
    olc__region_drop(&sc->pending);
    olc__free(sc);
}

uint32_t olc_scene_mode(const olc_scene *sc)
{
    return sc ? sc->mode : OLC_SCENE_DIRECT;
}

/* ---- layers ---- */

static void unlink_layer(olc_layer *l)
{
    olc_scene *sc = l->sc;
    if (l->front) l->front->back = l->back; else sc->frontmost = l->back;
    if (l->back) l->back->front = l->front; else sc->backmost = l->front;
    l->front = l->back = NULL;
}

/* In front of front, or in front of all when front is NULL */
static void link_layer(olc_layer *l, olc_layer *front)
{
    olc_scene *sc = l->sc;
    if (!front || front->sc != sc) {
        l->front = NULL;
        l->back = sc->frontmost;
        if (sc->frontmost) sc->frontmost->front = l; else sc->backmost = l;
        sc->frontmost = l;
        return;
    }
    l->back = front;
    l->front = front->front;
    if (front->front) front->front->back = l; else sc->frontmost = l;
    front->front = l;
}

static uint32_t surf_fmt(const olc_scene *sc)
{
    return sc->mode == OLC_SCENE_DIRECT ? OLC_FMT_XRGB32 : sc->fmt;
}

olc_layer *olc_layer_new(olc_scene *sc, const olc_box *at, olc_layer *front)
{
    olc_layer *l;
    if (!sc || !at || at->x1 < at->x0 || at->y1 < at->y0) return NULL;
    l = (olc_layer *)olc__alloc(sizeof *l, 0);
    if (!l) return NULL;
    l->sc = sc;
    l->at = *at;
    l->opacity = 255;
    if (sc->mode == OLC_SCENE_COMPOSITED &&
        !(l->s = olc_surface_new(at->x1 - at->x0 + 1, at->y1 - at->y0 + 1, surf_fmt(sc), OLC_MEM_ANY, sc->screen))) {
        olc__free(l);
        return NULL;
    }
    LOCK(&sc->lock);
    link_layer(l, front);
    mark(sc, at);
    UNLOCK(&sc->lock);
    return l;
}

void olc_layer_free(olc_layer *l)
{
    olc_scene *sc;
    if (!l) return;
    sc = l->sc;
    LOCK(&sc->lock);
    unlink_layer(l);
    if (!l->hidden) mark(sc, &l->at);
    UNLOCK(&sc->lock);
    olc_surface_free(l->s);     /* retires its slot, outside the lock */
    olc__free(l);
}

olc_surface *olc_layer_surface(olc_layer *l)
{
    return l ? l->s : NULL;
}

int olc_layer_move(olc_layer *l, const olc_box *at)
{
    olc_scene *sc;
    olc_surface *ns = NULL, *old = NULL;
    int32_t w, h, ow = 0, oh = 0;
    if (!l || !at || at->x1 < at->x0 || at->y1 < at->y0) return -1;
    sc = l->sc;
    w = at->x1 - at->x0 + 1;
    h = at->y1 - at->y0 + 1;
    if (l->s) olc_surface_size(l->s, &ow, &oh, NULL);
    if (l->s && (w != ow || h != oh)) {
        /* a new size: a new surface, with what the old one had where both have room. What is new is the
         * owner's to draw, and is damage. */
        int32_t bpr_o, bpr_n, y, rowbytes, bpp;
        unsigned char *po, *pn;
        olc_box nb;
        if (!(ns = olc_surface_new(w, h, surf_fmt(sc), OLC_MEM_ANY, sc->screen))) return -1;
        po = (unsigned char *)olc_surface_pixels(l->s, &bpr_o);
        pn = (unsigned char *)olc_surface_pixels(ns, &bpr_n);
        bpp = surf_fmt(sc) == OLC_FMT_CLUT8 ? 1 : surf_fmt(sc) == OLC_FMT_RGB16 ? 2 : 4;
        rowbytes = (w < ow ? w : ow) * bpp;
        for (y = 0; y < h && y < oh; y++) memcpy(pn + (size_t)y * bpr_n, po + (size_t)y * bpr_o, (size_t)rowbytes);
        if (w > ow) { nb.x0 = ow; nb.y0 = 0; nb.x1 = w - 1; nb.y1 = h - 1; olc_surface_damage(ns, &nb); }
        if (h > oh) { nb.x0 = 0; nb.y0 = oh; nb.x1 = w - 1; nb.y1 = h - 1; olc_surface_damage(ns, &nb); }
    }
    LOCK(&sc->lock);
    if (!l->hidden) {
        mark(sc, &l->at);
        mark(sc, at);
    }
    l->at = *at;
    if (ns) {
        old = l->s;
        l->s = ns;
    }
    UNLOCK(&sc->lock);
    olc_surface_free(old);
    return 0;
}

void olc_layer_restack(olc_layer *l, olc_layer *front)
{
    olc_scene *sc;
    if (!l || front == l) return;
    sc = l->sc;
    LOCK(&sc->lock);
    unlink_layer(l);
    link_layer(l, front);
    if (!l->hidden) mark(sc, &l->at);
    UNLOCK(&sc->lock);
}

void olc_layer_look(olc_layer *l, uint32_t what, uint32_t value)
{
    olc_scene *sc;
    if (!l) return;
    sc = l->sc;
    LOCK(&sc->lock);
    switch (what) {
    case OLC_LOOK_OPACITY: l->opacity = value > 255 ? 255 : value; break;
    case OLC_LOOK_SHADOW: l->shadow = value ? 1 : 0; break;
    case OLC_LOOK_HIDDEN: l->hidden = value ? 1 : 0; break;
    default: UNLOCK(&sc->lock); return;
    }
    mark(sc, &l->at);
    UNLOCK(&sc->lock);
}

/* ---- pixels, on the CPU ---- */

static uint32_t to565(uint32_t c)
{
    return ((c >> 8) & 0xF800UL) | ((c >> 5) & 0x07E0UL) | ((c >> 3) & 0x001FUL);
}

static unsigned blend8(unsigned s, unsigned d, unsigned a)
{
    return (s * a + d * (255u - a) + 127u) / 255u;
}

static void cpu_fill(olc_scene *sc, const olc_box *b, olc_colour c)
{
    int32_t x, y;
    unsigned char *row = (unsigned char *)sc->pixels + (size_t)b->y0 * sc->bpr;
    for (y = b->y0; y <= b->y1; y++, row += sc->bpr) {
        switch (sc->fmt) {
        case OLC_FMT_CLUT8:
            memset(row + b->x0, OLC_IS_PEN(c) ? (int)(c & 0xFF) : 0, (size_t)(b->x1 - b->x0 + 1));
            break;
        case OLC_FMT_RGB16: {
            uint16_t v = OLC_IS_PEN(c) ? 0 : (uint16_t)to565(c), *p = (uint16_t *)row;
            for (x = b->x0; x <= b->x1; x++) p[x] = v;
            break;
        }
        default: {
            uint32_t v = OLC_IS_PEN(c) ? 0 : (c & 0x00FFFFFFUL), *p = (uint32_t *)row;
            for (x = b->x0; x <= b->x1; x++) p[x] = v;
            break;
        }
        }
    }
}

/* The layer's part b (screen coordinates, inside the layer) onto the screen, opaque or see-through */
static void cpu_layer(olc_scene *sc, const olc_layer *l, const olc_box *b, unsigned opacity)
{
    int32_t sbpr, x, y, bpp = sc->fmt == OLC_FMT_CLUT8 ? 1 : sc->fmt == OLC_FMT_RGB16 ? 2 : 4;
    const unsigned char *src = (const unsigned char *)olc_surface_pixels(l->s, &sbpr);
    unsigned char *dst = (unsigned char *)sc->pixels;
    int alpha = 0;
    uint32_t sfmt;
    olc_surface_size(l->s, NULL, NULL, &sfmt);
    alpha = sfmt == OLC_FMT_ARGB32;
    src += (size_t)(b->y0 - l->at.y0) * sbpr + (size_t)(b->x0 - l->at.x0) * bpp;
    dst += (size_t)b->y0 * sc->bpr + (size_t)b->x0 * bpp;
    for (y = b->y0; y <= b->y1; y++, src += sbpr, dst += sc->bpr) {
        int32_t n = b->x1 - b->x0 + 1;
        if (opacity >= 255 && !alpha) {
            memcpy(dst, src, (size_t)n * bpp);
            continue;
        }
        if (bpp == 2) {
            const uint16_t *s = (const uint16_t *)src;
            uint16_t *d = (uint16_t *)dst;
            for (x = 0; x < n; x++) {
                unsigned sr = s[x] >> 11, sg = (s[x] >> 5) & 63, sb = s[x] & 31;
                unsigned dr = d[x] >> 11, dg = (d[x] >> 5) & 63, db = d[x] & 31;
                d[x] = (uint16_t)(blend8(sr, dr, opacity) << 11 | blend8(sg, dg, opacity) << 5 | blend8(sb, db, opacity));
            }
        } else {
            const uint32_t *s = (const uint32_t *)src;
            uint32_t *d = (uint32_t *)dst;
            for (x = 0; x < n; x++) {
                unsigned a = alpha ? (unsigned)((s[x] >> 24) * opacity + 127u) / 255u : opacity;
                d[x] = (uint32_t)blend8((s[x] >> 16) & 255, (d[x] >> 16) & 255, a) << 16 |
                       (uint32_t)blend8((s[x] >> 8) & 255, (d[x] >> 8) & 255, a) << 8 |
                       (uint32_t)blend8(s[x] & 255, d[x] & 255, a);
            }
        }
    }
}

static olc_colour backdrop(const olc_scene *sc)
{
    return sc->fmt == OLC_FMT_CLUT8 ? OLC_PEN(0) : 0xFF000000UL;
}

/* A layer is drawn see-through when it has an opacity below 255 or alpha of its own; on an 8-bit screen
 * nothing blends, so it is opaque there. */
static unsigned layer_opacity(const olc_scene *sc, const olc_layer *l)
{
    return sc->fmt == OLC_FMT_CLUT8 ? 255u : l->opacity;
}

static int layer_seethrough(const olc_scene *sc, const olc_layer *l)
{
    uint32_t f;
    if (sc->fmt == OLC_FMT_CLUT8) return 0;
    olc_surface_size(l->s, NULL, NULL, &f);
    return l->opacity < 255 || f == OLC_FMT_ARGB32;
}

static void cpu_compose(olc_scene *sc, const olc_region *d)
{
    int32_t i;
    olc_box b, c;
    for (i = 0; olc_region_box(d, i, &b); i++) {
        const olc_layer *l;
        cpu_fill(sc, &b, backdrop(sc));
        for (l = sc->backmost; l; l = l->front) {
            if (l->hidden || !l->opacity || !box_cut(&b, &l->at, &c)) continue;
            cpu_layer(sc, l, &c, layer_opacity(sc, l));
        }
    }
}

/* ---- through the back end ---- */

/* 0 when the whole compose went into one submitted batch; -1 for the CPU to do it (the batch aborted) */
static int accel_compose(olc_scene *sc, const olc_region *d)
{
    const olc_accel *a = accel;
    void *batch;
    olc_layer *l;
    olc_box bounds, c;
    int32_t i, n = olc_region_count(d);
    olc_box *boxes = NULL;
    uint32_t fence;
    if (!a || !olc_region_bounds(d, &bounds)) return -1;
    if (!(boxes = (olc_box *)olc__alloc((size_t)n * sizeof *boxes, 0))) return -1;
    for (i = 0; i < n; i++) olc_region_box(d, i, &boxes[i]);
    if (!(batch = a->begin(a->ctx, sc->screen, sc->pixels, sc->bpr, sc->fmt, sc->w, sc->h))) {
        olc__free(boxes);
        return -1;
    }
    /* each layer the damage reaches: bound once, its changes uploaded */
    for (l = sc->backmost; l; l = l->front) {
        const olc_region *sd;
        int32_t bpr, w, h, k, ns;
        uint32_t fmt, slot = 0;
        const void *px;
        if (l->hidden || !l->opacity || !box_cut(&bounds, &l->at, &c)) continue;
        px = olc_surface_pixels(l->s, &bpr);
        olc_surface_size(l->s, &w, &h, &fmt);
        if (a->bind(a->ctx, batch, px, bpr, w, h, fmt, &slot) < 0) goto cpu;
        l->s->upseq = sc->seq;
        if (l->s->gen != accel_gen || l->s->bound_to != a || l->s->slot != slot || l->s->stale) {
            /* new to this back end: all of it goes up */
            olc_box all;
            all.x0 = 0; all.y0 = 0; all.x1 = w - 1; all.y1 = h - 1;
            l->s->slot = slot;
            l->s->gen = accel_gen;
            l->s->bound_to = a;
            l->s->stale = 0;
            if (a->upload(a->ctx, batch, slot, &all, 1) < 0) goto cpu;
            continue;
        }
        sd = olc__surface_damage(l->s);
        ns = olc_region_count(sd);
        if (ns) {
            olc_box *up = (olc_box *)olc__alloc((size_t)ns * sizeof *up, 0);
            int rc;
            if (!up) goto cpu;
            for (k = 0; k < ns; k++) olc_region_box(sd, k, &up[k]);
            rc = a->upload(a->ctx, batch, slot, up, ns);
            olc__free(up);
            if (rc < 0) goto cpu;
        }
    }
    if (a->clip(a->ctx, batch, boxes, n) < 0) goto cpu;
    if (a->fill(a->ctx, batch, &bounds, backdrop(sc)) < 0) goto cpu;
    for (l = sc->backmost; l; l = l->front) {
        olc_box src;
        if (l->hidden || !l->opacity || !box_cut(&bounds, &l->at, &c)) continue;
        src.x0 = c.x0 - l->at.x0; src.y0 = c.y0 - l->at.y0;
        src.x1 = c.x1 - l->at.x0; src.y1 = c.y1 - l->at.y0;
        if (layer_seethrough(sc, l) ? a->composite(a->ctx, batch, l->s->slot, &src, c.x0, c.y0, l->opacity) < 0
                                    : a->copy(a->ctx, batch, l->s->slot, &src, c.x0, c.y0) < 0) goto cpu;
    }
    olc__free(boxes);
    return a->submit(a->ctx, batch, &fence) < 0 ? -1 : 0;
cpu:
    a->abort(a->ctx, batch);
    /* what this batch would have uploaded never went up */
    for (l = sc->backmost; l; l = l->front) l->s->stale = 1;
    olc__free(boxes);
    return -1;
}

/* ---- compose ---- */

int olc_compose(olc_scene *sc, const olc_region *damage)
{
    olc_region d = { NULL, 0, 0 };
    olc_box scr;
    olc_layer *l;
    int rc = 0;
    if (!sc) return -1;
    if (sc->mode == OLC_SCENE_DIRECT) return 0;
    screen_box(sc, &scr);
    LOCK(&sc->lock);
    if (damage) {
        if (olc_region_copy(&d, damage) < 0) rc = -1;
    } else if (sc->all) {
        if (olc_region_or_box(&d, &scr) < 0) rc = -1;
    } else {
        if (olc_region_copy(&d, &sc->pending) < 0) rc = -1;
        for (l = sc->backmost; l && !rc; l = l->front) {
            const olc_region *sd;
            int32_t k;
            olc_box b;
            if (l->hidden) continue;
            sd = olc__surface_damage(l->s);
            for (k = 0; olc_region_box(sd, k, &b) && !rc; k++) {
                b.x0 += l->at.x0; b.x1 += l->at.x0;
                b.y0 += l->at.y0; b.y1 += l->at.y0;
                if (olc_region_or_box(&d, &b) < 0) rc = -1;
            }
        }
    }
    if (rc < 0) {
        sc->all = 1;            /* nothing is lost: all of it next time */
        UNLOCK(&sc->lock);
        olc__region_drop(&d);
        return -1;
    }
    olc_region_and_box(&d, &scr);
    sc->seq++;
    if (olc_region_count(&d)) {
        if (accel_compose(sc, &d) < 0) cpu_compose(sc, &d);
    }
    if (!damage) {
        olc_region_clear(&sc->pending);
        sc->all = 0;
    }
    /* every surface's changes are now on screen. One whose changes didn't go up in a batch (hidden, see-through
     * to nothing, or no back end) is uploaded whole at its next bind. */
    if (!damage) {
        for (l = sc->backmost; l; l = l->front) {
            if (olc_region_count(olc__surface_damage(l->s)) && l->s->upseq != sc->seq) l->s->stale = 1;
            olc__surface_damage_clear(l->s);
        }
    }
    UNLOCK(&sc->lock);
    olc__region_drop(&d);
    return 0;
}
