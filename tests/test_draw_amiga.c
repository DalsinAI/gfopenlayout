/* DrawAmigaTest: amiga/oldraw_amiga.c on a real RastPort (run on an Amiga or in a lab; prints "ok" or what failed).
 * A layerless BitMap's RastPort (a program's own off-screen bitmap, as GFOpenTitle's bar slots are) gets oldraw's
 * own layer: making it and freeing it must leave what the owner drew there untouched, and oldraw_fill must draw
 * only its rectangle.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <stdio.h>

#include "gfopenlayout_draw.h"

#define W 48
#define H 16
#define BG 5                            /* what the owner painted first (not a pen the fill below gets) */

static int checks, failed;
#define CHECK(x) do { checks++; if (!(x)) { failed++; printf("FAILED line %d: %s\n", __LINE__, #x); } } while (0)

/* how many pixels of the rectangle are pen */
static int count(struct RastPort *rp, int x0, int y0, int w, int h, LONG pen)
{
    int x, y, n = 0;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++) n += (LONG)ReadPixel(rp, x, y) == pen;
    return n;
}

int main(void)
{
    struct BitMap *bm = AllocBitMap(W, H, 8, BMF_CLEAR, NULL);
    struct RastPort rp;
    oldraw *d;
    oldraw_rect r;
    LONG fill;
    if (!bm) { printf("no bitmap\n"); return 20; }
    InitRastPort(&rp);
    rp.BitMap = bm;
    SetAPen(&rp, BG);
    RectFill(&rp, 0, 0, W - 1, H - 1);
    CHECK(count(&rp, 0, 0, W, H, BG) == W * H);

    /* made and freed: nothing changes */
    d = oldraw_new_rastport(&rp, NULL, W, H);
    CHECK(d != NULL);
    CHECK(count(&rp, 0, 0, W, H, BG) == W * H);     /* making it cleared the bitmap before the fix */
    oldraw_free(d);
    CHECK(count(&rp, 0, 0, W, H, BG) == W * H);     /* and so did freeing it */

    /* a fill: its rectangle only, and still there after the oldraw is freed */
    d = oldraw_new_rastport(&rp, NULL, W, H);
    CHECK(d != NULL);
    if (d) {
        r.x = 4; r.y = 2; r.w = 10; r.h = 5;
        oldraw_fill(d, &r, OLDRAW_PEN(OLDRAW_PEN_SHINE));
        oldraw_free(d);
    }
    fill = (LONG)ReadPixel(&rp, 4, 2);
    CHECK(fill != BG);
    CHECK(count(&rp, 4, 2, 10, 5, fill) == 50);
    CHECK(count(&rp, 0, 0, W, H, BG) == W * H - 50);

    FreeBitMap(bm);
    printf("%s   %d checks%s\n", failed ? "FAIL" : "ok", checks, failed ? ", some failed" : " (oldraw on a layerless RastPort)");
    return failed ? 10 : 0;
}
