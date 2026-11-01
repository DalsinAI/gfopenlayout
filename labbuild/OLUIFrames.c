/* OLUIFrames (lab only): on the Amiga, each hover, press or focus drawn by
 * olui_amiga_draw against the window drawn again in full after a new
 * layout, the window's pixels compared; then hover changes timed.
 *   OLUIFrames            compare and time
 *   OLUIFrames TIME       time only
 * MIT, Copyright (c) 2026 AmigaChrome Team. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <inline/macros.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openlayout_ui.h"
#include "openlayout_ui_amiga.h"

static struct Library *CyberGfxBase;
#define CGX_READ(rp, x, y) LP3(0x6c, ULONG, ReadRGBPixel, struct RastPort *, (rp), a1, UWORD, (x), d0, UWORD, (y), d1, \
    , CyberGfxBase)
#define CGX_MAPATTR(bm, a) LP2(0x60, ULONG, GetCyberMapAttr, struct BitMap *, (bm), a0, ULONG, (a), d0, , CyberGfxBase)
#define CYBRMATTR_ISCYBERGFX 0x80000008UL

static const char markup[] =
    "<style>#title{font-size:16px}</style>\n"
    "<h1 id=title>Mail folders</h1>\n"
    "<row>\n"
    "  <label style='width:60px'>Search</label>\n"
    "  <input id=search placeholder='Type to search' style='flex:1'>\n"
    "  <button id=go>Go</button>\n"
    "</row>\n"
    "<row style='flex:1'>\n"
    "  <tree id=folders style='width:180px;height:auto'>\n"
    "    <item id=inbox value=INBOX open>Inbox\n"
    "      <item id=work>Work</item>\n"
    "      <item id=family>Family</item>\n"
    "    </item>\n"
    "    <item id=sent>Sent</item>\n"
    "    <item id=archive>Archive<item id=y2025>2025</item><item id=y2026>2026</item></item>\n"
    "  </tree>\n"
    "  <tabs id=tabs style='flex:1'>\n"
    "    <tab title=Message id=t1>\n"
    "      <checkbox id=html checked>Show HTML</checkbox>\n"
    "      <radio name=sort id=bydate value=date checked>By date</radio>\n"
    "      <radio name=sort id=byname value=name>By sender</radio>\n"
    "      <p>Some <b>bold</b> text &amp; more.</p>\n"
    "    </tab>\n"
    "    <tab title=Settings id=t2><label>Settings page</label></tab>\n"
    "  </tabs>\n"
    "</row>\n"
    "<progress id=bar max=10></progress>\n"
    "<row style='justify-content:end'><button id=ok>OK</button><button id=cancel disabled>Cancel</button></row>\n";

static const char css[] =
    "item{padding:2px 4px}"
    "button:hover{background:linear-gradient(#fff4dd,#e8c890);color:#803000}"
    "item:hover{background:#dde8ff}"
    "checkbox:hover{color:#0000aa}"
    "tabbutton:hover{border-color:#cc6600}"
    "input:focus{background:#ffffee}"
    "#go:hover{padding:3px 20px}";

enum { MOVE, DOWN, UP, TAB, WHEEL };
static const struct { int act; const char *id; } steps[] = {
    { MOVE, "ok" }, { MOVE, "go" }, { MOVE, NULL }, { MOVE, "ok" }, { DOWN, "ok" }, { MOVE, NULL },
    { MOVE, "ok" }, { UP, "ok" }, { TAB, NULL }, { TAB, NULL }, { TAB, NULL }, { MOVE, NULL },
    { MOVE, "html" }, { MOVE, NULL }, { MOVE, "work" }, { MOVE, "family" }, { WHEEL, "folders" },
    { MOVE, "item3" }, { MOVE, "item4" }, { MOVE, NULL }, { MOVE, "strip" }, { MOVE, NULL }
};

#define FOLDERS 40

static struct Window *win;
static olui *ui;
static olui_amiga *am;
static olui_el *added[FOLDERS];
static int iw, ih, rgb;

static void grab(ULONG *px)
{
    struct RastPort *rp = win->RPort;
    int x, y;
    for (y = 0; y < ih; ++y)
        for (x = 0; x < iw; ++x)
            px[y * iw + x] = rgb ? CGX_READ(rp, win->BorderLeft + x, win->BorderTop + y)
                                 : (ULONG)ReadPixel(rp, win->BorderLeft + x, win->BorderTop + y);
}

static void aim(const char *id, int *x, int *y)
{
    olui_rect at;
    *x = 3;
    *y = ih - 2;
    if (!id) return;
    if (!strcmp(id, "strip")) {
        if (olui_box(ui, olui_find(ui, "tabs"), &at)) { *x = at.x + 10; *y = at.y + 4; }
        return;
    }
    if (!strncmp(id, "item", 4) ? olui_box(ui, added[atoi(id + 4)], &at) : olui_box(ui, olui_find(ui, id), &at)) {
        *x = at.x + at.w / 2;
        *y = at.y + at.h / 2;
    }
}

static int make(const char *sheet)
{
    char err[128];
    int i;
    ui = olui_new();
    am = ui ? olui_amiga_new(ui, win) : NULL;
    if (!am || !olui_load(ui, markup, sheet, err, sizeof(err))) return 0;
    for (i = 0; i < FOLDERS; ++i) {
        char text[24];
        sprintf(text, "Folder %d", i);
        if (!(added[i] = olui_list_add(ui, olui_find(ui, "folders"), NULL, text, NULL))) return 0;
    }
    return olui_amiga_layout(am);
}

static void unmake(void)
{
    olui_amiga_free(am);
    olui_free(ui);
    am = NULL;
    ui = NULL;
}

static long ticks(void)
{
    struct DateStamp d;
    DateStamp(&d);
    return (d.ds_Minute * 60L * 50L) + d.ds_Tick;
}

/* Hover changes on OK, each drawn: per change in microseconds */
static long time_hover(void)
{
    olui_rect ok;
    long t0, t, n = 0;
    if (!olui_box(ui, olui_find(ui, "ok"), &ok)) return -1;
    t0 = ticks();
    do {
        olui_mouse(ui, ok.x + ok.w / 2, ok.y + ok.h / 2, OLUI_MOUSE_MOVE);
        olui_amiga_draw(am);
        olui_mouse(ui, 3, ih - 2, OLUI_MOUSE_MOVE);
        olui_amiga_draw(am);
        n += 2;
    } while ((t = ticks() - t0) < 150);
    return t * 20000L / n;
}

int main(int argc, char **argv)
{
    ULONG *a = NULL, *b = NULL;
    int rc = RETURN_FAIL, compare = !(argc > 1 && !strcmp(argv[1], "TIME"));
    size_t k;
    int bad = 0, partial = 0;
    CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 40);
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"OLUIFrames", WA_Left, 40, WA_Top, 40, WA_InnerWidth, 520,
                         WA_InnerHeight, 340, WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_Activate, TRUE,
                         WA_SimpleRefresh, TRUE, WA_IDCMP, 0, TAG_DONE);
    if (!win) {
        PutStr((STRPTR)"OLUIFrames: no window\n");
        goto out;
    }
    iw = win->Width - win->BorderLeft - win->BorderRight;
    ih = win->Height - win->BorderTop - win->BorderBottom;
    rgb = CyberGfxBase && CGX_MAPATTR(win->RPort->BitMap, CYBRMATTR_ISCYBERGFX);
    printf("OLUIFrames: window %dx%d, %s\n", iw, ih, rgb ? "true colour (ReadRGBPixel)" : "pens (ReadPixel)");
    if (compare) {
        a = (ULONG *)malloc((size_t)iw * ih * sizeof(ULONG));
        b = (ULONG *)malloc((size_t)iw * ih * sizeof(ULONG));
        if (!a || !b || !make(css)) {
            PutStr((STRPTR)"OLUIFrames: out of memory\n");
            goto out;
        }
        for (k = 0; k < sizeof(steps) / sizeof(steps[0]); ++k) {
            olui_rect r;
            long diff = 0, p;
            int x, y, i, part;
            aim(steps[k].id, &x, &y);
            switch (steps[k].act) {
            case MOVE: olui_mouse(ui, x, y, OLUI_MOUSE_MOVE); break;
            case DOWN: olui_mouse(ui, x, y, OLUI_MOUSE_DOWN); break;
            case UP: olui_mouse(ui, x, y, OLUI_MOUSE_UP); break;
            case TAB: olui_key(ui, OLUI_KEY_TAB, 0); break;
            case WHEEL: for (i = 0; i < 3; ++i) olui_mouse(ui, x, y, OLUI_WHEEL_DOWN); break;
            default: break;
            }
            part = olui_damaged(ui, &r);
            if (!part) r.x = r.y = r.w = r.h = 0;
            olui_amiga_draw(am);
            grab(a);
            /* every element styled and laid out again, all of it drawn */
            olui_add_css(ui, "");
            olui_amiga_layout(am);
            grab(b);
            for (p = 0; p < (long)iw * ih; ++p) diff += a[p] != b[p];
            if (part && (long)r.w * r.h < (long)iw * ih) ++partial;
            printf("step %2d: damaged %ld,%ld %ldx%ld; %ld pixels differ from a full draw after a new layout\n",
                   (int)k, (long)r.x, (long)r.y, (long)r.w, (long)r.h, diff);
            if (diff) ++bad;
        }
        printf("OLUIFrames: %d steps, %d drawn in part, %d differ\n", (int)k, partial, bad);
        unmake();
    }
    if (!make(css)) goto out;
    printf("hover change on OK, with a button:hover rule: %ld us each\n", time_hover());
    unmake();
    if (!make(NULL)) goto out;
    printf("hover change on OK, built-in look (no :hover rule): %ld us each\n", time_hover());
    unmake();
    rc = bad ? RETURN_ERROR : RETURN_OK;
    printf(bad ? "OLUIFrames FAIL\n" : "OLUIFrames PASS\n");
out:
    unmake();
    free(a);
    free(b);
    if (win) CloseWindow(win);
    if (CyberGfxBase) CloseLibrary(CyberGfxBase);
    return rc;
}
