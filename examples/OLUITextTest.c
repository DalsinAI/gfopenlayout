/* OLUITextTest: the lab check of gfopenlayout.library 19's outline text.
 *
 * - The same window twice, side by side: on the left with every text through
 *   diskfont.library (ENV:GFOpenLayout/Text "bitmap", set only while it
 *   opens), on the right drawn from outlines (the default). For each:
 *   MODE name OPEN ms and REDRAW ms, a full layout and draw, the mean of 20.
 * - A third window drawn with olui_amiga_text (OLUI_AmigaText) into its own RastPort and into
 *   an offscreen friend BitMap over a gradient (as a screen's title bar
 *   does), plain, with OLUI_AMIGA_TEXT_SHADOW and with _OUTLINE, at 11 to
 *   24 pixels; TEXT CALL us and TEXT() us, one string drawn 200 times each
 *   way; WIDTH n ASCENT n DESCENT n from olui_amiga_text_width.
 * - TEXT PASS when the outline text was drawn (a true-colour screen, or one
 *   of 16 to 256 pens), TEXT FEWPENS when the screen has fewer than 16 pens
 *   and olui_amiga_text rightly said 0. Run it on the Workbench and on an
 *   AGA screen (as the public screen) for both paths.
 *
 * It stays open for the screenshots until a close gadget or Ctrl-C, or the
 * seconds given (OLUITextTest 30).
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <inline/macros.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfopenlayout_ui.h"
#include "gfopenlayout_ui_amiga.h"

extern struct Library *OpenLayoutBase;     /* the link library's (gfopenlayout_link_base.c) */

static const char version[] = "$VER: OLUITextTest 1.0 (12.11.2026) AmiCradle Team";

static struct Library *CyberGfxBase;
#define CGX_FILL(rp, x, y, w, h, argb) LP6(0x96, ULONG, FillPixelArray, struct RastPort *, (rp), a1, UWORD, (x), d0, \
    UWORD, (y), d1, UWORD, (w), d2, UWORD, (h), d3, ULONG, (argb), d4, , CyberGfxBase)

static const char markup[] =
    "<label>Workbench screen: 1920 x 1080, 32-bit</label>"
    "<label style='font-weight:bold'>Bold: Disk, Ram Disk, Trashcan</label>"
    "<row style='gap:6px'><button>Save</button><button>Use</button><button>Cancel</button></row>"
    "<checkbox checked>Show all files</checkbox>"
    "<input value='minimum stems: m n a s e o'>"
    "<list id=l style='height:90px'></list>"
    "<label style='font-size:20px'>Large 20px: Hamburgefonstiv</label>"
    "<label style='font-size:11px'>Small 11px: the quick brown fox jumps over the lazy dog</label>";

static ULONG us_now(void)
{
    struct timeval tv;
    GetSysTime(&tv);
    return tv.tv_secs * 1000000UL + tv.tv_micro;
}

struct Device *TimerBase;

typedef struct {
    struct Window *win;
    olui *ui;
    olui_amiga *glue;
} pane;

static int open_pane(pane *p, const char *title, int left, int bitmap)
{
    char err[128];
    ULONG t0, t1;
    int i;
    if (bitmap) SetVar((STRPTR)"GFOpenLayout/Text", (STRPTR)"bitmap", -1, GVF_GLOBAL_ONLY);
    t0 = us_now();
    p->win = OpenWindowTags(NULL, WA_Title, (ULONG)title, WA_Left, left, WA_Top, 20, WA_Width, 420, WA_Height, 360,
                            WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SimpleRefresh, TRUE,
                            WA_IDCMP, olui_amiga_idcmp(), TAG_DONE);
    p->ui = p->win ? olui_new() : NULL;
    p->glue = p->ui ? olui_amiga_new(p->ui, p->win) : NULL;
    if (bitmap) DeleteVar((STRPTR)"GFOpenLayout/Text", GVF_GLOBAL_ONLY);
    if (!p->glue) { printf("OLUITextTest: no window\n"); return 0; }
    olui_amiga_theme(p->glue, NULL);
    if (!olui_load(p->ui, markup, NULL, err, sizeof(err))) { printf("OLUITextTest: %s\n", err); return 0; }
    {
        static const char *const rows[] = { "Devs", "Fonts", "Libs", "Prefs", "Storage", "System", "Utilities" };
        olui_el *l = olui_find(p->ui, "l");
        for (i = 0; i < 7; ++i) olui_list_add(p->ui, l, NULL, rows[i], NULL);
    }
    if (!olui_amiga_layout(p->glue)) { printf("OLUITextTest: no layout\n"); return 0; }
    t1 = us_now();
    printf("MODE %s OPEN %lu ms\n", bitmap ? "bitmap" : "outline", (unsigned long)((t1 - t0) / 1000));
    return 1;
}

static void time_pane(pane *p, int bitmap)
{
    ULONG t0 = us_now(), t;
    int i;
    for (i = 0; i < 20; ++i) olui_amiga_layout(p->glue);
    t = (us_now() - t0) / 20;
    printf("MODE %s REDRAW %lu.%02lu ms\n", bitmap ? "bitmap" : "outline", (unsigned long)(t / 1000),
           (unsigned long)(t % 1000 / 10));
}

static void close_pane(pane *p)
{
    if (p->glue) olui_amiga_free(p->glue);
    if (p->ui) olui_free(p->ui);
    if (p->win) CloseWindow(p->win);
}

static void gradient(struct RastPort *rp, int w, int h)
{
    int y;
    if (!CyberGfxBase || GetBitMapAttr(rp->BitMap, BMA_DEPTH) <= 8) {
        /* a screen with a palette: bands of its pens */
        int pens = 1 << GetBitMapAttr(rp->BitMap, BMA_DEPTH);
        for (y = 0; y < h; y += 8) {
            SetAPen(rp, (UBYTE)((y / 8) % (pens > 8 ? 8 : pens)));
            RectFill(rp, 0, y, w - 1, y + 7 < h ? y + 7 : h - 1);
        }
        return;
    }
    for (y = 0; y < h; ++y) {
        ULONG t = (ULONG)y * 255 / (h > 1 ? h - 1 : 1);
        ULONG c = 0xff000000UL | ((0x30 + t * 0x50 / 255) << 16) | ((0x60 + t * 0x60 / 255) << 8) | (0xa0 + t * 0x50 / 255);
        CGX_FILL(rp, 0, (UWORD)y, (UWORD)w, 1, c);
    }
}

int main(int argc, char **argv)
{
    struct timerequest tr;
    pane bm = { 0 }, aa = { 0 };
    struct Window *tw = NULL;
    int rc = RETURN_FAIL, secs = argc > 1 ? atoi(argv[1]) : 600, drawn = 0;
    (void)version;

    {
        struct Library *l = OpenLibrary((CONST_STRPTR)"gfopenlayout.library", 19);
        if (!l) {
            printf("OLUITextTest: needs gfopenlayout.library 19 in LIBS:\n");
            return RETURN_FAIL;
        }
        CloseLibrary(OpenLayoutBase);
        OpenLayoutBase = l;
        printf("LIBRARY %d.%d\n", l->lib_Version, l->lib_Revision);
    }
    memset(&tr, 0, sizeof(tr));
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ, &tr.tr_node, 0)) return RETURN_FAIL;
    TimerBase = tr.tr_node.io_Device;
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 40);

    if (!open_pane(&bm, "Bitmap text", 0, 1) || !open_pane(&aa, "Outline text", 430, 0)) goto out;
    /* the outline faces and glyphs are cached by now: both measured warm */
    time_pane(&bm, 1);
    time_pane(&aa, 0);
    time_pane(&bm, 1);
    time_pane(&aa, 0);

    tw = OpenWindowTags(NULL, WA_Title, (ULONG)"OLUI_AmigaText", WA_Left, 860, WA_Top, 20, WA_Width, 460,
                        WA_Height, 360, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                        WA_SimpleRefresh, TRUE, WA_IDCMP, IDCMP_CLOSEWINDOW, TAG_DONE);
    if (tw) {
        struct RastPort *rp = tw->RPort;
        static const char s[] = "Ram Disk  Workbench  12:34  Hamburgefonstiv";
        static const int sizes[] = { 11, 13, 16, 20, 24 };
        int x = tw->BorderLeft + 8, y = tw->BorderTop + 4, i;
        olui_amiga_text_size size;
        for (i = 0; i < 5; ++i) {
            y += sizes[i] + 4;
            drawn += olui_amiga_text(rp, x, y, s, strlen(s), "DejaVuSans.font", sizes[i], 0x000000, 0, 0);
        }
        if (olui_amiga_text_width(rp, s, strlen(s), "DejaVuSans.font", 13, 0, &size))
            printf("WIDTH %d ASCENT %d DESCENT %d\n", size.width, size.ascent, size.descent);
        /* a friend BitMap over a gradient, as the bar draws its row: plain, shadow, outline */
        {
            int w = 440, h = 120;
            struct BitMap *fb = AllocBitMap((ULONG)w, (ULONG)h, GetBitMapAttr(rp->BitMap, BMA_DEPTH), BMF_MINPLANES,
                                            rp->BitMap);
            if (fb) {
                struct RastPort frp;
                InitRastPort(&frp);
                frp.BitMap = fb;
                gradient(&frp, w, h);
                drawn += olui_amiga_text(&frp, 8, 30, "Plain label over a picture", 26, NULL, 16, 0xffffff, 0, 0);
                drawn += olui_amiga_text(&frp, 8, 64, "Shadow: Ram Disk", 16, NULL, 16, 0xffffff,
                                        OLUI_AMIGA_TEXT_SHADOW, 0x000000);
                drawn += olui_amiga_text(&frp, 8, 98, "Outline: Trashcan", 17, NULL, 16, 0xffffff,
                                        OLUI_AMIGA_TEXT_OUTLINE | OLUI_AMIGA_TEXT_BOLD, 0x202020);
                BltBitMapRastPort(fb, 0, 0, rp, x - 4, y + 12, w, h, 0xc0);
                WaitBlit();
                FreeBitMap(fb);
            }
        }
        /* the time of one call, cached, against Text() of the same string in the screen's font */
        {
            ULONG t0 = us_now(), ta, tt;
            int n;
            for (n = 0; n < 200; ++n)
                olui_amiga_text(rp, x, tw->BorderTop + 300, s, strlen(s), "DejaVuSans.font", 13, 0x000000, 0, 0);
            ta = (us_now() - t0) / 200;
            SetAPen(rp, 1);
            SetDrMd(rp, JAM1);
            t0 = us_now();
            for (n = 0; n < 200; ++n) {
                Move(rp, x, tw->BorderTop + 330);
                Text(rp, (STRPTR)s, (UWORD)strlen(s));
            }
            tt = (us_now() - t0) / 200;
            printf("TEXT CALL %lu us TEXT() %lu us\n", (unsigned long)ta, (unsigned long)tt);
        }
        if (drawn == 8) printf("TEXT PASS\n");
        else if (drawn == 0 && GetBitMapAttr(rp->BitMap, BMA_DEPTH) < 4) printf("TEXT FEWPENS\n");
        else printf("TEXT FAIL drawn %d of 8\n", drawn);
    }

    rc = RETURN_OK;
    {
        ULONG until = us_now() / 1000000UL + (ULONG)secs, mask = SIGBREAKF_CTRL_C;
        struct Window *ws[3];
        int k, quit = 0;
        ws[0] = bm.win;
        ws[1] = aa.win;
        ws[2] = tw;
        for (k = 0; k < 3; ++k)
            if (ws[k]) mask |= 1UL << ws[k]->UserPort->mp_SigBit;
        while (!quit && us_now() / 1000000UL < until) {
            ULONG sig;
            Delay(10);
            sig = SetSignal(0, mask);
            if (sig & SIGBREAKF_CTRL_C) break;
            for (k = 0; k < 3; ++k) {
                struct IntuiMessage *m;
                if (!ws[k]) continue;
                while ((m = (struct IntuiMessage *)GetMsg(ws[k]->UserPort)) != NULL) {
                    if (m->Class == IDCMP_CLOSEWINDOW) quit = 1;
                    if (k < 2) olui_amiga_event(k ? aa.glue : bm.glue, m);
                    ReplyMsg(&m->ExecMessage);
                }
            }
        }
    }
out:
    if (tw) CloseWindow(tw);
    close_pane(&aa);
    close_pane(&bm);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    CloseDevice(&tr.tr_node);
    return rc;
}
