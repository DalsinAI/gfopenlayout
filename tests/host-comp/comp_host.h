/* SPDX-License-Identifier: MIT */
/* The compositor (library/layers/layers_comp.c) on the host, inside
 * gfopengfx's layers_lib.c, for tests/host-comp/test_infolock.c: the exec,
 * DOS, GFOpenGPU and GFOpenRTG it calls, as plain functions the test plays.
 * Force-included after gfopengfx's tests/abihost/shim.h.
 * Copyright (c) 2026 AmiCradle Team. MIT. */
#ifndef COMP_HOST_H
#define COMP_HOST_H
#include <exec/types.h>
#include <exec/semaphores.h>
#include <exec/ports.h>
#include <utility/tagitem.h>

#ifndef CONST_STRPTR
#define CONST_STRPTR const char *
#endif
#define NT_TASK 1
#define NT_PROCESS 13
#define SIGBREAKF_CTRL_F (1UL << 15)
#define MODE_OLDFILE 1005
#define TR_ADDREQUEST 9
#define UNIT_MICROHZ 0
typedef LONG BPTR;
struct Task {
    struct Node tc_Node;
    UBYTE tc_Flags, tc_State;
    BYTE tc_IDNestCnt, tc_TDNestCnt;
    ULONG tc_SigAlloc, tc_SigWait, tc_SigRecvd, tc_SigExcept;
    APTR tc_SPReg, tc_SPLower, tc_SPUpper;
    APTR tc_UserData;
};
struct Process { struct Task pr_Task; APTR pr_WindowPtr; };
struct ExecBase { BYTE IDNestCnt, TDNestCnt; };
struct MsgPort { struct Node mp_Node; UBYTE mp_SigBit; struct Task *mp_SigTask; };
struct IORequest { struct Message io_Message; APTR io_Device, io_Unit; UWORD io_Command; UBYTE io_Flags; BYTE io_Error; };
struct ac_timeval { ULONG tv_secs, tv_micro; };
struct timerequest { struct IORequest tr_node; struct ac_timeval tr_time; };
struct GfxBase;
#define TIMERNAME "timer.device"
#define BMATags_Friend 0x80000001UL
#define BMATags_Clear 0x80000002UL
#define BMATags_Displayable 0x80000003UL
#define BMF_CHECKVALUE 0x10UL

struct Task *FindTask(CONST_STRPTR name);
APTR AddTask(struct Task *t, APTR pc, APTR end);
void Forbid(void);
void Permit(void);
ULONG AttemptSemaphore(struct SignalSemaphore *s);
void CacheClearU(void);
APTR SetFunction(struct Library *l, LONG lvo, APTR f);
struct MsgPort *CreateMsgPort(void);
BYTE OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *io, ULONG flags);
BYTE DoIO(struct IORequest *io);
void Signal(struct Task *t, ULONG sigs);
ULONG Wait(ULONG sigs);
BPTR Open(CONST_STRPTR name, LONG mode);
LONG Close(BPTR fh);
LONG Read(BPTR fh, APTR buf, LONG len);

/* GFOpenGPU */
#define OPENGPU_NAME "gfopengpu.library"
#define OPENGPU_VERSION 1
#define OGPU_OK 0
#define OGPU_FULL 2
#define OGPU_BACKEND_CPU 1
#define OGPU_FMT_RGB565 1
#define OGPU_FMT_ARGB32 2
#define OGPU_OP_SURFACE 1
#define OGPU_OP_TARGET 2
#define OGPU_OP_COPY 3
#define OGPU_OP_COMPOSITE 4
#define OGPU_OP_CLIP 5
#define OGPU_OP_FILL 6
#define OGPU_COMP_SRCALPHA 1
#define OGPU_HDR(op, n) ((ULONG)(op) << 16 | (ULONG)(n))
#define OGPU_XY(x, y) ((ULONG)(UWORD)(x) << 16 | (ULONG)(UWORD)(y))
#define OGPU_ANSWER(q) ((q) & 0xff)
#define OGPU_BACKEND(q) ((q) >> 8 & 0xff)
LONG OGPU_Submit(ULONG *w, ULONG n, ULONG *fence);
LONG OGPU_Wait(ULONG fence);
ULONG OGPU_Query(ULONG op, ULONG fmt);
CONST_STRPTR OGPU_BackEndName(ULONG which);

/* GFOpenRTG */
#define OPENRTG_NAME "gfopenrtg.library"
#define OPENRTG_VERSION 0
struct OpenRTGBitMapInfo { ULONG format, width, height, bytes_per_row; APTR memory; };
ULONG ORTG_BitMapInfo(struct BitMap *bm, struct OpenRTGBitMapInfo *bi);
struct oc_span;
ULONG ORTG_WrittenSpans(struct BitMap *bm, struct oc_span *s, ULONG max);
#define LP3(o, rt, name, t1, v1, r1, t2, v2, r2, t3, v3, r3, bt, bn) name(v1, v2, v3)

/* The test's tasks: which one is running, and what a semaphore held by
 * another would have made it wait for. */
extern int host_task;
#endif
