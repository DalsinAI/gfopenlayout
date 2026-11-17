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
    int i;

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

    {
        /* a display like acscreen: it takes the picture asked for last at each vertical blank (the flip
         * begins), and says so only at the next one (the flip has happened). Frames come `rate` to a blank. */
        unsigned long asked[3], n;
        int rate, tick, glass, bringing, want, t, bad, waits, frames;
        asked[0] = asked[1] = asked[2] = 0;
        CHECK("three pictures, none asked: the first", opr_pick(3, 0, asked) == 0);
        asked[0] = 1;
        CHECK("the display still on the screen's own bitmap: never the one asked for", opr_pick(3, 0, asked) == 1);
        asked[1] = 2; asked[2] = 3;
        CHECK("all three asked for and none shown yet: the frame waits", opr_pick(3, 0, asked) == -1);
        CHECK("the display on the first: the two asked for after it may be coming, the frame waits", opr_pick(3, 1u << 0, asked) == -1);
        CHECK("the display on the second: the first is free, the third is not", opr_pick(3, 1u << 1, asked) == 0);
        CHECK("the display on the third: the one asked for longest ago", opr_pick(3, 1u << 2, asked) == 0);
        asked[0] = 4;
        CHECK("and then the next oldest", opr_pick(3, 1u << 2, asked) == 1);
        asked[0] = 1; asked[1] = 2;
        CHECK("two pictures, the display on the first, the second asked for: the frame waits", opr_pick(2, 1u << 0, asked) == -1);
        CHECK("two pictures, the display on the second: the first", opr_pick(2, 1u << 1, asked) == 0);
        for (rate = 1, bad = 0, waits = 0, frames = 0; rate <= 5; rate++) {
            asked[0] = asked[1] = asked[2] = 0;
            n = 0; glass = -1; bringing = -1; want = -1;
            for (tick = 0; tick < 600; tick++) {
                if (tick % rate == 0) {                 /* a vertical blank */
                    if (bringing >= 0) { glass = bringing; bringing = -1; }
                    if (want >= 0 && want != glass) bringing = want;
                }
                t = opr_pick(3, glass < 0 ? 0 : 1u << glass, asked);
                if (t < 0) { waits++; continue; }
                if (t == glass || t == bringing) bad++;  /* written into what is on the glass, or on its way */
                asked[t] = ++n;
                want = t;
                frames++;
            }
        }
        CHECK("frames faster than the display never go into a picture it shows or is bringing", bad == 0);
        CHECK("and frames keep coming: at least one to each vertical blank", frames >= 600 + 300 + 200 + 150 + 120 - 15 && waits > 0);
    }
    printf("%d/%d present cases pass\n", cases - fails, cases);
    return fails != 0;
}
