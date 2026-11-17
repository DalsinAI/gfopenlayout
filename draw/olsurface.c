/* GFOpenLayout: the off-screen surface's portable half (include/gfopenlayout_surface.h).
 * Copyright (c) 2026 AmiCradle Team
 */
#include "gfopenlayout_surface.h"

int olsurface_clip(int bw, int bh, int *x, int *y, int *w, int *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*w > bw - *x) *w = bw - *x;
    if (*h > bh - *y) *h = bh - *y;
    return bw > 0 && bh > 0 && *w > 0 && *h > 0;
}
