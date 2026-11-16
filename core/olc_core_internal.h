/* GFOpenLayout's ROM core (include/gfopenlayout_core.h): what the core's own files share and nothing outside
 * the core uses. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef OLC_CORE_INTERNAL_H
#define OLC_CORE_INTERNAL_H

#include "gfopenlayout_core.h"

/* A region: boxes that never overlap, none empty. */
struct olc_region {
    olc_box *b;
    int32_t n, cap;
};

struct olc_surface {
    int32_t w, h, bpr;
    uint32_t fmt, where;        /* where: the memory it got, OLC_MEM_FAST or OLC_MEM_HOST, never OLC_MEM_ANY */
    void *pixels;
    olc_region damage;          /* what changed since the compose last took it, cut to the surface */
    uint32_t slot;              /* the back end's slot, good while gen is the attach's generation */
    uint32_t gen;               /* 0: never bound */
    const struct olc_accel *bound_to;   /* the back end that gave slot, for retire */
    int stale;                  /* the back end's copy is behind: upload all of it at the next bind */
    uint32_t upseq;             /* the compose that last uploaded its damage */
};

/* The core's memory: never Chip RAM. fast: pixels (MEMF_FAST on the Amiga, so a machine with only Chip RAM gets
 * none); otherwise the core's own small blocks. Both come back cleared. */
void *olc__alloc(size_t bytes, int fast);
void olc__free(void *p);

/* For the compose (section 4): what changed in s since the damage was last cleared (s's own coordinates), read
 * in place, then cleared once composed. Neither allocates, so neither can fail. */
const olc_region *olc__surface_damage(const olc_surface *s);
void olc__surface_damage_clear(olc_surface *s);

/* A screen bitmap's pixels, bytes per row and format (OLC_FMT_*), for a composited scene. The Amiga's comes with
 * the RTG step (ORTG_BitMapInfo); host tests set their own. 0, or -1 when the bitmap can't be composited. */
typedef int (*olc_screen_lookup)(void *screen, void **pixels, int32_t *bytes_per_row, uint32_t *fmt);
void olc__set_screen_lookup(olc_screen_lookup fn);
#ifdef OLC_AMIGA
/* core/olc_screen_amiga.c: through GFOpenRTG's ORTG_BitMapInfo; the default on the Amiga */
int olc__amiga_screen_lookup(void *screen, void **pixels, int32_t *bytes_per_row, uint32_t *fmt);
#endif

/* A region's boxes let go (the region itself is the caller's, as in a struct) */
void olc__region_drop(olc_region *r);

/* olc_surface_free's call into the compose: the back end lets the surface's slot go (outside every lock) */
void olc__surface_retire(olc_surface *s);

/* Whether olc_init has run */
int olc__ready(void);

#endif
