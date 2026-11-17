/* SPDX-License-Identifier: MIT */
/* library/layers/layers_present.h: what a picture lacks, and which picture
 * the next finished frame goes into. Copyright (c) 2026 AmiCradle Team. MIT. */
#include <stdio.h>
#include <stdint.h>
#include "layers_core.h"
#include "layers_present.h"

static int cases, fails;
#define CHECK(what, ok) do { cases++; if (!(ok)) { fails++; printf("FAIL: %s\n", what); } } while (0)
static struct lc_rect R(int x0, int y0, int x1, int y1) { struct lc_rect r = { x0, y0, x1, y1 }; return r; }
static int is(const struct lc_rect *r, int x0, int y0, int x1, int y1) { return r->x0 == x0 && r->y0 == y0 && r->x1 == x1 && r->y1 == y1; }

int main(void)
{
    struct opr_miss m = { { { 0, 0, 0, 0 } }, 0 };
    struct lc_rect r;
    int i, seq[6], last = -1;
    unsigned glass;

    r = R(10, 10, 19, 19); opr_add(&m, &r);
    CHECK("the first rectangle is kept", m.n == 1 && is(&m.r[0], 10, 10, 19, 19));
    r = R(12, 12, 15, 15); opr_add(&m, &r);
    CHECK("one inside it adds nothing", m.n == 1);
    r = R(100, 100, 199, 199); opr_add(&m, &r);
    CHECK("one apart is kept apart (nothing between them is copied)", m.n == 2 && is(&m.r[1], 100, 100, 199, 199));
    r = R(0, 0, 50, 50); opr_add(&m, &r);
    CHECK("one around an earlier one replaces it", m.n == 2 && is(&m.r[0], 100, 100, 199, 199) && is(&m.r[1], 0, 0, 50, 50));
    r = R(5, 5, 4, 9); opr_add(&m, &r);
    CHECK("an empty one is nothing", m.n == 2);
    m.n = 0;
    for (i = 0; i < OPR_RECTS; i++) { r = R(i * 100, 0, i * 100 + 9, 9); opr_add(&m, &r); }
    CHECK("the list fills", m.n == OPR_RECTS);
    r = R(20, 500, 29, 509); opr_add(&m, &r);
    CHECK("one more: all of it as its bounding box, nothing lost", m.n == 1 && is(&m.r[0], 0, 0, (OPR_RECTS - 1) * 100 + 9, 509));

    CHECK("three pictures, none shown, none asked: the second", opr_pick(3, 0, -1) == 1);
    CHECK("never the one asked for last", opr_pick(3, 0, 1) == 2 && opr_pick(3, 0, 2) == 0 && opr_pick(3, 0, 0) == 1);
    CHECK("never the one on the display", opr_pick(3, 1u << 2, 1) == 0 && opr_pick(3, 1u << 0, 2) == 1);
    CHECK("the display on the last asked: either of the others, in turn", opr_pick(3, 1u << 1, 1) == 2);
    /* a display that takes each picture at its vertical blank: frames faster than it never land on the glass */
    for (i = 0, glass = 0; i < 6; i++) {
        seq[i] = opr_pick(3, glass, last);
        if (i & 1) glass = 1u << seq[i];        /* the display takes every second one */
        last = seq[i];
    }
    for (i = 1, r.x0 = 1; i < 6; i++) if (seq[i] < 0 || seq[i] == seq[i - 1]) r.x0 = 0;
    CHECK("three pictures always have one free", r.x0 == 1);
    CHECK("two pictures: the other one while the display is not on it", opr_pick(2, 0, 0) == 1 && opr_pick(2, 1u << 0, 0) == 1);
    CHECK("two pictures, one on the display and the other asked for: the frame waits", opr_pick(2, 1u << 0, 1) == -1);
    CHECK("one picture is never free once asked for", opr_pick(1, 0, 0) == -1);
    printf("%d/%d present cases pass\n", cases - fails, cases);
    return fails != 0;
}
