/* SPDX-License-Identifier: MIT */
/* The compositor's first start against the lock order (amicradle-teams
 * designs/WORKBENCH-REDESIGN-20261113.md, "Lock order"): ComposeLayerInfo,
 * gfopengfx's layers_lib.c with this repository's layers_comp.c built in
 * (OL_COMPOSITE), on the host.
 *
 * olb->infolock is a leaf lock: nothing that can wait may run under it.
 * The compositor's first ComposeLayerInfo opens gfopengpu.library,
 * gfopenrtg.library and cybergraphics.library, which can go through ramlib
 * and DOS; a DOS requester needs the screen's Layer_Info, and a task that
 * holds a Layer_Info takes infolock (begin(), through info_of). If the
 * libraries are opened under infolock the three wait on each other.
 *
 * The test plays the tasks one at a time. A semaphore remembers its owner;
 * a task that obtains one another task holds would have waited, and the
 * test reports it. OpenLibrary stands in for ramlib: while task 1 opens
 * gfopengpu.library,
 *   1. deadlock: task 2, holding a Layer_Info as begin() does, asks for its
 *      record (infolock); it must not find infolock held, and no library is
 *      ever opened with infolock held;
 *   2. race: task 2 starts the compositor itself, to the end; task 1 must
 *      then keep task 2's state, close the libraries it opened and free its
 *      task, and only one compositor task is ever added;
 *   3. cost: once the compositor is there, a ComposeLayerInfo takes infolock
 *      only for its record (info_of), not again round oc_cb.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <graphics/gfopenlayers.h>
#include "layers_lib.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ---- tasks and semaphores ------------------------------------------ */

int host_task = 1;
static struct Process tasks[3];
static struct ol_base olb;
static int waits;                       /* obtains that would have waited */
static int infolock_obtains;

struct Task *FindTask(CONST_STRPTR name) { (void)name; return &tasks[host_task].pr_Task; }

void InitSemaphore(struct SignalSemaphore *s) { memset(s, 0, sizeof *s); }
void ObtainSemaphore(struct SignalSemaphore *s)
{
    struct Task *me = FindTask(0);
    if (s == &olb.infolock) infolock_obtains++;
    if (s->ss_NestCount > 0 && s->ss_Owner != me) {
        printf("  task %d would wait for a semaphore task %d holds%s\n", host_task, (int)((struct Process *)s->ss_Owner - tasks),
               s == &olb.infolock ? " (infolock)" : "");
        waits++;
    }
    s->ss_NestCount++;
    s->ss_Owner = me;
}
ULONG AttemptSemaphore(struct SignalSemaphore *s)
{
    if (s->ss_NestCount > 0 && s->ss_Owner != FindTask(0)) return 0;
    ObtainSemaphore(s);
    return 1;
}
void ReleaseSemaphore(struct SignalSemaphore *s)
{
    if (--s->ss_NestCount < 0) { printf("FAIL: semaphore released too often\n"); exit(20); }
    if (!s->ss_NestCount) s->ss_Owner = 0;
}

static long allocs;
APTR AllocMem(ULONG size, ULONG flags)
{
    UBYTE *p = malloc(size ? size : 1);
    if (p) memset(p, (flags & MEMF_CLEAR) ? 0 : 0xA5, size);
    if (p) allocs++;
    return p;
}
void FreeMem(APTR p, ULONG size) { (void)size; if (p) allocs--; free(p); }

static struct Task *added;
static int adds;
APTR AddTask(struct Task *t, APTR pc, APTR end) { (void)pc; (void)end; added = t; adds++; return t; }

/* ---- libraries: OpenLibrary is ramlib, and what it lets run meanwhile -- */

static struct Library libs[3];
static int opened, closed, under_infolock, nested;
static void (*meanwhile)(void);         /* runs once, as task 2, inside task 1's first open */

struct Library *OpenLibrary(const char *name, ULONG version)
{
    (void)version;
    if (olb.infolock.ss_NestCount > 0) {
        printf("  task %d opens %s with infolock held\n", host_task, name);
        under_infolock++;
    }
    if (meanwhile && !nested && host_task == 1) {
        void (*f)(void) = meanwhile;
        meanwhile = 0;
        nested = 1;
        host_task = 2;
        f();
        host_task = 1;
        nested = 0;
    }
    opened++;
    if (!strcmp(name, OPENGPU_NAME)) return &libs[0];
    if (!strcmp(name, OPENRTG_NAME)) return &libs[1];
    return &libs[2];
}
void CloseLibrary(struct Library *l) { if (l) closed++; }

/* ---- what the compositor's start needs, and nothing past it ------------ */

ULONG ORTG_BitMapInfo(struct BitMap *bm, struct OpenRTGBitMapInfo *bi) { (void)bm; (void)bi; return 0; }

static void never(const char *what) { printf("FAIL: %s called\n", what); exit(20); }
void Forbid(void) {}
void Permit(void) {}
void CacheClearU(void) {}
APTR SetFunction(struct Library *l, LONG lvo, APTR f) { (void)l; (void)lvo; (void)f; never("SetFunction"); return 0; }
struct MsgPort *CreateMsgPort(void) { return 0; }
BYTE OpenDevice(CONST_STRPTR n, ULONG u, struct IORequest *io, ULONG f) { (void)n; (void)u; (void)io; (void)f; return -1; }
BYTE DoIO(struct IORequest *io) { (void)io; return -1; }
void Signal(struct Task *t, ULONG s) { (void)t; (void)s; }
ULONG Wait(ULONG s) { (void)s; never("Wait"); return 0; }
BPTR Open(CONST_STRPTR n, LONG m) { (void)n; (void)m; return 0; }
LONG Close(BPTR f) { (void)f; return 0; }
LONG Read(BPTR f, APTR b, LONG l) { (void)f; (void)b; (void)l; return 0; }
LONG OGPU_Submit(ULONG *w, ULONG n, ULONG *fence) { (void)w; (void)n; (void)fence; never("OGPU_Submit"); return 0; }
LONG OGPU_Wait(ULONG fence) { (void)fence; never("OGPU_Wait"); return 0; }
ULONG OGPU_Query(ULONG op, ULONG fmt) { (void)op; (void)fmt; return 0; }
CONST_STRPTR OGPU_BackEndName(ULONG w) { (void)w; return 0; }
ULONG ORTG_WrittenSpans(struct BitMap *bm, struct oc_span *s, ULONG max) { (void)bm; (void)s; (void)max; return 0; }
ULONG ol_call_hook(struct Hook *h, APTR o, APTR m) { (void)h; (void)o; (void)m; never("a hook"); return 0; }
/* graphics: layers_lib.c's classic paths, never reached from here */
struct BitMap *AllocBitMap(ULONG w, ULONG h, ULONG d, ULONG f, struct BitMap *b) { (void)w; (void)h; (void)d; (void)f; (void)b; never("AllocBitMap"); return 0; }
void FreeBitMap(struct BitMap *b) { (void)b; never("FreeBitMap"); }
ULONG GetBitMapAttr(struct BitMap *b, ULONG a) { (void)b; (void)a; never("GetBitMapAttr"); return 0; }
LONG BltBitMap(struct BitMap *s, LONG sx, LONG sy, struct BitMap *d, LONG dx, LONG dy, LONG w, LONG h, ULONG m, ULONG k, UBYTE *t)
{ (void)s; (void)sx; (void)sy; (void)d; (void)dx; (void)dy; (void)w; (void)h; (void)m; (void)k; (void)t; never("BltBitMap"); return 0; }
void InitRastPort(struct RastPort *rp) { (void)rp; never("InitRastPort"); }
void WaitTOF(void) {}
struct Region *NewRegion(void) { never("NewRegion"); return 0; }
void DisposeRegion(struct Region *r) { (void)r; never("DisposeRegion"); }
void ClearRegion(struct Region *r) { (void)r; never("ClearRegion"); }
BOOL OrRectRegion(struct Region *r, struct Rectangle *x) { (void)r; (void)x; never("OrRectRegion"); return 0; }
void AndRectRegion(struct Region *r, struct Rectangle *x) { (void)r; (void)x; never("AndRectRegion"); }
APTR CreateIORequest(struct MsgPort *p, ULONG s) { (void)p; (void)s; return 0; }
void DeleteIORequest(APTR io) { (void)io; }

/* ---- the tasks' parts ------------------------------------------------ */

static struct Layer_Info li1, li2;
static struct BitMap bm;

static void reset(void)
{
    int i;
    memset(&olb, 0, sizeof olb);
    ol_init(&olb, 0);
    memset(&li1, 0, sizeof li1);
    memset(&li2, 0, sizeof li2);
    for (i = 0; i < 3; i++) tasks[i].pr_Task.tc_Node.ln_Type = NT_PROCESS;
    waits = under_infolock = opened = closed = adds = infolock_obtains = 0;
    added = 0;
}

/* task 2 as begin() is: the Layer_Info held, then its record (infolock) */
static void begin_on_li2(void)
{
    ObtainSemaphore(&li2.Lock);                                         /* LockLayerInfo */
    ol_ComposeLayerInfo(&olb, &li2, 0, LAYERS_COMPOSE_WHY);             /* info_of: infolock */
    ReleaseSemaphore(&li2.Lock);
}

/* task 2 starting the compositor itself, all the way */
static void compose_li2(void)
{
    ol_ComposeLayerInfo(&olb, &li2, &bm, LAYERS_COMPOSE_ON);
}

static void test_deadlock(void)
{
    reset();
    meanwhile = begin_on_li2;
    CHECK(ol_ComposeLayerInfo(&olb, &li1, &bm, LAYERS_COMPOSE_ON) == FALSE);   /* no GFOpenRTG bitmap here */
    CHECK(!meanwhile);                  /* task 2 did run inside the open */
    CHECK(waits == 0);
    CHECK(under_infolock == 0);
    CHECK(olb.cb != 0 && adds == 1 && added->tc_UserData == (APTR)olb.cb);
    CHECK(olb.infolock.ss_NestCount == 0 && li2.Lock.ss_NestCount == 0);
}

static void test_race(void)
{
    APTR mine;
    long before;
    reset();
    before = allocs;
    meanwhile = compose_li2;
    CHECK(ol_ComposeLayerInfo(&olb, &li1, &bm, LAYERS_COMPOSE_ON) == FALSE);
    mine = (APTR)olb.cb;
    CHECK(waits == 0 && under_infolock == 0);
    CHECK(mine != 0 && adds == 1 && added->tc_UserData == (APTR)mine);
    CHECK(opened == 6);                 /* three each */
    CHECK(closed == 3);                 /* task 1's three, closed again */
    /* left: the two Layer_Infos' records, task 2's state and its task */
    CHECK(allocs - before == 4);
    /* and later calls use that state */
    CHECK(ol_ComposeLayerInfo(&olb, &li1, &bm, LAYERS_COMPOSE_ON) == FALSE && (APTR)olb.cb == mine && adds == 1);
}

static void test_cost(void)
{
    reset();
    ol_ComposeLayerInfo(&olb, &li1, &bm, LAYERS_COMPOSE_ON);
    infolock_obtains = 0;
    ol_ComposeLayerInfo(&olb, &li1, &bm, LAYERS_COMPOSE_ON);
    CHECK(infolock_obtains == 1);       /* info_of's only */
}

int main(void)
{
    test_deadlock();
    test_race();
    test_cost();
    if (fails) { printf("test_infolock: %d failed\n", fails); return 1; }
    printf("test_infolock: ok\n");
    return 0;
}
