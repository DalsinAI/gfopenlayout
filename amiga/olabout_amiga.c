/* GFOpenLayout About windows on the Amiga (gfopenlayout_about.h): the window, centred on the screen, drawn through
 * oldraw on its RastPort, until OK, Return, Escape or the close gadget. Blocking, as an EasyRequest is. MIT,
 * Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout_about.h"

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <string.h>

int olabout_show(struct Screen *scr, const olabout *a)
{
    struct Screen *locked = NULL;
    struct Window *w;
    oldraw *d;
    oldraw_image *mark;
    olabout_layout l;
    char title[110];
    int done = 0, pressed = 0, armed = 0, bw, bh;
    if (!a) return 0;
    if (!scr && !(scr = locked = LockPubScreen(NULL))) return 0;
    /* measured on the screen's own RastPort (its font), before the window is opened */
    if (!(d = oldraw_new_rastport(&scr->RastPort, scr, 1, 1))) {
        if (locked) UnlockPubScreen(NULL, locked);
        return 0;
    }
    olabout_layout_for(d, a, &l);
    oldraw_free(d);
    bw = scr->WBorLeft + scr->WBorRight;
    bh = scr->WBorTop + scr->Font->ta_YSize + 1 + scr->WBorBottom;
    snprintf(title, sizeof title, "About %s", a->name ? a->name : "");
    w = OpenWindowTags(NULL,
                       WA_Left, (scr->Width - l.width - bw) / 2, WA_Top, (scr->Height - l.height - bh) / 2,
                       WA_InnerWidth, l.width, WA_InnerHeight, l.height,
                       WA_Title, (ULONG)title, WA_CustomScreen, (ULONG)scr,
                       WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                       WA_GimmeZeroZero, TRUE, WA_SmartRefresh, TRUE, WA_RMBTrap, TRUE,
                       WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_VANILLAKEY |
                                 IDCMP_REFRESHWINDOW,
                       WA_ReportMouse, TRUE, TAG_DONE);
    if (locked) UnlockPubScreen(NULL, locked);
    if (!w) return 0;
    if (!(d = oldraw_new_rastport(w->RPort, w->WScreen, l.width, l.height))) {
        CloseWindow(w);
        return 0;
    }
    mark = olabout_mark_image(d, l.mark.h, OLABOUT_ARC_RGB);
    olabout_draw(d, &l, mark, 0);
    oldraw_flush(d);
    while (!done) {
        struct IntuiMessage *m;
        int redraw = 0;
        WaitPort(w->UserPort);
        while ((m = (struct IntuiMessage *)GetMsg(w->UserPort))) {
            ULONG cls = m->Class;
            UWORD code = m->Code;
            int x = w->GZZMouseX, y = w->GZZMouseY;
            ReplyMsg((struct Message *)m);
            switch (cls) {
            case IDCMP_CLOSEWINDOW: done = 1; break;
            case IDCMP_VANILLAKEY: if (olabout_closes(code)) done = 1; break;
            case IDCMP_MOUSEBUTTONS:
                if (code == SELECTDOWN && olabout_on_button(&l, x, y)) { armed = pressed = 1; redraw = 1; }
                else if (code == SELECTUP && armed) {
                    if (olabout_on_button(&l, x, y)) done = 1;
                    armed = pressed = 0;
                    redraw = 1;
                }
                break;
            case IDCMP_MOUSEMOVE:
                if (armed && pressed != olabout_on_button(&l, x, y)) { pressed = !pressed; redraw = 1; }
                break;
            case IDCMP_REFRESHWINDOW:
                BeginRefresh(w);
                EndRefresh(w, TRUE);
                redraw = 1;
                break;
            }
        }
        if (redraw && !done) {
            olabout_draw(d, &l, mark, pressed);
            oldraw_flush(d);
        }
    }
    if (mark) oldraw_image_free(d, mark);
    oldraw_free(d);
    CloseWindow(w);
    return 1;
}
