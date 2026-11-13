/* GFOpenLayout drawing: the calls Workbench build wave 1's modules draw with (the Team, 13 November 2026: "no
 * direct graphics.library or layers.library drawing in wave-1 modules"). A module never calls RectFill, BltBitMap,
 * Text, Draw, SetAPen and the like itself: it asks an oldraw for an icon, a list row, text, a selection box, a
 * progress bar or a pie segment, and marks what changed with oldraw_damage, and GFOpenLayout draws it. What
 * GFOpenLayout does inside (batching, caches for icon imagery and text, the AC090 and GFOpenGPU hooks) may change in
 * wave 2 without a module changing. A primitive a module needs that isn't here is added here (Team 3, row 10), not
 * drawn round.
 *
 *     oldraw *d = oldraw_new_rastport(win->RPort, win->WScreen, win->Width, win->Height);
 *     oldraw_damage(d, &changed);                 (as often as things change)
 *     oldraw_redraw(d, draw_window, me);          (draw_window draws what crosses its rectangle, clipped to it)
 *     oldraw_free(d);
 *
 * Coordinates are the target's pixels (a window's RastPort: from the window's top left corner). A rectangle is
 * x, y, w, h; one with w or h of 0 or less is empty and draws nothing. Every call clips to the target and, inside
 * oldraw_redraw or after oldraw_clip, to the clip rectangle. No call draws outside its rectangle.
 *
 * Colours are 0xFFRRGGBB (opaque), OLDRAW_PEN(role) (the screen's pen for that job, from its DrawInfo, so GFOpenPrefs'
 * Palette (P3) applies), or OLDRAW_NONE (nothing is drawn). On a screen with a palette, an RGB colour is the screen's
 * nearest pen.
 *
 * The calls are for one task at a time on one oldraw. The library has no floating point.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GFOPENLAYOUT_DRAW_H
#define GFOPENLAYOUT_DRAW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OLDRAW_VERSION 1

typedef struct oldraw oldraw;
typedef struct oldraw_image oldraw_image;

typedef struct oldraw_rect {
    int32_t x, y, w, h;
} oldraw_rect;

/* ---- colours ---- */
typedef uint32_t oldraw_colour;
#define OLDRAW_NONE 0x00000000UL
#define OLDRAW_RGB(rgb) (0xFF000000UL | ((uint32_t)(rgb) & 0xFFFFFFUL))
#define OLDRAW_PEN(role) (0xFE000000UL | ((uint32_t)(role) & 0xFFUL))
/* The jobs a pen does: the same order and meaning as intuition's DrawInfo pens (DETAILPEN ... BARTRIMPEN), so a
 * module gives the 3.2.3 look and the user's Palette without knowing a pen number. */
enum {
    OLDRAW_PEN_DETAIL = 0,
    OLDRAW_PEN_BLOCK,
    OLDRAW_PEN_TEXT,
    OLDRAW_PEN_SHINE,
    OLDRAW_PEN_SHADOW,
    OLDRAW_PEN_FILL,            /* a selected row, a progress bar's done part */
    OLDRAW_PEN_FILLTEXT,        /* text over OLDRAW_PEN_FILL */
    OLDRAW_PEN_BACKGROUND,
    OLDRAW_PEN_HIGHLIGHTTEXT,
    OLDRAW_PEN_BARDETAIL,
    OLDRAW_PEN_BARBLOCK,
    OLDRAW_PEN_BARTRIM,
    OLDRAW_PEN_COUNT             /* how many */
};

/* ---- targets ---- */
struct RastPort;
struct Screen;
/* An oldraw on rp, width by height pixels (a window's RastPort, or a BitMap's of the screen's kind), with scr's
 * pens and font (NULL: the default public screen's). The pens are found when it is made: make it again (or call
 * oldraw_pens_changed) after the Palette changes. NULL when memory is short. Amiga only. */
oldraw *oldraw_new_rastport(struct RastPort *rp, struct Screen *scr, int width, int height);
/* The size changed (a window resized): the clip is the whole target again and the damage is dropped. */
void oldraw_resize(oldraw *d, int width, int height);
/* Read the screen's pens again (GFOpenPrefs' Palette was saved). */
void oldraw_pens_changed(oldraw *d);
void oldraw_free(oldraw *d);

/* What a target does, for other targets (a host test's pixels, wave 2's GPU): every call is already clipped to
 * the target and the clip, with w and h above 0. */
typedef struct oldraw_text_size {
    int width;              /* the pen's advance, whole pixels */
    int ascent;             /* above the baseline */
    int descent;            /* below it */
} oldraw_text_size;
typedef struct oldraw_font {
    const char *family;     /* "DejaVu Sans", "topaz.font", "sans-serif"; NULL: the screen's font */
    int px;                 /* its size in pixels; 0: the screen font's */
    unsigned style;         /* OLDRAW_BOLD ... */
} oldraw_font;
enum {
    OLDRAW_BOLD = 1u << 0,
    OLDRAW_ITALIC = 1u << 1,
    OLDRAW_UNDERLINE = 1u << 2,
    OLDRAW_SHADOW = 1u << 3,        /* one pixel down and right, in OLDRAW_PEN_SHADOW's pen (an icon's label) */
    OLDRAW_OUTLINE = 1u << 4        /* one pixel round it, in OLDRAW_PEN_SHADOW's pen */
};
typedef struct oldraw_ops {
    /* resolves a colour to what fill and text take (an RGB: the pen's colour; the target keeps pens itself) */
    void (*fill)(void *target, int x, int y, int w, int h, oldraw_colour c);
    /* every pixel in the rectangle complemented (drawn twice: as it was) */
    void (*invert)(void *target, int x, int y, int w, int h);
    /* image's pixels sx,sy on, w by h, at x,y; flags OLDRAW_SELECTED, OLDRAW_GHOSTED */
    void (*image)(void *target, const oldraw_image *img, int sx, int sy, int x, int y, int w, int h, unsigned flags);
    /* text with its baseline at y, cut to clip; 1 when drawn */
    int (*text)(void *target, int x, int y, const char *utf8, size_t length, const oldraw_font *font, oldraw_colour c,
                unsigned style, const oldraw_rect *clip);
    int (*measure)(void *target, const char *utf8, size_t length, const oldraw_font *font, oldraw_text_size *size);
    /* the end of a batch: what was drawn goes to the screen */
    void (*flush)(void *target);
    /* free the target's side of an image (may be NULL) */
    void (*image_free)(void *target, oldraw_image *img);
    /* the target's own pixels w by h at sx,sy copied to dx,dy, as memmove copies (the two may overlap); both are
     * inside the target and the clip */
    void (*copy)(void *target, int sx, int sy, int dx, int dy, int w, int h);
} oldraw_ops;
/* An oldraw on any target. pens: the colour of each OLDRAW_PEN role as 0xFFRRGGBB (NULL: grey 3.2.3 ones). */
oldraw *oldraw_new(const oldraw_ops *ops, void *target, int width, int height, const oldraw_colour *pens);
/* A role's colour as 0xFFRRGGBB; an RGB colour as it is. */
oldraw_colour oldraw_resolve(const oldraw *d, oldraw_colour c);

/* ---- damage and redraw ---- */
/* r needs drawing again. Touching and overlapping rectangles are merged; at most OLDRAW_DAMAGE_MAX are kept, past
 * that the two that grow least are joined. Cut to the target. */
#define OLDRAW_DAMAGE_MAX 16
void oldraw_damage(oldraw *d, const oldraw_rect *r);
/* All of it needs drawing again (after a resize, a theme change). */
void oldraw_damage_all(oldraw *d);
/* How many rectangles are waiting; with r, the i-th of them. */
int oldraw_damage_count(const oldraw *d);
int oldraw_damage_get(const oldraw *d, int i, oldraw_rect *r);
/* For each rectangle waiting: the clip set to it, fn called to draw what crosses it (fn draws with these calls
 * only; drawing outside it is cut off), the clip taken off. Then the damage is empty and the batch is flushed.
 * Returns how many rectangles were drawn. */
typedef void (*oldraw_redraw_fn)(void *userdata, oldraw *d, const oldraw_rect *area);
int oldraw_redraw(oldraw *d, oldraw_redraw_fn fn, void *userdata);
/* A clip of the program's own (inside oldraw_redraw: inside the damage rectangle too); NULL: none. */
void oldraw_clip(oldraw *d, const oldraw_rect *r);
/* What is drawn goes to the screen now (outside oldraw_redraw: after a selection box moves). */
void oldraw_flush(oldraw *d);

/* ---- copy and scroll ---- */
/* The pixels of src copied to dx,dy on the same target, as memmove copies, so the two may overlap (a drawer's
 * rows moved up). The source and the destination are cut together: what is copied is what both the source (cut to
 * the target) and the destination (cut to the target and the clip) cover. Where the destination's source was off
 * the target, nothing is copied and that part is damaged (oldraw_damage), for the next oldraw_redraw to draw. */
void oldraw_copy(oldraw *d, const oldraw_rect *src, int dx, int dy);
/* What is in area moved by ddx, ddy inside it (ddy below 0: up, as a list scrolled down a row); the strip it
 * leaves is damaged. A move as big as the area or bigger damages it all and copies nothing. */
void oldraw_scroll(oldraw *d, const oldraw_rect *area, int ddx, int ddy);

/* ---- boxes ---- */
void oldraw_fill(oldraw *d, const oldraw_rect *r, oldraw_colour c);
/* A one-pixel bevel round the inside of r: raised (shine top and left, shadow bottom and right), or recessed. */
void oldraw_frame(oldraw *d, const oldraw_rect *r, int recessed);

/* ---- text ---- */
/* utf8 (length bytes; (size_t)-1: to its NUL) with its baseline at y, its left at x. The width it took. */
int oldraw_text(oldraw *d, int x, int y, const char *utf8, size_t length, const oldraw_font *font, oldraw_colour c,
                unsigned style);
/* Its size, without drawing. 1 with *size filled in. */
int oldraw_text_measure(oldraw *d, const char *utf8, size_t length, const oldraw_font *font, unsigned style,
                     oldraw_text_size *size);
/* utf8 in the box r, centred top to bottom, at its left, centre or right; cut at a character with "..." on the end
 * when it doesn't fit. How many bytes of utf8 were drawn (length when all of it). */
enum { OLDRAW_LEFT = 0, OLDRAW_CENTRE = 1, OLDRAW_RIGHT = 2 };
size_t oldraw_text_box(oldraw *d, const oldraw_rect *r, const char *utf8, size_t length, const oldraw_font *font,
                       oldraw_colour c, unsigned style, int align);

/* ---- icons and pictures ---- */
/* A picture of w by h pixels, 0xAARRGGBB, rows stride pixels apart, copied (alpha 128 and over is drawn, the rest is
 * see-through). NULL when memory is short. */
oldraw_image *oldraw_image_argb(oldraw *d, int w, int h, const uint32_t *argb, int stride);
/* An icon's image (icon.library's DiskObject, its own colours or the screen's). The DiskObject must stay until
 * oldraw_image_free. Amiga only: NULL elsewhere, and on a screen icon.library can't draw on. */
struct DiskObject;
oldraw_image *oldraw_image_icon(oldraw *d, struct DiskObject *icon);
int oldraw_image_width(const oldraw_image *img);
int oldraw_image_height(const oldraw_image *img);
/* 0xAARRGGBB of the pixel at x,y of an ARGB picture (0 outside it, or for an icon). */
uint32_t oldraw_image_pixel(const oldraw_image *img, int x, int y);
void oldraw_image_free(oldraw *d, oldraw_image *img);
/* The picture's top left at x,y. OLDRAW_SELECTED: as a selected icon (its second image, or darkened);
 * OLDRAW_GHOSTED: dimmed (a disk being worked on, a busy drawer). */
enum { OLDRAW_SELECTED = 1u << 0, OLDRAW_GHOSTED = 1u << 1 };
void oldraw_image_draw(oldraw *d, const oldraw_image *img, int x, int y, unsigned flags);

/* ---- a list's row ---- */
typedef struct oldraw_cell {
    const char *text;           /* NULL: none */
    const oldraw_image *image;  /* drawn before the text, centred top to bottom; NULL: none */
    int width;                  /* pixels; 0: the rest of the row (the last cell only) */
    int align;                  /* OLDRAW_LEFT ... */
} oldraw_cell;
enum {
    OLDRAW_ROW_SELECTED = 1u << 0,  /* OLDRAW_PEN_FILL behind, OLDRAW_PEN_FILLTEXT text */
    OLDRAW_ROW_CURRENT = 1u << 1,   /* the keyboard's row: a dotted box round it */
    OLDRAW_ROW_STRIPE = 1u << 2,    /* an odd row of a striped list: a shade off the background */
    OLDRAW_ROW_GHOSTED = 1u << 3    /* dimmed text and images */
};
typedef struct oldraw_row {
    const oldraw_cell *cells;
    int ncells;
    unsigned flags;             /* OLDRAW_ROW_... */
    const oldraw_font *font;    /* NULL: the screen's */
    int pad;                    /* pixels between cells and inside each end; below 0: 4 */
} oldraw_row;
/* The row filling r: its background, then each cell left to right, cut to its width and to r. */
void oldraw_list_row(oldraw *d, const oldraw_rect *r, const oldraw_row *row);
/* Where cell i of row would go in r (its text and image inside pad), for hit tests and column drags. 1 when it has
 * room. */
int oldraw_list_cell_box(const oldraw_row *row, const oldraw_rect *r, int i, oldraw_rect *cell);

/* ---- the selection box (the lasso) ---- */
/* A dotted box from x0,y0 to x1,y1 (either corner first), complemented as 3.2.3's Workbench draws it: the same
 * call again takes it off, so a drag draws the old one, then the new. */
void oldraw_select_box(oldraw *d, int x0, int y0, int x1, int y1);

/* ---- a progress bar ---- */
/* A recessed bar in r, done of total filled from the left in OLDRAW_PEN_FILL, the rest OLDRAW_PEN_BACKGROUND. done above
 * total is total; total 0 is empty. OLDRAW_PROGRESS_PERCENT also writes "n%" in the middle. */
enum { OLDRAW_PROGRESS_PERCENT = 1u << 0 };
void oldraw_progress(oldraw *d, const oldraw_rect *r, uint32_t done, uint32_t total, unsigned flags);
/* How many pixels of the inside of a bar w wide are filled (what oldraw_progress draws). */
int oldraw_progress_fill(int w, uint32_t done, uint32_t total);

/* ---- a pie segment ---- */
/* The part of the circle round cx,cy of radius r from start for sweep, in tenths of a degree, 0 at the top,
 * clockwise (a disk's used part: start 0, sweep used * 3600 / size). sweep 3600 and over: the whole circle. */
void oldraw_pie(oldraw *d, int cx, int cy, int r, int start, int sweep, oldraw_colour c);
/* 1 when the pixel at dx,dy from the centre is in that segment (what oldraw_pie fills). */
int oldraw_pie_has(int r, int start, int sweep, int dx, int dy);

#ifdef __cplusplus
}
#endif

#endif
