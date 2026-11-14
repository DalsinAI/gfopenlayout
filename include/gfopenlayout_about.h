/* GFOpenLayout About windows: the same About window for every GFOS program (Team 5, 14 November 2026: "About
 * windows with the GFOS mark: GFOpenBench, GFOpenPrefs and GFOpenTitle first; the rest follow the same pattern").
 * The GFOS mark (the Deep cradle) on the left, the program's name and version in bold beside it, its own lines under
 * them, the Team's credit, and an OK button. Drawn only through oldraw (gfopenlayout_draw.h).
 *
 *     static const olabout about = { "GFOpenBench", "0.1.60", "Our own Workbench for AmigaOS 3.2.\n"
 *                                    "MIT, Copyright (c) 2026 AmiCradle Team." };
 *     olabout_show(screen, &about);          (Amiga: opens the window, waits for OK, Return, Escape or close)
 *
 * The mark: an arc #0b5ed7 open about 124 degrees at the top, holding a dot #ff7a1a (Dale, 14 November 2026), the
 * same shape as gfopenprefs' TitleBar/tools/cradle_mark.py and Blanker/src/bl_mark.h, here in whole numbers (no
 * floating point in the library). MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef GFOPENLAYOUT_ABOUT_H
#define GFOPENLAYOUT_ABOUT_H

#include "gfopenlayout_draw.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OLABOUT_VERSION "1.0"

/* ---- the GFOS mark ---- */
#define OLABOUT_ARC_RGB 0x0B5ED7UL
#define OLABOUT_DOT_RGB 0xFF7A1AUL
#define OLABOUT_CREDIT "Designed, tested and delivered by The AmiCradle Team."
enum { OLABOUT_NONE = 0, OLABOUT_ARC = 1, OLABOUT_DOT = 2 };
/* The mark's width at height h (2 to 512 pixels; 0 outside) */
int olabout_mark_width(int h);
/* What the pixel at x, y of the mark h high is: OLABOUT_NONE, OLABOUT_ARC or OLABOUT_DOT (each pixel in or out by its
 * centre) */
int olabout_mark_at(int h, int x, int y);
/* The mark h high as 0xAARRGGBB pixels into argb (olabout_mark_width(h) * h of them, rows that many apart): the arc
 * in arc_rgb (OLABOUT_ARC_RGB, or white on the blue title bar), the dot in OLABOUT_DOT_RGB, the rest see-through.
 * The width. */
int olabout_mark_argb(int h, uint32_t arc_rgb, uint32_t *argb);
/* The same as an oldraw picture (oldraw_image_free it). NULL when memory is short. */
oldraw_image *olabout_mark_image(oldraw *d, int h, uint32_t arc_rgb);

/* ---- the window's inside ---- */
typedef struct olabout {
    const char *name;           /* "GFOpenBench" */
    const char *version;        /* "0.1.60" (NULL: none) */
    const char *text;           /* the program's own lines, '\n' between them (NULL: none) */
} olabout;

#define OLABOUT_MAX_LINES 12
typedef struct olabout_layout {
    int width, height;                          /* the inside's size */
    oldraw_rect mark;                           /* where the mark is drawn (its height is the mark's) */
    int lines;                                  /* the name line, the program's lines, the credit */
    int line_x, baseline[OLABOUT_MAX_LINES];    /* each line's left and baseline */
    const char *line[OLABOUT_MAX_LINES];        /* each line's text, not ended: line_len bytes */
    size_t line_len[OLABOUT_MAX_LINES];
    oldraw_rect button;                         /* OK */
    char title[96];                             /* the name line: "GFOpenBench 0.1.60" */
} olabout_layout;

/* Lays a out in d's font: sizes measured with oldraw_text_measure. A line past OLABOUT_MAX_LINES - 2 of the program's
 * is left out. */
void olabout_layout_for(oldraw *d, const olabout *a, olabout_layout *l);
/* Draws the inside: the background, the mark (mark: olabout_mark_image at l->mark.h; NULL: none), the lines, and OK
 * (pressed: drawn pushed in) */
void olabout_draw(oldraw *d, const olabout_layout *l, const oldraw_image *mark, int pressed);
/* 1 when x, y is on OK */
int olabout_on_button(const olabout_layout *l, int x, int y);
/* 1 when the key (a VANILLAKEY's code) closes the window: Return, Enter, Escape, 'o' and 'O' */
int olabout_closes(unsigned key);

#if defined(AMIGA) || defined(__amigaos__)
struct Screen;
struct Window;
/* The About window on scr (NULL: the default public screen), centred, until OK, Return, Escape or its close
 * gadget. 1 when it was shown; 0 when it couldn't open (the caller may fall back to an EasyRequest). */
int olabout_show(struct Screen *scr, const olabout *a);
/* The same window for a program that keeps running while it is open (a commodity's clock, a title bar): open it,
 * Wait on its window's UserPort signal with the program's own, and call olabout_handle when it comes; 1 from
 * olabout_handle means close it (olabout_close). a is kept, not copied: it lives while the window does. NULL when
 * it couldn't open. */
typedef struct olabout_win olabout_win;
olabout_win *olabout_open(struct Screen *scr, const olabout *a);
struct Window *olabout_window(const olabout_win *aw);
int olabout_handle(olabout_win *aw);
void olabout_close(olabout_win *aw);
#endif

#ifdef __cplusplus
}
#endif
#endif
