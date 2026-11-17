/* SPDX-License-Identifier: MIT */
/* OpenLayers' composited mode: finished frames only (47.110). Copyright (c)
 * 2026 AmiCradle Team. MIT.
 *
 * A display that shows video RAM itself shows the screen's bitmap while a
 * compose is drawing into it. With GFOpenRTG 0.28 (ORTG_ShowInstead, on a
 * board of acrtg-v11) the compositor keeps the screen's bitmap for
 * assembling, as before, and copies what each compose changed into one of
 * a few pictures of the screen's size, which the board then shows in the
 * screen's place once that copy has run: the display goes from one whole
 * frame to the next.
 *
 * This is the part with no Amiga in it, for the host test
 * (tests/host-comp/test_present.c): what each picture still lacks, and
 * which picture the next frame goes into. struct lc_rect is layers_core.h's.
 */
#ifndef LAYERS_PRESENT_H
#define LAYERS_PRESENT_H

#define OPR_PICS 3              /* pictures at most; two are enough to work, three never wait for the display */
#define OPR_RECTS 8             /* rectangles kept for what a picture lacks, before they become their bounding box */

/* What a picture lacks of the screen's bitmap: every rectangle composed
 * since it was last filled. */
struct opr_miss {
    struct lc_rect r[OPR_RECTS];
    unsigned n;
};

static inline int opr_inside(const struct lc_rect *a, const struct lc_rect *b)
{
    return a->x0 >= b->x0 && a->y0 >= b->y0 && a->x1 <= b->x1 && a->y1 <= b->y1;
}

static inline void opr_grow(struct lc_rect *b, const struct lc_rect *r)
{
    if (r->x0 < b->x0) b->x0 = r->x0;
    if (r->y0 < b->y0) b->y0 = r->y0;
    if (r->x1 > b->x1) b->x1 = r->x1;
    if (r->y1 > b->y1) b->y1 = r->y1;
}

/* r (not empty) joins what m lacks. One inside another counts once; with
 * the list full, everything becomes its bounding box (more is copied than
 * changed, never less). */
static inline void opr_add(struct opr_miss *m, const struct lc_rect *r)
{
    unsigned i, k;
    if (r->x0 > r->x1 || r->y0 > r->y1) return;
    for (i = 0; i < m->n; i++)
        if (opr_inside(r, &m->r[i])) return;
    for (i = k = 0; i < m->n; i++)              /* those inside r go */
        if (!opr_inside(&m->r[i], r)) m->r[k++] = m->r[i];
    m->n = k;
    if (m->n == OPR_RECTS) {
        for (i = 1; i < m->n; i++) opr_grow(&m->r[0], &m->r[i]);
        opr_grow(&m->r[0], r);
        m->n = 1;
        return;
    }
    m->r[m->n++] = *r;
}

/* The picture the next frame goes into, of n: not one the display shows
 * now (shown: a bit each), and not one asked for after the one it shows
 * (asked: each picture's place in the order of the askings, 0 never). The
 * display says a picture is shown only once its flip has happened, a
 * vertical blank after it took it, so any of those asked for since may be
 * the one it is bringing: writing into it would be seen. Of the rest, the
 * one asked for longest ago. -1: none is free yet: the frame waits for the
 * display's word. */
static inline int opr_pick(int n, unsigned shown, const unsigned long *asked)
{
    unsigned long s = 0, best = ~0UL;
    int p, t = -1;
    for (p = 0; p < n; p++)
        if ((shown >> p & 1) && asked[p] > s) s = asked[p];
    for (p = 0; p < n; p++) {
        if ((shown >> p & 1) || asked[p] > s) continue;
        if (asked[p] < best) {
            best = asked[p];
            t = p;
        }
    }
    return t;
}

/* How many times in a row no picture was free while the display showed
 * none of them before the askings are forgotten: the board stopped showing
 * pictures (the screen went behind another and came back), and nothing
 * will free them. Tries come OC_SETTLE apart or more: several frames. */
#define OPR_GIVE_UP 100

#endif
