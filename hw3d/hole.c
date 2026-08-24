/*
 * Does the accumulating row model let a NEGATIVE dv/dy hide a coordinate
 * that the re-seeded engine actually reaches?
 */
#include <stdio.h>
#include <string.h>
#include "OpenStepMGAHW3D.h"

static OSMGAHW3DBatch b;
static OSMGAHW3DLimits lim;

static void one(OSMGAHW3DTri *t, long y, long h, unsigned long x0,
                unsigned long w)
{
    memset(t, 0, sizeof *t);
    t->dwgctl = OSMGA_HW3D_OPCODE_TEX | (OSMGA_HW3D_ATYPE_I << 4);
    t->alphactrl = 0x00000101UL;
    t->y = y; t->h = h;
    t->ar0 = h; t->ar6 = h;
    t->fxbndry = ((x0 + w) << 16) | x0;
    t->tq0 = OSMGA_HW3D_Q_ONE;
}

int
main(void)
{
    unsigned long badTri = 0UL;
    OSMGAHW3DTexReach reach;
    long CM = (long)OSMGA_HW3D_TEX_COORD_MAX;
    int v;
    long dx, worstV = 0L, worstM = 0L;

    lim.colourStart = 0UL;      lim.colourEnd   = 16UL * 1024UL * 1024UL;
    lim.depthStart  = 16UL * 1024UL * 1024UL;
    lim.depthEnd    = 24UL * 1024UL * 1024UL;
    lim.texStart    = 24UL * 1024UL * 1024UL;
    lim.texEnd      = 32UL * 1024UL * 1024UL;
    lim.pitchBytes  = 4096UL;
    lim.clipX1 = 1023UL; lim.clipY1 = 767UL;
    lim.maxEdgeWalk = 1UL << 16;
    lim.batchBytes = OSMGA_HW3D_BATCH_BYTES;

    memset(&b, 0, sizeof b);
    b.magic = OSMGA_HW3D_MAGIC;
    b.version = OSMGA_HW3D_VERSION;
    b.triCount = 2UL;
    b.state.dstorg = 0UL;
    b.state.dstWidth = 1024UL; b.state.dstHeight = 768UL;
    b.state.dstPitch = 1024UL;
    b.state.zorg = lim.depthStart;
    b.state.texorg = lim.texStart;
    b.state.texW = 64UL; b.state.texH = 64UL; b.state.texPitch = 64UL;
    b.state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
    b.state.texFlags = 0UL;

    /* one drawn row ahead of it, so the accumulator would stand at 1 */
    one(&b.tri[0], 0L, 1L, 0UL, 2UL);
    b.tri[0].tu0 = 0L; b.tri[0].tv0 = 0L;

    one(&b.tri[1], 4L, 1L, 0UL, 2UL);
    b.tri[1].tu0 = 0L;
    b.tri[1].tv0 = CM - 100L;
    b.state.tmr[2] =  200L;      /* dv/dx */
    b.state.tmr[3] = -300L;      /* dv/dy, NEGATIVE */

    v = osmgaHW3DValidateReach(&b, &lim, &badTri, &reach);
    printf("verdict %d  (0 = accepted)   badTri %lu\n", v, badTri);
    printf("reach v %ld  (COORD_MAX %ld)\n", reach.vMax, CM);

    /* what the re-seeded engine actually forms on that primitive's one row */
    for (dx = 0L; dx < 2L; dx++) {
        long real = b.tri[1].tv0 + b.state.tmr[2] * dx + b.state.tmr[3] * 0L;
        long mdl  = b.tri[1].tv0 + b.state.tmr[2] * dx + b.state.tmr[3] * 1L;

        if (real > worstV) worstV = real;
        if (mdl  > worstM) worstM = mdl;
    }
    printf("engine reaches %ld  -> %s\n", worstV,
           (worstV > CM) ? "PAST THE LIMIT" : "inside");
    printf("model  reaches %ld  -> %s\n", worstM,
           (worstM > CM) ? "PAST THE LIMIT" : "inside");
    if (v == OSMGA_HW3D_OK && worstV > CM) {
        printf("\nHOLE: accepted, and the engine's own coordinate is past"
               " the limit by %ld\n", worstV - CM);
        return 1;
    }
    printf("\nno hole in this construction\n");
    return 0;
}
