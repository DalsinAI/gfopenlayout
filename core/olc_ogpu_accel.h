/* GFOpenLayout core's GFOpenGPU accelerator, row N1. The Amiga shell supplies
 * the four small calls below only after RTG is ready; constructing this object
 * performs no probe, opens no library and submits nothing. MIT. */
#ifndef OLC_OGPU_ACCEL_H
#define OLC_OGPU_ACCEL_H

#include "gfopenlayout_core.h"
#include <stddef.h>
#include <stdint.h>

typedef struct olc_ogpu_io {
    void *ctx;
    /* An Amiga-visible address for CPU-master pixels only when they belong to
     * an RTG bitmap whose hazard tracking settles earlier GPU readers before
     * the CPU writes them again; 0 for Fast/host/untracked storage. */
    uint32_t (*address)(void *ctx, const void *pixels);
    /* GFOpenGPU's OGPU_Submit and OGPU_Wait. */
    int (*submit)(void *ctx, const void *stream, uint32_t words, uint32_t *fence);
    int (*wait)(void *ctx, uint32_t fence);
    void (*close)(void *ctx); /* optional; called once by accel.detach */
} olc_ogpu_io;

typedef struct olc_ogpu olc_ogpu;
olc_ogpu *olc_ogpu_new(const olc_ogpu_io *io);
void olc_ogpu_free(olc_ogpu *g); /* only before attach, or after olc_accel_detach */
const olc_accel *olc_ogpu_ops(olc_ogpu *g);

#endif
