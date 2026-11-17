/* GFOpenLayout: a picture off the screen that a window's contents are drawn into and shown from in one write.
 *
 * Dale, 17 November 2026, watching a drawer open on the Pi 5 with the monitor showing video memory itself:
 * "if its off screen composting , we should only see the frame udpated". A program that still draws its
 * contents itself draws them into the surface's RastPort, then olsurface_show puts the finished box in its
 * window: the screen never holds a half-drawn state. The drawing mode and the blit are GFOpenLayout's, so
 * such a program adds no drawing calls of its own for it.
 *
 * olsurface_clip is portable C (tests/test_surface.c); the rest is the Amiga side (amiga/olsurface_amiga.c,
 * built with -DOLSURFACE_AMIGA).
 *
 * Copyright (c) 2026 AmiCradle Team
 */
#ifndef GFOPENLAYOUT_SURFACE_H
#define GFOPENLAYOUT_SURFACE_H

/* The box x, y, w, h cut to a picture of bw x bh. 1: something is left (the box is the cut one); 0: nothing. */
int olsurface_clip(int bw, int bh, int *x, int *y, int *w, int *h);

#ifdef OLSURFACE_AMIGA
struct BitMap;
struct RastPort;
struct Layer;
struct Layer_Info;
struct TextFont;

typedef struct olsurface {
    struct BitMap *bm;          /* the picture */
    struct BitMap *friend_bm;   /* the screen's bitmap it was made like */
    struct Layer_Info *li;
    struct Layer *ly;           /* the picture's one layer: drawing clips to it, and takes clip regions */
    int w, h;
} olsurface;

/* A picture of at least w x h, like friend_bm (a screen's bitmap). One already that large and like friend_bm
 * is kept. 0: no memory; the surface is empty then. */
int olsurface_make(olsurface *s, struct BitMap *friend_bm, int w, int h);
void olsurface_free(olsurface *s);
/* The RastPort to draw the contents with, set as a window's is (font, JAM1). NULL on an empty surface. */
struct RastPort *olsurface_begin(olsurface *s, struct TextFont *font);
/* The box of the picture into to, at the same place, in one write. */
void olsurface_show(olsurface *s, struct RastPort *to, int x, int y, int w, int h);
#endif

#endif
