/*
 * GFOpenLayout drawing as a program calls it through gfopenlayout.library 25
 * (library/link/gfopenlayout_link_draw.c): each call once, on a target of
 * 0xRRGGBB pixels the test keeps itself, with what it draws or answers. Only
 * gfopenlayout_draw.h is used, nothing of the drawing's inside. On the host
 * this links draw/oldraw.c, so the same checks say what the library must
 * answer; build-library.sh links it with libgfopenlayout_link.a as
 * LayoutDrawTestLib, where every oldraw_ call goes through the library and
 * the target's calls come back from it into this program.
 * Text is a block font: each byte a 6-pixel advance, 7 up and 2 down.
 * MIT, Copyright (c) 2026 AmiCradle Team.
 */
#include "gfopenlayout_draw.h"

#include <stdio.h>
#include <string.h>

#define W 64
#define H 48

static uint32_t px[H][W];
static int failures, checks;
static int flushes, frees, texts, redraws;
static oldraw *cur;

#define CHECK(x) do { ++checks; if (!(x)) { \
    printf("FAIL line=%d expr=%s\n", __LINE__, #x); ++failures; \
} } while (0)

static void t_fill(void *t, int x, int y, int w, int h, oldraw_colour c)
{
    int i, j;
    uint32_t v = oldraw_resolve(cur, c) & 0xFFFFFFu;
    (void)t;
    for (j = y; j < y + h; ++j)
        for (i = x; i < x + w; ++i) {
            if (i < 0 || j < 0 || i >= W || j >= H) { ++failures; printf("FAIL fill outside the target\n"); return; }
            px[j][i] = v;
        }
}

static void t_invert(void *t, int x, int y, int w, int h)
{
    int i, j;
    (void)t;
    for (j = y; j < y + h; ++j)
        for (i = x; i < x + w; ++i)
            if (i >= 0 && j >= 0 && i < W && j < H) px[j][i] ^= 0xFFFFFFu;
}

static void t_image(void *t, const oldraw_image *img, int sx, int sy, int x, int y, int w, int h, unsigned flags)
{
    int i, j;
    (void)t;
    (void)flags;
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i) {
            uint32_t p = oldraw_image_pixel(img, sx + i, sy + j);
            if ((p >> 24) >= 128 && x + i >= 0 && y + j >= 0 && x + i < W && y + j < H) px[y + j][x + i] = p & 0xFFFFFFu;
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
                if ((!clip || (i >= clip->x && i < clip->x + clip->w && j >= clip->y && j < clip->y + clip->h)) &&
                    i >= 0 && j >= 0 && i < W && j < H)
                    px[j][i] = v;
    return 1;
}

static void t_flush(void *t) { (void)t; ++flushes; }
static void t_image_free(void *t, oldraw_image *img) { (void)t; (void)img; ++frees; }

static int t_copy(void *t, int sx, int sy, int dx, int dy, int w, int h)
{
    static uint32_t tmp[H][W];
    int i, j;
    (void)t;
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i) tmp[j][i] = px[sy + j][sx + i];
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i) px[dy + j][dx + i] = tmp[j][i];
    return 0;
}

static const oldraw_ops ops = { t_fill, t_invert, t_image, t_text, t_measure, t_flush, t_image_free, t_copy };

static const oldraw_colour pens[OLDRAW_PEN_COUNT] = {
    0xFF000000UL, 0xFFAAAAAAUL, 0xFF000001UL, 0xFFFFFFFFUL, 0xFF000002UL, 0xFF0000FFUL,
    0xFFFFFF00UL, 0xFFAAAAAAUL, 0xFFFFFFFEUL, 0xFF000000UL, 0xFFFFFFFFUL, 0xFF000000UL
};
#define BG 0xAAAAAAu
#define RED 0xCC0000u

static void clear(void)
{
    int i, j;
    for (j = 0; j < H; ++j)
        for (i = 0; i < W; ++i) px[j][i] = BG;
}

static int count(int x, int y, int w, int h, uint32_t v)
{
    int i, j, n = 0;
    for (j = y; j < y + h; ++j)
        for (i = x; i < x + w; ++i)
            if (i >= 0 && j >= 0 && i < W && j < H && px[j][i] == v) ++n;
    return n;
}

static int all(uint32_t v) { return count(0, 0, W, H, v); }

/* what a redraw is given: its userdata, the oldraw, and the rectangle to draw; it fills that rectangle */
static void redraw(void *userdata, oldraw *d, const oldraw_rect *area)
{
    int *sum = (int *)userdata;
    ++redraws;
    *sum += area->w * area->h;
    oldraw_fill(d, area, OLDRAW_RGB(RED));
    /* outside the rectangle is cut off */
    {
        oldraw_rect whole = { 0, 0, W, H };
        oldraw_fill(d, &whole, OLDRAW_RGB(RED));
    }
}

int main(void)
{
    oldraw *d = oldraw_new(&ops, NULL, W, H, pens);
    CHECK(d != NULL);
    if (!d) { printf("LINK-DRAW FAIL checks=%d failures=%d\n", checks, failures + 1); return 1; }
    cur = d;

    /* ---- colours ---- */
    CHECK(oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_FILL)) == 0xFF0000FFUL);
    CHECK(oldraw_resolve(d, OLDRAW_RGB(0x123456)) == 0xFF123456UL);

    /* ---- boxes and the clip ---- */
    {
        oldraw_rect r = { 10, 10, 8, 6 }, c = { 12, 0, 3, 48 };
        clear();
        oldraw_fill(d, &r, OLDRAW_RGB(RED));
        CHECK(count(10, 10, 8, 6, RED) == 48 && all(RED) == 48);
        clear();
        oldraw_clip(d, &c);
        oldraw_fill(d, &r, OLDRAW_RGB(RED));
        CHECK(all(RED) == 18);
        oldraw_clip(d, NULL);
        clear();
        oldraw_frame(d, &r, 0);
        CHECK(count(10, 10, 7, 1, 0xFFFFFFu) == 7 && count(11, 15, 7, 1, 0x000002u) == 7);
        CHECK(count(11, 11, 6, 4, BG) == 24);
    }

    /* ---- damage and redraw: the program's own call, called from the drawing ---- */
    {
        oldraw_rect a = { 2, 2, 4, 4 }, b = { 40, 30, 10, 5 }, got;
        int sum = 0, n;
        clear();
        oldraw_damage(d, &a);
        oldraw_damage(d, &b);
        CHECK(oldraw_damage_count(d) == 2);
        CHECK(oldraw_damage_get(d, 0, &got) && got.w * got.h + 0 > 0);
        CHECK(!oldraw_damage_get(d, 2, &got));
        flushes = 0;
        n = oldraw_redraw(d, redraw, &sum);
        CHECK(n == 2 && redraws == 2 && sum == 16 + 50);
        CHECK(all(RED) == 16 + 50 && count(2, 2, 4, 4, RED) == 16 && count(40, 30, 10, 5, RED) == 50);
        CHECK(oldraw_damage_count(d) == 0 && flushes >= 1);
        oldraw_damage_all(d);
        CHECK(oldraw_damage_count(d) == 1 && oldraw_damage_get(d, 0, &got) && got.w == W && got.h == H);
        oldraw_resize(d, W, H);                 /* the damage is dropped */
        CHECK(oldraw_damage_count(d) == 0);
        flushes = 0;
        oldraw_flush(d);
        CHECK(flushes == 1);
    }

    /* ---- copy and scroll ---- */
    {
        oldraw_rect src = { 0, 0, 4, 4 }, area = { 0, 20, 20, 10 }, row = { 0, 20, 20, 2 }, got;
        clear();
        oldraw_fill(d, &src, OLDRAW_RGB(RED));
        oldraw_copy(d, &src, 30, 30);
        CHECK(count(30, 30, 4, 4, RED) == 16 && all(RED) == 32);
        clear();
        oldraw_fill(d, &row, OLDRAW_RGB(RED));
        oldraw_scroll(d, &area, 0, 3);          /* down three rows: the strip at the top is damaged */
        CHECK(count(0, 23, 20, 2, RED) == 40);
        CHECK(oldraw_damage_count(d) == 1 && oldraw_damage_get(d, 0, &got) && got.y == 20 && got.h == 3 && got.w == 20);
        oldraw_resize(d, W, H);
    }

    /* ---- text ---- */
    {
        oldraw_text_size size;
        oldraw_rect box = { 4, 4, 20, 12 };
        size_t drawn;
        memset(&size, 0, sizeof size);
        CHECK(oldraw_text_measure(d, "abcd", 4, NULL, 0, &size) && size.width == 24 && size.ascent == 7 && size.descent == 2);
        clear();
        texts = 0;
        CHECK(oldraw_text(d, 2, 20, "ab", (size_t)-1, NULL, OLDRAW_RGB(RED), 0) == 12);
        CHECK(texts == 1 && count(2, 13, 5, 9, RED) == 45 && count(8, 13, 5, 9, RED) == 45 && all(RED) == 90);
        clear();
        drawn = oldraw_text_box(d, &box, "abcdefgh", 8, NULL, OLDRAW_RGB(RED), 0, OLDRAW_LEFT);
        CHECK(drawn < 8);                       /* 48 pixels of text in a box of 20: cut */
        CHECK(all(RED) > 0 && count(4, 4, 20, 12, RED) == all(RED));
        oldraw_theme_changed(d);                /* remembered sizes dropped: the same answer again */
        CHECK(oldraw_text_measure(d, "abcd", 4, NULL, 0, &size) && size.width == 24);
        oldraw_outline_text(d, 1);              /* not the Amiga target: nothing changes */
        CHECK(oldraw_text_measure(d, "ab", 2, NULL, 0, &size) && size.width == 12);
    }

    /* ---- pictures ---- */
    {
        uint32_t argb[4 * 3];
        const oldraw_rect *blocks = NULL;
        oldraw_image *img;
        int i, n;
        for (i = 0; i < 12; ++i) argb[i] = 0xFF000000UL | RED;
        argb[0] = 0x00FFFFFFUL;                 /* see-through */
        img = oldraw_image_argb(d, 4, 3, argb, 4);
        CHECK(img != NULL);
        if (img) {
            CHECK(oldraw_image_width(img) == 4 && oldraw_image_height(img) == 3);
            CHECK(oldraw_image_pixel(img, 1, 0) == (0xFF000000UL | RED) && oldraw_image_pixel(img, 0, 0) == 0x00FFFFFFUL);
            CHECK(oldraw_image_pixel(img, 9, 9) == 0);
            n = oldraw_image_blocks(img, &blocks);
            CHECK(n >= 1 && blocks != NULL);
            if (n >= 1 && blocks) {
                int area = 0;
                for (i = 0; i < n; ++i) area += blocks[i].w * blocks[i].h;
                CHECK(area == 11);
            }
            clear();
            oldraw_image_draw(d, img, 20, 20, 0);
            CHECK(all(RED) == 11 && px[20][20] == BG && px[20][21] == RED);
            frees = 0;
            oldraw_image_free(d, img);
            CHECK(frees == 1);
        }
        CHECK(oldraw_image_icon(d, NULL) == NULL);
    }

    /* ---- a list's row ---- */
    {
        oldraw_cell cells[2];
        oldraw_row row;
        oldraw_rect r = { 0, 30, 60, 12 }, cell;
        memset(cells, 0, sizeof cells);
        memset(&row, 0, sizeof row);
        cells[0].text = "ab";
        cells[0].width = 30;
        cells[1].text = "c";
        row.cells = cells;
        row.ncells = 2;
        row.flags = OLDRAW_ROW_SELECTED;
        row.pad = 2;
        clear();
        oldraw_list_row(d, &r, &row);
        CHECK(count(0, 30, 60, 12, 0x0000FFu) > 0 && count(0, 30, 60, 12, 0xFFFF00u) == 3 * 45);
        CHECK(count(0, 0, W, 30, BG) == W * 30);
        CHECK(oldraw_list_cell_box(&row, &r, 1, &cell) && cell.x == 32 && cell.w == 26);
        CHECK(!oldraw_list_cell_box(&row, &r, 2, &cell));
    }

    /* ---- the selection box: the same call again takes it off ---- */
    clear();
    oldraw_select_box(d, 5, 5, 20, 15);
    CHECK(all(BG) < W * H);
    oldraw_select_box(d, 5, 5, 20, 15);
    CHECK(all(BG) == W * H);

    /* ---- a progress bar ---- */
    {
        oldraw_rect r = { 4, 4, 42, 8 };
        int fill = oldraw_progress_fill(40, 1, 4);
        CHECK(fill == 10 && oldraw_progress_fill(40, 9, 4) == 40 && oldraw_progress_fill(40, 1, 0) == 0);
        clear();
        oldraw_progress(d, &r, 1, 4, 0);
        CHECK(count(5, 5, 40, 6, 0x0000FFu) == fill * 6);
    }

    /* ---- a pie segment ---- */
    CHECK(oldraw_pie_sweep(1, 4) == 900 && oldraw_pie_sweep(5, 4) == 3600 && oldraw_pie_sweep(1, 0) == 0);
    CHECK(oldraw_pie_has(10, 0, 900, 3, -3) && !oldraw_pie_has(10, 0, 900, -3, 3));
    {
        int i, j, n = 0;
        clear();
        oldraw_pie(d, 32, 24, 10, 0, 900, OLDRAW_RGB(RED));
        for (j = -12; j <= 12; ++j)
            for (i = -12; i <= 12; ++i)
                if (oldraw_pie_has(10, 0, 900, i, j)) { ++n; CHECK(px[24 + j][32 + i] == RED); }
        CHECK(n > 0 && all(RED) == n);
    }

    /* ---- lines, polygons and ellipses ---- */
    {
        oldraw_point tri[3] = { { 2, 2 }, { 10, 2 }, { 10, 6 } }, box[4] = { { 2, 2 }, { 10, 2 }, { 10, 6 }, { 2, 6 } };
        oldraw_rect e = { 20, 10, 12, 8 };
        int i, j, n = 0;
        clear();
        oldraw_line(d, 3, 3, 12, 3, 1, OLDRAW_RGB(RED));
        CHECK(all(RED) == 10 && count(3, 3, 10, 1, RED) == 10);
        clear();
        oldraw_line(d, 3, 10, 12, 10, 3, OLDRAW_RGB(RED));
        CHECK(all(RED) > 10 && count(3, 9, 10, 3, RED) == all(RED));
        clear();
        oldraw_polyline(d, tri, 3, 1, 1, OLDRAW_RGB(RED));
        CHECK(px[2][2] == RED && px[2][10] == RED && px[6][10] == RED && px[4][6] == RED);
        clear();
        oldraw_polygon(d, box, 4, OLDRAW_RGB(RED));
        CHECK(all(RED) == 32 && count(2, 2, 8, 4, RED) == 32);  /* what oldraw_fill fills for 2, 2, 8, 4 */
        clear();
        oldraw_ellipse(d, &e, OLDRAW_RGB(RED));
        for (j = 0; j < H; ++j)
            for (i = 0; i < W; ++i)
                if (oldraw_ellipse_has(&e, i, j)) { ++n; CHECK(px[j][i] == RED); }
        CHECK(n > 40 && n < 96 && all(RED) == n);
        clear();
        oldraw_ellipse_frame(d, &e, 1, OLDRAW_RGB(RED));
        CHECK(all(RED) > 0 && all(RED) < n && px[14][26] == BG);
    }

#ifdef __amigaos__
    /* the Amiga target's calls, with nothing for them to draw on */
    CHECK(oldraw_new_rastport(NULL, NULL, 1, 1) == NULL);
    oldraw_pens_changed(d);                     /* not an Amiga target: left as it is */
    CHECK(oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_FILL)) == 0xFF0000FFUL);
#endif

    oldraw_free(d);
    if (failures) {
        printf("LINK-DRAW FAIL checks=%d failures=%d\n", checks, failures);
        return 1;
    }
    printf("LINK-DRAW PASS checks=%d\n", checks);
    return 0;
}
