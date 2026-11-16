/* GFOpenLayout's ROM core, step 1 (core/olc_core.c): olc_version, olc_init, regions and CPU surfaces.
 * Regions are checked against a pixel model: after every call each pixel is in the region exactly when the model
 * says so, no two boxes overlap and none is empty. Running out of memory is checked at every allocation a call
 * makes: the region must be exactly as it was. Built with OLC_TEST_HOOKS for that.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "olc_core_internal.h"
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(x) do { checks++; if (!(x)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

extern long olc_test_fail_at;

#define W 48
#define H 40
typedef unsigned char model[H][W];

static uint32_t seed = 12345;
static int32_t rnd(int32_t n)
{
    seed = seed * 1103515245u + 12345u;
    return (int32_t)((seed >> 8) % (uint32_t)n);
}

/* A box that may stick out of the model's area, or be empty, now and then */
static olc_box rnd_box(void)
{
    olc_box b;
    b.x0 = rnd(W + 8) - 4;
    b.y0 = rnd(H + 8) - 4;
    b.x1 = b.x0 + rnd(W / 2) - (rnd(12) == 0 ? W : 0);
    b.y1 = b.y0 + rnd(H / 2);
    return b;
}

static void model_box(model m, const olc_box *b, int how)    /* 0 or, 1 and, 2 sub */
{
    int x, y;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            int in = x >= b->x0 && x <= b->x1 && y >= b->y0 && y <= b->y1;
            if (how == 0 && in) m[y][x] = 1;
            if (how == 1 && !in) m[y][x] = 0;
            if (how == 2 && in) m[y][x] = 0;
        }
    }
}

/* 1 when r is exactly m, its boxes non-empty and apart, and inside the model's area */
static int region_is(const olc_region *r, model m)
{
    static model seen;
    int32_t i, n = olc_region_count(r);
    olc_box b;
    int x, y;
    memset(seen, 0, sizeof seen);
    for (i = 0; i < n; i++) {
        if (!olc_region_box(r, i, &b)) return 0;
        if (b.x1 < b.x0 || b.y1 < b.y0) return 0;
        if (b.x0 < 0 || b.y0 < 0 || b.x1 >= W || b.y1 >= H) return 0;   /* every op is cut to the area */
        for (y = b.y0; y <= b.y1; y++)
            for (x = b.x0; x <= b.x1; x++) {
                if (seen[y][x]) return 0;
                seen[y][x] = 1;
            }
    }
    return memcmp(seen, m, sizeof seen) == 0;
}

static int same_boxes(const olc_region *a, const olc_region *b)
{
    int32_t i;
    olc_box x, y;
    if (olc_region_count(a) != olc_region_count(b)) return 0;
    for (i = 0; i < olc_region_count(a); i++) {
        olc_region_box(a, i, &x);
        olc_region_box(b, i, &y);
        if (memcmp(&x, &y, sizeof x)) return 0;
    }
    return 1;
}

static void test_start(void)
{
    printf("start\n");
    CHECK(olc_version() == 0x00010000UL);
    CHECK(!olc__ready());
    CHECK(olc_init() == 1);
    CHECK(olc__ready());
}

static void test_region_model(void)
{
    static model m, m2;
    olc_region *r = olc_region_new(), *o = olc_region_new();
    int i, bad = 0, most = 0;
    printf("regions against a pixel model\n");
    CHECK(r && o);
    if (!r || !o) return;
    CHECK(olc_region_count(r) == 0);
    memset(m, 0, sizeof m);
    for (i = 0; i < 4000; i++) {
        olc_box b = rnd_box(), c;
        int how = rnd(10);
        /* what is added is cut to the model's area, so the region stays checkable pixel by pixel */
        if (how < 6) {
            c = b;
            if (c.x0 < 0) c.x0 = 0;
            if (c.y0 < 0) c.y0 = 0;
            if (c.x1 > W - 1) c.x1 = W - 1;
            if (c.y1 > H - 1) c.y1 = H - 1;
            if (olc_region_or_box(r, &c) != 0) bad++;
            model_box(m, &c, 0);
        } else if (how < 8) {
            if (olc_region_sub_box(r, &b) != 0) bad++;
            model_box(m, &b, 2);
        } else if (how < 9) {
            /* a band wide enough to keep things */
            b.x0 = rnd(W / 2); b.y0 = rnd(H / 2); b.x1 = b.x0 + W / 2; b.y1 = b.y0 + H / 2;
            if (olc_region_and_box(r, &b) != 0) bad++;
            model_box(m, &b, 1);
        } else {
            /* or with another region built the same way */
            int k;
            olc_region_clear(o);
            memset(m2, 0, sizeof m2);
            for (k = 0; k < 3; k++) {
                c = rnd_box();
                if (c.x0 < 0) c.x0 = 0;
                if (c.y0 < 0) c.y0 = 0;
                if (c.x1 > W - 1) c.x1 = W - 1;
                if (c.y1 > H - 1) c.y1 = H - 1;
                olc_region_or_box(o, &c);
                model_box(m2, &c, 0);
            }
            if (olc_region_or(r, o) != 0) bad++;
            {
                int x, y;
                for (y = 0; y < H; y++)
                    for (x = 0; x < W; x++) m[y][x] |= m2[y][x];
            }
        }
        if (!region_is(r, m)) {
            if (!bad) printf("  step %d (op %d): region differs from the model\n", i, how);
            bad++;
        }
        if (olc_region_count(r) > most) most = olc_region_count(r);
        if (rnd(200) == 0) {
            olc_region_clear(r);
            memset(m, 0, sizeof m);
        }
    }
    CHECK(bad == 0);
    /* joining keeps it small: the 48x40 area can't need more than a box per row and run */
    printf("  most boxes at once: %d\n", most);
    CHECK(most < W * H / 4);
    olc_region_free(o);
    olc_region_free(r);
}

static void test_region_calls(void)
{
    olc_region *r = olc_region_new(), *c = olc_region_new();
    olc_box b = { 2, 3, 9, 7 }, e = { 5, 5, 4, 9 }, got;
    printf("region calls\n");
    if (!r || !c) { CHECK(0); return; }
    CHECK(olc_region_or_box(r, &e) == 0 && olc_region_count(r) == 0);     /* empty adds nothing */
    CHECK(olc_region_bounds(r, &got) == 0);
    CHECK(olc_region_or_box(r, &b) == 0 && olc_region_count(r) == 1);
    CHECK(olc_region_or_box(r, &b) == 0 && olc_region_count(r) == 1);     /* the same box again */
    CHECK(olc_region_box(r, 0, &got) == 1 && !memcmp(&got, &b, sizeof b));
    CHECK(olc_region_box(r, 1, &got) == 0 && olc_region_box(r, -1, &got) == 0);
    {   /* a box right beside it joins it */
        olc_box n = { 10, 3, 14, 7 };
        CHECK(olc_region_or_box(r, &n) == 0 && olc_region_count(r) == 1);
        CHECK(olc_region_bounds(r, &got) == 1 && got.x0 == 2 && got.y0 == 3 && got.x1 == 14 && got.y1 == 7);
    }
    {   /* a hole in the middle: four boxes round it, or fewer after joining, and the bounds unchanged */
        olc_box h = { 6, 4, 8, 5 };
        CHECK(olc_region_sub_box(r, &h) == 0 && olc_region_count(r) >= 2 && olc_region_count(r) <= 4);
        CHECK(olc_region_bounds(r, &got) == 1 && got.x0 == 2 && got.x1 == 14);
    }
    CHECK(olc_region_copy(c, r) == 0 && same_boxes(c, r));
    CHECK(olc_region_copy(c, c) == 0 && same_boxes(c, r));
    CHECK(olc_region_or(r, r) == 0 && same_boxes(c, r));
    olc_region_clear(r);
    CHECK(olc_region_count(r) == 0);
    CHECK(olc_region_copy(c, r) == 0 && olc_region_count(c) == 0);
    CHECK(olc_region_or_box(NULL, &b) == -1 && olc_region_or_box(r, NULL) == -1);
    CHECK(olc_region_count(NULL) == 0);
    olc_region_free(NULL);
    olc_region_free(c);
    olc_region_free(r);
}

/* Every allocation a call makes fails in turn: each failing call answers -1 and leaves the region as it was */
static void test_region_oom(void)
{
    olc_region *r = olc_region_new(), *keep = olc_region_new(), *o = olc_region_new();
    int i, op, bad = 0, failed = 0;
    printf("regions when memory runs out\n");
    if (!r || !keep || !o) { CHECK(0); return; }
    for (i = 0; i < 12; i++) {
        olc_box b = { i * 3, i, i * 3 + 5, i + 9 };
        olc_region_or_box(r, &b);
        b.x0 += 20; b.x1 += 20;
        olc_region_or_box(o, &b);
    }
    for (op = 0; op < 4; op++) {
        long at;
        for (at = 1; at < 40; at++) {
            olc_box b = { 4, 2, 30, 6 };
            int rc;
            olc_region_copy(keep, r);
            olc_test_fail_at = at;
            switch (op) {
            case 0: rc = olc_region_or_box(r, &b); break;
            case 1: rc = olc_region_sub_box(r, &b); break;
            case 2: rc = olc_region_or(r, o); break;
            default: rc = olc_region_copy(r, o); break;
            }
            if (olc_test_fail_at > 0) {         /* the call made fewer allocations than at: it worked */
                olc_test_fail_at = 0;
                olc_region_copy(r, keep);       /* back to the same start for the next at */
                break;
            }
            failed++;
            if (rc != -1 || !same_boxes(r, keep)) bad++;
        }
    }
    printf("  %d failed allocations, each left the region as it was\n", failed);
    CHECK(failed > 4);
    CHECK(bad == 0);
    olc_test_fail_at = 0;
    olc_region_free(o);
    olc_region_free(keep);
    olc_region_free(r);
}

static void test_surfaces(void)
{
    static const struct { uint32_t fmt; int32_t w, bpr; } f[] = {
        { OLC_FMT_CLUT8, 1, 16 }, { OLC_FMT_CLUT8, 17, 32 }, { OLC_FMT_RGB16, 8, 16 }, { OLC_FMT_RGB16, 9, 32 },
        { OLC_FMT_XRGB32, 4, 16 }, { OLC_FMT_ARGB32, 5, 32 }, { OLC_FMT_XRGB32, 1920, 7680 }
    };
    unsigned i;
    printf("surfaces\n");
    for (i = 0; i < sizeof f / sizeof f[0]; i++) {
        olc_surface *s = olc_surface_new(f[i].w, 3, f[i].fmt, OLC_MEM_ANY, NULL);
        int32_t bpr = 0, w, h;
        uint32_t fmt;
        unsigned char *p;
        int32_t k, zero = 1;
        CHECK(s != NULL);
        if (!s) continue;
        p = (unsigned char *)olc_surface_pixels(s, &bpr);
        CHECK(p != NULL && bpr == f[i].bpr);
        for (k = 0; k < bpr * 3; k++) if (p[k]) zero = 0;
        CHECK(zero);                                        /* cleared */
        p[bpr * 3 - 1] = 0xAA;                              /* the last byte is ours to write */
        olc_surface_size(s, &w, &h, &fmt);
        CHECK(w == f[i].w && h == 3 && fmt == f[i].fmt);
        CHECK(s->where == OLC_MEM_FAST);                    /* ANY is Fast until the RTG step */
        CHECK(olc_surface_rastport(s) == NULL);
        olc_surface_free(s);
    }
    {
        olc_surface *s = olc_surface_new(4, 4, OLC_FMT_RGB16, OLC_MEM_HOST, NULL);
#ifdef OLC_AMIGA
        CHECK(s == NULL);                                   /* host memory on the Amiga comes with a later step */
#else
        CHECK(s && s->where == OLC_MEM_HOST);
#endif
        olc_surface_free(s);
    }
    CHECK(olc_surface_new(4, 4, OLC_FMT_RGB16, OLC_MEM_RTG, NULL) == NULL);   /* not until the RTG step */
    CHECK(olc_surface_new(0, 4, OLC_FMT_RGB16, OLC_MEM_ANY, NULL) == NULL);
    CHECK(olc_surface_new(4, -1, OLC_FMT_RGB16, OLC_MEM_ANY, NULL) == NULL);
    CHECK(olc_surface_new(4, 4, 0, OLC_MEM_ANY, NULL) == NULL);
    CHECK(olc_surface_new(4, 4, 99, OLC_MEM_ANY, NULL) == NULL);
    CHECK(olc_surface_new(16385, 4, OLC_FMT_CLUT8, OLC_MEM_ANY, NULL) == NULL);
    CHECK(olc_surface_pixels(NULL, NULL) == NULL);
    olc_surface_free(NULL);
    {   /* out of memory at each of its three allocations: NULL, nothing kept */
        long at;
        for (at = 1; at <= 3; at++) {
            olc_test_fail_at = at;
            CHECK(olc_surface_new(8, 8, OLC_FMT_XRGB32, OLC_MEM_ANY, NULL) == NULL);
        }
        olc_test_fail_at = 0;
    }
}

static void test_damage(void)
{
    olc_surface *s = olc_surface_new(100, 50, OLC_FMT_XRGB32, OLC_MEM_ANY, NULL);
    const olc_region *d;
    olc_box b, got;
    printf("surface damage\n");
    if (!s) { CHECK(0); return; }
    d = olc__surface_damage(s);
    CHECK(olc_region_count(d) == 0);
    b.x0 = -10; b.y0 = 40; b.x1 = 5; b.y1 = 80;                       /* cut to the surface */
    olc_surface_damage(s, &b);
    CHECK(olc_region_count(d) == 1 && olc_region_box(d, 0, &got) && got.x0 == 0 && got.y0 == 40 &&
          got.x1 == 5 && got.y1 == 49);
    b.x0 = 200; b.y0 = 0; b.x1 = 300; b.y1 = 10;                      /* wholly outside: nothing */
    olc_surface_damage(s, &b);
    CHECK(olc_region_count(d) == 1);
    b.x0 = 50; b.y0 = 0; b.x1 = 60; b.y1 = 5;
    olc_test_fail_at = 1;                                             /* can't add it: the whole surface */
    olc_surface_damage(s, &b);
    olc_test_fail_at = 0;
    CHECK(olc_region_count(d) == 1 && olc_region_box(d, 0, &got) && got.x0 == 0 && got.y0 == 0 &&
          got.x1 == 99 && got.y1 == 49);
    olc__surface_damage_clear(s);
    CHECK(olc_region_count(d) == 0);
    olc_test_fail_at = 1;                                             /* even with nothing there yet */
    olc_surface_damage(s, &b);
    olc_test_fail_at = 0;
    CHECK(olc_region_count(d) == 1 && olc_region_bounds(d, &got) && got.x1 == 99 && got.y1 == 49);
    olc__surface_damage_clear(s);
    olc_surface_damage(s, &b);
    b.x0 = 61; b.x1 = 70;                                             /* beside it: one box */
    olc_surface_damage(s, &b);
    CHECK(olc_region_count(d) == 1 && olc_region_bounds(d, &got) && got.x0 == 50 && got.x1 == 70);
    olc_surface_damage(NULL, &b);
    olc_surface_damage(s, NULL);
    olc_surface_free(s);
}

int main(void)
{
    test_start();
    test_region_calls();
    test_region_model();
    test_region_oom();
    test_surfaces();
    test_damage();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
