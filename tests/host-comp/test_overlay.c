/* SPDX-License-Identifier: MIT */
/* 47.111, GFOpenLayout Overlay: what the compositor's batch gets for a
 * layer's overlay (oc_build_overlay), on the host. gfopengfx's layers_lib.c
 * with this repository's layers_comp.c built in, in this unit, so the
 * statics are reached:
 *   1. at its own size, all of it showing: one COPY of the whole picture;
 *   2. the layer partly covered: a COPY of the part that shows only;
 *   3. scaled 2x into the box: a COMPOSITE whose source is the picture's
 *      own pixels for what shows;
 *   4. a translucent layer: a COMPOSITE at its opacity, at its own size;
 *   5. nothing of the box showing: nothing but the SURFACE.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include <stdio.h>
#include <string.h>
#include "layers_lib.c"
#ifndef OGPU_HDR_OP
#define OGPU_HDR_OP(w)    ((unsigned)((w) >> 16))
#define OGPU_HDR_WORDS(w) ((unsigned)((w) & 0xFFFFUL))
#endif

static int fails, cases;
#define CHECK(what, ok) do { cases++; if (!(ok)) { fails++; printf("FAIL %s\n", what); } else printf("ok   %s\n", what); } while (0)

static ULONG ovw[256];

/* the rectangle lists' memory, from the host (layers_lib.c's goes through exec) */
#include <stdlib.h>
static void *t_alloc(void *ctx, uint32_t size) { (void)ctx; return malloc(size); }
static void t_free(void *ctx, void *p, uint32_t size) { (void)ctx; (void)size; free(p); }
static const struct lc_mem tmem = { t_alloc, t_free, 0 };

static ULONG ov_batch(struct Layer *l, struct OOverlay *ov, const struct lc_rect *vis, int nvis, UBYTE a)
{
    struct oc_batch b;
    struct lc_rects v;
    int i;
    b.w = ovw; b.n = 0; b.cap = 256;
    lc_init(&v, &tmem);
    for (i = 0; i < nvis; i++) lc_or_rect(&v, &vis[i]);
    oc_build_overlay(&b, l, ov, &v, a);
    lc_free(&v);
    return b.n;
}

/* the n-th command after the SURFACE: its op, and its words */
static const ULONG *cmd(ULONG n, ULONG k, unsigned *op)
{
    ULONG at = 0, i = 0;
    while (at < n) {
        unsigned o = OGPU_HDR_OP(ovw[at]), w = OGPU_HDR_WORDS(ovw[at]);
        if (o != OGPU_OP_SURFACE) { if (i == k) { *op = o; return &ovw[at]; } i++; }
        at += w;
    }
    *op = 0;
    return 0;
}

int main(void)
{
    struct Layer l;
    struct OOverlay ov;
    struct lc_rect all, part[2];
    const ULONG *c;
    unsigned op;
    ULONG n;

    memset(&l, 0, sizeof l);
    l.bounds.MinX = 100; l.bounds.MinY = 50; l.bounds.MaxX = 100 + 660 - 1; l.bounds.MaxY = 50 + 500 - 1;
    memset(&ov, 0, sizeof ov);
    ov.layer = &l;
    ov.bm = (struct BitMap *)1;
    ov.addr = 0x60100000; ov.bpr = 640 * 4; ov.fmt = OGPU_FMT_ARGB32;
    ov.box.MinX = 10; ov.box.MinY = 20; ov.box.MaxX = 10 + 640 - 1; ov.box.MaxY = 20 + 480 - 1;
    ov.pw = 640; ov.ph = 480;
    all.x0 = l.bounds.MinX; all.y0 = l.bounds.MinY; all.x1 = l.bounds.MaxX; all.y1 = l.bounds.MaxY;

    /* 1 */
    n = ov_batch(&l, &ov, &all, 1, 255);
    c = cmd(n, 0, &op);
    CHECK("its own size: SURFACE first, the picture's address and size",
          OGPU_HDR_OP(ovw[0]) == OGPU_OP_SURFACE && ovw[1] == S_OVL && ovw[2] == 0x60100000 && ovw[4] == OGPU_XY(640, 480));
    CHECK("its own size: one COPY of all of it, from 0,0 to the box",
          c && op == OGPU_OP_COPY && c[1] == S_OVL && c[2] == OGPU_XY(0, 0) && c[3] == OGPU_XY(110, 70) && c[4] == OGPU_XY(640, 480)
          && !cmd(n, 1, &op));

    /* 2: a window over its right half and its top 100 rows: what shows is the left part below them */
    part[0].x0 = 100; part[0].y0 = 170; part[0].x1 = 429; part[0].y1 = 549;
    n = ov_batch(&l, &ov, part, 1, 255);
    c = cmd(n, 0, &op);
    CHECK("covered: a COPY of what shows only, from the picture's 0,100",
          c && op == OGPU_OP_COPY && c[2] == OGPU_XY(0, 100) && c[3] == OGPU_XY(110, 170) && c[4] == OGPU_XY(320, 380));

    /* 3: a 320x240 picture in the 640x480 box */
    ov.pw = 320; ov.ph = 240;
    n = ov_batch(&l, &ov, &all, 1, 255);
    c = cmd(n, 0, &op);
    CHECK("scaled 2x: a COMPOSITE from all of the picture to all of the box",
          c && op == OGPU_OP_COMPOSITE && c[2] == OGPU_XY(0, 0) && c[3] == OGPU_XY(320, 240) && c[4] == OGPU_XY(110, 70)
          && c[5] == OGPU_XY(640, 480) && c[6] == 255);
    n = ov_batch(&l, &ov, part, 1, 255);
    c = cmd(n, 0, &op);
    CHECK("scaled 2x and covered: the source is the picture's part, 160x190 from 0,50",
          c && op == OGPU_OP_COMPOSITE && c[2] == OGPU_XY(0, 50) && c[3] == OGPU_XY(160, 190) && c[5] == OGPU_XY(320, 380));

    /* 4 */
    ov.pw = 640; ov.ph = 480;
    n = ov_batch(&l, &ov, &all, 1, 128);
    c = cmd(n, 0, &op);
    CHECK("translucent: a COMPOSITE at the layer's opacity", c && op == OGPU_OP_COMPOSITE && c[6] == 128 && c[3] == OGPU_XY(640, 480));

    /* 5: only the layer's border shows (the box is all covered) */
    part[0].x0 = 100; part[0].y0 = 50; part[0].x1 = 109; part[0].y1 = 549;
    n = ov_batch(&l, &ov, part, 1, 255);
    CHECK("nothing of the box shows: no COPY or COMPOSITE", !cmd(n, 0, &op));

    printf("%d/%d overlay cases pass\n", cases - fails, cases);
    return fails != 0;
}
