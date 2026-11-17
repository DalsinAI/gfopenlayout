/* GFOpenLayout: the off-screen surface on AmigaOS (include/gfopenlayout_surface.h). Linked into the program
 * with draw/olsurface.c; graphics.library and layers.library are the program's own bases.
 * Copyright (c) 2026 AmiCradle Team
 */
#ifndef OLSURFACE_AMIGA
#define OLSURFACE_AMIGA
#endif
#include "gfopenlayout_surface.h"

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/layers.h>
#include <graphics/text.h>
#include <proto/graphics.h>
#include <proto/layers.h>

void olsurface_free(olsurface *s)
{
    if (s->ly) { DeleteLayer(0, s->ly); s->ly = NULL; }
    if (s->li) { DisposeLayerInfo(s->li); s->li = NULL; }
    if (s->bm) { WaitBlit(); FreeBitMap(s->bm); s->bm = NULL; }
    s->w = s->h = 0;
    s->friend_bm = NULL;
}

int olsurface_make(olsurface *s, struct BitMap *friend_bm, int w, int h)
{
    if (s->ly && s->friend_bm == friend_bm && s->w >= w && s->h >= h) return 1;
    olsurface_free(s);
    if (w <= 0 || h <= 0 || !friend_bm) return 0;
    if (!(s->bm = AllocBitMap((ULONG)w, (ULONG)h, GetBitMapAttr(friend_bm, BMA_DEPTH), BMF_MINPLANES, friend_bm)) ||
        !(s->li = NewLayerInfo()) ||
        !(s->ly = CreateUpfrontLayer(s->li, s->bm, 0, 0, w - 1, h - 1, LAYERSIMPLE, NULL))) {
        olsurface_free(s);
        return 0;
    }
    s->w = w; s->h = h;
    s->friend_bm = friend_bm;
    return 1;
}

struct RastPort *olsurface_begin(olsurface *s, struct TextFont *font)
{
    if (!s->ly) return NULL;
    if (font) SetFont(s->ly->rp, font);
    SetDrMd(s->ly->rp, JAM1);
    return s->ly->rp;
}

void olsurface_show(olsurface *s, struct RastPort *to, int x, int y, int w, int h)
{
    if (!s->bm || !to || !olsurface_clip(s->w, s->h, &x, &y, &w, &h)) return;
    BltBitMapRastPort(s->bm, x, y, to, x, y, w, h, 0xc0);
}
