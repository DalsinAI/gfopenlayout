/* GFOpenLayers' composited mode: the marks (library/layers/layers_mark.h)
 * against the drawing itself, under qemu-m68k, where the registers are
 * 32 bits as on the Amiga. For Draw, PolyDraw and DrawEllipse (GFOpenGfx's
 * core, library/core/gfx_rp.c) and ClearEOL and ClearScreen (GFOpenGfx's
 * text, library/text/text_core.cpp), random calls are made as a thunk
 * makes them (ocm_pre, the call, ocm_bounds, with junk in the registers'
 * upper words), and every pixel the call changed must be inside the mark:
 * the compositor composes only what is marked. The mark must also be the
 * box the call's geometry gives (cut to the layer), not more.
 * WritePixelLine8's and ScalePixelArray's marks are checked against the
 * row and the destination their arguments give.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include "gfx_rp.h"
#include "text_core.h"
#include "layers_mark.h"

#define W 128
#define BPR (W / 8)
#define H 64
#define DEPTH 3
static UBYTE planes[DEPTH][BPR * H], before[DEPTH][BPR * H];
static struct BitMap bm;
static struct RastPort rp;
static unsigned long checks, fails, marked, whole;
static ULONG seed = 0x4d4b;
static ULONG rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static LONG coord(LONG n) { return (LONG)(rnd() % (n + 80)) - 40; }   /* off the edges too */
static ULONG junk(LONG v) { return rnd() << 16 | (UWORD)v; }           /* a WORD with junk above */

static void fail(const char *what, const char *why)
{
    if (++fails < 20) printf("FAIL %s: %s\n", what, why);
}

/* the box (x0..x1, y0..y1) cut to the layer, or none */
static int cut(LONG x0, LONG y0, LONG x1, LONG y1, LONG sx, LONG sy, struct lc_rect *r)
{
    x0 -= sx; x1 -= sx; y0 -= sy; y1 -= sy;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > W - 1) x1 = W - 1;
    if (y1 > H - 1) y1 = H - 1;
    if (x0 > x1 || y0 > y1) return 0;
    r->x0 = x0; r->y0 = y0; r->x1 = x1; r->y1 = y1;
    return 1;
}

static void same(const char *what, int have, const struct lc_rect *r, int want, const struct lc_rect *w)
{
    checks++;
    if (have != want || (have && memcmp(r, w, sizeof *r))) fail(what, "the mark is not the call's box");
    if (have) marked += (ULONG)(r->x1 - r->x0 + 1) * (r->y1 - r->y0 + 1);
    whole += W * H;
}

/* every pixel changed since before[] is inside the mark */
static void inside(const char *what, int have, const struct lc_rect *r)
{
    int p, i, b;
    checks++;
    for (p = 0; p < DEPTH; p++)
        for (i = 0; i < BPR * H; i++) {
            UBYTE d = planes[p][i] ^ before[p][i];
            for (b = 0; d && b < 8; b++)
                if (d & (0x80 >> b)) {
                    LONG x = (i % BPR) * 8 + b, y = i / BPR;
                    if (!have || x < r->x0 || x > r->x1 || y < r->y0 || y > r->y1) {
                        fail(what, "a pixel drawn outside the mark");
                        return;
                    }
                }
        }
}

static void fresh_rp(void)
{
    static const UBYTE modes[] = { JAM1, JAM2, COMPLEMENT, COMPLEMENT | JAM2, JAM1 | INVERSVID };
    rp.FgPen = (BYTE)(rnd() & 7);
    rp.BgPen = (BYTE)(rnd() & 7);
    rp.DrawMode = (BYTE)modes[rnd() % 5];
    rp.LinePtrn = rnd() % 3 ? 0xFFFF : (UWORD)rnd();
    rp.linpatcnt = 15;
    rp.Flags = rnd() % 2 ? FRST_DOT : 0;
    rp.Mask = 0xFF;
    memcpy(before, planes, sizeof planes);
}

/* the text calls' drawing, onto the planes */
static LONG org_x, org_y;
static void t_pixel(void *ctx, struct RastPort *r, int32_t x, int32_t y, uint32_t pen, int op)
{
    (void)ctx; (void)r; (void)x; (void)y; (void)pen; (void)op;
}
static void t_fill(void *ctx, struct RastPort *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t pen)
{
    LONG x, y;
    int p;
    (void)ctx; (void)r;
    for (y = y0 - org_y; y <= y1 - org_y; y++)
        for (x = x0 - org_x; x <= x1 - org_x; x++)
            for (p = 0; p < DEPTH; p++) {
                UBYTE *q = &planes[p][y * BPR + x / 8], m = (UBYTE)(0x80 >> (x & 7));
                *q = (UBYTE)((pen >> p & 1) ? *q | m : *q & ~m) ^ m;   /* changed, whatever the pen */
            }
}
static void t_bounds(void *ctx, struct RastPort *r, int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    (void)ctx; (void)r;
    *x0 = org_x; *y0 = org_y; *x1 = org_x + W - 1; *y1 = org_y + H - 1;
}
static struct gt_env env = {0, 0, 0, 0, 0, 0, 0, 0, 0, t_pixel, t_fill, t_bounds};

int main(void)
{
    ULONG regs[10];
    struct lc_rect r, w;
    int i, k, n, have, want;
    WORD pts[2 * 8];

    for (i = 0; i < DEPTH; i++) bm.Planes[i] = planes[i];
    bm.BytesPerRow = BPR;
    bm.Rows = H;
    bm.Depth = DEPTH;
    rp.BitMap = &bm;

    if (marks[11].type != MK_LINE || marks[12].type != MK_POLY || marks[8].type != MK_ELLIPSE ||
        marks[13].type != MK_EOL || marks[14].type != MK_BELOW || marks[6].type != MK_ROW)
        fail("marks", "Draw, PolyDraw, DrawEllipse, ClearEOL, ClearScreen or WritePixelLine8 marks the whole layer");

    for (k = 0; k < 3000; k++) {
        LONG x0, y0, x1, y1, a, b;
        for (i = 0; i < 10; i++) regs[i] = rnd();
        regs[9] = (ULONG)&rp;
        switch (k % 5) {
        case 0:     /* Draw: a1 rp, d0 x, d1 y */
            fresh_rp();
            ogfx_rp_Move(&rp, x0 = coord(W), y0 = coord(H));
            regs[0] = junk(x1 = coord(W)); regs[1] = junk(y1 = coord(H));
            ocm_pre(&marks[11], regs);
            ogfx_rp_Draw(&rp, (WORD)regs[0], (WORD)regs[1]);
            have = ocm_bounds(&marks[11], regs, &rp, W, H, 0, 0, &r);
            inside("Draw", have, &r);
            want = cut(x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, x0 < x1 ? x1 : x0, y0 < y1 ? y1 : y0, 0, 0, &w);
            same("Draw", have, &r, want, &w);
            break;
        case 1:     /* PolyDraw: a1 rp, d0 count, a0 points */
            fresh_rp();
            ogfx_rp_Move(&rp, x0 = coord(W), y0 = coord(H));
            x1 = x0; y1 = y0;
            n = (int)(rnd() % 9);
            for (i = 0; i < n; i++) {
                pts[2 * i] = (WORD)coord(W); pts[2 * i + 1] = (WORD)coord(H);
                if (pts[2 * i] < x0) x0 = pts[2 * i];
                if (pts[2 * i] > x1) x1 = pts[2 * i];
                if (pts[2 * i + 1] < y0) y0 = pts[2 * i + 1];
                if (pts[2 * i + 1] > y1) y1 = pts[2 * i + 1];
            }
            regs[0] = junk(n); regs[8] = (ULONG)pts;
            ocm_pre(&marks[12], regs);
            ogfx_rp_PolyDraw(&rp, (WORD)regs[0], (const WORD *)regs[8]);
            have = ocm_bounds(&marks[12], regs, &rp, W, H, 0, 0, &r);
            inside("PolyDraw", have, &r);
            same("PolyDraw", have, &r, cut(x0, y0, x1, y1, 0, 0, &w), &w);
            break;
        case 2:     /* DrawEllipse: a1 rp, d0 xc, d1 yc, d2 a, d3 b */
            fresh_rp();
            if (rp.DrawMode & (COMPLEMENT | INVERSVID)) rp.DrawMode = JAM1;  /* the core leaves those to graphics */
            rp.LinePtrn = 0xFFFF;
            x0 = coord(W); y0 = coord(H); a = (LONG)(rnd() % 50); b = (LONG)(rnd() % 30);
            regs[0] = junk(x0); regs[1] = junk(y0); regs[2] = junk(a); regs[3] = junk(b);
            ocm_pre(&marks[8], regs);
            if (!ogfx_rp_DrawEllipse(&rp, x0, y0, a, b)) { fail("DrawEllipse", "not drawn by the core"); break; }
            have = ocm_bounds(&marks[8], regs, &rp, W, H, 0, 0, &r);
            inside("DrawEllipse", have, &r);
            same("DrawEllipse", have, &r, cut(x0 - a, y0 - b, x0 + a, y0 + b, 0, 0, &w), &w);
            break;
        case 3:     /* ClearEOL, in a layer scrolled by org */
        case 4:     /* ClearScreen */
            fresh_rp();
            org_x = (LONG)(rnd() % 3 ? 0 : rnd() % 40);
            org_y = (LONG)(rnd() % 3 ? 0 : rnd() % 40);
            rp.cp_x = (WORD)(coord(W) + org_x);
            rp.cp_y = (WORD)(coord(H) + org_y);
            rp.TxHeight = (UWORD)(rnd() % 20);
            rp.TxBaseline = (UWORD)(rnd() % 16);
            {
                int m = k % 5 == 3 ? 13 : 14;
                ocm_pre(&marks[m], regs);
                if (m == 13) gt_ClearEOL(&env, &rp);
                else gt_ClearScreen(&env, &rp);
                have = ocm_bounds(&marks[m], regs, &rp, W, H, org_x, org_y, &r);
                inside(m == 13 ? "ClearEOL" : "ClearScreen", have, &r);
                y0 = rp.cp_y - rp.TxBaseline;
                if (m == 13) want = cut(rp.cp_x, y0, org_x + W - 1, y0 + rp.TxHeight - 1, org_x, org_y, &w);
                else want = cut(org_x, y0, org_x + W - 1, org_y + H - 1, org_x, org_y, &w);
                same(m == 13 ? "ClearEOL" : "ClearScreen", have, &r, want, &w);
            }
            break;
        }
    }
    /* WritePixelLine8 (a0 rp, d0 x, d1 y, d2 width) and ScalePixelArray (a1 rp, d3-d6 x, y, w, h) */
    for (k = 0; k < 500; k++) {
        LONG x = coord(W), y = coord(H), wd = (LONG)(rnd() % 200), ht = (LONG)(rnd() % 90);
        for (i = 0; i < 10; i++) regs[i] = rnd();
        regs[8] = (ULONG)&rp; regs[0] = junk(x); regs[1] = junk(y); regs[2] = junk(wd);
        have = ocm_bounds(&marks[6], regs, &rp, W, H, 0, 0, &r);
        same("WritePixelLine8", have, &r, wd ? cut(x, y, x + wd - 1, y, 0, 0, &w) : 0, &w);
        wd++; ht++;     /* MK_XYWH, as the blits: a size of 0 marks a pixel or two either side */
        regs[9] = (ULONG)&rp; regs[3] = junk(x); regs[4] = junk(y); regs[5] = junk(wd); regs[6] = junk(ht);
        have = ocm_bounds(&marks[9], regs, &rp, W, H, 0, 0, &r);
        same("ScalePixelArray", have, &r, cut(x, y, x + wd - 1, y + ht - 1, 0, 0, &w), &w);
    }
    printf("%lu checks, %lu failed; marked %lu of %lu pixels (%lu%%)\n", checks, fails, marked, whole,
           marked * 100 / (whole ? whole : 1));
    return fails != 0;
}
