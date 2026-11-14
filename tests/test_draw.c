/* GFOpenLayout drawing (gfopenlayout_draw.h) on a host target of 0xRRGGBB pixels: each primitive draws what and only
 * where it says, clipping, and the damage list. Text is a block font: each byte a 6-pixel advance, 7 up and 2 down,
 * drawn as a 5 by 9 block. MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout_draw.h"
#include "oldraw_internal.h"      /* oldraw_image_variant, for the selected-and-ghosted check */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 64
#define H 48

static uint32_t px[H][W];
static int fails, checks;
static int fills, texts, flushes, texts_clipped, measures;
static int font_extra;          /* the target's font made wider (a Font prefs change) */

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

/* as the Amiga target draws it: only inside the picture's blocks */
static void t_image(void *t, const oldraw_image *img, int sx, int sy, int x, int y, int w, int h, unsigned flags)
{
    const oldraw_rect *b;
    int i, j, k, n = oldraw_image_blocks(img, &b);
    (void)t;
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i) {
            uint32_t p = oldraw_image_pixel(img, sx + i, sy + j);
            int in = 0;
            for (k = 0; k < n; ++k)
                if (sx + i >= b[k].x && sx + i < b[k].x + b[k].w && sy + j >= b[k].y && sy + j < b[k].y + b[k].h) in = 1;
            if (!in) continue;
            p &= 0xFFFFFFu;
            if (flags & OLDRAW_SELECTED) p = (p >> 1) & 0x7F7F7Fu;
            px[y + j][x + i] = p;
        }
}

static int t_measure(void *t, const char *s, size_t n, const oldraw_font *f, oldraw_text_size *size)
{
    (void)t;
    (void)s;
    ++measures;
    size->width = (int)n * (6 + font_extra) + ((f && (f->style & OLDRAW_BOLD)) ? (int)n : 0);   /* bold: +1 each */
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
    if (clip) ++texts_clipped;
    for (k = 0; k < n; ++k)
        for (j = y - 7; j < y + 2; ++j)
            for (i = x + (int)k * 6; i < x + (int)k * 6 + 5; ++i)
                if (!clip || (i >= clip->x && i < clip->x + clip->w && j >= clip->y && j < clip->y + clip->h)) {
                    if (i < 0 || j < 0 || i >= W || j >= H) { ++fails; printf("FAIL text outside the target, no clip\n"); return 1; }
                    px[j][i] = v;
                }
    return 1;
}

static void t_flush(void *t) { (void)t; ++flushes; }

static int copy_refuses;      /* the target says part of the source was hidden */

static int t_copy(void *t, int sx, int sy, int dx, int dy, int w, int h)
{
    static uint32_t tmp[H][W];
    int i, j;
    (void)t;
    if (copy_refuses) return 1;
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i) {
            if (sx + i < 0 || sy + j < 0 || sx + i >= W || sy + j >= H || dx + i < 0 || dy + j < 0 || dx + i >= W ||
                dy + j >= H) { ++fails; printf("FAIL copy outside the target\n"); return 0; }
            tmp[j][i] = px[sy + j][sx + i];
        }
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i) px[dy + j][dx + i] = tmp[j][i];
    return 0;
}

static const oldraw_ops ops = { t_fill, t_invert, t_image, t_text, t_measure, t_flush, NULL, t_copy };

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
    /* a one-pixel frame is a pixel */
    {
        oldraw_rect one = { 5, 5, 1, 1 };
        clear(BG);
        oldraw_frame(d, &one, 0);
        CHECK(count(5, 5, 1, 1, SHINE) == 1 && count_not(0, 0, W, H, BG) == 1);
    }
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
    /* touching only at a corner: kept apart (joining them would redraw 200 pixels for 100) */
    {
        oldraw_rect c1 = { 0, 0, 10, 10 }, c2 = { 10, 10, 10, 10 };
        oldraw_damage(d, &c1);
        oldraw_damage(d, &c2);
        CHECK(oldraw_damage_count(d) == 2);
        oldraw_redraw(d, NULL, NULL);
    }
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
    /* the call's style reaches the measure (bold is wider), with no font and with one */
    {
        oldraw_font f = { "topaz.font", 8, 0 };
        CHECK(oldraw_text_measure(d, "abc", (size_t)-1, NULL, OLDRAW_BOLD, &s) && s.width == 21);
        CHECK(oldraw_text_measure(d, "abc", (size_t)-1, &f, OLDRAW_BOLD, &s) && s.width == 21);
        CHECK(f.style == 0);                        /* the caller's font is left as it was */
        f.style = OLDRAW_BOLD;
        CHECK(oldraw_text_measure(d, "abc", (size_t)-1, &f, 0, &s) && s.width == 21);
        /* and so a bold "..." cut is a bold one: 3 * 7 + 21 = 42 > 40, so 2 kept */
        kept = oldraw_text_box(d, &box, "abcdefghijkl", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), OLDRAW_BOLD,
                               OLDRAW_LEFT);
        CHECK(kept == 2);
    }
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
    /* a huge radius doesn't overflow: Franklin's probe, a point outside a circle of 40000 */
    CHECK(!oldraw_pie_has(40000, 0, 3600, 39000, -30000));
    CHECK(oldraw_pie_has(40000, 0, 3600, 100, -100));
    /* the sweep of used of size, for any size */
    CHECK(oldraw_pie_sweep(0, 0) == 0 && oldraw_pie_sweep(1, 2) == 1800 && oldraw_pie_sweep(5, 4) == 3600);
    CHECK(oldraw_pie_sweep(2000000000000ULL, 4000000000000ULL) == 1800);         /* 4 TB in bytes */
    CHECK(oldraw_pie_sweep(0xFFFFFFFFFFFFFFF0ULL / 4, 0xFFFFFFFFFFFFFFF0ULL) >= 899 &&
          oldraw_pie_sweep(0xFFFFFFFFFFFFFFF0ULL / 4, 0xFFFFFFFFFFFFFFF0ULL) <= 901);
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
    /* clipped to one row: that row only, the same pixels as unclipped */
    {
        oldraw_rect row = { 0, 20, W, 1 };
        int same = 1;
        clear(BG);
        oldraw_clip(d, &row);
        oldraw_pie(d, 30, 24, 10, 0, 3600, OLDRAW_RGB(0xFF0000));
        oldraw_clip(d, NULL);
        CHECK(count_not(0, 0, W, 20, BG) == 0 && count_not(0, 21, W, H - 21, BG) == 0);
        for (x = -12; x <= 12; ++x)
            if ((px[20][30 + x] == 0xFF0000u) != (oldraw_pie_has(10, 0, 3600, x, -4) != 0)) same = 0;
        CHECK(same);
    }
}

/* a picture whose every pixel says where it is */
static void stamp(void)
{
    int i, j;
    for (j = 0; j < H; ++j)
        for (i = 0; i < W; ++i) px[j][i] = (uint32_t)(j << 8 | i);
}

static int is_from(int x, int y, int fx, int fy) { return px[y][x] == (uint32_t)(fy << 8 | fx); }

static void test_copy_scroll(oldraw *d)
{
    oldraw_rect src = { 4, 4, 10, 6 }, got, c = { 0, 0, 20, 48 }, edge = { W - 4, 0, 8, 4 }, area = { 0, 10, 30, 20 };
    int ok, i, j;
    /* apart */
    stamp();
    oldraw_copy(d, &src, 30, 20);
    CHECK(is_from(30, 20, 4, 4) && is_from(39, 25, 13, 9));
    CHECK(is_from(29, 20, 29, 20) && is_from(40, 20, 40, 20) && is_from(30, 26, 30, 26));
    CHECK(oldraw_damage_count(d) == 0);
    /* overlapping, right and down by a little: as memmove, every pixel from where it was */
    stamp();
    oldraw_copy(d, &src, 7, 6);
    ok = 1;
    for (j = 0; j < 6; ++j)
        for (i = 0; i < 10; ++i) if (!is_from(7 + i, 6 + j, 4 + i, 4 + j)) ok = 0;
    CHECK(ok);
    /* and left and up */
    stamp();
    oldraw_copy(d, &src, 1, 2);
    ok = 1;
    for (j = 0; j < 6; ++j)
        for (i = 0; i < 10; ++i) if (!is_from(1 + i, 2 + j, 4 + i, 4 + j)) ok = 0;
    CHECK(ok);
    /* the destination cut by the clip: the source cut with it */
    stamp();
    oldraw_clip(d, &c);
    oldraw_copy(d, &src, 15, 30);
    oldraw_clip(d, NULL);
    CHECK(is_from(15, 30, 4, 4) && is_from(19, 30, 8, 4) && is_from(20, 30, 20, 30));
    CHECK(oldraw_damage_count(d) == 0);
    /* a source half off the target: the half that has one copied, the other half damaged */
    stamp();
    oldraw_copy(d, &edge, 10, 30);
    CHECK(is_from(10, 30, W - 4, 0) && is_from(13, 33, W - 1, 3));
    CHECK(is_from(14, 30, 14, 30));                 /* not copied */
    CHECK(oldraw_damage_count(d) == 1 && oldraw_damage_get(d, 0, &got) && got.x == 14 && got.y == 30 && got.w == 4 &&
          got.h == 4);
    oldraw_redraw(d, NULL, NULL);
    /* scroll a list up two rows of 5: the rows move, the strip at the bottom is damaged, nothing outside moves */
    stamp();
    oldraw_scroll(d, &area, 0, -10);
    CHECK(is_from(0, 10, 0, 20) && is_from(29, 19, 29, 29));
    CHECK(is_from(30, 10, 30, 10) && is_from(0, 9, 0, 9) && is_from(0, 30, 0, 30));
    CHECK(oldraw_damage_count(d) == 1 && oldraw_damage_get(d, 0, &got) && got.x == 0 && got.y == 20 && got.w == 30 &&
          got.h == 10);
    oldraw_redraw(d, NULL, NULL);
    /* down and right */
    stamp();
    oldraw_scroll(d, &area, 3, 4);
    CHECK(is_from(3, 14, 0, 10) && is_from(29, 29, 26, 25));
    CHECK(oldraw_damage_count(d) == 1 || oldraw_damage_count(d) == 2);   /* the L the move leaves, joined or not */
    {
        int k, n = oldraw_damage_count(d), cov = 1;
        for (j = 10; j < 30; ++j)
            for (i = 0; i < 30; ++i) {
                int left = i < 3 || j < 14, in = 0;
                for (k = 0; k < n; ++k) {
                    oldraw_damage_get(d, k, &got);
                    if (i >= got.x && i < got.x + got.w && j >= got.y && j < got.y + got.h) in = 1;
                }
                if (left && !in) cov = 0;
            }
        CHECK(cov);
    }
    oldraw_redraw(d, NULL, NULL);
    /* as big as the area: all of it damaged */
    oldraw_scroll(d, &area, 0, 20);
    CHECK(oldraw_damage_count(d) == 1 && oldraw_damage_get(d, 0, &got) && got.w == 30 && got.h == 20);
    oldraw_redraw(d, NULL, NULL);
    /* damage pending in the source moves with the copy: 0,20 20x5 damaged, then the area scrolls up 10, so 0,10
     * 20x5 (where the stale pixels went) is damaged too, and so is the strip left at the bottom */
    {
        oldraw_rect pend = { 0, 20, 20, 5 }, area2 = { 0, 10, 30, 20 };
        int k, n, cov = 1;
        oldraw_damage(d, &pend);
        oldraw_scroll(d, &area2, 0, -10);
        n = oldraw_damage_count(d);
        for (j = 10; j < 30; ++j)
            for (i = 0; i < 30; ++i) {
                int want = (i < 20 && j >= 10 && j < 15) || (i < 20 && j >= 20 && j < 25) || j >= 20, in = 0;
                for (k = 0; k < n; ++k) {
                    oldraw_damage_get(d, k, &got);
                    if (i >= got.x && i < got.x + got.w && j >= got.y && j < got.y + got.h) in = 1;
                }
                if (want && !in) cov = 0;
            }
        CHECK(cov);
        /* and nothing above it: rows 15 to 19 were copied from clean rows */
        for (k = 0; k < n; ++k) {
            oldraw_damage_get(d, k, &got);
            CHECK(!(got.y < 20 && got.y + got.h > 15));
        }
        oldraw_redraw(d, NULL, NULL);
    }
    /* a target that can't read the source (hidden in a simple-refresh window): all of the destination damaged */
    stamp();
    copy_refuses = 1;
    oldraw_copy(d, &src, 30, 20);
    copy_refuses = 0;
    CHECK(is_from(30, 20, 30, 20));
    CHECK(oldraw_damage_count(d) == 1 && oldraw_damage_get(d, 0, &got) && got.x == 30 && got.y == 20 && got.w == 10 &&
          got.h == 6);
    oldraw_redraw(d, NULL, NULL);
}

/* the speed-ups (13 Nov bench): what they keep from the target, and that nothing drawn changes */
static void test_speed(oldraw *d)
{
    /* a 6x5 picture: a frame with a see-through hole, as the bench's icon */
    uint32_t pic[5 * 6];
    oldraw_image *img;
    const oldraw_rect *b;
    oldraw_rect box = { 4, 4, 40, 12 }, half = { 0, 0, 30, 48 };
    int i, j, k, n, cover, overlap, before;
    for (j = 0; j < 5; ++j)
        for (i = 0; i < 6; ++i) pic[j * 6 + i] = (i >= 2 && i <= 3 && j >= 1 && j <= 2) ? 0x00000000u : 0xFF102030u + (uint32_t)i;
    img = oldraw_image_argb(d, 6, 5, pic, 6);
    n = oldraw_image_blocks(img, &b);
    cover = 1;
    overlap = 0;
    for (j = 0; j < 5; ++j)
        for (i = 0; i < 6; ++i) {
            int in = 0;
            for (k = 0; k < n; ++k)
                if (i >= b[k].x && i < b[k].x + b[k].w && j >= b[k].y && j < b[k].y + b[k].h) ++in;
            if (in > 1) overlap = 1;
            if ((in == 1) != ((pic[j * 6 + i] >> 24) >= 0x80)) cover = 0;
        }
    CHECK(cover && !overlap);                           /* every drawn pixel once, no see-through one */
    CHECK(n == 4);                                       /* the top row, the hole's two sides, the bottom rows */
    CHECK(b[0].x == 0 && b[0].y == 0 && b[0].w == 6 && b[0].h == 1);
    CHECK(b[1].h == 2 && b[2].h == 2);                   /* the rows with the same runs joined */
    CHECK(b[3].y == 3 && b[3].h == 2 && b[3].w == 6);
    oldraw_image_free(d, img);
    {   /* a solid picture is one block */
        uint32_t solid[16];
        for (i = 0; i < 16; ++i) solid[i] = 0xFF000000u | (uint32_t)i;
        img = oldraw_image_argb(d, 4, 4, solid, 4);
        CHECK(oldraw_image_blocks(img, &b) == 1 && b[0].w == 4 && b[0].h == 4);
        oldraw_image_free(d, img);
        for (i = 0; i < 16; ++i) solid[i] = 0x10FFFFFFu;        /* all see-through: none */
        img = oldraw_image_argb(d, 4, 4, solid, 4);
        CHECK(img && oldraw_image_blocks(img, &b) == 0);
        oldraw_image_free(d, img);
    }
    /* the same text again is not measured again; another text is */
    oldraw_text(d, 2, 10, "speed", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0);
    before = measures;
    oldraw_text(d, 2, 10, "speed", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0);
    oldraw_text(d, 2, 30, "speed", 5, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0);
    CHECK(measures == before);
    oldraw_text(d, 2, 10, "speeds", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0);
    CHECK(measures == before + 1);
    {   /* another font is another measure */
        oldraw_font f = { "topaz.font", 8, 0 };
        oldraw_text(d, 2, 10, "speed", (size_t)-1, &f, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0);
        CHECK(measures == before + 2);
        oldraw_text(d, 2, 10, "speed", (size_t)-1, &f, OLDRAW_PEN(OLDRAW_PEN_TEXT), OLDRAW_BOLD);
        CHECK(measures == before + 3);
    }
    /* a text box that fits measures once and draws without a clip */
    before = measures;
    texts_clipped = 0;
    texts = 0;
    oldraw_text_box(d, &box, "fits!", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_LEFT);
    CHECK(measures == before + 1 && texts == 1 && texts_clipped == 0);
    /* text crossing the clip is given it, and cut there */
    clear(BG);
    oldraw_clip(d, &half);
    oldraw_text(d, 20, 20, "abcd", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0);
    oldraw_clip(d, NULL);
    CHECK(texts_clipped == 1);
    CHECK(count(30, 0, W - 30, H, TEXT) == 0 && count(20, 13, 10, 9, TEXT) > 0);
    /* a text wholly off the target is not drawn at all */
    texts = 0;
    oldraw_text(d, W + 10, 20, "abcd", (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0);
    CHECK(texts == 0);
    /* a long text is measured each time (not kept), and still right */
    {
        static const char longtext[] = "0123456789012345678901234567890123456789012345678901234567890123456789";
        oldraw_text_size s;
        CHECK(oldraw_text_measure(d, longtext, (size_t)-1, NULL, 0, &s) && s.width == 70 * 6);
    }
    /* the fonts change: the remembered sizes go with oldraw_theme_changed, and the new font is measured */
    {
        oldraw_text_size s;
        CHECK(oldraw_text_measure(d, "label", (size_t)-1, NULL, 0, &s) && s.width == 30);
        font_extra = 2;
        CHECK(oldraw_text_measure(d, "label", (size_t)-1, NULL, 0, &s) && s.width == 30);   /* still remembered */
        oldraw_theme_changed(d);
        CHECK(oldraw_text_measure(d, "label", (size_t)-1, NULL, 0, &s) && s.width == 40);
        font_extra = 0;
        oldraw_theme_changed(d);
    }
    /* selected and ghosted together: darkened, then dimmed (as each alone) */
    {
        uint32_t one = 0xFF808080u;
        oldraw_image *im = oldraw_image_argb(d, 1, 1, &one, 1);
        const uint32_t *v;
        CHECK(im != NULL);
        v = oldraw_image_variant(im, OLDRAW_SELECTED);
        CHECK(v && v[0] == 0xFF404040u);
        v = oldraw_image_variant(im, OLDRAW_GHOSTED);
        CHECK(v && v[0] == 0xFF808080u);
        v = oldraw_image_variant(im, OLDRAW_SELECTED | OLDRAW_GHOSTED);
        CHECK(v && v[0] == 0xFF606060u);
        oldraw_image_free(d, im);
        CHECK(oldraw_image_argb(d, 16384, 1, &one, 16384) == NULL);      /* too wide for a 16-bit modulo */
    }
}

/* ---- lines, polygons and ellipses ---- */
#define RED 0xFF0000u
#define BLUE 0x0000FFu
static uint32_t saved[H][W];

static void test_lines(oldraw *d)
{
    oldraw_rect all = { 0, 0, W, H };
    int x, y, k, f0, same = 1;
    unsigned seed = 12345;
    oldraw_clip(d, &all);
    /* a row: nine pixels in one fill */
    clear(BG);
    f0 = fills;
    oldraw_line(d, 2, 3, 10, 3, 1, OLDRAW_RGB(RED));
    CHECK(count(2, 3, 9, 1, RED) == 9 && count(0, 0, W, H, RED) == 9 && fills - f0 == 1);
    /* the diagonal: one pixel on each */
    clear(BG);
    oldraw_line(d, 7, 7, 0, 0, 1, OLDRAW_RGB(RED));
    for (k = 0, x = 0; x < 8; ++x) k += px[x][x] == RED;
    CHECK(k == 8 && count(0, 0, W, H, RED) == 8);
    /* steep: one a row, in at most a column's runs */
    clear(BG);
    f0 = fills;
    oldraw_line(d, 5, 0, 7, 20, 1, OLDRAW_RGB(RED));
    for (k = 0, y = 0; y <= 20; ++y) k += count(0, y, W, 1, RED) == 1;
    CHECK(k == 21 && count(0, 0, W, H, RED) == 21 && fills - f0 == 3);
    CHECK(px[0][5] == RED && px[20][7] == RED);
    /* the same pixels whichever end a line starts from */
    for (k = 0; k < 40; ++k) {
        int a, b, c, e;
        seed = seed * 1103515245u + 12345u; a = (int)(seed >> 16) % W;
        seed = seed * 1103515245u + 12345u; b = (int)(seed >> 16) % H;
        seed = seed * 1103515245u + 12345u; c = (int)(seed >> 16) % W;
        seed = seed * 1103515245u + 12345u; e = (int)(seed >> 16) % H;
        clear(BG);
        oldraw_line(d, a, b, c, e, 1, OLDRAW_RGB(RED));
        memcpy(saved, px, sizeof px);
        clear(BG);
        oldraw_line(d, c, e, a, b, 1, OLDRAW_RGB(RED));
        if (memcmp(saved, px, sizeof px)) same = 0;
        if (!px[b][a] || px[b][a] != RED || px[e][c] != RED) same = 0;
    }
    CHECK(same);
    /* far off the target: only the columns inside are walked, and nothing overflows */
    clear(BG);
    f0 = fills;
    oldraw_line(d, -2000000000, 10, 2000000000, 10, 1, OLDRAW_RGB(RED));
    CHECK(count(0, 10, W, 1, RED) == W && count(0, 0, W, H, RED) == W && fills - f0 <= 2);   /* drawn in two halves */
    clear(BG);
    oldraw_line(d, -2000000000, -2000000000, 2000000000, 2000000000, 1, OLDRAW_RGB(RED));
    for (k = 0, x = 0; x < H; ++x) k += px[x][x] == RED;
    CHECK(k >= H - 1 && count(0, 0, W, H, RED) <= W + H);
    clear(BG);
    oldraw_line(d, -2000000000, -2000000000, 2000000000, 2000000000, 3, OLDRAW_RGB(RED));
    CHECK(px[20][20] == RED && px[20][21] == RED && px[5][40] == BG);
    /* cut to the clip */
    {
        oldraw_rect box = { 10, 10, 8, 8 };
        clear(BG);
        oldraw_clip(d, &box);
        oldraw_line(d, 0, 14, 63, 14, 1, OLDRAW_RGB(RED));
        oldraw_line(d, 0, 0, 40, 40, 5, OLDRAW_RGB(RED));
        CHECK(count(10, 14, 8, 1, RED) == 8 && count_not(0, 0, W, H, BG) == count_not(10, 10, 8, 8, BG));
        oldraw_clip(d, &all);
    }
    /* wide: a band centred on the line, both points' pixels in */
    clear(BG);
    oldraw_line(d, 5, 10, 15, 10, 3, OLDRAW_RGB(RED));
    CHECK(count(5, 9, 11, 3, RED) == 33 && count(0, 0, W, H, RED) == 33);
    clear(BG);
    oldraw_line(d, 20, 5, 20, 15, 4, OLDRAW_RGB(RED));
    CHECK(count(18, 5, 4, 11, RED) == 44 && count(0, 0, W, H, RED) == 44);
    clear(BG);
    oldraw_line(d, 30, 30, 30, 30, 3, OLDRAW_RGB(RED));
    CHECK(count(29, 29, 3, 3, RED) == 9 && count(0, 0, W, H, RED) == 9);
    clear(BG);
    oldraw_line(d, 10, 10, 30, 30, 4, OLDRAW_RGB(RED));
    CHECK(px[20][20] == RED && px[21][19] == RED && px[19][21] == RED && px[10][30] == BG && px[30][10] == BG);
    oldraw_line(d, 1, 1, 9, 1, 2, OLDRAW_NONE);
    CHECK(count(0, 0, W, 4, RED) == 0);
    /* a polyline: closed, a triangle's three sides */
    {
        oldraw_point tri[3] = { { 2, 2 }, { 12, 2 }, { 2, 12 } }, one = { 40, 40 };
        clear(BG);
        oldraw_polyline(d, tri, 3, 1, 1, OLDRAW_RGB(RED));
        CHECK(count(2, 2, 11, 1, RED) == 11 && count(2, 2, 1, 11, RED) == 11 && px[7][7] == RED && px[5][5] == BG);
        clear(BG);
        oldraw_polyline(d, tri, 3, 0, 1, OLDRAW_RGB(RED));
        CHECK(px[7][2] == BG && px[7][7] == RED && count(2, 2, 11, 1, RED) == 11);   /* not closed: no left side */
        clear(BG);
        oldraw_polyline(d, &one, 1, 0, 1, OLDRAW_RGB(RED));
        CHECK(px[40][40] == RED && count(0, 0, W, H, RED) == 1);
    }
}

static void test_polygons(oldraw *d)
{
    oldraw_point box[4] = { { 2, 2 }, { 10, 2 }, { 10, 6 }, { 2, 6 } };
    oldraw_point t1[3] = { { 20, 4 }, { 40, 4 }, { 40, 30 } }, t2[3] = { { 20, 4 }, { 40, 30 }, { 20, 30 } };
    oldraw_point star[5] = { { 32, 4 }, { 44, 40 }, { 13, 18 }, { 51, 18 }, { 20, 40 } };
    oldraw_point ring[40];
    int n1, n2, k;
    /* a box: the pixels its corners hold between them, as oldraw_fill fills them */
    clear(BG);
    oldraw_polygon(d, box, 4, OLDRAW_RGB(RED));
    CHECK(count(2, 2, 8, 4, RED) == 32 && count(0, 0, W, H, RED) == 32);
    /* two triangles sharing an edge: no pixel twice and none left out */
    clear(BG);
    oldraw_polygon(d, t1, 3, OLDRAW_RGB(RED));
    n1 = count(0, 0, W, H, RED);
    clear(BG);
    oldraw_polygon(d, t2, 3, OLDRAW_RGB(BLUE));
    n2 = count(0, 0, W, H, BLUE);
    oldraw_polygon(d, t1, 3, OLDRAW_RGB(RED));
    CHECK(n1 + n2 == 20 * 26 && count(20, 4, 20, 26, RED) + count(20, 4, 20, 26, BLUE) == 20 * 26);
    CHECK(count(0, 0, W, H, RED) == n1 && count(0, 0, W, H, BLUE) == n2);
    /* even-odd: a five-pointed star's middle is left empty */
    clear(BG);
    oldraw_polygon(d, star, 5, OLDRAW_RGB(RED));
    CHECK(px[24][32] == BG && px[10][32] == RED && px[19][16] == RED && px[19][47] == RED);
    /* more corners than the stack holds; fewer than three draw nothing */
    for (k = 0; k < 40; ++k) {
        static const int dx[40] = { 0, 3, 6, 9, 11, 14, 16, 17, 19, 19, 20, 19, 19, 17, 16, 14, 11, 9, 6, 3,
                                    0, -3, -6, -9, -11, -14, -16, -17, -19, -19, -20, -19, -19, -17, -16, -14, -11, -9,
                                    -6, -3 };
        ring[k].x = 32 + dx[k];
        ring[k].y = 24 + dx[(k + 30) % 40];
    }
    clear(BG);
    oldraw_polygon(d, ring, 40, OLDRAW_RGB(RED));
    k = count(0, 0, W, H, RED);
    CHECK(k > 1000 && k < 1300 && px[24][32] == RED && px[0][0] == BG);
    clear(BG);
    oldraw_polygon(d, box, 2, OLDRAW_RGB(RED));
    oldraw_polygon(d, NULL, 4, OLDRAW_RGB(RED));
    oldraw_polygon(NULL, box, 4, OLDRAW_RGB(RED));
    CHECK(count(0, 0, W, H, RED) == 0);
    /* cut to the clip, and far corners don't overflow */
    {
        oldraw_rect c = { 4, 4, 6, 6 }, all = { 0, 0, W, H };
        oldraw_point big[3] = { { -2000000000, -2000000000 }, { 2000000000, -2000000000 }, { 0, 2000000000 } };
        clear(BG);
        oldraw_clip(d, &c);
        oldraw_polygon(d, big, 3, OLDRAW_RGB(RED));
        CHECK(count(4, 4, 6, 6, RED) == 36 && count(0, 0, W, H, RED) == 36);
        oldraw_clip(d, &all);
    }
}

static void test_ellipses(oldraw *d)
{
    oldraw_rect c = { 10, 10, 21, 21 }, e = { 2, 30, 40, 12 }, dot = { 50, 5, 1, 1 }, in;
    int x, y, n, sym = 1, match = 1;
    clear(BG);
    oldraw_ellipse(d, &c, OLDRAW_RGB(RED));
    n = count(0, 0, W, H, RED);
    CHECK(n > 300 && n < 380);                                     /* pi * 10.5^2 = 346 */
    CHECK(px[20][20] == RED && px[10][20] == RED && px[20][10] == RED && px[10][10] == BG && px[30][30] == BG);
    for (y = 10; y < 31; ++y)
        for (x = 10; x < 31; ++x) {
            if (px[y][x] != px[40 - y][x] || px[y][x] != px[y][40 - x] || px[y][x] != px[x][y]) sym = 0;
            if ((px[y][x] == RED) != oldraw_ellipse_has(&c, x, y)) match = 0;
        }
    CHECK(sym && match && count(0, 0, W, H, RED) == count(10, 10, 21, 21, RED));
    clear(BG);
    oldraw_ellipse(d, &e, OLDRAW_RGB(RED));
    CHECK(count(2, 30, 40, 12, RED) == count(0, 0, W, H, RED) && px[36][2] == RED && px[36][41] == RED &&
          px[30][22] == RED && px[41][22] == RED && px[30][2] == BG);
    clear(BG);
    oldraw_ellipse(d, &dot, OLDRAW_RGB(RED));
    CHECK(px[5][50] == RED && count(0, 0, W, H, RED) == 1);
    /* the outline: what the ellipse fills less the one inset by the width */
    clear(BG);
    oldraw_ellipse_frame(d, &c, 2, OLDRAW_RGB(RED));
    in.x = 12; in.y = 12; in.w = 17; in.h = 17;
    for (match = 1, y = 0; y < H; ++y)
        for (x = 0; x < W; ++x)
            if ((px[y][x] == RED) != (oldraw_ellipse_has(&c, x, y) && !oldraw_ellipse_has(&in, x, y))) match = 0;
    CHECK(match && px[20][20] == BG && px[10][20] == RED && px[11][20] == RED && px[12][20] == BG);
    clear(BG);
    oldraw_ellipse_frame(d, &c, 20, OLDRAW_RGB(RED));
    CHECK(count(0, 0, W, H, RED) == n);
    /* huge: clamped, no overflow; empty: nothing */
    {
        oldraw_rect huge = { -1000000, -1000000, 2000000, 2000000 }, none = { 5, 5, 0, 9 };
        clear(BG);
        oldraw_ellipse(d, &huge, OLDRAW_RGB(RED));
        oldraw_ellipse(d, &none, OLDRAW_RGB(BLUE));
        CHECK(count(0, 0, W, H, BLUE) == 0 && !oldraw_ellipse_has(&none, 5, 5) && !oldraw_ellipse_has(NULL, 0, 0));
    }
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
    test_lines(d);
    test_polygons(d);
    test_ellipses(d);
    test_copy_scroll(d);
    test_speed(d);
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
