/* GFOpenGTK on AmigaOS 3.x: each GTK window an Intuition window on the
 * default public screen with GFOpenLayout UI's glue (gfopenlayout_ui_amiga.h)
 * drawing it in the GFOpenPrefs Look theme; the main loop Wait()s on every
 * window's port and timer.device for the next timeout. A key goes to GTK's
 * key_press_event handlers first, and to olui only when none took it.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gtkc_internal.h"
#include "gfopenlayout_ui_amiga.h"

#include <exec/types.h>
#include <exec/io.h>
#include <devices/timer.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include <stdlib.h>

typedef struct {
    struct Window *win;
    olui_amiga *glue;   /* not "amiga": the compiler defines that */
} host_win;

static struct MsgPort *timer_port;
static struct timerequest *timer_io;
static int timer_open, timer_sent;

int gtkc_host_init(void)
{
    if (!(timer_port = CreateMsgPort())) return 0;
    if (!(timer_io = (struct timerequest *)CreateIORequest(timer_port, sizeof(struct timerequest)))) return 0;
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)timer_io, 0)) return 0;
    timer_open = 1;
    return 1;
}

static void stop_timer(void)
{
    if (!timer_sent) return;
    if (!CheckIO((struct IORequest *)timer_io)) AbortIO((struct IORequest *)timer_io);
    WaitIO((struct IORequest *)timer_io);
    timer_sent = 0;
}

void gtkc_host_exit(void)
{
    if (timer_open) {
        stop_timer();
        CloseDevice((struct IORequest *)timer_io);
        timer_open = 0;
    }
    if (timer_io) DeleteIORequest((struct IORequest *)timer_io);
    if (timer_port) DeleteMsgPort(timer_port);
    timer_io = NULL;
    timer_port = NULL;
}

guint32 gtkc_host_now(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return (guint32)ds.ds_Days * 86400000u + (guint32)ds.ds_Minute * 60000u + (guint32)ds.ds_Tick * 20u;
}

int gtkc_host_char_width(void)
{
    struct Screen *s = LockPubScreen(NULL);
    int w = 8;
    if (s) {
        if (s->Font) w = s->Font->ta_YSize * 3 / 5 + 1;
        UnlockPubScreen(NULL, s);
    }
    return w;
}

/* Where a window of outer size w by h goes: in the middle, or where Intuition likes */
static void place(gtkc_widget *win, struct Screen *s, int w, int h, int *left, int *top)
{
    *left = *top = -1;
    if (win->top->position != GTK_WIN_POS_NONE && win->top->position != GTK_WIN_POS_MOUSE) {
        *left = (s->Width - w) / 2;
        *top = (s->Height - h) / 2;
    } else if (win->top->position == GTK_WIN_POS_MOUSE) {
        *left = s->MouseX - w / 2;
        *top = s->MouseY - h / 2;
    }
    if (*left >= 0 || *top >= 0) {
        if (*left + w > s->Width) *left = s->Width - w;
        if (*top + h > s->Height) *top = s->Height - h;
        if (*left < 0) *left = 0;
        if (*top < 0) *top = 0;
    }
}

int gtkc_host_open(gtkc_widget *win, int w, int h)
{
    struct Screen *s = LockPubScreen(NULL);
    host_win *hw;
    int bw, bh, left, top;
    if (!s) return 0;
    if (!(hw = calloc(1, sizeof(*hw)))) {
        UnlockPubScreen(NULL, s);
        return 0;
    }
    bw = s->WBorLeft + s->WBorRight + (win->top->resizable ? 14 : 0);
    bh = s->WBorTop + s->Font->ta_YSize + 1 + s->WBorBottom;
    place(win, s, w + bw, h + bh, &left, &top);
    hw->win = OpenWindowTags(NULL,
                             WA_PubScreen, (ULONG)s,
                             WA_Title, (ULONG)(win->text ? win->text : ""),
                             WA_InnerWidth, (ULONG)w, WA_InnerHeight, (ULONG)h,
                             left >= 0 ? WA_Left : TAG_IGNORE, (ULONG)left,
                             top >= 0 ? WA_Top : TAG_IGNORE, (ULONG)top,
                             WA_MinWidth, 80UL, WA_MinHeight, 40UL, WA_MaxWidth, ~0UL, WA_MaxHeight, ~0UL,
                             WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE,
                             WA_SizeGadget, (ULONG)(win->top->resizable ? TRUE : FALSE),
                             WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_ReportMouse, TRUE, WA_AutoAdjust, TRUE,
                             WA_IDCMP, olui_amiga_idcmp(), TAG_DONE);
    UnlockPubScreen(NULL, s);
    if (!hw->win || !(hw->glue = olui_amiga_new(win->top->ui, hw->win))) {
        if (hw->win) CloseWindow(hw->win);
        free(hw);
        return 0;
    }
    olui_amiga_theme(hw->glue, NULL);
    win->top->host = hw;
    win->top->width = hw->win->Width - hw->win->BorderLeft - hw->win->BorderRight;
    win->top->height = hw->win->Height - hw->win->BorderTop - hw->win->BorderBottom;
    return 1;
}

void gtkc_host_size(gtkc_widget *win, int w, int h)
{
    host_win *hw = win->top->host;
    struct Window *wn = hw->win;
    int ow = w + wn->BorderLeft + wn->BorderRight, oh = h + wn->BorderTop + wn->BorderBottom;
    int left = wn->LeftEdge, top = wn->TopEdge;
    struct Screen *s = wn->WScreen;
    if (ow > s->Width) ow = s->Width;
    if (oh > s->Height) oh = s->Height;
    if (win->top->position != GTK_WIN_POS_NONE) place(win, s, ow, oh, &left, &top);
    if (left + ow > s->Width) left = s->Width - ow;
    if (top + oh > s->Height) top = s->Height - oh;
    if (ow != wn->Width || oh != wn->Height || left != wn->LeftEdge || top != wn->TopEdge)
        ChangeWindowBox(wn, left, top, ow, oh);     /* IDCMP_NEWSIZE lays it out again at its new size */
    win->top->width = ow - wn->BorderLeft - wn->BorderRight;
    win->top->height = oh - wn->BorderTop - wn->BorderBottom;
    olui_amiga_layout(hw->glue);
}

void gtkc_host_layout(gtkc_widget *win)
{
    host_win *hw = win->top->host;
    if (hw) olui_amiga_layout(hw->glue);
}

void gtkc_host_draw(gtkc_widget *win)
{
    host_win *hw = win->top->host;
    if (hw) olui_amiga_draw(hw->glue);
}

void gtkc_host_close(gtkc_widget *win)
{
    host_win *hw = win->top->host;
    if (!hw) return;
    olui_amiga_free(hw->glue);
    CloseWindow(hw->win);
    free(hw);
    win->top->host = NULL;
}

void gtkc_host_title(gtkc_widget *win)
{
    host_win *hw = win->top->host;
    if (hw) SetWindowTitles(hw->win, (UBYTE *)(win->text ? win->text : ""), (UBYTE *)~0);
}

/* ---- Input ----------------------------------------------------------------------------- */

static guint state_of(UWORD qual)
{
    guint s = 0;
    if (qual & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) s |= GDK_SHIFT_MASK;
    if (qual & IEQUALIFIER_CAPSLOCK) s |= GDK_LOCK_MASK;
    if (qual & IEQUALIFIER_CONTROL) s |= GDK_CONTROL_MASK;
    if (qual & (IEQUALIFIER_LALT | IEQUALIFIER_RALT)) s |= GDK_MOD1_MASK;
    if (qual & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND)) s |= GDK_MOD4_MASK;
    return s;
}

/* The key an IDCMP message is, as GDK names it (0: none, or not a key going down) */
static guint keyval_of(const struct IntuiMessage *m)
{
    if (m->Class == IDCMP_VANILLAKEY) {
        switch (m->Code) {
        case 13: return GDK_Return;
        case 27: return GDK_Escape;
        case 8: return GDK_BackSpace;
        case 9: return GDK_Tab;
        case 127: return GDK_Delete;
        default: return m->Code >= 0x20 ? m->Code : 0;
        }
    }
    if (m->Class == IDCMP_RAWKEY && !(m->Code & IECODE_UP_PREFIX)) {
        switch (m->Code) {
        case 0x4c: return GDK_Up;
        case 0x4d: return GDK_Down;
        case 0x4e: return GDK_Right;
        case 0x4f: return GDK_Left;
        case 0x5f: return GDK_Help;
        default:
            if (m->Code >= 0x50 && m->Code <= 0x59) return GDK_F1 + (m->Code - 0x50);
            return 0;
        }
    }
    return 0;
}

static int is_input(ULONG class)
{
    return class == IDCMP_MOUSEBUTTONS || class == IDCMP_MOUSEMOVE || class == IDCMP_VANILLAKEY ||
           class == IDCMP_RAWKEY || class == IDCMP_CLOSEWINDOW;
}

/* One window's messages. 1 when there were any. */
static int drain(gtkc_widget *win)
{
    host_win *hw = win->top->host;
    struct IntuiMessage *m;
    int any = 0;
    while (win->top->open && hw == win->top->host && (m = (struct IntuiMessage *)GetMsg(hw->win->UserPort))) {
        guint keyval = keyval_of(m);
        any = 1;
        if (is_input(m->Class) && !gtkc_takes_input(win)) {
            if (m->Class == IDCMP_MOUSEBUTTONS && m->Code == SELECTDOWN) DisplayBeep(hw->win->WScreen);
            ReplyMsg(&m->ExecMessage);
            continue;
        }
        if (keyval && gtkc_key_signal(win, keyval, state_of(m->Qualifier))) {
            ReplyMsg(&m->ExecMessage);
            continue;
        }
        gtkc_enter(win);
        if (olui_amiga_event(hw->glue, m) == OLUI_AMIGA_CLOSE) {
            ReplyMsg(&m->ExecMessage);
            gtkc_leave(win);
            gtkc_close_signal(win);
            continue;
        }
        gtkc_leave(win);
        ReplyMsg(&m->ExecMessage);
    }
    return any;
}

/* The windows to listen to: open, and not inside one of their own handlers
 * (a modal dialog's gtk_main from one): those wait until it returns. */
static GList *listening(ULONG *sigs)
{
    GList *l = NULL, *p;
    *sigs = 0;
    for (p = gtkc_toplevels(); p; p = p->next) {
        gtkc_widget *win = p->data;
        host_win *hw = win->top->host;
        if (!win->top->open || win->top->busy || !hw) continue;
        gtk_widget_ref(&win->pub);
        l = g_list_append(l, win);
        *sigs |= 1UL << hw->win->UserPort->mp_SigBit;
    }
    return l;
}

int gtkc_host_wait(long ms)
{
    ULONG sigs, got, tsig = 1UL << timer_port->mp_SigBit;
    GList *wins = listening(&sigs), *p;
    int any = 0;
    if (ms < 0 && !wins) return -1;
    if (ms != 0) {
        if (ms > 0) {
            timer_io->tr_node.io_Command = TR_ADDREQUEST;
            timer_io->tr_time.tv_secs = (ULONG)(ms / 1000);
            timer_io->tr_time.tv_micro = (ULONG)(ms % 1000) * 1000;
            SendIO((struct IORequest *)timer_io);
            timer_sent = 1;
        }
        got = Wait(sigs | (ms > 0 ? tsig : 0) | SIGBREAKF_CTRL_C);
        stop_timer();
        if (got & SIGBREAKF_CTRL_C) gtk_main_quit();
    }
    for (p = wins; p; p = p->next) {
        gtkc_widget *win = p->data;
        if (!win->top->busy) any |= drain(win);
        gtk_widget_unref(&win->pub);
    }
    g_list_free(wins);
    return any;
}

int gtkc_host_pending(void)
{
    GList *p;
    for (p = gtkc_toplevels(); p; p = p->next) {
        gtkc_widget *win = p->data;
        host_win *hw = win->top->host;
        if (win->top->open && !win->top->busy && hw && !IsListEmpty(&hw->win->UserPort->mp_MsgList)) return 1;
    }
    return 0;
}
