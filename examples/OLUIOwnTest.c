/* OLUIOwnTest: the lab check of gfopenlayout.library 18's window parts.
 *
 * - A calculator's keys in a 4-column grid with same-size both: it prints
 *   GRID PASS when every key is the same size and the columns and rows
 *   line up.
 * - Eight canvases the program draws itself (more than library 17's four),
 *   each owned with OLUI_AMIGA_OWN_MOUSE and OLUI_AMIGA_OWN_KEYS: it
 *   prints each one's middle in screen pixels, then a line for each
 *   message that olui_amiga_owner says was a canvas's (OWN n PRESS,
 *   MOVE, RELEASE, KEY). A field below them shows that a click outside
 *   gives the keys back to olui (FIELD "..." when it changes).
 * - The time from OpenWindow to the first drawn window (OPEN ms) and of
 *   a full layout and draw again (REDRAW ms).
 *
 * q typed on a canvas, the close gadget or Ctrl-C ends it.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>

#include <stdio.h>
#include <string.h>

#include "gfopenlayout_ui.h"
#include "gfopenlayout_ui_amiga.h"

extern struct Library *OpenLayoutBase;     /* the link library's (gfopenlayout_link_base.c) */

static const char version[] = "$VER: OLUIOwnTest 1.0 (10.11.2026) AmiCradle Team";

#define CANVASES 8

static const char markup[] =
    "<grid id=keys columns=4 style='same-size:both;gap:4px'>"
    "<button id=k0>7</button><button id=k1>8</button><button id=k2>9</button><button id=k3>/</button>"
    "<button id=k4>4</button><button id=k5>5</button><button id=k6>6</button><button id=k7>*</button>"
    "<button id=k8>1</button><button id=k9>2</button><button id=k10>3</button><button id=k11>-</button>"
    "<button id=k12>0</button><button id=k13>.</button><button id=k14>Clear</button><button id=k15>+</button>"
    "</grid>"
    "<row style='gap:4px'>"
    "<div id=c0 style='flex:1;height:40px;background:#c0c0c0'></div>"
    "<div id=c1 style='flex:1;height:40px;background:#c0c0c0'></div>"
    "<div id=c2 style='flex:1;height:40px;background:#c0c0c0'></div>"
    "<div id=c3 style='flex:1;height:40px;background:#c0c0c0'></div>"
    "<div id=c4 style='flex:1;height:40px;background:#c0c0c0'></div>"
    "<div id=c5 style='flex:1;height:40px;background:#c0c0c0'></div>"
    "<div id=c6 style='flex:1;height:40px;background:#c0c0c0'></div>"
    "<div id=c7 style='flex:1;height:40px;background:#c0c0c0'></div>"
    "</row>"
    "<input id=field placeholder='olui has the keys here'>";

static struct Window *win;
static const olui_el *canvas[CANVASES];
static int drawn[CANVASES];

static void draw_canvas(void *userdata, const olui_el *el, int x, int y, int w, int h)
{
    int i = (int)(long)userdata;
    (void)el;
    SetAPen(win->RPort, (UBYTE)(1 + (i & 1)));
    RectFill(win->RPort, x, y, x + w - 1, y + h - 1);
    ++drawn[i];
}

static int which(const olui_el *el)
{
    int i;
    for (i = 0; i < CANVASES; ++i)
        if (canvas[i] == el) return i;
    return -1;
}

static ULONG ms_now(void)
{
    struct timeval tv;
    GetSysTime(&tv);
    return tv.tv_secs * 1000UL + tv.tv_micro / 1000UL;
}

struct Device *TimerBase;

int main(void)
{
    struct timerequest tr;
    olui *ui = NULL;
    olui_amiga *glue = NULL;
    char err[128], last[64] = "";
    int i, done = 0, rc = RETURN_FAIL;
    ULONG t0, t1;
    olui_rect k[16], b;
    (void)version;

    {
        /* version 18 itself: a ROM's older one answers a version 10 open first */
        struct Library *l18 = OpenLibrary((CONST_STRPTR)"gfopenlayout.library", 18);
        if (!l18) {
            printf("OLUIOwnTest: needs gfopenlayout.library 18 in LIBS:\n");
            return RETURN_FAIL;
        }
        CloseLibrary(OpenLayoutBase);
        OpenLayoutBase = l18;
        printf("LIBRARY %d.%d\n", l18->lib_Version, l18->lib_Revision);
    }
    memset(&tr, 0, sizeof(tr));
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_MICROHZ, &tr.tr_node, 0)) return RETURN_FAIL;
    TimerBase = tr.tr_node.io_Device;

    t0 = ms_now();
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"OLUIOwnTest", WA_Left, 0, WA_Top, 20, WA_Width, 520, WA_Height, 300,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                         WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_ReportMouse, TRUE, WA_RMBTrap, TRUE,
                         WA_IDCMP, olui_amiga_idcmp() | IDCMP_MOUSEMOVE | IDCMP_RAWKEY, TAG_DONE);
    if (!win) { printf("OLUIOwnTest: no window\n"); goto out; }
    ui = olui_new();
    glue = ui ? olui_amiga_new(ui, win) : NULL;
    if (!glue) { printf("OLUIOwnTest: out of memory\n"); goto out; }
    olui_amiga_theme(glue, NULL);
    if (!olui_load(ui, markup, NULL, err, sizeof(err))) { printf("OLUIOwnTest: %s\n", err); goto out; }
    for (i = 0; i < CANVASES; ++i) {
        char id[4];
        sprintf(id, "c%d", i);
        canvas[i] = olui_find(ui, id);
        if (!canvas[i] || !olui_amiga_own(glue, canvas[i], draw_canvas, (void *)(long)i)) {
            printf("OWN FAIL: canvas %d not taken\n", i);
            goto out;
        }
        if (!olui_amiga_own_input(glue, canvas[i], OLUI_AMIGA_OWN_MOUSE | OLUI_AMIGA_OWN_KEYS)) {
            printf("OWN FAIL: canvas %d input not taken (library 17 or older?)\n", i);
            goto out;
        }
    }
    if (!olui_amiga_layout(glue)) { printf("OLUIOwnTest: no layout\n"); goto out; }
    t1 = ms_now();
    printf("OPEN %lu ms\n", (unsigned long)(t1 - t0));

    for (i = 0; i < 16; ++i) {
        char id[4];
        sprintf(id, "k%d", i);
        if (!olui_box(ui, olui_find(ui, id), &k[i])) break;
    }
    {
        int ok = i == 16;
        for (i = 0; ok && i < 16; ++i) {
            ok = k[i].w == k[0].w && k[i].h == k[0].h && k[i].x == k[i % 4].x && k[i].y == k[(i / 4) * 4].y;
        }
        printf("GRID %s key %ldx%ld\n", ok ? "PASS" : "FAIL", (long)k[0].w, (long)k[0].h);
    }
    for (i = 0; i < CANVASES; ++i) {
        olui_box(ui, canvas[i], &b);
        printf("CANVAS %d at %ld,%ld drawn %d\n", i, (long)(win->LeftEdge + win->BorderLeft + b.x + b.w / 2),
               (long)(win->TopEdge + win->BorderTop + b.y + b.h / 2), drawn[i]);
    }
    olui_box(ui, olui_find(ui, "field"), &b);
    printf("FIELD at %ld,%ld\n", (long)(win->LeftEdge + win->BorderLeft + b.x + 8),
           (long)(win->TopEdge + win->BorderTop + b.y + b.h / 2));
    t0 = ms_now();
    for (i = 0; i < 10; ++i) olui_amiga_layout(glue);
    printf("REDRAW %lu ms for 10\n", (unsigned long)(ms_now() - t0));
    fflush(stdout);

    while (!done) {
        struct IntuiMessage *m;
        ULONG sig = Wait((1UL << win->UserPort->mp_SigBit) | SIGBREAKF_CTRL_C);
        if (sig & SIGBREAKF_CTRL_C) break;
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            int r = olui_amiga_event(glue, m), n = which(olui_amiga_owner(glue));
            ReplyMsg(&m->ExecMessage);
            if (r == OLUI_AMIGA_CLOSE) done = 1;
            if (n < 0) continue;
            if (cls == IDCMP_MOUSEBUTTONS)
                printf("OWN %d %s %u\n", n, (code & IECODE_UP_PREFIX) ? "RELEASE" : "PRESS", (unsigned)code);
            else if (cls == IDCMP_MOUSEMOVE)
                printf("OWN %d MOVE\n", n);
            else if (cls == IDCMP_VANILLAKEY) {
                printf("OWN %d KEY %u\n", n, (unsigned)code);
                if (code == 'q') done = 1;
            } else if (cls == IDCMP_RAWKEY && !(code & IECODE_UP_PREFIX))
                printf("OWN %d RAWKEY %u\n", n, (unsigned)code);
            fflush(stdout);
        }
        olui_amiga_draw(glue);
        {
            const char *v = olui_get_text(olui_find(ui, "field"));
            if (v && strcmp(v, last)) {
                strncpy(last, v, sizeof(last) - 1);
                printf("FIELD \"%s\"\n", v);
                fflush(stdout);
            }
        }
    }
    printf("END\n");
    rc = RETURN_OK;
out:
    if (glue) olui_amiga_free(glue);
    if (ui) olui_free(ui);
    if (win) CloseWindow(win);
    CloseDevice(&tr.tr_node);
    return rc;
}
