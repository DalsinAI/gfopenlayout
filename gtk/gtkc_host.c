/* GFOpenGTK on the host with no screen: each window an olui laid out at
 * its size and drawn to nothing, for the tests (tests/test_gtk.c), which
 * give it input through gtkc_click, gtkc_key and gtkc_type. Time is the
 * loop's own: it moves on only when the loop waits, by as long as it
 * waits, so timeouts run at once and the same each time.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gtkc_internal.h"

static guint32 now;
static int windows;

int gtkc_host_init(void) { return 1; }
void gtkc_host_exit(void) {}

int gtkc_host_open(gtkc_widget *win, int w, int h)
{
    win->top->width = w;
    win->top->height = h;
    ++windows;
    return 1;
}

static void nothing(void *userdata, const olui_draw_op *op)
{
    (void)userdata;
    (void)op;
}

void gtkc_host_layout(gtkc_widget *win)
{
    olui_layout(win->top->ui, win->top->width, win->top->height);
    olui_draw(win->top->ui, NULL, nothing, NULL);
}

void gtkc_host_size(gtkc_widget *win, int w, int h)
{
    win->top->width = w;
    win->top->height = h;
    gtkc_host_layout(win);
}

void gtkc_host_draw(gtkc_widget *win)
{
    olui_rect r;
    if (olui_damaged(win->top->ui, &r)) olui_draw(win->top->ui, &r, nothing, NULL);
}

void gtkc_host_close(gtkc_widget *win)
{
    (void)win;
    --windows;
}

void gtkc_host_title(gtkc_widget *win) { (void)win; }

int gtkc_host_wait(long ms)
{
    if (ms < 0) return -1;      /* no input ever comes here: waiting for ever would be for ever */
    now += (guint32)ms;
    return 0;
}

int gtkc_host_pending(void) { return 0; }
guint32 gtkc_host_now(void) { return now; }
int gtkc_host_char_width(void) { return 8; }
