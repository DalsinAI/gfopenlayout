/* GFLayoutBench on the host: gflbench.c through oldraw on a target of 0xRRGGBB pixels in memory, timed by
 * CLOCK_MONOTONIC. It is the CPU-only figure for the host until GFOpenGPU's target (wave N, N1) comes in behind the
 * same oldraw calls; then the same program runs on that target. Text is a block font, as tests/test_draw.c's: each
 * byte a 6-pixel advance, drawn as a 5 by 9 block.
 *
 *     gflbench-host [--runs N] [--ms N] [--only a,b,c]
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#define _POSIX_C_SOURCE 199309L
#include "gflbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t *px;
static oldraw *cur;

static void h_fill(void *t, int x, int y, int w, int h, oldraw_colour c)
{
    uint32_t v = oldraw_resolve(cur, c) & 0xFFFFFFu;
    int i, j;
    (void)t;
    for (j = y; j < y + h; ++j) {
        uint32_t *p = px + (size_t)j * GFLBENCH_CANVAS_W + x;
        for (i = 0; i < w; ++i) p[i] = v;
    }
}

static void h_invert(void *t, int x, int y, int w, int h)
{
    int i, j;
    (void)t;
    for (j = y; j < y + h; ++j) {
        uint32_t *p = px + (size_t)j * GFLBENCH_CANVAS_W + x;
        for (i = 0; i < w; ++i) p[i] ^= 0xFFFFFFu;
    }
}

static void h_image(void *t, const oldraw_image *img, int sx, int sy, int x, int y, int w, int h, unsigned flags)
{
    int i, j;
    (void)t;
    for (j = 0; j < h; ++j) {
        uint32_t *p = px + (size_t)(y + j) * GFLBENCH_CANVAS_W + x;
        for (i = 0; i < w; ++i) {
            uint32_t a = oldraw_image_pixel(img, sx + i, sy + j);
            if ((a >> 24) < 0x80) continue;
            a &= 0xFFFFFFu;
            if (flags & OLDRAW_SELECTED) a = (a >> 1) & 0x7F7F7Fu;
            if (flags & OLDRAW_GHOSTED) a = (a | 0x808080u);
            p[i] = a;
        }
    }
}

static int h_measure(void *t, const char *s, size_t n, const oldraw_font *f, oldraw_text_size *size)
{
    (void)t;
    (void)s;
    (void)f;
    size->width = (int)n * 6;
    size->ascent = 7;
    size->descent = 2;
    return 1;
}

static int h_text(void *t, int x, int y, const char *s, size_t n, const oldraw_font *f, oldraw_colour c, unsigned st,
                  const oldraw_rect *clip)
{
    uint32_t v = oldraw_resolve(cur, c) & 0xFFFFFFu;
    size_t k;
    int i, j;
    (void)t;
    (void)s;
    (void)f;
    (void)st;
    for (k = 0; k < n; ++k)
        for (j = y - 7; j < y + 2; ++j) {
            if (j < clip->y || j >= clip->y + clip->h) continue;
            for (i = x + (int)k * 6; i < x + (int)k * 6 + 5; ++i)
                if (i >= clip->x && i < clip->x + clip->w) px[(size_t)j * GFLBENCH_CANVAS_W + i] = v;
        }
    return 1;
}

static void h_flush(void *t) { (void)t; }

static const oldraw_ops host_ops = { h_fill, h_invert, h_image, h_text, h_measure, h_flush, NULL };

static uint64_t h_now(void *u)
{
    struct timespec ts;
    (void)u;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void h_out(void *u, const char *line)
{
    (void)u;
    puts(line);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    gflbench_env e;
    int i, na;
    memset(&e, 0, sizeof e);
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--runs") && i + 1 < argc) e.runs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ms") && i + 1 < argc) e.run_ms = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--only") && i + 1 < argc) e.only = argv[++i];
        else {
            fprintf(stderr, "usage: %s [--runs N] [--ms N] [--only a,b,c]\n", argv[0]);
            return 2;
        }
    }
    px = (uint32_t *)calloc((size_t)GFLBENCH_CANVAS_W * GFLBENCH_CANVAS_H, sizeof *px);
    cur = oldraw_new(&host_ops, NULL, GFLBENCH_CANVAS_W, GFLBENCH_CANVAS_H, NULL);
    if (!px || !cur) {
        fprintf(stderr, "gflbench-host: no memory\n");
        return 1;
    }
    e.d = cur;
    e.now = h_now;
    e.hz = 1000000u;
    e.out = h_out;
    printf("BENCH-INFO target host memory %dx%d 0xRRGGBB (CPU only, no GPU)\n", GFLBENCH_CANVAS_W, GFLBENCH_CANVAS_H);
    na = gflbench_run(&e);
    oldraw_free(cur);
    free(px);
    return na < 0 ? 1 : 0;
}
