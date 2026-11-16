/* GFOpenLayout's ROM core: a screen bitmap's pixels for a composited scene, on the Amiga (step 2's lookup).
 * GFOpenRTG's ORTG_BitMapInfo (gfopenrtg.library 0.4 and later) gives a bitmap's memory, bytes per row and format:
 * 0 is 8-bit pens, 1 is 16-bit RGB565, 2 is 32-bit with its top byte unused. A bitmap GFOpenRTG doesn't know
 * (AGA, planar) answers no, and the scene is direct. The library is opened at the first composited scene, from
 * olc_scene_new, which is called outside every lock; it stays open, as layers' compositor keeps it. Before
 * GFOpenRTG is up a scene is direct and the next one tries again.
 * The struct and the call are gfopenrtg's own (include/gfopenrtg/gfopenrtg.h, MIT), copied so the core builds
 * without a gfopenrtg checkout. MIT, Copyright (c) 2026 AmiCradle Team. */
#include "olc_core_internal.h"

#ifdef OLC_AMIGA
#include <exec/libraries.h>
#include <graphics/gfx.h>
#include <proto/exec.h>
#include <inline/macros.h>

struct olc_ortg_info {          /* struct OpenRTGBitMapInfo */
    void *memory;
    unsigned long bytes_per_row;
    unsigned short width, height;
    unsigned char depth, format, monitor, flags;
};
typedef char olc_ortg_info_size[sizeof(struct olc_ortg_info) == 16 ? 1 : -1];

#define OLC_ORTG_BitMapInfo(base, bm, info) \
    LP2(0x66, BOOL, ORTG_BitMapInfo, struct BitMap *, (bm), a0, struct olc_ortg_info *, (info), a1, , (base))

static struct Library *ortg;

int olc__amiga_screen_lookup(void *screen, void **pixels, int32_t *bytes_per_row, uint32_t *fmt)
{
    struct olc_ortg_info bi;
    if (!screen) return -1;
    if (!ortg) {
        struct Library *lb = OpenLibrary((CONST_STRPTR)"gfopenrtg.library", 0);
        if (!lb) return -1;
        if (lb->lib_Version == 0 && lb->lib_Revision < 4) {     /* ORTG_BitMapInfo came with 0.4 */
            CloseLibrary(lb);
            return -1;
        }
        Forbid();
        if (!ortg) {
            ortg = lb;
            lb = NULL;
        }
        Permit();
        if (lb) CloseLibrary(lb);
    }
    if (!OLC_ORTG_BitMapInfo(ortg, (struct BitMap *)screen, &bi) || !bi.memory || !bi.bytes_per_row) return -1;
    switch (bi.format) {
    case 0: *fmt = OLC_FMT_CLUT8; break;
    case 1: *fmt = OLC_FMT_RGB16; break;
    case 2: *fmt = OLC_FMT_XRGB32; break;
    default: return -1;
    }
    *pixels = bi.memory;
    *bytes_per_row = (int32_t)bi.bytes_per_row;
    return 0;
}
#else
typedef int olc_screen_amiga_unused;     /* the host build has no screens of its own */
#endif
