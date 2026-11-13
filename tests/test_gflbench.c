/* GFLayoutBench's portable part (bench/gflbench.c): its integer maths, and a whole run on a target whose clock
 * moves only when something is drawn (1 ms a target call), so every figure is known in advance and the timing is
 * checked, not only that lines come out. MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gflbench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint64_t clock_ticks;
static int syncs, flushes;
static void tick(void) { clock_ticks += 1000; }
static void f_fill(void *t, int x, int y, int w, int h, oldraw_colour c) { (void)t; (void)x; (void)y; (void)w; (void)h; (void)c; tick(); }
static void f_invert(void *t, int x, int y, int w, int h) { (void)t; (void)x; (void)y; (void)w; (void)h; tick(); }
static void f_image(void *t, const oldraw_image *img, int sx, int sy, int x, int y, int w, int h, unsigned fl)
{
    (void)t; (void)img; (void)sx; (void)sy; (void)x; (void)y; (void)w; (void)h; (void)fl;
    tick();
}
static int f_measure(void *t, const char *s, size_t n, const oldraw_font *f, oldraw_text_size *z)
{
    (void)t; (void)s; (void)f;
    z->width = (int)n * 6; z->ascent = 7; z->descent = 2;
    return 1;
}
static int f_text(void *t, int x, int y, const char *s, size_t n, const oldraw_font *f, oldraw_colour c, unsigned st,
                  const oldraw_rect *clip)
{
    (void)t; (void)x; (void)y; (void)s; (void)n; (void)f; (void)c; (void)st; (void)clip;
    tick();
    return 1;
}
static void f_flush(void *t) { (void)t; ++flushes; }
static const oldraw_ops fops = { f_fill, f_invert, f_image, f_text, f_measure, f_flush, NULL };

static uint64_t f_now(void *u) { (void)u; return clock_ticks; }
static void f_sync(void *u) { (void)u; ++syncs; }

static char lines[64][160];
static int nlines;
static void f_out(void *u, const char *l)
{
    (void)u;
    if (nlines < 64) strncpy(lines[nlines++], l, 159);
    if (getenv("GFLBENCH_TEST_VERBOSE")) puts(l);
}

static const char *value_of(const char *name)
{
    static char v[64];
    int i;
    size_t n = strlen(name);
    for (i = 0; i < nlines; ++i)
        if (!strncmp(lines[i], "BENCH ", 6) && !strncmp(lines[i] + 6, name, n) && lines[i][6 + n] == ' ') {
            sscanf(lines[i] + 7 + n, "%63s", v);
            return v;
        }
    return NULL;
}

static void test_maths(void)
{
    char b[24];
    uint64_t v[5] = { 5, 1, 4, 2, 3 }, w[4] = { 4, 1, 3, 2 };
    CHECK(gflbench_rate100(1000, 1000000, 1000000) == 100000);         /* 1000 in a second: 1000.00 */
    CHECK(gflbench_rate100(3, 2, 1) == 150);                           /* 1.5 a second */
    CHECK(gflbench_rate100(1, 0, 1000) == 100000);                     /* no time: one tick */
    CHECK(gflbench_rate100(4000000000UL, 709379, 709379) == 400000000000ULL);  /* big n, E clock */
    CHECK(gflbench_rate100(UINT64_MAX, 1, 1000000) == UINT64_MAX);     /* saturates */
    CHECK(gflbench_ms100(709379, 709379) == 100000);                   /* a second: 1000.00 ms */
    CHECK(gflbench_ms100(1234, 1000000) == 123);                       /* 1.23 ms */
    CHECK(!strcmp(gflbench_fmt100(0, b), "0.00"));
    CHECK(!strcmp(gflbench_fmt100(5, b), "0.05"));
    CHECK(!strcmp(gflbench_fmt100(123456, b), "1234.56"));
    CHECK(!strcmp(gflbench_fmt100(UINT64_MAX, b), "184467440737095516.15"));
    CHECK(gflbench_median(v, 5) == 3 && v[0] == 1 && v[4] == 5);
    CHECK(gflbench_median(w, 4) == 2);                                /* (2 + 3) / 2, whole */
    CHECK(gflbench_median(v, 1) == 1);
    CHECK(gflbench_wanted(NULL, "text_32") && gflbench_wanted("", "text_32"));
    CHECK(gflbench_wanted("pie_segment,text_32", "text_32"));
    CHECK(!gflbench_wanted("text_32x,text", "text_32"));
}

static void test_run(void)
{
    static const char *names[] = { "fill_128", "frame_128", "text_32", "text_box_cut", "icon_blit_argb",
                                   "icon_blit_selected", "icon_blit_diskobject", "list_row", "select_box",
                                   "progress_bar", "pie_segment", "damage_redraw", "drawer_200_redraw",
                                   "drawer_200_icons_per_frame", "list_scroll_1000" };
    gflbench_env e;
    oldraw *d = oldraw_new(&fops, NULL, GFLBENCH_CANVAS_W, GFLBENCH_CANVAS_H, NULL);
    unsigned i;
    int na, bench_lines = 0;
    const char *v;
    memset(&e, 0, sizeof e);
    e.d = d;
    e.now = f_now;
    e.hz = 1000000u;
    e.sync = f_sync;
    e.out = f_out;
    e.run_ms = 200;
    na = gflbench_run(&e);
    CHECK(na == 1);                               /* the DiskObject icon: none on the host */
    CHECK(syncs > 0 && flushes >= syncs);
    for (i = 0; i < sizeof names / sizeof *names; ++i) CHECK(value_of(names[i]) != NULL);
    for (i = 0; i < (unsigned)nlines; ++i) {
        char name[64], value[64], unit[64], extra[8];
        if (strncmp(lines[i], "BENCH ", 6)) continue;
        ++bench_lines;
        /* exactly BENCH <name> <value> <unit> */
        CHECK(sscanf(lines[i], "BENCH %63s %63s %63s %7s", name, value, unit, extra) == 3);
    }
    CHECK(bench_lines == (int)(sizeof names / sizeof *names));
    CHECK(!strcmp(lines[nlines - 1], "BENCH-END ok 1 n/a"));
    /* one target call each, at 1 ms: 1000 a second */
    CHECK((v = value_of("fill_128")) && !strcmp(v, "1000.00"));
    CHECK((v = value_of("text_32")) && !strcmp(v, "1000.00"));
    CHECK((v = value_of("icon_blit_argb")) && !strcmp(v, "1000.00"));
    /* the rest depend on how many target calls a primitive makes (Team 3's to change): only that they ran */
    CHECK((v = value_of("select_box")) && atof(v) > 0.0);
    CHECK((v = value_of("list_scroll_1000")) && atof(v) > 0.0);
    CHECK(!strcmp(value_of("icon_blit_diskobject"), "n/a"));
    /* the drawer: a fill and 200 icons with their labels; 200 x 2 + 1 calls at least, 401 ms and more */
    CHECK((v = value_of("drawer_200_redraw")) && atof(v) >= 401.0);
    oldraw_free(d);
}

int main(void)
{
    test_maths();
    test_run();
    printf("test_gflbench: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
