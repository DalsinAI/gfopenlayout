/* GFOpenLayout's ROM core, step 2 (core/olc_compose.c): scenes, layers and compose.
 * After every compose the whole screen is checked against a reference that draws every layer from scratch, so a
 * change the damage missed shows as a wrong pixel. The same runs go through a fake back end that composes only
 * from what was uploaded to it, so a change that was never uploaded shows too. Built with OLC_TEST_HOOKS.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "olc_core_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

extern long olc_test_fail_at;

#define SW 96
#define SH 64
#define MAXL 6

static uint32_t seed = 777;
static int32_t rnd(int32_t n)
{
    seed = seed * 1103515245u + 12345u;
    return (int32_t)((seed >> 8) % (uint32_t)n);
}

/* ---- the fake screen ---- */

typedef struct fake_screen {
    uint32_t fmt;
    int32_t bpr;
    unsigned char px[SH * SW * 4 + 64];
} fake_screen;

static int lookup(void *screen, void **pixels, int32_t *bpr, uint32_t *fmt)
{
    fake_screen *f = (fake_screen *)screen;
    if (!f || f->fmt == 99) return -1;
    *pixels = f->px;
    *bpr = f->bpr;
    *fmt = f->fmt;
    return 0;
}

static int32_t bpp_of(uint32_t fmt)
{
    return fmt == OLC_FMT_CLUT8 ? 1 : fmt == OLC_FMT_RGB16 ? 2 : 4;
}

/* ---- the reference ---- */

typedef struct ref_layer {
    olc_layer *l;
    olc_box at;
    unsigned opacity;
    int hidden;
} ref_layer;

static ref_layer order[MAXL];       /* back to front */
static int nlayers;

static unsigned blend8(unsigned s, unsigned d, unsigned a)
{
    return (s * a + d * (255u - a) + 127u) / 255u;
}

static void reference(uint32_t fmt, unsigned char *out, int32_t bpr)
{
    int32_t x, y, bpp = bpp_of(fmt);
    int i;
    memset(out, 0, (size_t)bpr * SH);
    for (i = 0; i < nlayers; i++) {
        const ref_layer *r = &order[i];
        int32_t sbpr;
        const unsigned char *sp;
        unsigned op = fmt == OLC_FMT_CLUT8 ? 255u : r->opacity;
        if (r->hidden || !r->opacity) continue;
        sp = (const unsigned char *)olc_surface_pixels(olc_layer_surface(r->l), &sbpr);
        for (y = r->at.y0 < 0 ? 0 : r->at.y0; y <= r->at.y1 && y < SH; y++) {
            for (x = r->at.x0 < 0 ? 0 : r->at.x0; x <= r->at.x1 && x < SW; x++) {
                const unsigned char *s = sp + (size_t)(y - r->at.y0) * sbpr + (size_t)(x - r->at.x0) * bpp;
                unsigned char *d = out + (size_t)y * bpr + (size_t)x * bpp;
                if (op >= 255) { memcpy(d, s, (size_t)bpp); continue; }
                if (bpp == 2) {
                    uint16_t sv, dv;
                    memcpy(&sv, s, 2); memcpy(&dv, d, 2);
                    dv = (uint16_t)(blend8(sv >> 11, dv >> 11, op) << 11 | blend8((sv >> 5) & 63, (dv >> 5) & 63, op) << 5 |
                                    blend8(sv & 31, dv & 31, op));
                    memcpy(d, &dv, 2);
                } else {
                    uint32_t sv, dv;
                    memcpy(&sv, s, 4); memcpy(&dv, d, 4);
                    dv = (uint32_t)blend8((sv >> 16) & 255, (dv >> 16) & 255, op) << 16 |
                         (uint32_t)blend8((sv >> 8) & 255, (dv >> 8) & 255, op) << 8 | blend8(sv & 255, dv & 255, op);
                    memcpy(d, &dv, 4);
                }
            }
        }
    }
}

/* ---- the fake back end: composes from its own copies, which change only by upload ---- */

typedef struct tex {
    uint32_t slot;
    const void *pixels;
    int32_t bpr, w, h;
    uint32_t fmt;
    unsigned char *copy;        /* what the GPU holds once a batch is submitted */
    unsigned char *stage;       /* this batch's uploads; dropped on abort */
} tex;

typedef struct fake {
    olc_accel ops;
    tex t[32];
    int nt;
    uint32_t next;
    olc_box clip[256];
    int nclip;
    unsigned char *px;
    int32_t bpr;
    uint32_t fmt;
    int begins, submits, aborts, retires, detaches, in_batch, failed_submits;
    long uploaded;              /* pixels uploaded */
    int fail_bind_at;           /* the bind call that fails, 0 none */
    int fail_submit;            /* the next submit fails: the batch is dropped, uploads and all */
    int binds;
} fake;

static fake fk;

static tex *tex_of(uint32_t slot)
{
    int i;
    for (i = 0; i < fk.nt; i++) if (fk.t[i].slot == slot) return &fk.t[i];
    return NULL;
}

static void *f_begin(void *ctx, void *screen, void *pixels, int32_t bpr, uint32_t fmt, int32_t w, int32_t h)
{
    (void)ctx; (void)screen; (void)w; (void)h;
    fk.begins++;
    fk.in_batch = 1;
    fk.px = (unsigned char *)pixels;
    fk.bpr = bpr;
    fk.fmt = fmt;
    fk.nclip = 0;
    return &fk;
}

static int f_bind(void *ctx, void *batch, const void *pixels, int32_t bpr, int32_t w, int32_t h, uint32_t fmt,
                  uint32_t *slot)
{
    int i;
    (void)ctx; (void)batch;
    if (fk.fail_bind_at && ++fk.binds == fk.fail_bind_at) return -1;
    for (i = 0; i < fk.nt; i++) if (fk.t[i].pixels == pixels && fk.t[i].w == w && fk.t[i].h == h) {
        *slot = fk.t[i].slot;
        return 0;
    }
    if (fk.nt == 32) return -1;
    fk.t[fk.nt].slot = ++fk.next;
    fk.t[fk.nt].pixels = pixels;
    fk.t[fk.nt].bpr = bpr;
    fk.t[fk.nt].w = w;
    fk.t[fk.nt].h = h;
    fk.t[fk.nt].fmt = fmt;
    fk.t[fk.nt].copy = (unsigned char *)calloc((size_t)bpr, (size_t)h);   /* not the surface's pixels yet */
    fk.t[fk.nt].stage = (unsigned char *)calloc((size_t)bpr, (size_t)h);
    *slot = fk.t[fk.nt++].slot;
    return 0;
}

static int f_upload(void *ctx, void *batch, uint32_t slot, const olc_box *b, int32_t n)
{
    tex *t = tex_of(slot);
    int32_t i, y, bpp;
    (void)ctx; (void)batch;
    if (!t) return -1;
    bpp = bpp_of(t->fmt);
    for (i = 0; i < n; i++) {
        for (y = b[i].y0; y <= b[i].y1; y++)
            memcpy(t->stage + (size_t)y * t->bpr + (size_t)b[i].x0 * bpp,
                   (const unsigned char *)t->pixels + (size_t)y * t->bpr + (size_t)b[i].x0 * bpp,
                   (size_t)(b[i].x1 - b[i].x0 + 1) * bpp);
        fk.uploaded += (long)(b[i].x1 - b[i].x0 + 1) * (b[i].y1 - b[i].y0 + 1);
    }
    return 0;
}

static int f_clip(void *ctx, void *batch, const olc_box *b, int32_t n)
{
    (void)ctx; (void)batch;
    if (n > 256) return -1;
    memcpy(fk.clip, b, (size_t)n * sizeof *b);
    fk.nclip = n;
    return 0;
}

static int in_clip(int32_t x, int32_t y)
{
    int i;
    for (i = 0; i < fk.nclip; i++)
        if (x >= fk.clip[i].x0 && x <= fk.clip[i].x1 && y >= fk.clip[i].y0 && y <= fk.clip[i].y1) return 1;
    return 0;
}

static int f_fill(void *ctx, void *batch, const olc_box *b, olc_colour c)
{
    int32_t x, y, bpp = bpp_of(fk.fmt);
    (void)ctx; (void)batch; (void)c;    /* the backdrop: 0 in every format the test uses */
    for (y = b->y0; y <= b->y1; y++)
        for (x = b->x0; x <= b->x1; x++)
            if (in_clip(x, y)) memset(fk.px + (size_t)y * fk.bpr + (size_t)x * bpp, 0, (size_t)bpp);
    return 0;
}

static int f_draw(uint32_t slot, const olc_box *src, int32_t dx, int32_t dy, unsigned op)
{
    tex *t = tex_of(slot);
    int32_t x, y, bpp;
    if (!t) return -1;
    bpp = bpp_of(fk.fmt);
    for (y = src->y0; y <= src->y1; y++)
        for (x = src->x0; x <= src->x1; x++) {
            int32_t sx = dx + x - src->x0, sy = dy + y - src->y0;
            const unsigned char *s = t->stage + (size_t)y * t->bpr + (size_t)x * bpp;
            unsigned char *d = fk.px + (size_t)sy * fk.bpr + (size_t)sx * bpp;
            if (!in_clip(sx, sy)) continue;
            if (op >= 255) { memcpy(d, s, (size_t)bpp); continue; }
            if (bpp == 2) {
                uint16_t sv, dv;
                memcpy(&sv, s, 2); memcpy(&dv, d, 2);
                dv = (uint16_t)(blend8(sv >> 11, dv >> 11, op) << 11 | blend8((sv >> 5) & 63, (dv >> 5) & 63, op) << 5 |
                                blend8(sv & 31, dv & 31, op));
                memcpy(d, &dv, 2);
            } else {
                uint32_t sv, dv;
                memcpy(&sv, s, 4); memcpy(&dv, d, 4);
                dv = (uint32_t)blend8((sv >> 16) & 255, (dv >> 16) & 255, op) << 16 |
                     (uint32_t)blend8((sv >> 8) & 255, (dv >> 8) & 255, op) << 8 | blend8(sv & 255, dv & 255, op);
                memcpy(d, &dv, 4);
            }
        }
    return 0;
}

static int f_copy(void *ctx, void *batch, uint32_t slot, const olc_box *src, int32_t dx, int32_t dy)
{
    (void)ctx; (void)batch;
    return f_draw(slot, src, dx, dy, 255);
}

static int f_composite(void *ctx, void *batch, uint32_t slot, const olc_box *src, int32_t dx, int32_t dy,
                       uint32_t opacity)
{
    (void)ctx; (void)batch;
    return f_draw(slot, src, dx, dy, opacity);
}

static int f_submit(void *ctx, void *batch, uint32_t *fence)
{
    int i;
    (void)ctx; (void)batch;
    if (fk.fail_submit) {
        for (i = 0; i < fk.nt; i++) memcpy(fk.t[i].stage, fk.t[i].copy, (size_t)fk.t[i].bpr * fk.t[i].h);
        fk.fail_submit = 0;
        fk.failed_submits++;
        fk.in_batch = 0;
        return -1;
    }
    for (i = 0; i < fk.nt; i++) memcpy(fk.t[i].copy, fk.t[i].stage, (size_t)fk.t[i].bpr * fk.t[i].h);
    fk.submits++;
    fk.in_batch = 0;
    *fence = (uint32_t)fk.submits;
    return 0;
}

static void f_abort(void *ctx, void *batch)
{
    int i;
    (void)ctx; (void)batch;
    for (i = 0; i < fk.nt; i++) memcpy(fk.t[i].stage, fk.t[i].copy, (size_t)fk.t[i].bpr * fk.t[i].h);
    fk.aborts++;
    fk.in_batch = 0;
}

static void f_retire(void *ctx, uint32_t slot)
{
    int i;
    (void)ctx;
    CHECK(!fk.in_batch);                /* never during a compose */
    for (i = 0; i < fk.nt; i++) if (fk.t[i].slot == slot) {
        free(fk.t[i].copy);
        free(fk.t[i].stage);
        fk.t[i] = fk.t[--fk.nt];
        fk.retires++;
        return;
    }
    CHECK(0);                           /* a slot the back end never gave */
}

static void f_detach(void *ctx)
{
    int i;
    (void)ctx;
    for (i = 0; i < fk.nt; i++) {
        free(fk.t[i].copy);
        free(fk.t[i].stage);
    }
    fk.nt = 0;
    fk.detaches++;
}

static void fake_init(void)
{
    memset(&fk, 0, sizeof fk);
    fk.ops.version = OLC_ACCEL_VERSION;
    fk.ops.ctx = &fk;
    fk.ops.begin = f_begin; fk.ops.bind = f_bind; fk.ops.upload = f_upload; fk.ops.clip = f_clip;
    fk.ops.fill = f_fill; fk.ops.copy = f_copy; fk.ops.composite = f_composite; fk.ops.submit = f_submit;
    fk.ops.abort = f_abort; fk.ops.retire = f_retire; fk.ops.detach = f_detach;
}

/* ---- the runs ---- */

static void paint(olc_layer *l, uint32_t fmt)
{
    olc_surface *s = olc_layer_surface(l);
    int32_t w, h, bpr, x, y, bpp = bpp_of(fmt);
    unsigned char *p = (unsigned char *)olc_surface_pixels(s, &bpr);
    olc_box b;
    uint32_t v = (uint32_t)rnd(0x7FFFFFFF) | 1;
    olc_surface_size(s, &w, &h, NULL);
    b.x0 = rnd(w); b.y0 = rnd(h); b.x1 = b.x0 + rnd(w - b.x0); b.y1 = b.y0 + rnd(h - b.y0);
    if (fmt == OLC_FMT_XRGB32) v &= 0x00FFFFFFUL;
    for (y = b.y0; y <= b.y1; y++)
        for (x = b.x0; x <= b.x1; x++) memcpy(p + (size_t)y * bpr + (size_t)x * bpp, &v, (size_t)bpp);
    olc_surface_damage(s, &b);
}

static olc_box rnd_at(void)
{
    olc_box b;
    b.x0 = rnd(SW + 20) - 20;
    b.y0 = rnd(SH + 20) - 20;
    b.x1 = b.x0 + 4 + rnd(40);
    b.y1 = b.y0 + 4 + rnd(30);
    return b;
}

static int find(olc_layer *l)
{
    int i;
    for (i = 0; i < nlayers; i++) if (order[i].l == l) return i;
    return -1;
}

static void run(uint32_t fmt, int with_accel, int steps)
{
    static fake_screen scr;
    static unsigned char want[SH * SW * 4 + 64];
    olc_scene *sc;
    int step, bad = 0, i;
    memset(&scr, 0, sizeof scr);
    scr.fmt = fmt;
    scr.bpr = ((SW * bpp_of(fmt)) + 15) & ~15;
    nlayers = 0;
    if (with_accel) {
        fake_init();
        CHECK(olc_accel_attach(&fk.ops) == 0);
    }
    sc = olc_scene_new(&scr, SW, SH, OLC_SCENE_COMPOSITED);
    CHECK(sc != NULL && olc_scene_mode(sc) == OLC_SCENE_COMPOSITED);
    if (!sc) return;
    for (step = 0; step < steps; step++) {
        int what = rnd(12), rc;
        if (nlayers < 2 || (what == 0 && nlayers < MAXL)) {
            olc_box at = rnd_at();
            /* index in order (back to front); the API puts a layer in front of another, or in front of all */
            int pos = nlayers ? 1 + rnd(nlayers) : 0;
            olc_layer *front = pos ? order[pos - 1].l : NULL;
            olc_layer *l = olc_layer_new(sc, &at, front);
            CHECK(l != NULL);
            if (!l) continue;
            memmove(&order[pos + 1], &order[pos], (size_t)(nlayers - pos) * sizeof order[0]);
            order[pos].l = l; order[pos].at = at; order[pos].opacity = 255; order[pos].hidden = 0;
            nlayers++;
            paint(l, fmt);
        } else {
            int k = rnd(nlayers);
            ref_layer *r = &order[k];
            switch (what) {
            case 1: {                       /* free */
                olc_layer_free(r->l);
                memmove(&order[k], &order[k + 1], (size_t)(nlayers - k - 1) * sizeof order[0]);
                nlayers--;
                break;
            }
            case 2: case 3: {               /* move, same size */
                int32_t dx = rnd(21) - 10, dy = rnd(21) - 10;
                olc_box at = r->at;
                at.x0 += dx; at.x1 += dx; at.y0 += dy; at.y1 += dy;
                CHECK(olc_layer_move(r->l, &at) == 0);
                r->at = at;
                break;
            }
            case 4: {                       /* move and size */
                olc_box at = r->at;
                at.x1 = at.x0 + 4 + rnd(40);
                at.y1 = at.y0 + 4 + rnd(30);
                CHECK(olc_layer_move(r->l, &at) == 0);
                r->at = at;
                paint(r->l, fmt);
                break;
            }
            case 5: {                       /* restack */
                ref_layer t = *r;
                int to;
                memmove(&order[k], &order[k + 1], (size_t)(nlayers - k - 1) * sizeof order[0]);
                nlayers--;
                to = rnd(nlayers + 1);
                if (!to && nlayers) to = 1;     /* there is no "behind all" */
                olc_layer_restack(t.l, rnd(4) == 0 && to == nlayers ? NULL : to ? order[to - 1].l : NULL);
                memmove(&order[to + 1], &order[to], (size_t)(nlayers - to) * sizeof order[0]);
                order[to] = t;
                nlayers++;
                break;
            }
            case 6:
                r->hidden = !r->hidden;
                olc_layer_look(r->l, OLC_LOOK_HIDDEN, (uint32_t)r->hidden);
                break;
            case 7:
                r->opacity = (unsigned)(rnd(3) == 0 ? 255 : rnd(256));
                olc_layer_look(r->l, OLC_LOOK_OPACITY, r->opacity);
                break;
            default:
                paint(r->l, fmt);
                break;
            }
        }
        if (with_accel && rnd(8) == 0) fk.fail_bind_at = 1 + rnd(4), fk.binds = 0;   /* the CPU takes over */
        if (with_accel && rnd(10) == 0) fk.fail_submit = 1;                        /* so does a failed submit */
        rc = olc_compose(sc, NULL);
        fk.fail_bind_at = 0;
        fk.fail_submit = 0;
        if (rc != 0) bad++;
        reference(fmt, want, scr.bpr);
        if (memcmp(want, scr.px, (size_t)scr.bpr * SH)) {
            if (!bad) printf("  step %d (op %d): the screen differs from the reference\n", step, what);
            bad++;
        }
        /* order[] must agree with the scene's own order: checked through the picture above */
        (void)find;
    }
    CHECK(bad == 0);
    if (with_accel) {
        printf("  %d composes: %d batches, %d aborted to the CPU, %ld pixels uploaded\n", steps, fk.begins, fk.aborts,
               fk.uploaded);
        printf("  %d submits failed\n", fk.failed_submits);
        CHECK(fk.begins == fk.submits + fk.aborts + fk.failed_submits);
        CHECK(fk.failed_submits > 0);
        CHECK(fk.aborts > 0 && fk.submits > steps / 3);
    }
    for (i = 0; i < nlayers; i++) {}
    olc_scene_free(sc);
    if (with_accel) {
        CHECK(fk.nt == 0);                  /* every slot retired when its surface went */
        olc_accel_detach();
        CHECK(fk.detaches == 1);
    }
    nlayers = 0;
}

static void test_scenes(void)
{
    static fake_screen scr;
    olc_scene *sc;
    olc_layer *a, *b;
    olc_box at = { 2, 2, 11, 9 };
    printf("scenes\n");
    memset(&scr, 0, sizeof scr);
    scr.fmt = OLC_FMT_XRGB32;
    scr.bpr = SW * 4;
    olc__set_screen_lookup(NULL);
    CHECK(olc_scene_new(&scr, SW, SH, OLC_SCENE_COMPOSITED) == NULL);    /* no way to see its pixels */
    sc = olc_scene_new(&scr, SW, SH, OLC_SCENE_DIRECT);                  /* a direct one always */
    CHECK(sc && olc_scene_mode(sc) == OLC_SCENE_DIRECT);
    a = olc_layer_new(sc, &at, NULL);
    CHECK(a && olc_layer_surface(a) == NULL);                            /* direct: no surfaces */
    CHECK(olc_compose(sc, NULL) == 0);
    CHECK(olc_layer_move(a, &at) == 0);
    olc_scene_free(sc);
    olc__set_screen_lookup(lookup);
    scr.fmt = 99;
    CHECK(olc_scene_new(&scr, SW, SH, OLC_SCENE_COMPOSITED) == NULL);    /* the bitmap can't be composited */
    CHECK(olc_scene_new(&scr, 0, SH, OLC_SCENE_DIRECT) == NULL);
    CHECK(olc_scene_new(&scr, SW, SH, 7) == NULL);
    scr.fmt = OLC_FMT_XRGB32;
    sc = olc_scene_new(&scr, SW, SH, OLC_SCENE_COMPOSITED);
    CHECK(sc != NULL);
    if (!sc) return;
    a = olc_layer_new(sc, &at, NULL);
    b = olc_layer_new(sc, &at, a);          /* in front of a */
    CHECK(a && b);
    {   /* b's pixels show, not a's */
        int32_t bpr;
        uint32_t *pa = (uint32_t *)olc_surface_pixels(olc_layer_surface(a), &bpr);
        uint32_t *pb = (uint32_t *)olc_surface_pixels(olc_layer_surface(b), &bpr);
        uint32_t got;
        pa[0] = 0x00112233; pb[0] = 0x00445566;
        CHECK(olc_compose(sc, NULL) == 0);
        memcpy(&got, scr.px + 2 * scr.bpr + 2 * 4, 4);
        CHECK(got == 0x00445566);
        olc_layer_restack(a, NULL);         /* a to the front */
        CHECK(olc_compose(sc, NULL) == 0);
        memcpy(&got, scr.px + 2 * scr.bpr + 2 * 4, 4);
        CHECK(got == 0x00112233);
        /* out of memory while gathering the damage: -1, and the next compose does the whole screen */
        olc_layer_restack(b, NULL);
        olc_test_fail_at = 1;
        CHECK(olc_compose(sc, NULL) == -1);
        olc_test_fail_at = 0;
        memcpy(&got, scr.px + 2 * scr.bpr + 2 * 4, 4);
        CHECK(got == 0x00112233);           /* nothing drawn yet */
        memset(scr.px + 40 * scr.bpr, 0x7F, 8);                         /* junk far from any layer */
        CHECK(olc_compose(sc, NULL) == 0);
        memcpy(&got, scr.px + 2 * scr.bpr + 2 * 4, 4);
        CHECK(got == 0x00445566);
        CHECK(scr.px[40 * scr.bpr] == 0);   /* the whole screen was composed */
        /* a caller's damage: only that is composed */
        memset(scr.px + 40 * scr.bpr, 0x7F, 8);
        {
            olc_region *r = olc_region_new();
            olc_box d = { 0, 40, 0, 40 };
            olc_region_or_box(r, &d);
            CHECK(olc_compose(sc, r) == 0);
            CHECK(scr.px[40 * scr.bpr] == 0 && scr.px[40 * scr.bpr + 4] == 0x7F);
            olc_region_free(r);
        }
        /* a moved layer that can't get its new size stays as it was */
        {
            olc_box big = { 2, 2, 41, 39 };
            olc_test_fail_at = 1;
            CHECK(olc_layer_move(a, &big) == -1);
            olc_test_fail_at = 0;
        }
        /* rebind to a new bitmap: everything composed again */
        {
            static fake_screen s2;
            memset(&s2, 0, sizeof s2);
            s2.fmt = OLC_FMT_XRGB32;
            s2.bpr = SW * 4;
            CHECK(olc_scene_rebind(sc, &s2, SW, SH) == 0);
            CHECK(olc_compose(sc, NULL) == 0);
            memcpy(&got, s2.px + 2 * s2.bpr + 2 * 4, 4);
            CHECK(got == 0x00445566);
            s2.fmt = 99;
            CHECK(olc_scene_rebind(sc, &s2, SW, SH) == -1);
            /* a bitmap of another format: refused, and the scene stays on s2 as it was */
            {
                static fake_screen s3;
                memset(&s3, 0, sizeof s3);
                s3.fmt = OLC_FMT_CLUT8;
                s3.bpr = SW;
                s2.fmt = OLC_FMT_XRGB32;
                CHECK(olc_scene_rebind(sc, &s3, SW, SH) == -1);
                olc_layer_restack(a, NULL);
                CHECK(olc_compose(sc, NULL) == 0);
                memcpy(&got, s2.px + 2 * s2.bpr + 2 * 4, 4);
                CHECK(got == 0x00112233);
                CHECK(s3.px[2 * s3.bpr + 2] == 0);
            }
        }
    }
    olc_scene_free(sc);
}

static void test_accel_rules(void)
{
    static fake_screen scr;
    olc_scene *sc;
    olc_layer *a;
    olc_box at = { 0, 0, 31, 31 }, d = { 4, 4, 5, 5 };
    long before;
    printf("the back end\n");
    fake_init();
    CHECK(olc_accel_attach(&fk.ops) == 0);
    CHECK(olc_accel_attach(&fk.ops) == -1);                 /* one at a time */
    olc_accel_detach();
    fake_init();
    fk.ops.version = 2;
    CHECK(olc_accel_attach(&fk.ops) == -1);                 /* a version the core doesn't know */
    fake_init();
    CHECK(olc_accel_attach(&fk.ops) == 0);
    memset(&scr, 0, sizeof scr);
    scr.fmt = OLC_FMT_RGB16;
    scr.bpr = SW * 2;
    sc = olc_scene_new(&scr, SW, SH, OLC_SCENE_COMPOSITED);
    a = olc_layer_new(sc, &at, NULL);
    CHECK(olc_compose(sc, NULL) == 0);
    CHECK(fk.begins == 1 && fk.submits == 1 && fk.uploaded == 32 * 32);      /* new: all of it */
    before = fk.uploaded;
    olc_surface_damage(olc_layer_surface(a), &d);
    CHECK(olc_compose(sc, NULL) == 0);
    CHECK(fk.begins == 2 && fk.submits == 2 && fk.uploaded - before == 4);   /* then only what changed */
    CHECK(olc_compose(sc, NULL) == 0 && fk.begins == 2);                     /* nothing changed: no batch */
    olc_layer_look(a, OLC_LOOK_HIDDEN, 1);
    olc_surface_damage(olc_layer_surface(a), &d);           /* drawn while hidden */
    CHECK(olc_compose(sc, NULL) == 0);
    before = fk.uploaded;
    olc_layer_look(a, OLC_LOOK_HIDDEN, 0);
    CHECK(olc_compose(sc, NULL) == 0);
    CHECK(fk.uploaded - before == 32 * 32);                 /* stale: all of it again */
    olc_accel_detach();                                     /* slots are the back end's to drop */
    CHECK(fk.detaches == 1 && fk.nt == 0);
    olc_layer_free(a);
    CHECK(fk.retires == 0);                                 /* no retire to a detached back end */
    olc_scene_free(sc);
}

int main(void)
{
    olc_init();
    olc__set_screen_lookup(lookup);
    test_scenes();
    test_accel_rules();
    printf("CPU compose against the reference\n");
    run(OLC_FMT_XRGB32, 0, 600);
    run(OLC_FMT_RGB16, 0, 600);
    run(OLC_FMT_CLUT8, 0, 400);
    printf("through a back end, against the reference\n");
    run(OLC_FMT_XRGB32, 1, 600);
    run(OLC_FMT_RGB16, 1, 600);
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
