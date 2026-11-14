/* GFOpenLayout About windows (gfopenlayout_about.h): the GFOS mark in whole numbers, the layout and the drawing,
 * through oldraw only. No Amiga calls: amiga/olabout_amiga.c opens the window. MIT, Copyright (c) 2026 AmiCradle
 * Team. */
#include "gfopenlayout_about.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the mark: a band the stroke wide round a circle, open 62 degrees either side of up, round ends, a dot low
 * in it. Units of 1/Q pixel; sin 28 = cos 62 = 481/1024, sin 62 = 904/1024. The stroke is 0.13 of the height
 * (133/1024), the dot's radius 0.46 of the inside's (471/1024), resting 0.7 of the stroke (717/1024) above the
 * inside's bottom. ---- */
#define Q 256
#define UP 481
#define SIDE 904

typedef struct { long r, s, cx, cy, dr, dy; int w, h; } geo;

static int geometry(int h, geo *g)
{
    if (h < 2 || h > 512) return 0;
    g->h = h;
    g->s = (long)h * Q * 133 / 1024;
    g->r = ((long)h * Q - g->s) * 1024 / (1024 + UP);   /* the band's middle: its ends' tops at 0, its bottom at h */
    g->w = (int)((2 * g->r + g->s + Q - 1) / Q);
    g->cx = (long)g->w * Q / 2;
    g->cy = g->s / 2 + g->r * UP / 1024;
    g->dr = (g->r - g->s / 2) * 471 / 1024;
    g->dy = g->cy + (g->r - g->s / 2) - g->dr - g->s * 717 / 1024;
    return 1;
}

static int sq_in(long dx, long dy, long rad) { return (long long)dx * dx + (long long)dy * dy <= (long long)rad * rad; }

static int at(const geo *g, int x, int y)
{
    long px = (long)x * Q + Q / 2 - g->cx, py = (long)y * Q + Q / 2 - g->cy;
    long long d2 = (long long)px * px + (long long)py * py;
    long ro = g->r + g->s / 2, ri = g->r - g->s / 2, ex = g->r * SIDE / 1024, ey = -g->r * UP / 1024;
    if (x < 0 || y < 0 || x >= g->w || y >= g->h) return OLABOUT_NONE;
    if (sq_in(px, (long)y * Q + Q / 2 - g->dy, g->dr)) return OLABOUT_DOT;
    /* the band, less the gap at the top: within 62 degrees of straight up (py < 0 and py^2 > (481/1024)^2 d2) */
    if (d2 <= (long long)ro * ro && d2 >= (long long)ri * ri &&
        !(py < 0 && (long long)py * py * 1024 * 1024 > (long long)UP * UP * d2))
        return OLABOUT_ARC;
    if (sq_in(px - ex, py - ey, g->s / 2) || sq_in(px + ex, py - ey, g->s / 2)) return OLABOUT_ARC;
    return OLABOUT_NONE;
}

int olabout_mark_width(int h)
{
    geo g;
    return geometry(h, &g) ? g.w : 0;
}

int olabout_mark_at(int h, int x, int y)
{
    geo g;
    return geometry(h, &g) ? at(&g, x, y) : OLABOUT_NONE;
}

int olabout_mark_argb(int h, uint32_t arc_rgb, uint32_t *argb)
{
    geo g;
    int x, y;
    if (!geometry(h, &g)) return 0;
    for (y = 0; y < g.h; y++)
        for (x = 0; x < g.w; x++) {
            int k = at(&g, x, y);
            argb[y * g.w + x] = k == OLABOUT_ARC ? 0xFF000000u | (arc_rgb & 0xFFFFFFu)
                              : k == OLABOUT_DOT ? 0xFF000000u | OLABOUT_DOT_RGB : 0;
        }
    return g.w;
}

oldraw_image *olabout_mark_image(oldraw *d, int h, uint32_t arc_rgb)
{
    int w = olabout_mark_width(h);
    uint32_t *px;
    oldraw_image *img;
    if (!w || !(px = malloc(sizeof *px * (size_t)w * (size_t)h))) return NULL;
    olabout_mark_argb(h, arc_rgb, px);
    img = oldraw_image_argb(d, w, h, px, w);
    free(px);
    return img;
}

/* ---- the layout: in the font's line height (lh) and its 'M' (em) ---- */
static const oldraw_font bold = { NULL, 0, OLDRAW_BOLD };

void olabout_layout_for(oldraw *d, const olabout *a, olabout_layout *l)
{
    oldraw_text_size ts;
    int lh, em, asc, pad, i, textw = 0, okw, top;
    const char *p;
    memset(l, 0, sizeof *l);
    if (!oldraw_text_measure(d, "M", 1, NULL, 0, &ts)) { ts.width = 8; ts.ascent = 7; ts.descent = 2; }
    em = ts.width > 0 ? ts.width : 8;
    asc = ts.ascent;
    lh = ts.ascent + ts.descent + 2;
    pad = em;
    snprintf(l->title, sizeof l->title, "%s%s%s", a->name ? a->name : "", a->version ? " " : "",
             a->version ? a->version : "");
    l->line[0] = l->title;
    l->line_len[0] = strlen(l->title);
    l->lines = 1;
    for (p = a->text; p && *p && l->lines < OLABOUT_MAX_LINES - 1; ) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        l->line[l->lines] = p;
        l->line_len[l->lines++] = n;
        p = e ? e + 1 : p + n;
    }
    l->line[l->lines] = OLABOUT_CREDIT;
    l->line_len[l->lines++] = strlen(OLABOUT_CREDIT);
    for (i = 0; i < l->lines; i++)
        if (oldraw_text_measure(d, l->line[i], l->line_len[i], i ? NULL : &bold, i ? 0 : OLDRAW_BOLD, &ts) &&
            ts.width > textw)
            textw = ts.width;
    /* the mark: three lines high (the name and the first two under it), at least 16 pixels */
    l->mark.h = 3 * lh < 16 ? 16 : 3 * lh;
    if (l->mark.h > 512) l->mark.h = 512;
    l->mark.w = olabout_mark_width(l->mark.h);
    l->mark.x = pad;
    l->mark.y = pad;
    l->line_x = pad + l->mark.w + pad;
    top = pad;
    for (i = 0; i < l->lines; i++)            /* a blank line's gap before the credit */
        l->baseline[i] = top + asc + i * lh + (i == l->lines - 1 && l->lines > 1 ? lh / 2 : 0);
    if (!oldraw_text_measure(d, "OK", 2, NULL, 0, &ts)) ts.width = 2 * em;
    okw = ts.width + 4 * em;
    l->width = l->line_x + textw + pad;
    l->button.h = lh + 4;
    l->button.y = l->baseline[l->lines - 1] + (lh - asc) + pad;
    if (l->button.y < l->mark.y + l->mark.h + pad) l->button.y = l->mark.y + l->mark.h + pad;
    l->button.w = okw;
    l->button.x = (l->width - okw) / 2;
    l->height = l->button.y + l->button.h + pad;
}

void olabout_draw(oldraw *d, const olabout_layout *l, const oldraw_image *mark, int pressed)
{
    oldraw_rect all = { 0, 0, 0, 0 }, in;
    int i;
    all.w = l->width;
    all.h = l->height;
    oldraw_fill(d, &all, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
    if (mark) oldraw_image_draw(d, mark, l->mark.x, l->mark.y, 0);
    for (i = 0; i < l->lines; i++)
        oldraw_text(d, l->line_x, l->baseline[i], l->line[i], l->line_len[i], i ? NULL : &bold,
                    OLDRAW_PEN(OLDRAW_PEN_TEXT), i ? 0 : OLDRAW_BOLD);
    in = l->button;
    in.x++; in.y++; in.w -= 2; in.h -= 2;
    oldraw_fill(d, &in, OLDRAW_PEN(pressed ? OLDRAW_PEN_FILL : OLDRAW_PEN_BACKGROUND));
    oldraw_frame(d, &l->button, pressed);
    oldraw_text_box(d, &in, "OK", 2, NULL, OLDRAW_PEN(pressed ? OLDRAW_PEN_FILLTEXT : OLDRAW_PEN_TEXT), 0,
                    OLDRAW_CENTRE);
}

int olabout_on_button(const olabout_layout *l, int x, int y)
{
    return x >= l->button.x && y >= l->button.y && x < l->button.x + l->button.w && y < l->button.y + l->button.h;
}

int olabout_closes(unsigned key)
{
    return key == '\r' || key == '\n' || key == 27 || key == 'o' || key == 'O';
}
