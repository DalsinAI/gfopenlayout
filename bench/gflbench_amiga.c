/* GFLayoutBench on the Amiga: gflbench.c through oldraw_new_rastport on a borderless window on the Workbench screen,
 * timed by the E clock (timer.device). Each run ends with WaitBlit, so the work counted is done. Other tasks run
 * meanwhile, as they do for a module that draws. The same program runs on a plain 68k (the AC090's interpreter,
 * JIT_INTERP=1) and with the AC090's JIT, and the lines it prints are the ones the host build prints.
 *
 *     GFLayoutBench [RUNS=<n>] [MS=<n>] [ONLY=<a,b,c>] [ICON=<file without .info>] [LABEL=<text>]
 *
 * RUNS: timed runs a result, the median printed (default 3). MS: each run at least this long (default 500).
 * ICON: the Workbench icon for icon_blit_diskobject (default SYS:Disk; a default drawer icon when it has none).
 * The window is 1024 x 640 when the screen has room; a smaller one is said in BENCH-INFO.
 * MIT, Copyright (c) 2026 AmiCradle Team. */
#include "gflbench.h"

#include <exec/types.h>
#include <exec/execbase.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/timer.h>

#include <stdio.h>
#include <string.h>

static const char version[] __attribute__((used)) = "$VER: GFLayoutBench " GFLBENCH_VERSION " (13.11.2026) AmiCradle Team";

struct Device *TimerBase;
static struct timerequest treq;

static uint64_t a_now(void *u)
{
    struct EClockVal e;
    (void)u;
    ReadEClock(&e);
    return ((uint64_t)e.ev_hi << 32) | e.ev_lo;
}

static void a_sync(void *u)
{
    (void)u;
    WaitBlit();
}

static void a_out(void *u, const char *line)
{
    (void)u;
    printf("%s\n", line);
    fflush(stdout);
}

static const char *cpu_name(void)
{
    UWORD f = SysBase->AttnFlags;
    if (f & AFF_68060) return "68060";
    if (f & AFF_68040) return "68040";
    if (f & AFF_68030) return "68030";
    if (f & AFF_68020) return "68020";
    if (f & AFF_68010) return "68010";
    return "68000";
}

int main(void)
{
    enum { A_RUNS, A_MS, A_ONLY, A_ICON, A_LABEL, A_COUNT };
    LONG args[A_COUNT] = { 0 };
    struct RDArgs *rda;
    struct Screen *scr;
    struct Window *win = NULL;
    struct DiskObject *dob = NULL;
    oldraw_image *icon = NULL;
    gflbench_env e;
    ULONG hz;
    int w, h, top, na = -1;
    const char *iconname;

    rda = ReadArgs((CONST_STRPTR) "RUNS/K/N,MS/K/N,ONLY/K,ICON/K,LABEL/K", args, NULL);
    if (!rda) {
        PrintFault(IoErr(), (CONST_STRPTR) "GFLayoutBench");
        return RETURN_FAIL;
    }
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&treq, 0)) {
        printf("BENCH-INFO error no timer.device\nBENCH-END fail\n");
        FreeArgs(rda);
        return RETURN_FAIL;
    }
    TimerBase = treq.tr_node.io_Device;
    {
        struct EClockVal ev;
        hz = ReadEClock(&ev);
    }

    memset(&e, 0, sizeof e);
    if (args[A_RUNS]) e.runs = (int)*(LONG *)args[A_RUNS];
    if (args[A_MS]) e.run_ms = (uint32_t)*(LONG *)args[A_MS];
    e.only = (const char *)args[A_ONLY];
    iconname = args[A_ICON] ? (const char *)args[A_ICON] : "SYS:Disk";

    if ((scr = LockPubScreen(NULL))) {
        top = scr->BarHeight + 1;
        w = scr->Width < GFLBENCH_CANVAS_W ? scr->Width : GFLBENCH_CANVAS_W;
        h = scr->Height - top < GFLBENCH_CANVAS_H ? scr->Height - top : GFLBENCH_CANVAS_H;
        win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)scr, WA_Left, 0, WA_Top, top, WA_Width, w, WA_Height, h,
                             WA_Borderless, TRUE, WA_SmartRefresh, TRUE, WA_Activate, FALSE, WA_RMBTrap, TRUE,
                             TAG_DONE);
        printf("BENCH-INFO screen %dx%d depth %lu\n", scr->Width, scr->Height,
               (unsigned long)GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH));
        UnlockPubScreen(NULL, scr);
    }
    printf("BENCH-INFO target amiga %s exec %u.%u label %s\n", cpu_name(), (unsigned)SysBase->LibNode.lib_Version,
           (unsigned)SysBase->LibNode.lib_Revision, args[A_LABEL] ? (const char *)args[A_LABEL] : "-");
    if (!win) {
        printf("BENCH-INFO error no window on the Workbench screen\nBENCH-END fail\n");
    } else {
        printf("BENCH-INFO canvas %dx%d%s\n", w, h,
               (w < GFLBENCH_CANVAS_W || h < GFLBENCH_CANVAS_H) ? " (smaller than 1024x640: drawer figures clipped)" : "");
        e.d = oldraw_new_rastport(win->RPort, win->WScreen, w, h);
        if (e.d) {
            dob = GetDiskObjectNew((CONST_STRPTR)iconname);
            if (dob) icon = oldraw_image_icon(e.d, dob);
            printf("BENCH-INFO icon %s %s\n", iconname, icon ? "drawn through oldraw_image_icon" : "not available");
            e.icon = icon;
            e.now = a_now;
            e.hz = hz;
            e.sync = a_sync;
            e.out = a_out;
            na = gflbench_run(&e);
            if (icon) oldraw_image_free(e.d, icon);
            oldraw_free(e.d);
        } else
            printf("BENCH-INFO error oldraw_new_rastport failed\nBENCH-END fail\n");
        if (dob) FreeDiskObject(dob);
        CloseWindow(win);
    }
    CloseDevice((struct IORequest *)&treq);
    FreeArgs(rda);
    return na < 0 ? RETURN_FAIL : RETURN_OK;
}
