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
    int ox, oy;                 /* OLDRAW_IMG_ICON: GetIconRectangle's MinX, MinY (the picture's corner) */
    void *target_data;          /* the target's own (a cached BitMap in wave 2) */
};

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
};

/* The clip now (the target, the redraw area, the program's clip and a call's own box together). 0: empty. */
int oldraw_clip_now(const oldraw *d, oldraw_rect *c);

#endif
