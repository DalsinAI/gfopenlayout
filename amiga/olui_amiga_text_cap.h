/* The height of an outline face's capitals in whole rows, as olui_amiga_text
 * draws them. MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef OLUI_AMIGA_TEXT_CAP_H
#define OLUI_AMIGA_TEXT_CAP_H

/* cap and xh: the face's capital height and x-height at px, 26.6; target:
 * the x-height the glyphs are scaled to (olui_amiga_text's light hinting
 * puts it on a whole pixel), 26.6. The rows a capital's ink fills above the
 * baseline, to the nearest; a face that gives no capital height: 23/32 of px,
 * a sans-serif's. Never less than 1. */
static inline int olui_aa_cap_rows(long cap, long xh, long target, int px)
{
    long rows;
    if (cap > 0 && xh > 0 && target > 0)
        rows = (long)(((long long)cap * target / xh + 32) >> 6);
    else if (cap > 0)
        rows = (cap + 32) >> 6;
    else
        rows = ((long)px * 23 + 16) / 32;
    return rows < 1 ? 1 : (int)rows;
}

#endif
