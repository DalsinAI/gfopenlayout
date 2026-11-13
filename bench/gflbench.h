/* GFLayoutBench: the same GFOpenLayout drawing benchmark on every target (Workbench build plan, wave N, N4: 68k,
 * AC090 and native, run at each merge). The portable part: it times the primitives of gfopenlayout_draw.h and two
 * Workbench scenes through any oldraw, and prints one line a result:
 *
 *     BENCH <name> <value> <unit>
 *
 * <value> is a decimal number with two places after the point, or "n/a" when the result couldn't be taken
 * (its unit then says why). Lines starting "BENCH-INFO " describe the run; "BENCH-END" is the last line.
 * The work is fixed: a result compares across days, builds and machines. No floating point: the same code runs
 * soft-float on a plain 68k. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GFLBENCH_H
#define GFLBENCH_H

#include <stdint.h>
#include "gfopenlayout_draw.h"

#define GFLBENCH_VERSION "1.0"

/* The canvas the benchmark draws on: the drawer of 200 icons is 16 x 13 cells of 64 x 48. A smaller target is
 * clipped (BENCH-INFO says so), which makes the drawer's figures not comparable. */
#define GFLBENCH_CANVAS_W 1024
#define GFLBENCH_CANVAS_H 640

typedef struct gflbench_env {
    oldraw *d;                          /* drawing on a GFLBENCH_CANVAS_W x GFLBENCH_CANVAS_H (or larger) target */
    uint64_t (*now)(void *u);           /* a clock that only goes up, ticks */
    uint32_t hz;                        /* its ticks a second */
    void (*sync)(void *u);              /* wait until what was drawn is done (WaitBlit); NULL: none */
    void (*out)(void *u, const char *line);   /* one line, without its newline */
    void *u;
    const oldraw_image *icon;           /* a Workbench icon (oldraw_image_icon); NULL: icon_blit_diskobject is n/a */
    uint32_t run_ms;                    /* each timed run lasts at least this long; 0: 500 */
    int runs;                           /* timed runs a result, the median reported; 0: 3 */
    const char *only;                   /* a comma-separated list of result names; NULL: all */
} gflbench_env;

/* Runs every benchmark and prints its lines (BENCH-INFO, BENCH ..., BENCH-END). Returns how many results were
 * n/a. */
int gflbench_run(gflbench_env *e);

/* For tests: n operations in ticks at hz, as operations a second times 100 (two decimal places), saturating. */
uint64_t gflbench_rate100(uint64_t n, uint64_t ticks, uint32_t hz);
/* ticks at hz, in milliseconds times 100. */
uint64_t gflbench_ms100(uint64_t ticks, uint32_t hz);
/* v / 100 written as "123.45" into buf (at least 24 bytes). */
char *gflbench_fmt100(uint64_t v, char *buf);
/* The median of n values (n at most 15); the values are sorted. */
uint64_t gflbench_median(uint64_t *v, int n);
/* 1 when name is in the comma-separated list (NULL or "": every name). */
int gflbench_wanted(const char *list, const char *name);

#endif
