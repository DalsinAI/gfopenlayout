/* GFOpenLayout UI in an Intuition window: draws olui's ops through
 * graphics.library (GFOpenGfx), true-colour boxes with cybergraphics'
 * FillPixelArray as GFOpenGadTools does, text in the screen's font at each
 * size through diskfont.library, all into a buffer the size of the window's
 * inside that is copied to the window when it changes; and turns the
 * window's IDCMP messages into olui_mouse and olui_key.
 *
 *     struct Window *w = OpenWindowTags(..., WA_IDCMP, olui_amiga_idcmp(), WA_ReportMouse, TRUE, ...);
 *     olui_amiga *a = olui_amiga_new(ui, w);
 *     olui_amiga_theme(a, NULL);              (the theme GFOpenPrefs Look chose)
 *     olui_amiga_layout(a);
 *     for (;;) { WaitPort(w->UserPort); while ((m = GT_GetIMsg / GetMsg(...))) {
 *         int r = olui_amiga_event(a, m); ReplyMsg(m); if (r == OLUI_AMIGA_CLOSE) ...; }
 *         olui_amiga_draw(a); }
 *
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#ifndef OPENLAYOUT_UI_AMIGA_H
#define OPENLAYOUT_UI_AMIGA_H

#include <exec/types.h>
#include "gfopenlayout_ui.h"

struct Window;
struct IntuiMessage;

typedef struct olui_amiga olui_amiga;

/* What olui_amiga_event saw */
enum {
    OLUI_AMIGA_NONE = 0,
    OLUI_AMIGA_USED,        /* olui took it (its handlers have run) */
    OLUI_AMIGA_CLOSE        /* the close gadget */
};

/* The window's IDCMP for olui: the mouse, keys, size, refresh, close, and
 * IntuiTicks (olui_tick: tooltips, a number field's held arrow). */
ULONG olui_amiga_idcmp(void);

/* Draws ui in win, measuring with the window's screen font (olui_set_measure
 * is set here). NULL when memory or a library is missing. */
olui_amiga *olui_amiga_new(olui *ui, struct Window *win);
void olui_amiga_free(olui_amiga *a);

/* The theme as CSS before the program's own: a theme file's path, or NULL
 * for the one GFOpenPrefs Look chose (ENV:GFOpenGadTools/Look, from
 * SYS:Prefs/Presets/Themes). 1 when one was read. */
int olui_amiga_theme(olui_amiga *a, const char *path);

/* A widget the program draws itself (a div with a background, so it has a
 * box): the buffer is copied to the window around it, and fn is called
 * with its box in the window's RastPort's pixels each time the window is
 * drawn where it is, to draw it there. fn NULL gives it back. 32 at most
 * (four before library 18); 1 when it was taken. */
typedef void (*olui_amiga_own_fn)(void *userdata, const olui_el *el, int x, int y, int w, int h);
int olui_amiga_own(olui_amiga *a, const olui_el *el, olui_amiga_own_fn fn, void *userdata);

/* Library 18: input for a widget the program draws itself (MUI's
 * MUIM_HandleInput and HandleEvent). After olui_amiga_own, what it asks
 * for (OLUI_AMIGA_OWN_..., 0: none, the default) is not given to olui:
 * olui_amiga_event returns OLUI_AMIGA_NONE for it, and olui_amiga_owner
 * says whose it was, so the program hands the message on.
 * OLUI_AMIGA_OWN_MOUSE: a press of either button in its box, every move
 * and the release while the button is held (anywhere: it keeps the mouse
 * from its press), and the wheel over it. OLUI_AMIGA_OWN_KEYS: once a
 * press in its box has given it the keys, every key (VANILLAKEY and
 * RAWKEY, Tab too) until a press anywhere else, which goes on to olui as
 * usual; olui's focus is taken off while it has them. While a pop-up of
 * olui's is open (olui_popup), everything is olui's, so the click that
 * closes it gets there. 1 when the widget is owned. */
enum {
    OLUI_AMIGA_OWN_MOUSE = 1u << 0,
    OLUI_AMIGA_OWN_KEYS = 1u << 1
};
int olui_amiga_own_input(olui_amiga *a, const olui_el *el, unsigned what);
/* The owned widget the last message olui_amiga_event returned
 * OLUI_AMIGA_NONE for was for; NULL: none, the message is the program's
 * own as before. */
const olui_el *olui_amiga_owner(const olui_amiga *a);

/* The pictures of img elements (<img image=N width=.. height=..>, in a
 * button too, for a toolbox): fn draws picture N into rp, the buffer the
 * window is drawn in, at x,y and w by h, cut to what olui is drawing. It is
 * called each time the window is drawn where it is; the pen, draw mode and font are
 * fn's to set. NULL: img keeps its place empty. */
struct RastPort;
typedef void (*olui_amiga_image_fn)(void *userdata, struct RastPort *rp, const olui_el *el, int image, int x, int y,
                                    int w, int h);
void olui_amiga_images(olui_amiga *a, olui_amiga_image_fn fn, void *userdata);

/* The fonts text is drawn in (names as diskfont.library takes them, "x.font"):
 * the screen's and topaz.font at first. NULL keeps one. */
void olui_amiga_fonts(olui_amiga *a, const char *font, const char *mono);

/* Library 19: text drawn from outlines, anti-aliased, on a true-colour
 * screen, for what draws into a RastPort of its own (a screen's title bar,
 * icon labels): the same text as olui's windows. utf8 (length bytes) is
 * shaped and drawn from family (a font's name as diskfont.library takes
 * it, "DejaVuSans.font", or a CSS one, "DejaVu Sans", "sans-serif"; NULL:
 * sans-serif) at px pixels, its baseline at y, in argb's colour (0xRRGGBB,
 * its top byte unused), each glyph's grey levels blended onto what is in
 * rp: a window's, a layer's or an offscreen BitMap's (AllocBitMap with the
 * screen's as its friend). Its glyphs are hinted lightly at px (the
 * x-height on a whole pixel, whole-pixel advances) and kept for the next
 * time. OLUI_AMIGA_TEXT_SHADOW puts a one-pixel shadow down and right of
 * it in shadow's colour, OLUI_AMIGA_TEXT_OUTLINE a one-pixel outline, for
 * text over a picture. 1 when it was drawn; 0 when it can't be: rp is on
 * a screen with a palette (8 to 256 pens), gfopenfont.library 1.3 isn't
 * there or family isn't an outline font, so the program draws it with
 * Text() as before. Called from a process (the font index is read there
 * the first time a family is used). */
enum {
    OLUI_AMIGA_TEXT_BOLD = 1u << 0,
    OLUI_AMIGA_TEXT_ITALIC = 1u << 1,
    OLUI_AMIGA_TEXT_UNDERLINE = 1u << 2,
    OLUI_AMIGA_TEXT_SHADOW = 1u << 3,
    OLUI_AMIGA_TEXT_OUTLINE = 1u << 4
};
typedef struct {
    int width;                  /* the pen's advance, whole pixels */
    int ascent;                 /* above the baseline */
    int descent;                /* below it */
} olui_amiga_text_size;
int olui_amiga_text(struct RastPort *rp, int x, int y, const char *utf8, size_t length, const char *family, int px,
                    unsigned long argb, unsigned flags, unsigned long shadow);
/* How big olui_amiga_text draws it (the shadow or outline not counted),
 * for centring and sizing. 1 with *size filled in; 0 where olui_amiga_text
 * would draw nothing (rp NULL leaves the screen out of it). */
int olui_amiga_text_width(struct RastPort *rp, const char *utf8, size_t length, const char *family, int px,
                          unsigned flags, olui_amiga_text_size *size);

/* olui in a strip of the window (a toolbar, a tab strip), x,y from the
 * window's inside, w or h 0 to its edge, so it follows the window's size;
 * y below 0 from the inside's bottom (-20, h 0: a status bar 20 pixels
 * tall that stays at the bottom); all 0: the whole inside again. Set it
 * before the first olui_amiga_layout, which otherwise lays out and draws
 * the whole window; set again later, it lays out again at once. The buffer is the
 * strip's size. Pop-ups (a select's list, a menu, a tooltip) are drawn in
 * the strip too, so a strip a line tall has no room for them: open those
 * in a window of the program's own. olui then
 * takes the mouse only inside it (a press there keeps it until the button
 * is let go), and leaves the window's refresh and the rest of the window to
 * the program: olui_amiga_event draws the strip and returns
 * OLUI_AMIGA_NONE for IDCMP_REFRESHWINDOW and IDCMP_NEWSIZE, so the
 * program does its BeginRefresh/EndRefresh and lays out the rest. */
int olui_amiga_strip(olui_amiga *a, int x, int y, int w, int h);

/* Who has the keys */
enum {
    OLUI_AMIGA_KEYS_ALL = 0,    /* every key goes to olui (the default) */
    OLUI_AMIGA_KEYS_FOCUS       /* only while one of olui's widgets has the focus (olui_focus);
                                   otherwise OLUI_AMIGA_NONE, and the program has it */
};
int olui_amiga_keys(olui_amiga *a, int keys);
/* Both are library 11 calls: 1 when done, 0 from a version 10 library
 * (through the link library), where olui would have the whole window and
 * every key, so a program that needs them checks. With KEYS_FOCUS, Tab
 * can't bring the focus into olui's widgets (the keys are the program's
 * then): a click, or the program's olui_focus, gives it. IntuiTicks in a
 * strip are always OLUI_AMIGA_NONE, so the program has them too; what a
 * tick changes in the strip is drawn before it comes back.
 * A window opened without IDCMP_VANILLAKEY (to have every key as a RAWKEY)
 * works too: the keymap gives olui the characters. */

/* Lays out at the window's inner size (or the strip's), and draws it all. */
int olui_amiga_layout(olui_amiga *a);
/* Draws what changed, if anything did. */
void olui_amiga_draw(olui_amiga *a);
/* One IDCMP message, before it is replied. The Amiga key with x, c and v
 * is cut, copy and paste (OLUI_CUT, OLUI_COPY, OLUI_PASTE: the program
 * puts the text on the clipboard, or gives it from there); Alt with an
 * arrow is PageUp, PageDown, Home and End. */
int olui_amiga_event(olui_amiga *a, struct IntuiMessage *msg);

#endif
