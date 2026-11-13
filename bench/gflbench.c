/* GFLayoutBench's portable part: what is timed, how, and the lines it prints (gflbench.h).
 *
 * Each result is timed the same way. The work (one draw, one redraw, one scroll) is done once, then twice as often
 * and so on until a run lasts a tenth of a second; then it is sized to last run_ms, done runs times, and the median
 * run is reported. Each run ends with oldraw_flush and the target's sync, so the work counted is done.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gflbench.h"

#include <stdio.h>
#include <string.h>

#define ICON 32
#define DRAWER_ICONS 200
#define DRAWER_CELL_W 64
#define DRAWER_CELL_H 48
#define DRAWER_COLS 16             /* 16 x 13 cells of 64 x 48: 1024 x 624 */
#define LIST_ROWS 1000
#define LIST_ROW_H 18
#define LIST_VISIBLE 20
#define LIST_W 600
#define MAX_RUNS 15

/* ---- integer maths ---- */

/* a * b / c, without overflow when c * b fits (c ticks, b a clock's rate); saturating otherwise */
static uint64_t muldiv(uint64_t a, uint64_t b, uint64_t c)
{
    if (!c) return UINT64_MAX;
    if (!a || !b) return 0;
    if (a <= UINT64_MAX / b) return a * b / c;
    if (a / c > UINT64_MAX / b) return UINT64_MAX;
    return (a / c) * b + ((a % c) * b) / c;
}

uint64_t gflbench_rate100(uint64_t n, uint64_t ticks, uint32_t hz)
{
    if (!ticks) ticks = 1;
    if (n > UINT64_MAX / 100) return UINT64_MAX;
    return muldiv(n * 100, hz, ticks);
}

uint64_t gflbench_ms100(uint64_t ticks, uint32_t hz)
{
    if (!hz) return UINT64_MAX;
    if (ticks > UINT64_MAX / 100000) return UINT64_MAX;
    return muldiv(ticks * 100000, 1, hz);
}

char *gflbench_fmt100(uint64_t v, char *buf)
{
    char tmp[24];
    int i = 0, o = 0;
    uint64_t whole = v / 100;
    do { tmp[i++] = (char)('0' + whole % 10); whole /= 10; } while (whole && i < 22);
    while (i) buf[o++] = tmp[--i];
    buf[o++] = '.';
    buf[o++] = (char)('0' + (v / 10) % 10);
    buf[o++] = (char)('0' + v % 10);
    buf[o] = 0;
    return buf;
}

uint64_t gflbench_median(uint64_t *v, int n)
{
    int i, j;
    if (n <= 0) return 0;
    for (i = 1; i < n; ++i)
        for (j = i; j > 0 && v[j - 1] > v[j]; --j) { uint64_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    return (n & 1) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

int gflbench_wanted(const char *list, const char *name)
{
    size_t n = strlen(name);
    const char *p = list;
    if (!list || !*list) return 1;
    while (*p) {
        const char *e = strchr(p, ',');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len == n && !strncmp(p, name, n)) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

/* ---- the scene ---- */

typedef struct bench {
    gflbench_env *e;
    oldraw_image *icon, *small;
    uint32_t pix[ICON * ICON];
    uint32_t spix[16 * 16];
    unsigned long k;            /* moves things a little each op, so nothing is the same draw twice in a row */
    int top;                    /* the list's first row shown */
    char names[DRAWER_ICONS][16];
} bench;

static const char text32[] = "GFLayoutBench text 0123456789ABC";   /* 32 bytes */
static const char text48[] = "A long file name that is cut to fit its column.info";

static void make_icon(uint32_t *p, int s)
{
    int x, y;
    for (y = 0; y < s; ++y)
        for (x = 0; x < s; ++x) {
            int edge = x == 0 || y == 0 || x == s - 1 || y == s - 1;
            int hole = x > s / 4 && x < s - s / 4 && y > s / 3 && y < s / 2;   /* see-through: alpha is used */
            uint32_t rgb = edge ? 0x000000u : (uint32_t)(((x * 7) & 0xFF) << 16 | ((y * 7) & 0xFF) << 8 | 0x99);
            p[y * s + x] = hole ? 0x00000000u : (0xFF000000u | rgb);
        }
}

/* the drawer of 200 icons: what crosses area */
static void draw_drawer(void *ud, oldraw *d, const oldraw_rect *area)
{
    bench *b = (bench *)ud;
    const oldraw_image *img = b->e->icon ? b->e->icon : b->icon;
    int i, iw = oldraw_image_width(img), ih = oldraw_image_height(img);
    oldraw_fill(d, area, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
    for (i = 0; i < DRAWER_ICONS; ++i) {
        int cx = (i % DRAWER_COLS) * DRAWER_CELL_W, cy = (i / DRAWER_COLS) * DRAWER_CELL_H;
        oldraw_rect cell = { cx, cy, DRAWER_CELL_W, DRAWER_CELL_H };
        oldraw_rect label = { cx, cy + DRAWER_CELL_H - 14, DRAWER_CELL_W, 14 };
        if (cell.x >= area->x + area->w || cell.y >= area->y + area->h || cell.x + cell.w <= area->x ||
            cell.y + cell.h <= area->y)
            continue;   /* what a module does: only what crosses the damage */
        oldraw_image_draw(d, img, cx + (DRAWER_CELL_W - iw) / 2, cy + 2, (i % 7) == 0 ? OLDRAW_SELECTED : 0);
        oldraw_text_box(d, &label, b->names[i], (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_CENTRE);
        (void)ih;
    }
}

static void list_row(bench *b, int row, const oldraw_rect *r)
{
    char name[32], size[16];
    oldraw_cell cells[4];
    oldraw_row rw;
    sprintf(name, "File_%04d.txt", row);
    sprintf(size, "%d", (row * 997) % 100000);
    memset(cells, 0, sizeof cells);
    cells[0].image = b->small;
    cells[0].width = 20;
    cells[1].text = name;
    cells[1].width = 280;
    cells[2].text = size;
    cells[2].width = 100;
    cells[2].align = OLDRAW_RIGHT;
    cells[3].text = "13-Nov-26 08:00";
    memset(&rw, 0, sizeof rw);
    rw.cells = cells;
    rw.ncells = 4;
    rw.flags = (row & 1 ? OLDRAW_ROW_STRIPE : 0) | (row % 9 == 0 ? OLDRAW_ROW_SELECTED : 0);
    rw.pad = -1;
    oldraw_list_row(b->e->d, r, &rw);
}

/* the list's visible rows from b->top, what crosses area */
static void draw_list(void *ud, oldraw *d, const oldraw_rect *area)
{
    bench *b = (bench *)ud;
    int i;
    (void)d;
    for (i = 0; i < LIST_VISIBLE; ++i) {
        oldraw_rect r = { 0, i * LIST_ROW_H, LIST_W, LIST_ROW_H };
        if (r.y >= area->y + area->h || r.y + r.h <= area->y) continue;
        list_row(b, b->top + i, &r);
    }
}

/* ---- the work, n times ---- */

typedef void (*work_fn)(bench *b, unsigned long n);

static void w_fill(bench *b, unsigned long n)
{
    while (n--) {
        oldraw_rect r = { (int)(b->k++ & 7), 0, 128, 128 };
        oldraw_fill(b->e->d, &r, OLDRAW_PEN((b->k & 1) ? OLDRAW_PEN_FILL : OLDRAW_PEN_BACKGROUND));
    }
}

static void w_frame(bench *b, unsigned long n)
{
    while (n--) {
        oldraw_rect r = { (int)(b->k++ & 7), 0, 128, 128 };
        oldraw_frame(b->e->d, &r, (int)(b->k & 1));
    }
}

static void w_text(bench *b, unsigned long n)
{
    while (n--) oldraw_text(b->e->d, 4, 20 + (int)((b->k++ & 7) * 16), text32, 32, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0);
}

static void w_text_box(bench *b, unsigned long n)
{
    while (n--) {
        oldraw_rect r = { 4, 4 + (int)((b->k++ & 7) * 16), 120, 16 };
        oldraw_text_box(b->e->d, &r, text48, (size_t)-1, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_LEFT);
    }
}

static void w_icon(bench *b, unsigned long n)
{
    while (n--) {
        unsigned long k = b->k++;
        oldraw_image_draw(b->e->d, b->icon, (int)(k & 15) * 36, (int)((k >> 4) & 7) * 36, 0);
    }
}

static void w_icon_selected(bench *b, unsigned long n)
{
    while (n--) {
        unsigned long k = b->k++;
        oldraw_image_draw(b->e->d, b->icon, (int)(k & 15) * 36, (int)((k >> 4) & 7) * 36, OLDRAW_SELECTED);
    }
}

static void w_icon_do(bench *b, unsigned long n)
{
    while (n--) {
        unsigned long k = b->k++;
        oldraw_image_draw(b->e->d, b->e->icon, (int)(k & 7) * 72, (int)((k >> 3) & 3) * 72, 0);
    }
}

static void w_list_row(bench *b, unsigned long n)
{
    while (n--) {
        unsigned long k = b->k++;
        oldraw_rect r = { 0, (int)(k % LIST_VISIBLE) * LIST_ROW_H, LIST_W, LIST_ROW_H };
        list_row(b, (int)(k % LIST_ROWS), &r);
    }
}

static void w_select(bench *b, unsigned long n)
{
    while (n--) {
        int o = (int)((b->k++ >> 1) & 7);   /* drawn and taken off in pairs, as a lasso drag does */
        oldraw_select_box(b->e->d, 10 + o, 10 + o, 210 + o, 160 + o);
    }
}

static void w_progress(bench *b, unsigned long n)
{
    while (n--) {
        oldraw_rect r = { 10, 200, 400, 20 };
        oldraw_progress(b->e->d, &r, (uint32_t)(b->k++ % 1001), 1000, OLDRAW_PROGRESS_PERCENT);
    }
}

static void w_pie(bench *b, unsigned long n)
{
    while (n--) {
        unsigned long k = b->k++;
        oldraw_pie(b->e->d, 300, 300, 64, (int)(k % 3600), 2500, OLDRAW_PEN((k & 1) ? OLDRAW_PEN_FILL : OLDRAW_PEN_SHINE));
    }
}

/* four 32x32 icons' worth of damage on the drawer, then the redraw (an icon selected, a label renamed) */
static void w_damage(bench *b, unsigned long n)
{
    while (n--) {
        int i;
        for (i = 0; i < 4; ++i) {
            unsigned long k = b->k++ * 37;
            oldraw_rect r = { (int)(k % DRAWER_COLS) * DRAWER_CELL_W + 16, (int)((k / DRAWER_COLS) % 12) * DRAWER_CELL_H + 2,
                              ICON, ICON };
            oldraw_damage(b->e->d, &r);
        }
        oldraw_redraw(b->e->d, draw_drawer, b);
    }
}

static void w_drawer(bench *b, unsigned long n)
{
    while (n--) {
        oldraw_damage_all(b->e->d);
        oldraw_redraw(b->e->d, draw_drawer, b);
    }
}

/* the list of 1000 rows scrolled from its top to its end, a row a step: each step redraws the view (there is no
 * scroll-by-copy in gfopenlayout_draw.h yet) */
static void w_scroll(bench *b, unsigned long n)
{
    oldraw_rect view = { 0, 0, LIST_W, LIST_VISIBLE * LIST_ROW_H };
    while (n--) {
        for (b->top = 0; b->top <= LIST_ROWS - LIST_VISIBLE; ++b->top) {
            oldraw_damage(b->e->d, &view);
            oldraw_redraw(b->e->d, draw_list, b);
        }
        b->top = 0;
    }
}

/* ---- timing ---- */

static uint64_t timed(bench *b, work_fn w, unsigned long n)
{
    gflbench_env *e = b->e;
    uint64_t t0 = e->now(e->u), t1;
    w(b, n);
    oldraw_flush(e->d);
    if (e->sync) e->sync(e->u);
    t1 = e->now(e->u);
    return t1 > t0 ? t1 - t0 : 1;
}

/* the median run's ticks for n ops; *n_out the ops a run */
static uint64_t measure(bench *b, work_fn w, unsigned long *n_out)
{
    gflbench_env *e = b->e;
    uint64_t t, v[MAX_RUNS], want = muldiv(e->run_ms, e->hz, 1000);
    unsigned long n = 1;
    int i, runs = e->runs;
    t = timed(b, w, n);
    while (t < e->hz / 10 && n < 0x10000000UL) {
        n *= 2;
        t = timed(b, w, n);
    }
    if (t < want) {
        uint64_t m = muldiv(n, want, t) + 1;
        n = m > 0x40000000UL ? 0x40000000UL : (unsigned long)m;
    }
    for (i = 0; i < runs; ++i) {
        uint64_t tt = timed(b, w, n);
        v[i] = tt;
    }
    *n_out = n;
    return gflbench_median(v, runs);
}

static void line(gflbench_env *e, const char *name, const char *value, const char *unit)
{
    char buf[160];
    sprintf(buf, "BENCH %s %s %s", name, value, unit);
    e->out(e->u, buf);
}

static int rate(bench *b, const char *name, const char *unit, work_fn w, int ok, const char *why)
{
    char v[24];
    unsigned long n;
    uint64_t t;
    if (!gflbench_wanted(b->e->only, name)) return 0;
    if (!ok) { line(b->e, name, "n/a", why); return 1; }
    t = measure(b, w, &n);
    line(b->e, name, gflbench_fmt100(gflbench_rate100(n, t, b->e->hz), v), unit);
    return 0;
}

int gflbench_run(gflbench_env *e)
{
    static bench b;   /* static: 68k programs have small stacks */
    char buf[160], v[24];
    int na = 0, i;
    unsigned long n;
    uint64_t t;
    memset(&b, 0, sizeof b);
    b.e = e;
    if (!e->run_ms) e->run_ms = 500;
    if (e->runs <= 0) e->runs = 3;
    if (e->runs > MAX_RUNS) e->runs = MAX_RUNS;
    make_icon(b.pix, ICON);
    make_icon(b.spix, 16);
    b.icon = oldraw_image_argb(e->d, ICON, ICON, b.pix, ICON);
    b.small = oldraw_image_argb(e->d, 16, 16, b.spix, 16);
    for (i = 0; i < DRAWER_ICONS; ++i) sprintf(b.names[i], i % 5 ? "File%d" : "A long name %d", i);
    sprintf(buf, "BENCH-INFO version GFLayoutBench %s oldraw %d", GFLBENCH_VERSION, OLDRAW_VERSION);
    e->out(e->u, buf);
    sprintf(buf, "BENCH-INFO timing clock %lu Hz, runs %d of at least %lu ms, median", (unsigned long)e->hz, e->runs,
            (unsigned long)e->run_ms);
    e->out(e->u, buf);
    if (!b.icon || !b.small) {
        e->out(e->u, "BENCH-INFO error no memory for the icons");
        e->out(e->u, "BENCH-END fail");
        return -1;
    }

    /* primitives: operations a second */
    na += rate(&b, "fill_128", "fills/s", w_fill, 1, "");
    na += rate(&b, "frame_128", "frames/s", w_frame, 1, "");
    na += rate(&b, "text_32", "strings/s", w_text, 1, "");
    na += rate(&b, "text_box_cut", "strings/s", w_text_box, 1, "");
    na += rate(&b, "icon_blit_argb", "icons/s", w_icon, 1, "");
    na += rate(&b, "icon_blit_selected", "icons/s", w_icon_selected, 1, "");
    na += rate(&b, "icon_blit_diskobject", "icons/s", w_icon_do, e->icon != NULL, "no-diskobject");
    na += rate(&b, "list_row", "rows/s", w_list_row, 1, "");
    na += rate(&b, "select_box", "boxes/s", w_select, 1, "");
    na += rate(&b, "progress_bar", "bars/s", w_progress, 1, "");
    na += rate(&b, "pie_segment", "pies/s", w_pie, 1, "");
    na += rate(&b, "damage_redraw", "redraws/s", w_damage, 1, "");

    /* scenes: time for one */
    if (gflbench_wanted(e->only, "drawer_200_redraw") || gflbench_wanted(e->only, "drawer_200_icons_per_frame")) {
        t = measure(&b, w_drawer, &n);
        t = t / n ? t / n : 1;    /* ticks a redraw */
        if (gflbench_wanted(e->only, "drawer_200_redraw")) line(e, "drawer_200_redraw", gflbench_fmt100(gflbench_ms100(t, e->hz), v), "ms");
        /* icons a 60 Hz frame: 200 icons in t ticks, a frame hz / 60 ticks */
        if (gflbench_wanted(e->only, "drawer_200_icons_per_frame"))
            line(e, "drawer_200_icons_per_frame", gflbench_fmt100(muldiv((uint64_t)DRAWER_ICONS * 100, e->hz, t * 60), v), "icons");
    }
    if (gflbench_wanted(e->only, "list_scroll_1000")) {
        t = measure(&b, w_scroll, &n);
        line(e, "list_scroll_1000", gflbench_fmt100(gflbench_ms100(t / n, e->hz), v), "ms");
    }

    oldraw_image_free(e->d, b.icon);
    oldraw_image_free(e->d, b.small);
    sprintf(buf, "BENCH-END ok %d n/a", na);
    e->out(e->u, buf);
    return na;
}
