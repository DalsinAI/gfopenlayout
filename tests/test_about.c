/* GFOpenLayout About windows (gfopenlayout_about.h) on a host target of 0xRRGGBB pixels: the GFOS mark's shape
 * (against the same test in floating point, as gfopenprefs' bl_mark.h draws it), the layout, the drawing and the
 * keys. Text is a block font: each byte a 6-pixel advance, 7 up and 2 down. MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout_about.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 400
#define H 200
static uint32_t px[H][W];
static int fails, checks;
static oldraw *cur;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void t_fill(void *t, int x, int y, int w, int h, oldraw_colour c)
{
    int i, j;
    uint32_t v = oldraw_resolve(cur, c) & 0xFFFFFFu;
    (void)t;
    for (j = y; j < y + h; ++j) for (i = x; i < x + w; ++i) px[j][i] = v;
}
static void t_invert(void *t, int x, int y, int w, int h)
{
    int i, j;
    (void)t;
    for (j = y; j < y + h; ++j) for (i = x; i < x + w; ++i) px[j][i] ^= 0xFFFFFFu;
}
static void t_image(void *t, const oldraw_image *img, int sx, int sy, int x, int y, int w, int h, unsigned f)
{
    int i, j;
    (void)t; (void)f;
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i) {
            uint32_t p = oldraw_image_pixel(img, sx + i, sy + j);
            if (p >> 24 >= 128) px[y + j][x + i] = p & 0xFFFFFFu;
        }
}
static int t_measure(void *t, const char *s, size_t n, const oldraw_font *f, oldraw_text_size *size)
{
    (void)t; (void)s;
    size->width = (int)n * 6 + (f && (f->style & OLDRAW_BOLD) ? 1 : 0);
    size->ascent = 7;
    size->descent = 2;
    return 1;
}
static int t_text(void *t, int x, int y, const char *s, size_t n, const oldraw_font *f, oldraw_colour c, unsigned st,
                  const oldraw_rect *clip)
{
    int i, j;
    uint32_t v = oldraw_resolve(cur, c) & 0xFFFFFFu;
    (void)t; (void)s; (void)f; (void)st; (void)clip;
    for (j = y - 7; j < y + 2; ++j)
        for (i = x; i < x + (int)n * 6; ++i)
            if (i % 6 != 5 && j >= 0 && j < H && i >= 0 && i < W) px[j][i] = v;
    return 1;
}
static void t_flush(void *t) { (void)t; }
static int t_copy(void *t, int sx, int sy, int dx, int dy, int w, int h) { (void)t; (void)sx; (void)sy; (void)dx;
    (void)dy; (void)w; (void)h; return 1; }
static const oldraw_ops ops = { t_fill, t_invert, t_image, t_text, t_measure, t_flush, NULL, t_copy };

/* bl_mark.h's test, in floating point */
static int ref_at(int h, int x, int y)
{
    double s = h * 0.13, r = (h - s) / (1.0 + 0.46947), w = (int)(2.0 * r + s + 0.999), cx = w / 2.0;
    double cy = s / 2.0 + r * 0.46947, dr = (r - s / 2.0) * 0.46, dy = cy + (r - s / 2.0) - dr - s * 0.7;
    double ppx = x + 0.5 - cx, ppy = y + 0.5 - cy, d2 = ppx * ppx + ppy * ppy, ro = r + s / 2.0, ri = r - s / 2.0;
    double ex = r * 0.88295, ey = -r * 0.46947, qy = y + 0.5 - dy;
    if (ppx * ppx + qy * qy <= dr * dr) return OLABOUT_DOT;
    if (d2 <= ro * ro && d2 >= ri * ri && !(ppy < 0 && ppy * ppy > 0.46947 * 0.46947 * d2)) return OLABOUT_ARC;
    if ((ppx - ex) * (ppx - ex) + (ppy - ey) * (ppy - ey) <= s * s / 4 ||
        (ppx + ex) * (ppx + ex) + (ppy - ey) * (ppy - ey) <= s * s / 4) return OLABOUT_ARC;
    return OLABOUT_NONE;
}

static void test_mark(void)
{
    static const int hs[] = { 16, 24, 33, 48, 64 };
    unsigned k;
    CHECK(olabout_mark_width(1) == 0 && olabout_mark_width(513) == 0 && olabout_mark_at(1, 0, 0) == OLABOUT_NONE);
    for (k = 0; k < sizeof hs / sizeof hs[0]; k++) {
        int h = hs[k], w = olabout_mark_width(h), x, y, diff = 0, all = 0, top = 0, bottom = 0, dots = 0, sym = 1;
        int dot_low = 1, gap = 1;
        CHECK(w > h && w < 2 * h);                  /* wider than high: the arc's ends are above its centre */
        CHECK(olabout_mark_at(h, -1, 0) == OLABOUT_NONE && olabout_mark_at(h, w, h - 1) == OLABOUT_NONE);
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                int m = olabout_mark_at(h, x, y);
                all += m != OLABOUT_NONE || ref_at(h, x, y) != OLABOUT_NONE;
                diff += m != ref_at(h, x, y);
                top += y == 0 && m == OLABOUT_ARC;
                bottom += y == h - 1 && m == OLABOUT_ARC;
                if (m == OLABOUT_DOT) { dots++; if (y < h / 4) dot_low = 0; }
                if (m != olabout_mark_at(h, w - 1 - x, y)) sym = 0;
                if (y < h / 4 && x >= w / 2 - 1 && x <= w / 2 && m != OLABOUT_NONE) gap = 0;
            }
        CHECK(diff * 50 <= all);                    /* within 2% of the floating-point mark */
        CHECK(top > 0 && bottom > 0);               /* the ends' tops on row 0, the band's bottom on the last row */
        CHECK(dots > 0 && dot_low && sym && gap);   /* a dot low in it, the same both sides, open at the top */
        if (diff * 50 > all || !top || !bottom) printf("  h %d: %d of %d differ, top %d bottom %d\n", h, diff, all,
                                                      top, bottom);
    }
    {
        int w = olabout_mark_width(32), x, y, arc = 0, dot = 0, clear = 0;
        uint32_t *p = malloc(sizeof *p * (size_t)w * 32);
        CHECK(p && olabout_mark_argb(32, 0xFFFFFF, p) == w);
        for (y = 0; p && y < 32; y++)
            for (x = 0; x < w; x++) {
                uint32_t v = p[y * w + x];
                int m = olabout_mark_at(32, x, y);
                arc += m == OLABOUT_ARC && v == 0xFFFFFFFFu;
                dot += m == OLABOUT_DOT && v == (0xFF000000u | OLABOUT_DOT_RGB);
                clear += m == OLABOUT_NONE && v == 0;
            }
        CHECK(arc > 0 && dot > 0 && arc + dot + clear == w * 32);  /* white arc (on the blue bar), orange dot */
        free(p);
    }
}

static void test_layout_draw(void)
{
    static const olabout a = { "GFOpenBench", "0.1.60", "Our own Workbench for AmigaOS 3.2.\nMIT, Copyright (c) 2026 "
                               "AmiCradle Team." };
    olabout_layout l;
    oldraw_image *mark;
    int i, arc = 0, text_px = 0, x, y;
    oldraw *d = oldraw_new(&ops, NULL, W, H, NULL);
    cur = d;
    CHECK(d != NULL);
    olabout_layout_for(d, &a, &l);
    CHECK(l.lines == 4 && !strcmp(l.title, "GFOpenBench 0.1.60"));
    CHECK(l.line_len[1] == strlen("Our own Workbench for AmigaOS 3.2.") && !strncmp(l.line[3], OLABOUT_CREDIT,
          l.line_len[3]));
    CHECK(l.mark.h == 33 && l.mark.w == olabout_mark_width(33) && l.line_x > l.mark.x + l.mark.w);
    for (i = 1; i < l.lines; i++) CHECK(l.baseline[i] > l.baseline[i - 1]);
    CHECK(l.width >= l.line_x + (int)strlen(OLABOUT_CREDIT) * 6);       /* the longest line fits */
    CHECK(l.button.y >= l.mark.y + l.mark.h && l.button.y > l.baseline[l.lines - 1]);
    CHECK(l.button.x * 2 + l.button.w == l.width || l.button.x * 2 + l.button.w == l.width - 1);   /* centred */
    CHECK(l.height == l.button.y + l.button.h + 6 && l.width <= W && l.height <= H);
    CHECK(olabout_on_button(&l, l.button.x, l.button.y) && olabout_on_button(&l, l.button.x + l.button.w - 1,
          l.button.y + l.button.h - 1));
    CHECK(!olabout_on_button(&l, l.button.x - 1, l.button.y) && !olabout_on_button(&l, l.mark.x, l.mark.y));

    mark = olabout_mark_image(d, l.mark.h, OLABOUT_ARC_RGB);
    CHECK(mark && oldraw_image_width(mark) == l.mark.w && oldraw_image_height(mark) == l.mark.h);
    memset(px, 0x55, sizeof px);
    olabout_draw(d, &l, mark, 0);
    oldraw_flush(d);
    for (y = l.mark.y; y < l.mark.y + l.mark.h; y++)
        for (x = l.mark.x; x < l.mark.x + l.mark.w; x++)
            arc += px[y][x] == OLABOUT_ARC_RGB;
    for (x = l.line_x; x < l.line_x + 60; x++) text_px += px[l.baseline[0] - 1][x] != px[0][0];
    CHECK(arc > 20 && text_px > 20);                        /* the mark in its colours, the name drawn */
    CHECK(px[0][0] != 0x555555u && px[l.height - 1][l.width - 1] != 0x555555u);   /* the whole inside filled */
    {
        uint32_t up = px[l.button.y][l.button.x + 2];       /* the bevel's top edge, raised then pushed in */
        olabout_draw(d, &l, mark, 1);
        oldraw_flush(d);
        CHECK(px[l.button.y][l.button.x + 2] != up);
    }
    oldraw_image_free(d, mark);

    {
        olabout_layout m;
        static const olabout b = { "GFOpenTitle", NULL, NULL };
        olabout_layout_for(d, &b, &m);
        CHECK(m.lines == 2 && !strcmp(m.title, "GFOpenTitle") && !strncmp(m.line[1], OLABOUT_CREDIT, m.line_len[1]));
    }
    {
        olabout_layout m;
        static const olabout c = { "X", "1", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13" };
        olabout_layout_for(d, &c, &m);
        CHECK(m.lines == OLABOUT_MAX_LINES && !strncmp(m.line[m.lines - 1], OLABOUT_CREDIT, m.line_len[m.lines - 1]));
    }
    oldraw_free(d);
}

int main(void)
{
    test_mark();
    test_layout_draw();
    CHECK(olabout_closes('\r') && olabout_closes(27) && olabout_closes('o') && !olabout_closes(' ') &&
          !olabout_closes('x'));
    printf("test_about: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
