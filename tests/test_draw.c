/* GFOpenLayout drawing (gfopenlayout_draw.h) on a host target of 0xRRGGBB pixels: each primitive draws what and only
 * where it says, clipping, and the damage list. Text is a block font: each byte a 6-pixel advance, 7 up and 2 down,
 * drawn as a 5 by 9 block. MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout_draw.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 64
#define H 48

static uint32_t px[H][W];
static int fails, checks;
static int fills, texts, flushes;

#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static oldraw *cur;     /* for resolving pens */

static void t_fill(void *t, int x, int y, int w, int h, oldraw_colour c)
{
    int i, j;
    uint32_t v = oldraw_resolve(cur, c) & 0xFFFFFFu;
    (void)t;
    ++fills;
    for (j = y; j < y + h; ++j)
        for (i = x; i < x + w; ++i) {
            if (i < 0 || j < 0 || i >= W || j >= H) { ++fails; printf("FAIL fill outside the target %d,%d\n", i, j); return; }
            px[j][i] = v;
        }
}

static void t_invert(void *t, int x, int y, int w, int h)
{
    int i, j;
    (void)t;
    for (j = y; j < y + h; ++j)
        for (i = x; i < x + w; ++i) px[j][i] ^= 0xFFFFFFu;
}

static void t_image(void *t, const oldraw_image *img, int sx, int sy, int x, int y, int w, int h, unsigned flags)
{
    int i, j;
    (void)t;
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i) {
            uint32_t p = oldraw_image_pixel(img, sx + i, sy + j);
            if ((p >> 24) < 0x80) continue;
            p &= 0xFFFFFFu;
            if (flags & OLDRAW_SELECTED) p = (p >> 1) & 0x7F7F7Fu;
            px[y + j][x + i] = p;
        }
}

static int t_measure(void *t, const char *s, size_t n, const oldraw_font *f, oldraw_text_size *size)
{
    (void)t;
    (void)s;
    (void)f;
    size->width = (int)n * 6;
    size->ascent = 7;
    size->descent = 2;
    return 1;
}

static int t_text(void *t, int x, int y, const char *s, size_t n, const oldraw_font *f, oldraw_colour c, unsigned st,
                  const oldraw_rect *clip)
{
    size_t k;
    int i, j;
    uint32_t v = oldraw_resolve(cur, c) & 0xFFFFFFu;
    (void)t;
    (void)s;
    (void)f;
    (void)st;
    ++texts;
    for (k = 0; k < n; ++k)
        for (j = y - 7; j < y + 2; ++j)
            for (i = x + (int)k * 6; i < x + (int)k * 6 + 5; ++i)
                if (i >= clip->x && i < clip->x + clip->w && j >= clip->y && j < clip->y + clip->h) px[j][i] = v;
    return 1;
}

static void t_flush(void *t) { (void)t; ++flushes; }

static const oldraw_ops ops = { t_fill, t_invert, t_image, t_text, t_measure, t_flush, NULL };

static void clear(uint32_t v)
{
    int i, j;
    for (j = 0; j < H; ++j)
        for (i = 0; i < W; ++i) px[j][i] = v;
}

/* how many pixels of the rectangle are v */
static int count(int x, int y, int w, int h, uint32_t v)
{
    int i, j, n = 0;
    for (j = y; j < y + h; ++j)
        for (i = x; i < x + w; ++i)
            if (i >= 0 && j >= 0 && i < W && j < H && px[j][i] == v) ++n;
    return n;
}

static int count_not(int x, int y, int w, int h, uint32_t v) { return w * h - count(x, y, w, h, v); }

static const oldraw_colour pens[OLDRAW_PEN_COUNT] = {
    0xFF000000UL, 0xFFAAAAAAUL, 0xFF000001UL, 0xFFFFFFFFUL, 0xFF000002UL, 0xFF0000FFUL,
    0xFFFFFF00UL, 0xFFAAAAAAUL, 0xFFFFFFFEUL, 0xFF000000UL, 0xFFFFFFFFUL, 0xFF000000UL
};
#define BG 0xAAAAAAu
#define TEXT 0x000001u
#define SHINE 0xFFFFFFu
#define SHADOW 0x000002u
#define FILLC 0x0000FFu
#define FILLTEXT 0xFFFF00u

static void test_fill_frame_clip(oldraw *d)
{
    oldraw_rect r = { 10, 10, 8, 6 }, c = { 12, 0, 3, 48 }, out = { -5, -5, 8, 8 };
    clear(BG);
    oldraw_fill(d, &r, OLDRAW_RGB(0x123456));
    CHECK(count(10, 10, 8, 6, 0x123456) == 48);
    CHECK(count(0, 0, W, H, 0x123456) == 48);
    clear(BG);
    oldraw_fill(d, &out, OLDRAW_RGB(0x123456));     /* cut to the target */
    CHECK(count(0, 0, 3, 3, 0x123456) == 9 && count(0, 0, W, H, 0x123456) == 9);
    clear(BG);
    oldraw_clip(d, &c);
    oldraw_fill(d, &r, OLDRAW_RGB(0x123456));
    CHECK(count(0, 0, W, H, 0x123456) == 18);       /* 3 wide, 6 tall */
    oldraw_clip(d, NULL);
    clear(BG);
    oldraw_fill(d, &r, OLDRAW_NONE);
    CHECK(count(0, 0, W, H, BG) == W * H);
    /* a raised frame: shine top and left, shadow bottom and right, inside untouched */
    clear(BG);
    oldraw_frame(d, &r, 0);
    CHECK(count(10, 10, 7, 1, SHINE) == 7);
    CHECK(count(10, 11, 1, 4, SHINE) == 4);
    CHECK(count(11, 15, 7, 1, SHADOW) == 7);
    CHECK(count(17, 10, 1, 5, SHADOW) == 5);
    CHECK(count(11, 11, 6, 4, BG) == 24);
    CHECK(count_not(0, 0, W, H, BG) == 2 * 8 + 2 * 4);
    clear(BG);
    oldraw_frame(d, &r, 1);
    CHECK(count(10, 10, 7, 1, SHADOW) == 7 && count(17, 10, 1, 5, SHINE) == 5);
}

static void test_damage(oldraw *d)
{
    oldraw_rect a = { 0, 0, 10, 10 }, b = { 10, 0, 5, 5 }, far = { 40, 30, 4, 4 }, got, outside = { 100, 100, 5, 5 };
    int i;
    oldraw_damage_all(d);
    CHECK(oldraw_damage_count(d) == 1);
    CHECK(oldraw_redraw(d, NULL, NULL) == 1 && oldraw_damage_count(d) == 0);
    oldraw_damage(d, &a);
    oldraw_damage(d, &b);                           /* touches a: joined */
    CHECK(oldraw_damage_count(d) == 1);
    CHECK(oldraw_damage_get(d, 0, &got) && got.x == 0 && got.y == 0 && got.w == 15 && got.h == 10);
    oldraw_damage(d, &far);
    CHECK(oldraw_damage_count(d) == 2);
    oldraw_damage(d, &outside);                     /* off the target: dropped */
    CHECK(oldraw_damage_count(d) == 2);
    oldraw_redraw(d, NULL, NULL);
    /* more than OLDRAW_DAMAGE_MAX apart: never more than the most kept, and every pixel still covered */
    for (i = 0; i < 30; ++i) {
        oldraw_rect s;
        s.x = (i % 10) * 6;
        s.y = (i / 10) * 15;
        s.w = 2;
        s.h = 2;
        oldraw_damage(d, &s);
    }
    CHECK(oldraw_damage_count(d) <= OLDRAW_DAMAGE_MAX);
    {
        int k, n = oldraw_damage_count(d), covered = 0;
        for (i = 0; i < 30; ++i) {
            int x = (i % 10) * 6, y = (i / 10) * 15;
            for (k = 0; k < n; ++k) {
                oldraw_damage_get(d, k, &got);
                if (x >= got.x && y >= got.y && x + 2 <= got.x + got.w && y + 2 <= got.y + got.h) { ++covered; break; }
            }
        }
        CHECK(covered == 30);
    }
    oldraw_redraw(d, NULL, NULL);
}

static int areas_seen;
static void redraw_fill(void *ud, oldraw *d, const oldraw_rect *area)
{
    oldraw_rect all = { 0, 0, W, H };
    (void)ud;
    (void)area;
    ++areas_seen;
    oldraw_fill(d, &all, OLDRAW_RGB(0x00FF00));     /* asks for all of it: gets only the area */
}

static void test_redraw_clips(oldraw *d)
{
    oldraw_rect a = { 2, 2, 4, 4 }, b = { 30, 20, 6, 3 };
    clear(BG);
    flushes = 0;
    oldraw_damage(d, &a);
    oldraw_damage(d, &b);
    areas_seen = 0;
    CHECK(oldraw_redraw(d, redraw_fill, NULL) == 2);
    CHECK(areas_seen == 2);
    CHECK(count(0, 0, W, H, 0x00FF00) == 16 + 18);
    CHECK(flushes == 1);
    /* after it, no clip is left */
    clear(BG);
    oldraw_fill(d, &b, OLDRAW_RGB(0x00FF00));
    oldraw_fill(d, &a, OLDRAW_RGB(0x00FF00));
    CHECK(count(0, 0, W, H, 0x00FF00) == 34);
}

static void test_text(oldraw *d)
{
    oldraw_text_size s;
    oldraw_rect box = { 4, 4, 40, 12 }, small = { 0, 0, 10, 12 };
    size_t kept;
    CHECK(oldraw_text_measure(d, "abc", (size_t)-1, NULL, 0, &s) && s.width == 18 && s.ascent == 7);
    clear(BG);
    CHECK(oldraw_text(d, 2, 10, "ab", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0) == 12);
    CHECK(count(2, 3, 5, 9, TEXT) == 45 && count(8, 3, 5, 9, TEXT) == 45);
    CHECK(count_not(0, 0, W, H, BG) == 90);
    /* fits: all of it, left, centred top to bottom (9 tall in 12: baseline 4 + 1 + 7) */
    clear(BG);
    kept = oldraw_text_box(d, &box, "hello", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_LEFT);
    CHECK(kept == 5);
    CHECK(count(4, 5, 5, 9, TEXT) == 45);
    CHECK(count(4, 4, 40, 1, TEXT) == 0);
    /* right aligned: its last block ends at the box's right */
    clear(BG);
    oldraw_text_box(d, &box, "hi", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_RIGHT);
    CHECK(count(4 + 40 - 6, 5, 5, 9, TEXT) == 45);
    /* too long: cut with "..." and nothing outside the box */
    clear(BG);
    kept = oldraw_text_box(d, &box, "abcdefghijkl", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_LEFT);
    CHECK(kept == 3);       /* 3 * 6 + 18 = 36 <= 40; 4 * 6 + 18 = 42 */
    CHECK(count(44, 0, W - 44, H, TEXT) == 0);
    /* UTF-8: cut only where a character starts ("é" is two bytes) */
    kept = oldraw_text_box(d, &box, "\xC3\xA9\xC3\xA9\xC3\xA9xxxxxxxxx", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0,
                           OLDRAW_LEFT);
    CHECK(kept == 2 || kept == 4 || kept == 6);
    /* no room even for "...": nothing */
    clear(BG);
    small.w = 10;
    CHECK(oldraw_text_box(d, &small, "abcdef", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_LEFT) == 0);
    CHECK(count(0, 0, W, H, TEXT) == 0);
}

static void test_image(oldraw *d)
{
    uint32_t pic[3 * 2] = { 0xFF112233u, 0x00FFFFFFu, 0xFF445566u, 0xFF778899u, 0xFFAABBCCu, 0x7F000000u };
    oldraw_image *img = oldraw_image_argb(d, 3, 2, pic, 3);
    oldraw_rect c = { 11, 0, 10, 10 };
    CHECK(img && oldraw_image_width(img) == 3 && oldraw_image_height(img) == 2);
    CHECK(oldraw_image_pixel(img, 2, 1) == 0x7F000000u && oldraw_image_pixel(img, 3, 0) == 0);
    clear(BG);
    oldraw_image_draw(d, img, 10, 5, 0);
    CHECK(px[5][10] == 0x112233u && px[5][11] == BG && px[5][12] == 0x445566u);
    CHECK(px[6][10] == 0x778899u && px[6][11] == 0xAABBCCu && px[6][12] == BG);
    CHECK(count_not(0, 0, W, H, BG) == 4);
    clear(BG);
    oldraw_image_draw(d, img, 10, 5, OLDRAW_SELECTED);
    CHECK(px[5][10] == ((0x112233u >> 1) & 0x7F7F7Fu));
    /* clipped: its left column cut off, the rest where it was */
    clear(BG);
    oldraw_clip(d, &c);
    oldraw_image_draw(d, img, 10, 5, 0);
    oldraw_clip(d, NULL);
    CHECK(px[5][10] == BG && px[5][12] == 0x445566u && px[6][11] == 0xAABBCCu);
    /* off the target's edge: no write outside it (t_fill/t_image would index past) */
    clear(BG);
    oldraw_image_draw(d, img, W - 1, H - 1, 0);
    CHECK(px[H - 1][W - 1] == 0x112233u);
    oldraw_image_draw(d, img, -1, -1, 0);
    CHECK(px[0][0] == 0xAABBCCu && px[0][1] == BG);
    CHECK(oldraw_image_argb(d, 0, 2, pic, 3) == NULL);
    CHECK(oldraw_image_icon(d, NULL) == NULL);
    oldraw_image_free(d, img);
}

static void test_list_row(oldraw *d)
{
    uint32_t pic[4 * 4];
    oldraw_image *img;
    oldraw_cell cells[3];
    oldraw_row row;
    oldraw_rect r = { 0, 10, 60, 12 }, cell;
    int i;
    for (i = 0; i < 16; ++i) pic[i] = 0xFF00AA00u;
    img = oldraw_image_argb(d, 4, 4, pic, 4);
    memset(cells, 0, sizeof cells);
    cells[0].image = img;
    cells[0].text = "ab";
    cells[0].width = 30;
    cells[1].text = "9";
    cells[1].width = 14;
    cells[1].align = OLDRAW_RIGHT;
    cells[2].text = "x";                             /* the rest: 16 */
    memset(&row, 0, sizeof row);
    row.cells = cells;
    row.ncells = 3;
    row.pad = 2;
    CHECK(oldraw_list_cell_box(&row, &r, 0, &cell) && cell.x == 2 && cell.w == 26 && cell.y == 10 && cell.h == 12);
    CHECK(oldraw_list_cell_box(&row, &r, 1, &cell) && cell.x == 32 && cell.w == 10);
    CHECK(oldraw_list_cell_box(&row, &r, 2, &cell) && cell.x == 46 && cell.w == 12);
    CHECK(!oldraw_list_cell_box(&row, &r, 3, &cell));
    clear(0x123456);
    oldraw_list_row(d, &r, &row);
    CHECK(count(0, 0, W, 10, 0x123456) == W * 10);          /* nothing above */
    CHECK(count(0, 22, W, H - 22, 0x123456) == W * (H - 22)); /* nothing below */
    CHECK(count(2, 14, 4, 4, 0x00AA00) == 16);               /* the image, centred: (12 - 4) / 2 */
    CHECK(count(8, 11, 5, 9, TEXT) == 45);                   /* "a" after image and pad: 2 + 4 + 2 */
    CHECK(count(36, 11, 5, 9, TEXT) == 45);                  /* "9" right in 32..42 */
    CHECK(count(0, 10, 60, 1, BG) == 60);                    /* the row's top line is background */
    /* selected: the fill and the fill's text pen */
    row.flags = OLDRAW_ROW_SELECTED;
    clear(0x123456);
    oldraw_list_row(d, &r, &row);
    CHECK(count(0, 10, 60, 1, FILLC) == 60);
    CHECK(count(8, 11, 5, 9, FILLTEXT) == 45 && count(0, 0, W, H, TEXT) == 0);
    /* current: a dotted box round it, drawn twice: off again */
    row.flags = OLDRAW_ROW_CURRENT;
    clear(0x123456);
    oldraw_list_row(d, &r, &row);
    CHECK(count(0, 10, 4, 1, BG ^ 0xFFFFFFu) == 4 && count(4, 10, 4, 1, BG) == 4);
    /* a text too long for its cell is cut inside the cell */
    cells[1].text = "123456789";
    row.flags = 0;
    clear(0x123456);
    oldraw_list_row(d, &r, &row);
    CHECK(count(42, 10, 4, 12, TEXT) == 0);                   /* the pad between cells 1 and 2 */
    oldraw_image_free(d, img);
}

static void test_select_box(oldraw *d)
{
    clear(BG);
    oldraw_select_box(d, 30, 20, 5, 3);             /* either corner first */
    CHECK(count_not(0, 0, W, H, BG) > 0);
    CHECK(count(5, 3, 3, 1, BG) == 3 && count(8, 3, 4, 1, BG ^ 0xFFFFFFu) == 4);   /* dots by screen position */
    CHECK(count_not(6, 4, 24, 16, BG) == 0);         /* the inside untouched */
    CHECK(count_not(0, 0, W, 3, BG) == 0 && count_not(31, 0, W - 31, H, BG) == 0);
    oldraw_select_box(d, 5, 3, 30, 20);             /* the same box again: as it was */
    CHECK(count_not(0, 0, W, H, BG) == 0);
    /* one pixel tall and wide: no pixel complemented twice */
    oldraw_select_box(d, 8, 8, 8, 8);
    CHECK(count_not(0, 0, W, H, BG) == 1);
    oldraw_select_box(d, 8, 8, 8, 8);
    CHECK(count_not(0, 0, W, H, BG) == 0);
}

static void test_progress(oldraw *d)
{
    oldraw_rect r = { 2, 2, 42, 12 };
    CHECK(oldraw_progress_fill(40, 0, 100) == 0);
    CHECK(oldraw_progress_fill(40, 50, 100) == 20);
    CHECK(oldraw_progress_fill(40, 200, 100) == 40);
    CHECK(oldraw_progress_fill(40, 5, 0) == 0);
    CHECK(oldraw_progress_fill(40, 0xFFFFFFFEu, 0xFFFFFFFFu) == 39);   /* no overflow */
    clear(0x123456);
    oldraw_progress(d, &r, 1, 4, 0);
    CHECK(count(3, 3, 10, 10, FILLC) == 100);       /* 40 / 4 */
    CHECK(count(13, 3, 30, 10, BG) == 300);
    CHECK(count(2, 2, 41, 1, SHADOW) == 41);        /* recessed */
    CHECK(count_not(0, 0, W, 2, 0x123456) == 0);
    clear(0x123456);
    oldraw_progress(d, &r, 1, 2, OLDRAW_PROGRESS_PERCENT);
    /* "50%": 18 wide, from 3 + (40 - 18) / 2 = 14; its first two blocks over the fill (3..22), the last after */
    CHECK(count(14, 3, 5, 9, FILLTEXT) == 45);
    CHECK(count(26, 3, 5, 9, TEXT) == 45);
    CHECK(count(20, 3, 3, 9, FILLTEXT) == 27 && count(23, 3, 2, 9, TEXT) == 18);   /* the "0" across the edge */
    CHECK(count(23, 3, 20, 10, FILLTEXT) == 0 && count(3, 3, 20, 10, TEXT) == 0);
}

static void test_pie(oldraw *d)
{
    int full = 0, half = 0, q = 0, x, y, missed = 0;
    CHECK(oldraw_pie_has(10, 0, 3600, 0, -10));
    CHECK(!oldraw_pie_has(10, 0, 3600, 8, 8));       /* outside the circle */
    CHECK(oldraw_pie_has(10, 0, 900, 5, -5));        /* top right quarter */
    CHECK(!oldraw_pie_has(10, 0, 900, -5, -5));
    CHECK(!oldraw_pie_has(10, 0, 900, 5, 5));
    CHECK(oldraw_pie_has(10, 900, 900, 5, 5));       /* 3 to 6 o'clock */
    CHECK(oldraw_pie_has(10, 0, 2700, -5, 5) && !oldraw_pie_has(10, 0, 2700, -5, -5));   /* more than half */
    CHECK(oldraw_pie_has(10, 3500, 200, 1, -9) && oldraw_pie_has(10, 3500, 200, -1, -9)); /* across 0 */
    CHECK(!oldraw_pie_has(10, 0, 0, 1, -9));
    for (y = -10; y <= 10; ++y)
        for (x = -10; x <= 10; ++x) {
            full += oldraw_pie_has(10, 0, 3600, x, y);
            half += oldraw_pie_has(10, 0, 1800, x, y);
            {
                int k, in = 0;
                for (k = 0; k < 4; ++k) in += oldraw_pie_has(10, 450 + 900 * k, 900, x, y);
                q += in;
                if (oldraw_pie_has(10, 0, 3600, x, y) && !in) ++missed;
            }
        }
    CHECK(half * 2 >= full - 25 && half * 2 <= full + 25);
    CHECK(missed == 0);                              /* four quarters cover the circle */
    CHECK(q >= full && q <= full + 4 * 11);          /* overlapping only on their edges */
    /* drawn: exactly the pixels oldraw_pie_has gives */
    clear(BG);
    oldraw_pie(d, 30, 24, 10, 0, 900, OLDRAW_RGB(0xFF0000));
    {
        int same = 1;
        for (y = -12; y <= 12; ++y)
            for (x = -12; x <= 12; ++x)
                if ((px[24 + y][30 + x] == 0xFF0000u) != (oldraw_pie_has(10, 0, 900, x, y) != 0)) same = 0;
        CHECK(same);
    }
    CHECK(count_not(0, 0, 30, H, BG) == 1 || count_not(0, 0, 30, H, BG) == 0);  /* left of centre: at most its column */
}

int main(void)
{
    oldraw *d = oldraw_new(&ops, NULL, W, H, pens);
    cur = d;
    CHECK(d != NULL);
    CHECK(oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_FILL)) == 0xFF0000FFUL);
    CHECK(oldraw_resolve(d, OLDRAW_RGB(0x010203)) == 0xFF010203UL);
    test_fill_frame_clip(d);
    test_damage(d);
    test_redraw_clips(d);
    test_text(d);
    test_image(d);
    test_list_row(d);
    test_select_box(d);
    test_progress(d);
    test_pie(d);
    oldraw_resize(d, 10, 10);
    clear(BG);
    {
        oldraw_rect all = { 0, 0, W, H };
        oldraw_fill(d, &all, OLDRAW_RGB(0x00FF00));
        CHECK(count(0, 0, W, H, 0x00FF00) == 100);
    }
    oldraw_free(d);
    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails ? 1 : 0;
}
