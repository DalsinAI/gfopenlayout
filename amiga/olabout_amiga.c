/* GFOpenLayout About windows on the Amiga (gfopenlayout_about.h): the window, centred on the screen, drawn through
 * oldraw on its RastPort, until OK, Return, Escape or the close gadget. olabout_show blocks, as an EasyRequest does;
 * olabout_open, olabout_handle and olabout_close are the same window for a program whose own loop keeps running.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gfopenlayout_about.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <string.h>

struct olabout_win {
    struct Window *w;
    oldraw *d;
    oldraw_image *mark;
    olabout_layout l;
    int pressed, armed;
    char title[110];
};

olabout_win *olabout_open(struct Screen *scr, const olabout *a)
{
    struct Screen *locked = NULL;
    olabout_win *aw;
    oldraw *d;
    int bw, bh;
    if (!a || !(aw = AllocVec(sizeof *aw, MEMF_PUBLIC | MEMF_CLEAR))) return NULL;
    if (!scr && !(scr = locked = LockPubScreen(NULL))) { FreeVec(aw); return NULL; }
    /* measured on the screen's own RastPort (its font), before the window is opened */
    if (!(d = oldraw_new_rastport(&scr->RastPort, scr, 1, 1))) {
        if (locked) UnlockPubScreen(NULL, locked);
        FreeVec(aw);
        return NULL;
    }
    olabout_layout_for(d, a, &aw->l);
    oldraw_free(d);
    bw = scr->WBorLeft + scr->WBorRight;
    bh = scr->WBorTop + scr->Font->ta_YSize + 1 + scr->WBorBottom;
    snprintf(aw->title, sizeof aw->title, "About %s", a->name ? a->name : "");
    aw->w = OpenWindowTags(NULL,
                           WA_Left, (scr->Width - aw->l.width - bw) / 2, WA_Top, (scr->Height - aw->l.height - bh) / 2,
                           WA_InnerWidth, aw->l.width, WA_InnerHeight, aw->l.height,
                           WA_Title, (ULONG)aw->title, WA_CustomScreen, (ULONG)scr,
                           WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                           WA_GimmeZeroZero, TRUE, WA_SmartRefresh, TRUE, WA_RMBTrap, TRUE,
                           WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_VANILLAKEY |
                                     IDCMP_REFRESHWINDOW,
                           WA_ReportMouse, TRUE, TAG_DONE);
    if (locked) UnlockPubScreen(NULL, locked);
    if (!aw->w || !(aw->d = oldraw_new_rastport(aw->w->RPort, aw->w->WScreen, aw->l.width, aw->l.height))) {
        if (aw->w) CloseWindow(aw->w);
        FreeVec(aw);
        return NULL;
    }
    aw->mark = olabout_mark_image(aw->d, aw->l.mark.h, OLABOUT_ARC_RGB);
    olabout_draw(aw->d, &aw->l, aw->mark, 0);
    oldraw_flush(aw->d);
    return aw;
}

struct Window *olabout_window(const olabout_win *aw)
{
    return aw ? aw->w : NULL;
}

int olabout_handle(olabout_win *aw)
{
    struct IntuiMessage *m;
    int done = 0, redraw = 0;
    if (!aw) return 1;
    while ((m = (struct IntuiMessage *)GetMsg(aw->w->UserPort))) {
        ULONG cls = m->Class;
        UWORD code = m->Code;
        int x = aw->w->GZZMouseX, y = aw->w->GZZMouseY;
        ReplyMsg((struct Message *)m);
        switch (cls) {
        case IDCMP_CLOSEWINDOW: done = 1; break;
        case IDCMP_VANILLAKEY: if (olabout_closes(code)) done = 1; break;
        case IDCMP_MOUSEBUTTONS:
            if (code == SELECTDOWN && olabout_on_button(&aw->l, x, y)) { aw->armed = aw->pressed = 1; redraw = 1; }
            else if (code == SELECTUP && aw->armed) {
                if (olabout_on_button(&aw->l, x, y)) done = 1;
                aw->armed = aw->pressed = 0;
                redraw = 1;
            }
            break;
        case IDCMP_MOUSEMOVE:
            if (aw->armed && aw->pressed != olabout_on_button(&aw->l, x, y)) { aw->pressed = !aw->pressed; redraw = 1; }
            break;
        case IDCMP_REFRESHWINDOW:
            BeginRefresh(aw->w);
            EndRefresh(aw->w, TRUE);
            redraw = 1;
            break;
        }
    }
    if (redraw && !done) {
        olabout_draw(aw->d, &aw->l, aw->mark, aw->pressed);
        oldraw_flush(aw->d);
    }
    return done;
}

void olabout_close(olabout_win *aw)
{
    if (!aw) return;
    if (aw->mark) oldraw_image_free(aw->d, aw->mark);
    oldraw_free(aw->d);
    CloseWindow(aw->w);
    FreeVec(aw);
}

int olabout_show(struct Screen *scr, const olabout *a)
{
    olabout_win *aw = olabout_open(scr, a);
    if (!aw) return 0;
    do WaitPort(aw->w->UserPort);
    while (!olabout_handle(aw));
    olabout_close(aw);
    return 1;
}
