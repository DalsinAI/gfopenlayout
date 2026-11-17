/*
 * The height of an outline face's capitals in whole rows (library 23,
 * OLUI_AMIGA_TEXT_CAPS): what OLUI_AmigaTextWidth answers so a title is
 * centred in its bar by its capitals.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "olui_amiga_text_cap.h"

#include <stdio.h>

static int failures;

#define CHECK(x) do { if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); ++failures; \
} } while (0)

/* a face's capital height and x-height at px, 26.6, from its share of the em in thousandths */
static long at(int px, int thousandths)
{
    return ((long)px * 64 * thousandths + 500) / 1000;
}

/* the x-height olui_amiga_text scales to: the nearest whole pixel, at least one */
static long target(long xh)
{
    long t = (xh + 32) & ~63L;
    return t < 64 ? 64 : t;
}

static int rows(int px, int cap_th, int xh_th)
{
    long xh = at(px, xh_th);
    return olui_aa_cap_rows(at(px, cap_th), xh, target(xh), px);
}

int main(void)
{
    int px;
    /* DejaVu Sans (capitals 0.729 of the em, x-height 0.547): the generic sans-serif a bitmap screen font's
       titles are drawn in. At 13 px the x-height is 7.1, drawn 7 tall, and the capitals 9.3 of 9.5: 9 rows,
       as counted on a lab screen (16 November 2026, rows 48 to 56 under a baseline at 57) */
    CHECK(rows(13, 729, 547) == 9);
    CHECK(rows(11, 729, 547) == 8);
    CHECK(rows(16, 729, 547) == 12);
    CHECK(rows(24, 729, 547) == 17);
    /* a serif (capitals 0.662, x-height 0.450) and a face with tall capitals */
    CHECK(rows(13, 662, 450) == 9);
    CHECK(rows(13, 760, 547) == 10);
    /* the hinting's scale is followed: an x-height of 6.4 drawn 6 tall takes the capitals down with it */
    CHECK(olui_aa_cap_rows(9 * 64, 6 * 64 + 26, 6 * 64, 13) == 8);
    CHECK(olui_aa_cap_rows(9 * 64, 6 * 64 + 38, 7 * 64, 13) == 10);
    /* no x-height: the capital height as it is */
    CHECK(olui_aa_cap_rows(9 * 64 + 20, 0, 64, 13) == 9);
    CHECK(olui_aa_cap_rows(9 * 64 + 40, 0, 0, 13) == 10);
    /* no capital height: 23/32 of the size, never under a row */
    CHECK(olui_aa_cap_rows(0, 7 * 64, 7 * 64, 13) == 9);
    CHECK(olui_aa_cap_rows(-5, 0, 0, 32) == 23);
    CHECK(olui_aa_cap_rows(0, 0, 0, 1) == 1);
    CHECK(olui_aa_cap_rows(10, 64, 64, 4) == 1);
    /* capitals grow with the size and stay under it */
    for (px = 6; px < 255; ++px) {
        CHECK(rows(px, 729, 547) <= rows(px + 1, 729, 547) + 1);
        CHECK(rows(px, 729, 547) < px);
    }
    printf("test_text_cap: %s\n", failures ? "FAIL" : "ok");
    return failures ? 1 : 0;
}
