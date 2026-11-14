/* GFOpenLayout drawing, the portable part: damage, clipping and the shapes of each primitive, drawn through a
 * target's oldraw_ops (oldraw_amiga.c's RastPort; a host test's pixels). No floating point.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "oldraw_internal.h"

#include <stdlib.h>
#include <string.h>

/* 3.2.3's grey Workbench pens, for a target that gives none */
static const oldraw_colour grey_pens[OLDRAW_PEN_COUNT] = {
    0xFF000000UL, 0xFFAAAAAAUL, 0xFF000000UL, 0xFFFFFFFFUL, 0xFF000000UL, 0xFF6688BBUL,
    0xFF000000UL, 0xFFAAAAAAUL, 0xFFFFFFFFUL, 0xFF000000UL, 0xFFFFFFFFUL, 0xFF000000UL
};

oldraw *oldraw_new(const oldraw_ops *ops, void *target, int width, int height, const oldraw_colour *pens)
{
    oldraw *d;
    if (!ops) return NULL;
    d = (oldraw *)calloc(1, sizeof *d);
    if (!d) return NULL;
    d->ops = ops;
    d->target = target;
    d->w = width;
    d->h = height;
    memcpy(d->pens, pens ? pens : grey_pens, sizeof d->pens);
    return d;
}

void oldraw_free(oldraw *d)
{
    if (!d) return;
    if (d->free_target) d->free_target(d);
    free(d);
}

void oldraw_resize(oldraw *d, int width, int height)
{
    if (!d) return;
    d->w = width;
    d->h = height;
    d->ndamage = 0;
    d->has_user = 0;
    d->has_extra = 0;
}

oldraw_colour oldraw_resolve(const oldraw *d, oldraw_colour c)
{
    if ((c >> 24) == 0xFEu) return d->pens[(c & 0xFFu) < OLDRAW_PEN_COUNT ? (c & 0xFFu) : OLDRAW_PEN_TEXT];
    return c;
}

/* ---- rectangles ---- */
static int rect_cut(oldraw_rect *a, const oldraw_rect *b)
{
    int32_t x1 = a->x > b->x ? a->x : b->x, y1 = a->y > b->y ? a->y : b->y;
    int32_t x2 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
    int32_t y2 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
    a->x = x1;
    a->y = y1;
    a->w = x2 - x1;
    a->h = y2 - y1;
    return a->w > 0 && a->h > 0;
}

static oldraw_rect rect_join(const oldraw_rect *a, const oldraw_rect *b)
{
    oldraw_rect r;
    int32_t x2 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
    int32_t y2 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
    r.x = a->x < b->x ? a->x : b->x;
    r.y = a->y < b->y ? a->y : b->y;
    r.w = x2 - r.x;
    r.h = y2 - r.y;
    return r;
}

/* overlapping, or sharing a stretch of edge (not only a corner) */
static int rect_touch(const oldraw_rect *a, const oldraw_rect *b)
{
    int xs = a->x <= b->x + b->w && b->x <= a->x + a->w;        /* touching or overlapping across */
    int ys = a->y <= b->y + b->h && b->y <= a->y + a->h;
    int xo = a->x < b->x + b->w && b->x < a->x + a->w;          /* overlapping across */
    int yo = a->y < b->y + b->h && b->y < a->y + a->h;
    return (xo && ys) || (yo && xs);
}

static long area(const oldraw_rect *r) { return (long)r->w * (long)r->h; }

int oldraw_clip_now(const oldraw *d, oldraw_rect *c)
{
    c->x = 0;
    c->y = 0;
    c->w = d->w;
    c->h = d->h;
    if (c->w <= 0 || c->h <= 0) return 0;
    if (d->in_redraw && !rect_cut(c, &d->area)) return 0;
    if (d->has_user && !rect_cut(c, &d->user)) return 0;
    if (d->has_extra && !rect_cut(c, &d->extra)) return 0;
    return 1;
}

/* r cut to the clip now; 0 when nothing is left */
static int cut(const oldraw *d, int *x, int *y, int *w, int *h)
{
    oldraw_rect c, r;
    if (*w <= 0 || *h <= 0 || !oldraw_clip_now(d, &c)) return 0;
    r.x = *x;
    r.y = *y;
    r.w = *w;
    r.h = *h;
    if (!rect_cut(&r, &c)) return 0;
    *x = (int)r.x;
    *y = (int)r.y;
    *w = (int)r.w;
    *h = (int)r.h;
    return 1;
}

/* a call's own box on top of the clip, given back with pop */
static void push_box(oldraw *d, const oldraw_rect *r, int *had, oldraw_rect *saved)
{
    *had = d->has_extra;
    *saved = d->extra;
    if (d->has_extra) {
        oldraw_rect n = *r;
        if (!rect_cut(&n, &d->extra)) n.w = n.h = 0;
        d->extra = n;
    } else {
        d->extra = *r;
    }
    d->has_extra = 1;
}

static void pop_box(oldraw *d, int had, const oldraw_rect *saved)
{
    d->has_extra = had;
    d->extra = *saved;
}

/* ---- damage ---- */
static void damage_put(oldraw *d, oldraw_rect r)
{
    int i, again = 1;
    while (again) {
        again = 0;
        for (i = 0; i < d->ndamage; ++i)
            if (rect_touch(&d->damage[i], &r)) {
                r = rect_join(&d->damage[i], &r);
                d->damage[i] = d->damage[--d->ndamage];
                again = 1;
                break;
            }
    }
    if (d->ndamage == OLDRAW_DAMAGE_MAX) {
        /* full: joined to the one it grows least, which may then touch others */
        int best = 0;
        long grow = -1;
        for (i = 0; i < d->ndamage; ++i) {
            oldraw_rect j = rect_join(&d->damage[i], &r);
            long g = area(&j) - area(&d->damage[i]) - area(&r);
            if (grow < 0 || g < grow) { grow = g; best = i; }
        }
        r = rect_join(&d->damage[best], &r);
        d->damage[best] = d->damage[--d->ndamage];
        damage_put(d, r);
        return;
    }
    d->damage[d->ndamage++] = r;
}

void oldraw_damage(oldraw *d, const oldraw_rect *r)
{
    oldraw_rect n, all;
    if (!d || !r) return;
    n = *r;
    all.x = 0;
    all.y = 0;
    all.w = d->w;
    all.h = d->h;
    if (!rect_cut(&n, &all)) return;
    damage_put(d, n);
}

void oldraw_damage_all(oldraw *d)
{
    if (!d) return;
    d->ndamage = 0;
    if (d->w > 0 && d->h > 0) {
        d->damage[0].x = 0;
        d->damage[0].y = 0;
        d->damage[0].w = d->w;
        d->damage[0].h = d->h;
        d->ndamage = 1;
    }
}

int oldraw_damage_count(const oldraw *d) { return d ? d->ndamage : 0; }

int oldraw_damage_get(const oldraw *d, int i, oldraw_rect *r)
{
    if (!d || i < 0 || i >= d->ndamage) return 0;
    if (r) *r = d->damage[i];
    return 1;
}

int oldraw_redraw(oldraw *d, oldraw_redraw_fn fn, void *userdata)
{
    oldraw_rect list[OLDRAW_DAMAGE_MAX];
    int i, n;
    if (!d) return 0;
    n = d->ndamage;
    memcpy(list, d->damage, (size_t)n * sizeof list[0]);
    d->ndamage = 0;     /* what fn marks now is for the next redraw */
    for (i = 0; i < n; ++i) {
        d->in_redraw = 1;
        d->area = list[i];
        if (fn) fn(userdata, d, &list[i]);
        d->in_redraw = 0;
    }
    oldraw_flush(d);
    return n;
}

void oldraw_clip(oldraw *d, const oldraw_rect *r)
{
    if (!d) return;
    d->has_user = r != NULL;
    if (r) d->user = *r;
}

void oldraw_flush(oldraw *d)
{
    if (d && d->ops->flush) d->ops->flush(d->target);
}

/* ---- copy and scroll ---- */
/* the parts of a not in b (at most four) damaged */
static void damage_minus(oldraw *d, const oldraw_rect *a, const oldraw_rect *b)
{
    oldraw_rect in = *a, s;
    if (a->w <= 0 || a->h <= 0) return;
    if (!rect_cut(&in, b)) { oldraw_damage(d, a); return; }
    s = *a;
    s.h = in.y - a->y;                          /* above */
    if (s.h > 0) oldraw_damage(d, &s);
    s.y = in.y + in.h;                          /* below */
    s.h = a->y + a->h - s.y;
    if (s.h > 0) oldraw_damage(d, &s);
    s.y = in.y;                                 /* left and right of it */
    s.h = in.h;
    s.x = a->x;
    s.w = in.x - a->x;
    if (s.w > 0) oldraw_damage(d, &s);
    s.x = in.x + in.w;
    s.w = a->x + a->w - s.x;
    if (s.w > 0) oldraw_damage(d, &s);
}

void oldraw_copy(oldraw *d, const oldraw_rect *src, int dx, int dy)
{
    oldraw_rect all, clip, s, dst, want;
    int32_t ox, oy;
    if (!d || !src || src->w <= 0 || src->h <= 0 || !d->ops->copy) return;
    if (!oldraw_clip_now(d, &clip)) return;
    ox = dx - src->x;
    oy = dy - src->y;
    want.x = dx;
    want.y = dy;
    want.w = src->w;
    want.h = src->h;
    if (!rect_cut(&want, &clip)) return;        /* what the copy should give: inside the clip */
    all.x = 0;
    all.y = 0;
    all.w = d->w;
    all.h = d->h;
    s = *src;
    if (rect_cut(&s, &all)) {
        dst = s;
        dst.x += ox;
        dst.y += oy;
        if (rect_cut(&dst, &want)) {
            int i, n = d->ndamage;
            oldraw_rect pend[OLDRAW_DAMAGE_MAX];
            memcpy(pend, d->damage, (size_t)n * sizeof pend[0]);
            if (d->ops->copy(d->target, (int)(dst.x - ox), (int)(dst.y - oy), (int)dst.x, (int)dst.y, (int)dst.w,
                             (int)dst.h)) {
                oldraw_damage(d, &want);       /* the target couldn't read all of the source (a hidden part) */
                return;
            }
            damage_minus(d, &want, &dst);
            /* damage pending in the source is stale there, so it is stale where it was copied to */
            for (i = 0; i < n; ++i) {
                oldraw_rect m = pend[i];
                m.x += ox;
                m.y += oy;
                if (rect_cut(&m, &dst)) oldraw_damage(d, &m);
            }
            return;
        }
    }
    oldraw_damage(d, &want);
}

void oldraw_scroll(oldraw *d, const oldraw_rect *area, int ddx, int ddy)
{
    oldraw_rect s;
    if (!d || !area || area->w <= 0 || area->h <= 0) return;
    if (ddx == 0 && ddy == 0) return;
    if ((ddx < 0 ? -ddx : ddx) >= area->w || (ddy < 0 ? -ddy : ddy) >= area->h) {
        oldraw_damage(d, area);
        return;
    }
    /* the part that stays inside the area after the move */
    s.x = area->x + (ddx < 0 ? -ddx : 0);
    s.y = area->y + (ddy < 0 ? -ddy : 0);
    s.w = area->w - (ddx < 0 ? -ddx : ddx);
    s.h = area->h - (ddy < 0 ? -ddy : ddy);
    {
        int had = d->has_extra;
        oldraw_rect saved = d->extra, moved;
        push_box(d, area, &had, &saved);
        oldraw_copy(d, &s, (int)s.x + ddx, (int)s.y + ddy);
        pop_box(d, had, &saved);
        moved = s;
        moved.x += ddx;
        moved.y += ddy;
        damage_minus(d, area, &moved);          /* the strip it left */
    }
}

/* ---- boxes ---- */
static void fill(oldraw *d, int x, int y, int w, int h, oldraw_colour c)
{
    if (c == OLDRAW_NONE) return;
    if (cut(d, &x, &y, &w, &h)) d->ops->fill(d->target, x, y, w, h, c);
}

static void invert(oldraw *d, int x, int y, int w, int h)
{
    if (cut(d, &x, &y, &w, &h)) d->ops->invert(d->target, x, y, w, h);
}

void oldraw_fill(oldraw *d, const oldraw_rect *r, oldraw_colour c)
{
    if (d && r) fill(d, (int)r->x, (int)r->y, (int)r->w, (int)r->h, c);
}

void oldraw_frame(oldraw *d, const oldraw_rect *r, int recessed)
{
    oldraw_colour tl, br;
    int x, y, w, h;
    if (!d || !r || r->w <= 0 || r->h <= 0) return;
    x = (int)r->x;
    y = (int)r->y;
    w = (int)r->w;
    h = (int)r->h;
    tl = OLDRAW_PEN(recessed ? OLDRAW_PEN_SHADOW : OLDRAW_PEN_SHINE);
    br = OLDRAW_PEN(recessed ? OLDRAW_PEN_SHINE : OLDRAW_PEN_SHADOW);
    fill(d, x, y, w - 1, 1, tl);                /* top, its right end the other's */
    fill(d, x, y + 1, 1, h - 1, tl);            /* left, with the bottom left corner */
    fill(d, x + 1, y + h - 1, w - 1, 1, br);    /* bottom */
    if (h > 1 || w == 1) fill(d, x + w - 1, y, 1, h - 1, br);   /* right */
    if (h == 1 && w > 1) fill(d, x + w - 1, y, 1, 1, br);
    if (w == 1 && h == 1) fill(d, x, y, 1, 1, tl);         /* one pixel: the top left's */
}

/* ---- text ---- */
static size_t text_len(const char *utf8, size_t length)
{
    if (!utf8) return 0;
    return length == (size_t)-1 ? strlen(utf8) : length;
}

/* the last measures, by text, font and style: a list row's columns and a drawer's labels are measured each redraw
 * (and oldraw_text_box measures what it then draws), so most are found here */
static unsigned text_hash(const char *s, size_t n, const oldraw_font *f, unsigned style)
{
    unsigned h = 2166136261u;
    size_t i;
    for (i = 0; i < n; ++i) h = (h ^ (unsigned char)s[i]) * 16777619u;
    h ^= (unsigned)n * 31u + style * 7u;
    if (f) {
        h ^= (unsigned)f->px * 131u + f->style;
        if (f->family) for (i = 0; f->family[i]; ++i) h = (h ^ (unsigned char)f->family[i]) * 16777619u;
    }
    return h;
}

static int same_font(const oldraw_measure_entry *m, const oldraw_font *f, unsigned style)
{
    if (m->style != style) return 0;
    if (!f) return !m->has_font;
    if (!m->has_font || m->px != f->px || m->fstyle != f->style) return 0;
    if (!f->family) return m->family[0] == 0 && !m->has_family;
    return m->has_family && !strcmp(m->family, f->family);
}

void oldraw_theme_changed(oldraw *d)
{
    if (!d) return;
    memset(d->measures, 0, sizeof d->measures);
}

/* font with the call's bold, italic and underline added to its style (what a target measures by); font itself
 * when nothing is added */
static const oldraw_font *with_style(const oldraw_font *font, unsigned style, oldraw_font *both)
{
    unsigned st = style & (OLDRAW_BOLD | OLDRAW_ITALIC | OLDRAW_UNDERLINE);
    if (!st || (font && (font->style | st) == font->style)) return font;
    if (font) *both = *font;
    else { both->family = NULL; both->px = 0; both->style = 0; }
    both->style |= st;
    return both;
}

static int measure_cached(oldraw *d, const char *utf8, size_t length, const oldraw_font *font, unsigned style,
                          oldraw_text_size *size)
{
    oldraw_measure_entry *m;
    unsigned h;
    if (length > OLDRAW_MEASURE_TEXT || (font && font->family && strlen(font->family) >= sizeof m->family))
        return d->ops->measure(d->target, utf8, length, font, size);
    h = text_hash(utf8, length, font, style);
    m = &d->measures[h % OLDRAW_MEASURES];
    if (m->used && m->hash == h && m->length == length && same_font(m, font, style) &&
        !memcmp(m->text, utf8, length)) {
        *size = m->size;
        d->measure_hits++;
        return 1;
    }
    if (!d->ops->measure(d->target, utf8, length, font, size)) return 0;
    m->used = 1;
    m->hash = h;
    m->length = length;
    memcpy(m->text, utf8, length);
    m->style = style;
    m->has_font = font != NULL;
    m->px = font ? font->px : 0;
    m->fstyle = font ? font->style : 0;
    m->has_family = font && font->family;
    if (m->has_family) strcpy(m->family, font->family);
    else m->family[0] = 0;
    m->size = *size;
    return 1;
}

int oldraw_text_measure(oldraw *d, const char *utf8, size_t length, const oldraw_font *font, unsigned style,
                     oldraw_text_size *size)
{
    oldraw_text_size s;
    oldraw_font both;
    if (!d || !d->ops->measure) return 0;
    length = text_len(utf8, length);
    memset(&s, 0, sizeof s);
    if (!measure_cached(d, utf8 ? utf8 : "", length, with_style(font, style, &both), style, &s)) return 0;
    if (size) *size = s;
    return 1;
}

/* text already measured: drawn, and given the clip only when it crosses it (a target clips at a cost: a layer's
 * clip region on the Amiga) */
static void text_draw(oldraw *d, int x, int y, const char *utf8, size_t length, const oldraw_font *font, oldraw_colour c,
                      unsigned style, const oldraw_text_size *s)
{
    oldraw_rect clip, box, whole;
    box.x = x;
    box.y = y - s->ascent;
    box.w = s->width + ((style & (OLDRAW_SHADOW | OLDRAW_OUTLINE)) ? 2 : 0);
    box.h = s->ascent + s->descent + ((style & (OLDRAW_SHADOW | OLDRAW_OUTLINE)) ? 2 : 0);
    if (style & OLDRAW_OUTLINE) { box.x--; box.y--; }
    whole = box;
    if (c == OLDRAW_NONE || !oldraw_clip_now(d, &clip) || !rect_cut(&box, &clip)) return;
    d->ops->text(d->target, x, y, utf8, length, font, c, style,
                 (box.x == whole.x && box.y == whole.y && box.w == whole.w && box.h == whole.h) ? NULL : &clip);
}

int oldraw_text(oldraw *d, int x, int y, const char *utf8, size_t length, const oldraw_font *font, oldraw_colour c,
                unsigned style)
{
    oldraw_text_size s;
    if (!d || !utf8) return 0;
    length = text_len(utf8, length);
    if (!length || !oldraw_text_measure(d, utf8, length, font, style, &s)) return 0;
    text_draw(d, x, y, utf8, length, font, c, style, &s);
    return s.width;
}

size_t oldraw_text_box(oldraw *d, const oldraw_rect *r, const char *utf8, size_t length, const oldraw_font *font,
                       oldraw_colour c, unsigned style, int align)
{
    static const char dots[] = "...";
    oldraw_text_size s, e;
    oldraw_font fs;
    size_t keep, lo;
    int had, x, y, tw;
    oldraw_rect saved;
    memset(&e, 0, sizeof e);
    if (!d || !r || !utf8 || r->w <= 0 || r->h <= 0) return 0;
    length = text_len(utf8, length);
    if (!length || !oldraw_text_measure(d, utf8, length, font, style, &s)) return 0;
    keep = length;
    tw = s.width;
    if (s.width > r->w) {
        /* the longest start that fits with "..." after it: a binary search over where characters start */
        size_t starts[256];     /* the first 256 character starts: a cut lands in the first 256 characters (labels
                                 * are shorter). Code points, not graphemes: a combining mark can be cut off */
        int n = 0, a, b;
        if (!oldraw_text_measure(d, dots, 3, font, style, &e) || e.width > r->w) return 0;
        for (lo = 1; lo < length && n < 256; ++lo)
            if (((unsigned char)utf8[lo] & 0xC0u) != 0x80u) starts[n++] = lo;
        a = -1;                 /* starts[a] fits (-1: none) */
        b = n - 1;
        while (a < b) {
            int mid = (a + b + 1) / 2;
            oldraw_text_size m;
            /* the search's probes go to the target uncached: each is measured once and would only push out
             * entries that come round again */
            memset(&m, 0, sizeof m);
            if (d->ops->measure(d->target, utf8, starts[mid], with_style(font, style, &fs), &m) && m.width + e.width <= r->w)
                a = mid;
            else b = mid - 1;
        }
        lo = a >= 0 ? starts[a] : 0;
        keep = lo;
        if (keep) oldraw_text_measure(d, utf8, keep, font, style, &s);
        else s.width = 0;
        tw = s.width + e.width;
    }
    x = (int)r->x;
    if (align == OLDRAW_CENTRE) x += ((int)r->w - tw) / 2;
    else if (align == OLDRAW_RIGHT) x += (int)r->w - tw;
    y = (int)r->y + ((int)r->h - (s.ascent + s.descent)) / 2 + s.ascent;
    push_box(d, r, &had, &saved);
    if (keep) {
        text_draw(d, x, y, utf8, keep, font, c, style, &s);   /* s: keep's own size */
        x += s.width;
    }
    if (keep < length) text_draw(d, x, y, dots, 3, font, c, style, &e);
    pop_box(d, had, &saved);
    return keep;
}

/* ---- pictures ---- */
/* img's blocks: each row's runs of drawn pixels, a run joined to the block above when that block has the same left
 * and width and ends on the row above */
static int make_blocks(oldraw_image *img)
{
    int y, cap = 8, n = 0;
    oldraw_rect *b = (oldraw_rect *)malloc((size_t)cap * sizeof *b);
    int *open_ = NULL, nopen = 0, nnext;
    int *next;
    if (!b) return 0;
    open_ = (int *)malloc((size_t)img->w * sizeof(int));
    next = (int *)malloc((size_t)img->w * sizeof(int));
    if (!open_ || !next) { free(b); free(open_); free(next); return 0; }
    for (y = 0; y < img->h; ++y) {
        const uint32_t *row = img->argb + (size_t)y * (size_t)img->w;
        int x = 0;
        nnext = 0;
        while (x < img->w) {
            int from, k, joined = -1;
            while (x < img->w && (row[x] >> 24) < 0x80) ++x;
            if (x >= img->w) break;
            from = x;
            while (x < img->w && (row[x] >> 24) >= 0x80) ++x;
            for (k = 0; k < nopen; ++k)
                if (b[open_[k]].x == from && b[open_[k]].w == x - from) { joined = open_[k]; break; }
            if (joined >= 0) {
                b[joined].h++;
            } else {
                if (n == cap) {
                    oldraw_rect *nb = (oldraw_rect *)realloc(b, (size_t)cap * 2 * sizeof *b);
                    if (!nb) { free(b); free(open_); free(next); return 0; }
                    b = nb;
                    cap *= 2;
                }
                b[n].x = from;
                b[n].y = y;
                b[n].w = x - from;
                b[n].h = 1;
                joined = n++;
            }
            next[nnext++] = joined;
        }
        memcpy(open_, next, (size_t)nnext * sizeof(int));
        nopen = nnext;
    }
    free(open_);
    free(next);
    img->blocks = b;
    img->nblocks = n;
    return 1;
}

oldraw_image *oldraw_image_argb(oldraw *d, int w, int h, const uint32_t *argb, int stride)
{
    oldraw_image *img;
    int y;
    (void)d;
    /* at most 16383 across: a target's row modulo (w * 4) is 16 bits on the Amiga */
    if (w <= 0 || h <= 0 || w > 16383 || h > 16383 || !argb || stride < w) return NULL;
    img = (oldraw_image *)calloc(1, sizeof *img);
    if (!img) return NULL;
    img->argb = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!img->argb) { free(img); return NULL; }
    for (y = 0; y < h; ++y) memcpy(img->argb + (size_t)y * (size_t)w, argb + (size_t)y * (size_t)stride,
                                   (size_t)w * sizeof(uint32_t));
    img->kind = OLDRAW_IMG_ARGB;
    img->w = w;
    img->h = h;
    if (!make_blocks(img)) { free(img->argb); free(img); return NULL; }
    return img;
}

int oldraw_image_blocks(const oldraw_image *img, const oldraw_rect **blocks)
{
    if (!img || img->kind != OLDRAW_IMG_ARGB) { if (blocks) *blocks = NULL; return 0; }
    if (blocks) *blocks = img->blocks;
    return img->nblocks;
}

const uint32_t *oldraw_image_variant(oldraw_image *img, unsigned flags)
{
    int k;
    size_t i, n;
    if (!img || img->kind != OLDRAW_IMG_ARGB) return NULL;
    if (!(flags & (OLDRAW_SELECTED | OLDRAW_GHOSTED))) return img->argb;
    /* selected (0), ghosted (1), or both (2: darkened, then dimmed, as each is drawn alone) */
    k = (flags & OLDRAW_SELECTED) ? ((flags & OLDRAW_GHOSTED) ? 2 : 0) : 1;
    if (img->variant[k]) return img->variant[k];
    n = (size_t)img->w * (size_t)img->h;
    img->variant[k] = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!img->variant[k]) return NULL;
    for (i = 0; i < n; ++i) {
        uint32_t p = img->argb[i];
        if (k != 1) p = (p & 0xFF000000UL) | ((p >> 1) & 0x7F7F7FUL);                     /* half as bright */
        if (k != 0) p = (p & 0xFF000000UL) | (((p & 0xFEFEFEUL) >> 1) + 0x404040UL);      /* towards grey */
        img->variant[k][i] = p;
    }
    return img->variant[k];
}

#ifndef OLDRAW_AMIGA
oldraw_image *oldraw_image_icon(oldraw *d, struct DiskObject *icon)
{
    (void)d;
    (void)icon;
    return NULL;
}
#endif

int oldraw_image_width(const oldraw_image *img) { return img ? img->w : 0; }
int oldraw_image_height(const oldraw_image *img) { return img ? img->h : 0; }

uint32_t oldraw_image_pixel(const oldraw_image *img, int x, int y)
{
    if (!img || img->kind != OLDRAW_IMG_ARGB || x < 0 || y < 0 || x >= img->w || y >= img->h) return 0;
    return img->argb[(size_t)y * (size_t)img->w + (size_t)x];
}

void oldraw_image_free(oldraw *d, oldraw_image *img)
{
    if (!img) return;
    if (d && d->ops->image_free) d->ops->image_free(d->target, img);
    free(img->argb);
    free(img->blocks);
    free(img->variant[0]);
    free(img->variant[1]);
    free(img->variant[2]);
    free(img);
}

void oldraw_image_draw(oldraw *d, const oldraw_image *img, int x, int y, unsigned flags)
{
    int cx = x, cy = y, w, h;
    if (!d || !img) return;
    w = img->w;
    h = img->h;
    if (cut(d, &cx, &cy, &w, &h)) d->ops->image(d->target, img, cx - x, cy - y, cx, cy, w, h, flags);
}

/* ---- a list's row ---- */
static int row_pad(const oldraw_row *row) { return row->pad < 0 ? 4 : row->pad; }

int oldraw_list_cell_box(const oldraw_row *row, const oldraw_rect *r, int i, oldraw_rect *cell)
{
    int k, x, pad, end;
    if (!row || !r || i < 0 || i >= row->ncells) return 0;
    pad = row_pad(row);
    x = (int)r->x;
    end = (int)(r->x + r->w);
    for (k = 0; k <= i; ++k) {
        int w = row->cells[k].width;
        if (w <= 0) w = end - x;
        if (k == i) {
            oldraw_rect c;
            c.x = x + pad;
            c.y = r->y;
            c.w = (x + w > end ? end - x : w) - 2 * pad;
            c.h = r->h;
            if (cell) *cell = c;
            return c.w > 0 && c.h > 0;
        }
        x += w;
        if (x >= end) break;
    }
    return 0;
}

/* a dotted line of complemented pixels, by where it is on the screen, so the same call takes it off again */
static void dots_h(oldraw *d, int x0, int x1, int y)
{
    int x = x0;
    while (x <= x1) {
        int start = x, stop;
        if ((x >> 2) & 1) { x = ((x >> 2) + 1) * 4; continue; }
        stop = ((x >> 2) + 1) * 4 - 1;
        if (stop > x1) stop = x1;
        invert(d, start, y, stop - start + 1, 1);
        x = stop + 1;
    }
}

static void dots_v(oldraw *d, int x, int y0, int y1)
{
    int y = y0;
    while (y <= y1) {
        int start = y, stop;
        if ((y >> 2) & 1) { y = ((y >> 2) + 1) * 4; continue; }
        stop = ((y >> 2) + 1) * 4 - 1;
        if (stop > y1) stop = y1;
        invert(d, x, start, 1, stop - start + 1);
        y = stop + 1;
    }
}

void oldraw_select_box(oldraw *d, int x0, int y0, int x1, int y1)
{
    if (!d) return;
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    dots_h(d, x0, x1, y0);
    if (y1 != y0) dots_h(d, x0, x1, y1);
    if (y1 - y0 > 1) {
        dots_v(d, x0, y0 + 1, y1 - 1);
        if (x1 != x0) dots_v(d, x1, y0 + 1, y1 - 1);
    }
}

void oldraw_list_row(oldraw *d, const oldraw_rect *r, const oldraw_row *row)
{
    oldraw_colour bg, fg;
    int i, had;
    oldraw_rect saved;
    if (!d || !r || !row || r->w <= 0 || r->h <= 0) return;
    if (row->flags & OLDRAW_ROW_SELECTED) bg = OLDRAW_PEN(OLDRAW_PEN_FILL);
    else if (row->flags & OLDRAW_ROW_STRIPE) {
        /* a shade darker than the background */
        oldraw_colour b = oldraw_resolve(d, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
        uint32_t rr = (b >> 16) & 255, gg = (b >> 8) & 255, bb = b & 255;
        rr = rr > 16 ? rr - 16 : 0;
        gg = gg > 16 ? gg - 16 : 0;
        bb = bb > 16 ? bb - 16 : 0;
        bg = OLDRAW_RGB((rr << 16) | (gg << 8) | bb);
    } else bg = OLDRAW_PEN(OLDRAW_PEN_BACKGROUND);
    fg = (row->flags & OLDRAW_ROW_GHOSTED) ? OLDRAW_PEN(OLDRAW_PEN_SHADOW)
         : OLDRAW_PEN((row->flags & OLDRAW_ROW_SELECTED) ? OLDRAW_PEN_FILLTEXT : OLDRAW_PEN_TEXT);
    oldraw_fill(d, r, bg);
    push_box(d, r, &had, &saved);
    for (i = 0; i < row->ncells; ++i) {
        const oldraw_cell *cell = &row->cells[i];
        oldraw_rect box;
        int h2, cbox_had;
        oldraw_rect cbox_saved;
        if (!oldraw_list_cell_box(row, r, i, &box)) continue;
        push_box(d, &box, &cbox_had, &cbox_saved);
        if (cell->image) {
            int iy = (int)box.y + ((int)box.h - cell->image->h) / 2;
            oldraw_image_draw(d, cell->image, (int)box.x, iy,
                              (row->flags & OLDRAW_ROW_GHOSTED) ? OLDRAW_GHOSTED : 0);
            h2 = cell->image->w + row_pad(row);
            box.x += h2;
            box.w -= h2;
        }
        if (cell->text && box.w > 0)
            oldraw_text_box(d, &box, cell->text, (size_t)-1, row->font, fg, 0, cell->align);
        pop_box(d, cbox_had, &cbox_saved);
    }
    pop_box(d, had, &saved);
    if (row->flags & OLDRAW_ROW_CURRENT)
        oldraw_select_box(d, (int)r->x, (int)r->y, (int)(r->x + r->w - 1), (int)(r->y + r->h - 1));
}

/* ---- a progress bar ---- */
int oldraw_progress_fill(int w, uint32_t done, uint32_t total)
{
    if (w <= 0 || total == 0) return 0;
    if (done >= total) return w;
    return (int)(((uint64_t)done * (uint64_t)w) / total);
}

void oldraw_progress(oldraw *d, const oldraw_rect *r, uint32_t done, uint32_t total, unsigned flags)
{
    oldraw_rect in, part;
    int n;
    if (!d || !r || r->w < 3 || r->h < 3) return;
    oldraw_frame(d, r, 1);
    in.x = r->x + 1;
    in.y = r->y + 1;
    in.w = r->w - 2;
    in.h = r->h - 2;
    n = oldraw_progress_fill((int)in.w, done, total);
    part = in;
    part.w = n;
    oldraw_fill(d, &part, OLDRAW_PEN(OLDRAW_PEN_FILL));
    part.x = in.x + n;
    part.w = in.w - n;
    oldraw_fill(d, &part, OLDRAW_PEN(OLDRAW_PEN_BACKGROUND));
    if (flags & OLDRAW_PROGRESS_PERCENT) {
        char buf[8];
        uint32_t pct = total ? (uint32_t)(((uint64_t)(done > total ? total : done) * 100u) / total) : 0;
        int k = 0, had;
        oldraw_rect saved;
        if (pct >= 100) buf[k++] = '1';
        if (pct >= 10) buf[k++] = (char)('0' + (pct / 10) % 10);
        buf[k++] = (char)('0' + pct % 10);
        buf[k++] = '%';
        buf[k] = 0;
        /* over the done part in the fill's text pen, over the rest in the text pen */
        part = in;
        part.w = n;
        push_box(d, &part, &had, &saved);
        oldraw_text_box(d, &in, buf, (size_t)k, NULL, OLDRAW_PEN(OLDRAW_PEN_FILLTEXT), 0, OLDRAW_CENTRE);
        pop_box(d, had, &saved);
        part.x = in.x + n;
        part.w = in.w - n;
        push_box(d, &part, &had, &saved);
        oldraw_text_box(d, &in, buf, (size_t)k, NULL, OLDRAW_PEN(OLDRAW_PEN_TEXT), 0, OLDRAW_CENTRE);
        pop_box(d, had, &saved);
    }
}

/* ---- a pie segment ---- */
/* sin of each whole degree 0-90, times 16384 */
static const uint16_t sin_deg[91] = {
    0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845, 3126, 3406, 3686, 3964, 4240, 4516, 4790, 5063,
    5334, 5604, 5872, 6138, 6402, 6664, 6924, 7182, 7438, 7692, 7943, 8192, 8438, 8682, 8923, 9162, 9397, 9630,
    9860, 10087, 10311, 10531, 10749, 10963, 11174, 11381, 11585, 11786, 11982, 12176, 12365, 12551, 12733, 12911,
    13085, 13255, 13421, 13583, 13741, 13894, 14044, 14189, 14330, 14466, 14598, 14726, 14849, 14968, 15082,
    15191, 15296, 15396, 15491, 15582, 15668, 15749, 15826, 15897, 15964, 16026, 16083, 16135, 16182, 16225,
    16262, 16294, 16322, 16344, 16362, 16374, 16382, 16384
};

/* sin of a in tenths of a degree, 0-900 */
static int32_t sin_q(int a)
{
    int deg = a / 10, frac = a % 10;
    if (deg >= 90) return 16384;
    return (int32_t)sin_deg[deg] + ((int32_t)sin_deg[deg + 1] - (int32_t)sin_deg[deg]) * frac / 10;
}

static int32_t isin(int a)
{
    a %= 3600;
    if (a < 0) a += 3600;
    if (a <= 900) return sin_q(a);
    if (a <= 1800) return sin_q(1800 - a);
    if (a <= 2700) return -sin_q(a - 1800);
    return -sin_q(3600 - a);
}

static int32_t icos(int a) { return isin(a + 900); }

/* a segment's two edges, worked out once for every pixel of it */
typedef struct {
    int32_t r2, sx, sy, ex, ey;
    int sweep;
} pie_edges;

static void pie_setup(pie_edges *p, int r, int start, int sweep)
{
    /* a direction a: (sin a, -cos a) on a screen whose y goes down; clockwise from u to v when u x v > 0 */
    if (r > OLDRAW_PIE_MAX) r = OLDRAW_PIE_MAX;       /* so r * r and dx * dx + dy * dy fit in 32 bits */
    p->r2 = (int32_t)r * r;
    p->sweep = sweep;
    p->sx = isin(start);
    p->sy = -icos(start);
    p->ex = isin(start + sweep);
    p->ey = -icos(start + sweep);
}

static int pie_in(const pie_edges *p, int dx, int dy)
{
    int32_t cs, ce;
    if (p->sweep <= 0) return 0;
    if (dx < -OLDRAW_PIE_MAX || dx > OLDRAW_PIE_MAX || dy < -OLDRAW_PIE_MAX || dy > OLDRAW_PIE_MAX) return 0;
    if ((int32_t)dx * dx + (int32_t)dy * dy > p->r2) return 0;
    if (p->sweep >= 3600 || (dx == 0 && dy == 0)) return 1;
    cs = p->sx * dy - p->sy * dx;     /* start x point */
    ce = dx * p->ey - dy * p->ex;     /* point x end */
    if (p->sweep <= 1800) return cs >= 0 && ce >= 0;
    return !(cs < 0 && ce < 0);
}

unsigned oldraw_pie_sweep(uint64_t used, uint64_t size)
{
    if (!size) return 0;
    if (used >= size) return 3600;
    /* used * 3600 overflows 64 bits only past 5e15 TB; divide first then */
    if (used > UINT64_MAX / 3600u) return (unsigned)(used / (size / 3600u ? size / 3600u : 1u));
    return (unsigned)(used * 3600u / size);
}

int oldraw_pie_has(int r, int start, int sweep, int dx, int dy)
{
    pie_edges p;
    if (r <= 0) return 0;
    pie_setup(&p, r, start, sweep);
    return pie_in(&p, dx, dy);
}

static int32_t isqrt32(int32_t v)
{
    int32_t r = 0, b = (int32_t)1 << 30;
    if (v <= 0) return 0;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

void oldraw_pie(oldraw *d, int cx, int cy, int r, int start, int sweep, oldraw_colour c)
{
    pie_edges p;
    int dy, y0, y1;
    oldraw_rect clip;
    if (!d || r <= 0 || sweep <= 0 || c == OLDRAW_NONE || !oldraw_clip_now(d, &clip)) return;
    if (r > OLDRAW_PIE_MAX) r = OLDRAW_PIE_MAX;
    pie_setup(&p, r, start, sweep);
    /* only the rows and columns inside the clip are walked */
    y0 = (int)clip.y - cy > -r ? (int)clip.y - cy : -r;
    y1 = (int)(clip.y + clip.h - 1) - cy < r ? (int)(clip.y + clip.h - 1) - cy : r;
    for (dy = y0; dy <= y1; ++dy) {
        int hw = (int)isqrt32((int32_t)r * r - (int32_t)dy * dy), dx = -hw, xe = hw;
        if ((int)clip.x - cx > dx) dx = (int)clip.x - cx;
        if ((int)(clip.x + clip.w - 1) - cx < xe) xe = (int)(clip.x + clip.w - 1) - cx;
        hw = xe;
        while (dx <= hw) {
            int from;
            while (dx <= hw && !pie_in(&p, dx, dy)) ++dx;
            from = dx;
            while (dx <= hw && pie_in(&p, dx, dy)) ++dx;
            if (dx > from) fill(d, cx + from, cy + dy, dx - from, 1, c);
        }
    }
}

/* ---- lines, polygons and ellipses ---- */
/* Polygons are filled in half pixels: a pixel's corners are at even numbers and its centre at the odd one between,
 * so a wide line's edges and a polygon's corners can sit half way across a pixel. A pixel is filled when its centre
 * is inside, with the top and left edges in and the bottom and right ones out. */
static long long floor_div64(long long a, long long b)
{
    long long q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

static long long ceil_div64(long long a, long long b) { return -floor_div64(-a, b); }

static uint64_t isqrt64(uint64_t v)
{
    uint64_t r = 0, b = (uint64_t)1 << 62;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* a fill from column a to column b - 1 of row y, cut to the clip by fill() */
static void span(oldraw *d, long long a, long long b, int y, const oldraw_rect *clip, oldraw_colour c)
{
    if (a < clip->x) a = clip->x;
    if (b > (long long)clip->x + clip->w) b = (long long)clip->x + clip->w;
    if (b > a) fill(d, (int)a, y, (int)(b - a), 1, c);
}

static void sort_ints(long long *v, int n)
{
    int i, j;
    for (i = 1; i < n; ++i) {
        long long t = v[i];
        for (j = i; j > 0 && v[j - 1] > t; --j) v[j] = v[j - 1];
        v[j] = t;
    }
}

/* the polygon through n points, in half pixels (each within 2 * OLDRAW_FAR of 0, so the products below fit) */
static long long far_cut(long long v) { return v > 2LL * OLDRAW_FAR ? 2LL * OLDRAW_FAR : v < -2LL * OLDRAW_FAR ? -2LL * OLDRAW_FAR : v; }

static void polygon_half(oldraw *d, long long *xy, int n, oldraw_colour c)
{
    long long stack[32], *k = stack, ymin, ymax;
    long j0, j1, j;
    int i;
    oldraw_rect clip;
    if (n < 3 || c == OLDRAW_NONE || !oldraw_clip_now(d, &clip)) return;
    for (i = 0; i < 2 * n; ++i) xy[i] = far_cut(xy[i]);
    ymin = ymax = xy[1];
    for (i = 1; i < n; ++i) {
        if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
        if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
    }
    j0 = (long)ceil_div64(ymin - 1, 2);                          /* the first row whose centre 2j + 1 >= ymin */
    j1 = (long)ceil_div64(ymax - 1, 2);                          /* rows before it: 2j + 1 < ymax */
    if (j0 < clip.y) j0 = clip.y;
    if (j1 > (long)clip.y + clip.h) j1 = (long)clip.y + clip.h;
    if (j1 <= j0) return;
    if (n > 32 && !(k = malloc(sizeof *k * (size_t)n))) return;
    for (j = j0; j < j1; ++j) {
        long long y = 2LL * j + 1;
        int m = 0;
        for (i = 0; i < n; ++i) {
            const long long *a = xy + 2 * i, *b = xy + 2 * ((i + 1) % n);
            long long den, num;
            if (a[1] == b[1]) continue;
            if (!((a[1] <= y && y < b[1]) || (b[1] <= y && y < a[1]))) continue;
            /* the first pixel whose centre is right of the crossing x: ceil((x - 1) / 2) */
            den = b[1] - a[1];
            num = (a[0] - 1) * den + (b[0] - a[0]) * (y - a[1]);
            if (den < 0) { den = -den; num = -num; }
            k[m++] = ceil_div64(num, 2 * den);
        }
        sort_ints(k, m);
        for (i = 0; i + 1 < m; i += 2) span(d, k[i], k[i + 1], (int)j, &clip, c);
    }
    if (k != stack) free(k);
}

void oldraw_polygon(oldraw *d, const oldraw_point *pts, int n, oldraw_colour c)
{
    long long stack[64], *xy = stack;
    int i;
    if (!d || !pts || n < 3) return;
    if (n > 32 && !(xy = malloc(sizeof *xy * 2 * (size_t)n))) return;
    for (i = 0; i < n; ++i) {
        xy[2 * i] = 2LL * pts[i].x;
        xy[2 * i + 1] = 2LL * pts[i].y;
    }
    polygon_half(d, xy, n, c);
    if (xy != stack) free(xy);
}

/* One pixel wide: for each column (or row, when the line is steeper than 45 degrees) the pixel nearest the line,
 * worked out for that column alone, so only the columns inside the clip are walked and the line's pixels are the
 * same whichever end it is drawn from. Runs of pixels in a row (a column) are one fill. */
#define BIG (1LL << 30)
static void thin_line(oldraw *d, long long x0, long long y0, long long x1, long long y1, oldraw_colour c)
{
    oldraw_rect clip;
    long long dx, dy, t, from, to, lo, hi;
    int steep;
    if (!oldraw_clip_now(d, &clip)) return;
    if (x1 - x0 > BIG || x0 - x1 > BIG || y1 - y0 > BIG || y0 - y1 > BIG) {
        /* so 2 * (u - x0) * dy fits in 64 bits: the two halves, from the middle */
        long long mx = floor_div64(x0 + x1, 2), my = floor_div64(y0 + y1, 2);
        thin_line(d, x0, y0, mx, my, c);
        thin_line(d, mx, my, x1, y1, c);
        return;
    }
    steep = (y1 > y0 ? y1 - y0 : y0 - y1) > (x1 > x0 ? x1 - x0 : x0 - x1);
    if (steep) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x1 < x0) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    dx = x1 - x0;
    dy = y1 - y0;
    lo = steep ? clip.y : clip.x;
    hi = (steep ? (long long)clip.y + clip.h : (long long)clip.x + clip.w) - 1;
    from = x0 > lo ? x0 : lo;
    to = x1 < hi ? x1 : hi;
    if (to < from) return;
    {
        long long run = from, ry = 0, u;
        for (u = from; u <= to + 1; ++u) {
            long long v = u <= to ? (dx ? y0 + floor_div64(2 * (u - x0) * dy + dx, 2 * dx) : y0) : 0;
            if (u > from && (u > to || v != ry)) {
                if (ry >= -2147483647LL && ry <= 2147483647LL) {
                    if (steep) fill(d, (int)ry, (int)run, 1, (int)(u - run), c);
                    else fill(d, (int)run, (int)ry, (int)(u - run), 1, c);
                }
                run = u;
            }
            ry = v;
        }
    }
}

/* A band width pixels across: a quadrilateral in half pixels. A line reaching far off the target is halved, and the
 * halves that miss the clip dropped, until its points are near enough for the band's corners to be exact. */
static void wide_line(oldraw *d, long long x0, long long y0, long long x1, long long y1, int width, oldraw_colour c,
                      const oldraw_rect *clip)
{
    long long dx = x1 - x0, dy = y1 - y0, len, ox, oy, ex, ey, cx0, cy0, cx1, cy1, xy[8];
    long long near = OLDRAW_FAR / 4;
    if ((x0 < x1 ? x0 : x1) - width > (long long)clip->x + clip->w || (x0 > x1 ? x0 : x1) + width < clip->x ||
        (y0 < y1 ? y0 : y1) - width > (long long)clip->y + clip->h || (y0 > y1 ? y0 : y1) + width < clip->y)
        return;
    if ((x0 > near || -x0 > near || y0 > near || -y0 > near || x1 > near || -x1 > near || y1 > near || -y1 > near) &&
        (dx > 2 || -dx > 2 || dy > 2 || -dy > 2)) {
        long long mx = floor_div64(x0 + x1, 2), my = floor_div64(y0 + y1, 2);
        wide_line(d, x0, y0, mx, my, width, c, clip);
        wide_line(d, mx, my, x1, y1, width, c, clip);
        return;
    }
    if (!dx && !dy) {
        oldraw_rect r;
        r.x = (int32_t)(x0 - width / 2);
        r.y = (int32_t)(y0 - width / 2);
        r.w = r.h = width;
        oldraw_fill(d, &r, c);
        return;
    }
    len = (long long)isqrt64((uint64_t)(dx * dx + dy * dy));
    if (len < 1) len = 1;
    /* in half pixels: the band's half width is width, and the ends go half a pixel past the points' centres */
    ox = floor_div64(-dy * width * 2 + len, 2 * len);
    oy = floor_div64(dx * width * 2 + len, 2 * len);
    ex = floor_div64(dx * 2 + len, 2 * len);
    ey = floor_div64(dy * 2 + len, 2 * len);
    cx0 = 2 * x0 + 1 - ex;
    cy0 = 2 * y0 + 1 - ey;
    cx1 = 2 * x1 + 1 + ex;
    cy1 = 2 * y1 + 1 + ey;
    xy[0] = cx0 - ox; xy[1] = cy0 - oy;
    xy[2] = cx1 - ox; xy[3] = cy1 - oy;
    xy[4] = cx1 + ox; xy[5] = cy1 + oy;
    xy[6] = cx0 + ox; xy[7] = cy0 + oy;
    polygon_half(d, xy, 4, c);
}

void oldraw_line(oldraw *d, int x0, int y0, int x1, int y1, int width, oldraw_colour c)
{
    oldraw_rect clip;
    if (!d || c == OLDRAW_NONE) return;
    if (width <= 1) { thin_line(d, x0, y0, x1, y1, c); return; }
    if (width > 4096) width = 4096;
    if (oldraw_clip_now(d, &clip)) wide_line(d, x0, y0, x1, y1, width, c, &clip);
}

void oldraw_polyline(oldraw *d, const oldraw_point *pts, int n, int closed, int width, oldraw_colour c)
{
    int i;
    if (!d || !pts || n < 1) return;
    if (n == 1) { oldraw_line(d, pts[0].x, pts[0].y, pts[0].x, pts[0].y, width, c); return; }
    for (i = 0; i + 1 < n; ++i) oldraw_line(d, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, width, c);
    if (closed && n > 2) oldraw_line(d, pts[n - 1].x, pts[n - 1].y, pts[0].x, pts[0].y, width, c);
}

/* The columns l to r - 1 an ellipse in box x, y, w, h fills on row j; 0 when none. In half pixels the centre is
 * 2x + w, 2y + h and the radii are w and h; a pixel's centre 2i + 1, 2j + 1 is in when
 * dx^2 h^2 + dy^2 w^2 <= w^2 h^2. */
static int ellipse_row(long long x, long long y, long long w, long long h, long long j, long long *l, long long *r)
{
    long long dy, m;
    if (w <= 0 || h <= 0) return 0;
    if (w > OLDRAW_ELLIPSE_MAX) w = OLDRAW_ELLIPSE_MAX;
    if (h > OLDRAW_ELLIPSE_MAX) h = OLDRAW_ELLIPSE_MAX;
    dy = 2 * j + 1 - (2 * y + h);
    if (dy < 0) dy = -dy;
    if (dy > h) return 0;
    m = (long long)isqrt64((uint64_t)(w * w * (h * h - dy * dy) / (h * h)));
    *l = ceil_div64(2 * x + w - m - 1, 2);
    *r = floor_div64(2 * x + w + m - 1, 2) + 1;
    return *r > *l;
}

int oldraw_ellipse_has(const oldraw_rect *r, int x, int y)
{
    long long l, rr;
    return r && ellipse_row(r->x, r->y, r->w, r->h, y, &l, &rr) && x >= l && x < rr;
}

static void ellipse_rows(oldraw *d, const oldraw_rect *r, int width, oldraw_colour c)
{
    oldraw_rect clip;
    long long j, j0, j1;
    if (!d || !r || r->w <= 0 || r->h <= 0 || c == OLDRAW_NONE || !oldraw_clip_now(d, &clip)) return;
    j0 = r->y > clip.y ? r->y : clip.y;
    j1 = (long long)r->y + r->h < (long long)clip.y + clip.h ? (long long)r->y + r->h : (long long)clip.y + clip.h;
    for (j = j0; j < j1; ++j) {
        long long l0, r0, l1, r1;
        if (!ellipse_row(r->x, r->y, r->w, r->h, j, &l0, &r0)) continue;
        if (width <= 0 || !ellipse_row((long long)r->x + width, (long long)r->y + width, (long long)r->w - 2LL * width,
                                       (long long)r->h - 2LL * width, j, &l1, &r1)) {
            span(d, l0, r0, (int)j, &clip, c);
            continue;
        }
        span(d, l0, l1 < r0 ? l1 : r0, (int)j, &clip, c);
        span(d, r1 > l0 ? r1 : l0, r0, (int)j, &clip, c);
    }
}

void oldraw_ellipse(oldraw *d, const oldraw_rect *r, oldraw_colour c) { ellipse_rows(d, r, 0, c); }

void oldraw_ellipse_frame(oldraw *d, const oldraw_rect *r, int width, oldraw_colour c)
{
    ellipse_rows(d, r, width < 1 ? 1 : width, c);
}
