/* GFOpenLayout drawing's insides, shared by the portable part (oldraw.c) and the Amiga target (oldraw_amiga.c).
 * Not for modules: they use gfopenlayout_draw.h. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef OLDRAW_INTERNAL_H
#define OLDRAW_INTERNAL_H

#include "gfopenlayout_draw.h"

enum { OLDRAW_IMG_ARGB = 1, OLDRAW_IMG_ICON = 2 };

struct oldraw_image {
    int kind;                   /* OLDRAW_IMG_... */
    int w, h;
    uint32_t *argb;             /* OLDRAW_IMG_ARGB: w * h, rows w apart */
    void *icon;                 /* OLDRAW_IMG_ICON: the DiskObject */
    void *target_data;          /* the target's own (the Amiga's: the selected and ghosted pixels, made once) */
    oldraw_rect *blocks;        /* OLDRAW_IMG_ARGB: rectangles covering exactly its drawn pixels */
    int nblocks;
    uint32_t *variant[2];       /* its pixels selected (0) and ghosted (1), made the first time they're drawn */
};

#define OLDRAW_MEASURES 64          /* measures kept */
#define OLDRAW_MEASURE_TEXT 63      /* the longest text kept, bytes */
typedef struct {
    int used;
    unsigned hash;
    size_t length;
    char text[OLDRAW_MEASURE_TEXT];
    unsigned style;
    int has_font, has_family, px;
    unsigned fstyle;
    char family[48];
    oldraw_text_size size;
} oldraw_measure_entry;

struct oldraw {
    const oldraw_ops *ops;
    void *target;
    int w, h;
    oldraw_colour pens[OLDRAW_PEN_COUNT];
    oldraw_rect damage[OLDRAW_DAMAGE_MAX];
    int ndamage;
    int in_redraw;
    oldraw_rect area;           /* inside oldraw_redraw: the rectangle being drawn */
    int has_user;
    oldraw_rect user;           /* oldraw_clip's */
    int has_extra;
    oldraw_rect extra;          /* a call's own box (a list row's, a text box's) */
    void (*free_target)(oldraw *d);   /* the Amiga target's own free */
    oldraw_measure_entry measures[OLDRAW_MEASURES];
    unsigned long measure_hits;   /* for the tests */
};

/* An ARGB picture's pixels as flags draw them (OLDRAW_SELECTED, OLDRAW_GHOSTED; neither: its own), w * h, made
 * once and kept. NULL when memory is short. */
const uint32_t *oldraw_image_variant(oldraw_image *img, unsigned flags);

/* The clip now (the target, the redraw area, the program's clip and a call's own box together). 0: empty. */
int oldraw_clip_now(const oldraw *d, oldraw_rect *c);

#endif
