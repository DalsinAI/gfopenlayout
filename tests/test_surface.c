/* GFOpenLayout: the off-screen surface's box cut (include/gfopenlayout_surface.h).
 * Copyright (c) 2026 AmiCradle Team
 */
#include "gfopenlayout_surface.h"
#include <stdio.h>

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static int cut(int bw, int bh, int x, int y, int w, int h, int ex, int ey, int ew, int eh)
{
    int r = olsurface_clip(bw, bh, &x, &y, &w, &h);
    return r && x == ex && y == ey && w == ew && h == eh;
}

int main(void)
{
    int x, y, w, h;
    CHECK(cut(640, 480, 10, 20, 100, 50, 10, 20, 100, 50));      /* inside: as given */
    CHECK(cut(640, 480, 0, 0, 640, 480, 0, 0, 640, 480));        /* the whole picture */
    CHECK(cut(640, 480, -5, -7, 100, 50, 0, 0, 95, 43));         /* off the left and top */
    CHECK(cut(640, 480, 600, 460, 100, 50, 600, 460, 40, 20));   /* off the right and bottom */
    CHECK(cut(640, 480, -10, -10, 700, 500, 0, 0, 640, 480));    /* larger all round */
    x = 640; y = 0; w = 10; h = 10;
    CHECK(!olsurface_clip(640, 480, &x, &y, &w, &h));            /* wholly right of it */
    x = 0; y = 480; w = 10; h = 10;
    CHECK(!olsurface_clip(640, 480, &x, &y, &w, &h));            /* wholly below */
    x = -20; y = 0; w = 20; h = 10;
    CHECK(!olsurface_clip(640, 480, &x, &y, &w, &h));            /* wholly left */
    x = 5; y = 5; w = 0; h = 10;
    CHECK(!olsurface_clip(640, 480, &x, &y, &w, &h));            /* no width */
    x = 5; y = 5; w = 10; h = -3;
    CHECK(!olsurface_clip(640, 480, &x, &y, &w, &h));            /* a height below nothing */
    x = 0; y = 0; w = 10; h = 10;
    CHECK(!olsurface_clip(0, 0, &x, &y, &w, &h));                /* an empty surface */
    printf("%s  %d checks (surface)\n", fails ? "FAIL" : "ok ", checks);
    return fails != 0;
}
