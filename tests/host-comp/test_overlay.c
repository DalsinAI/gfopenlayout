/* SPDX-License-Identifier: MIT */
/* 47.111, GFOpenLayout Overlay, on the host: gfopengfx's layers_lib.c with
 * this repository's layers_comp.c built in, in this unit, so the statics are
 * reached. The compositor leaves a hole for an overlay's box (oc_hole) and
 * keeps its place record (oc_place) as the host reads it:
 *   1. the hole: the layer's own pixels are drawn everywhere but the box;
 *   2. nothing in front: shown, composited, one rect, the box on the screen;
 *   3. a window over its right half: what shows is the left half;
 *   4. the box reaching past the screen: cut to the screen;
 *   5. the layer hidden, then gone: not shown, no rects;
 *   6. seq even after every write, two more each time.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "layers_lib.c"

void Forbid(void) {}
void Permit(void) {}
APTR AllocMem(ULONG size, ULONG flags) { (void)flags; return calloc(1, size); }
void FreeMem(APTR p, ULONG size) { (void)size; free(p); }

static int fails, cases;
#define CHECK(what, ok) do { cases++; if (!(ok)) { fails++; printf("FAIL %s\n", what); } else printf("ok   %s\n", what); } while (0)

static void *t_alloc(void *ctx, uint32_t size) { (void)ctx; return malloc(size); }
static void t_free(void *ctx, void *p, uint32_t size) { (void)ctx; (void)size; free(p); }
static const struct lc_mem tmem = { t_alloc, t_free, 0 };

static long area(const struct lc_rects *v)
{
    long a = 0;
    for (ULONG k = 0; k < v->n; k++) a += (long)(v->r[k].x1 - v->r[k].x0 + 1) * (v->r[k].y1 - v->r[k].y0 + 1);
    return a;
}
static long place_area(const struct LayerOverlayPlace *p)
{
    long a = 0;
    for (int k = 0; k < p->nrects; k++) a += (long)(p->rects[k].MaxX - p->rects[k].MinX + 1) * (p->rects[k].MaxY - p->rects[k].MinY + 1);
    return a;
}
static void bounds(struct Layer *l, int x, int y, int w, int h)
{
    l->bounds.MinX = x; l->bounds.MinY = y; l->bounds.MaxX = x + w - 1; l->bounds.MaxY = y + h - 1;
}

int main(void)
{
    static struct OLayer game, front;
    static struct Layer_Info li;
    static struct ol_base olb;
    static struct OCB cb;
    static struct OComp comp;
    static struct LayerOverlayPlace place;
    static struct OOverlay ov;
    struct Layer *l = &game.l, *f = &front.l;
    struct lc_rects v;
    struct lc_rect all;
    ULONG seq;

    /* a 1920x1080 composited ARGB32 screen; the game's window at 100,50, 660x500; its picture's box at 10,20 in it, 640x480 */
    olb.cb = &cb;
    cb.olb = &olb;
    cb.comps = &comp;
    comp.li = &li;
    comp.saddr = 0x60001000; comp.sbpr = 7680; comp.w = 1920; comp.h = 1080; comp.depth = 32;
    li.top_layer = l;
    l->LayerInfo = &li;
    game.surf = (struct BitMap *)1;
    bounds(l, 100, 50, 660, 500);
    ov.layer = l;
    ov.place = &place;
    ov.box.MinX = 10; ov.box.MinY = 20; ov.box.MaxX = 10 + 640 - 1; ov.box.MaxY = 20 + 480 - 1;
    game.ovl = &ov;

    /* 1 */
    lc_init(&v, &tmem);
    all.x0 = 100; all.y0 = 50; all.x1 = 759; all.y1 = 549;
    lc_or_rect(&v, &all);
    oc_hole(l, &ov, &v);
    CHECK("the hole: the layer is drawn everywhere but the box", area(&v) == 660L * 500 - 640L * 480);
    lc_free(&v);

    /* 2 */
    oc_place(&olb, &ov);
    CHECK("nothing in front: shown and composited", place.flags == (OVPF_SHOWN | OVPF_COMPOSITED));
    CHECK("the screen: its memory, bytes a row, size and ARGB32",
          place.screen == (APTR)0x60001000 && place.bytes_per_row == 7680 && place.screen_width == 1920 && place.format == 2);
    CHECK("the box on the screen, and one rect, all of it",
          place.box.MinX == 110 && place.box.MinY == 70 && place.box.MaxX == 749 && place.box.MaxY == 549
          && place.nrects == 1 && place.rects[0].MinX == 110 && place.rects[0].MaxY == 549);
    seq = place.seq;
    CHECK("seq even after it is written", (seq & 1) == 0 && seq == 2);

    /* 3: another window in front, over the box's right half */
    li.top_layer = f;
    f->back = l;
    l->front = f;
    f->LayerInfo = &li;
    bounds(f, 430, 0, 600, 700);
    oc_place(&olb, &ov);
    CHECK("covered on the right: the left half shows, 320x480",
          place.flags & OVPF_SHOWN && place_area(&place) == 320L * 480 && place.rects[0].MaxX <= 429);
    CHECK("seq two more", place.seq == seq + 2);

    /* 4: the window moved so the box reaches past the screen's right edge */
    li.top_layer = l;
    l->front = 0;
    bounds(l, 1600, 50, 660, 500);
    oc_place(&olb, &ov);
    CHECK("past the screen's edge: cut to it (1610 to 1919)", place_area(&place) == 310L * 480 && place.box.MaxX == 1600 + 649);

    /* 5 */
    game.hidden = 1;
    oc_place(&olb, &ov);
    CHECK("hidden: not shown, no rects", place.flags == 0 && place.nrects == 0);
    game.hidden = 0;
    ov.layer = 0;
    oc_place(&olb, &ov);
    CHECK("gone: not shown, no rects", place.flags == 0 && place.nrects == 0 && (place.seq & 1) == 0);

    printf("%d/%d overlay cases pass\n", cases - fails, cases);
    return fails != 0;
}
