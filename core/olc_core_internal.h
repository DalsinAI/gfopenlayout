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
};

/* The core's memory: never Chip RAM. fast: pixels (MEMF_FAST on the Amiga, so a machine with only Chip RAM gets
 * none); otherwise the core's own small blocks. Both come back cleared. */
void *olc__alloc(size_t bytes, int fast);
void olc__free(void *p);

/* For the compose (section 4): what changed in s since the damage was last cleared (s's own coordinates), read
 * in place, then cleared once composed. Neither allocates, so neither can fail. */
const olc_region *olc__surface_damage(const olc_surface *s);
void olc__surface_damage_clear(olc_surface *s);

/* Whether olc_init has run */
int olc__ready(void);

#endif
