/*
 * test-hw3d-validate-cost.c -- what does walking every row in the kernel cost?
 *
 * The validator walks both edges of every trapezoid.  The worry raised
 * against it was that this makes validation proportional to the area drawn,
 * which for a full batch of full-screen triangles would be hundreds of
 * millions of steps under a lock.
 *
 * It is not proportional to area.  The row loop runs once per row, and the
 * inner loop moves a column at a time -- but a walk only ever moves one way
 * and is refused the moment it leaves the rectangle, so the total column
 * movement per edge is bounded by the width whatever the height.  The cost is
 * therefore rows plus width per edge, not rows times width.
 *
 * That is an argument.  This measures it, at the largest size the driver
 * drives: 1600x1200, 200 triangles, every one of them full height with an
 * edge crossing the whole width.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "OpenStepMGAHW3D.h"

extern long time(long *);

static OSMGAHW3DBatch b;
static OSMGAHW3DLimits lim;

int
main(int argc, char **argv)
{
    long reps = (argc > 1) ? atol(argv[1]) : 200L;
    long i, t0, t1;
    unsigned long badTri = 0;
    int v = 0;

    memset(&lim, 0, sizeof lim);
    lim.clipX1 = 1599; lim.clipY1 = 1199;
    lim.pitchBytes = 1600UL * 4UL;
    lim.colourStart = 4UL * 1024UL * 1024UL;
    lim.colourEnd = lim.colourStart + 1600UL * 1200UL * 4UL;
    lim.depthStart = lim.colourEnd;
    lim.depthEnd = lim.depthStart + 1600UL * 1200UL * 2UL;
    lim.texStart = lim.depthEnd;
    lim.texEnd = lim.texStart + 1024UL * 1024UL;
    lim.batchBytes = OSMGA_HW3D_BATCH_BYTES;
    lim.maxEdgeWalk = 16384;

    memset(&b, 0, sizeof b);
    b.magic = OSMGA_HW3D_MAGIC;
    b.version = OSMGA_HW3D_VERSION;
    b.triCount = OSMGA_HW3D_MAX_TRI;
    b.state.dstorg = lim.colourStart;
    b.state.dstPitch = 1600;
    b.state.dstWidth = 1600;
    b.state.dstHeight = 1200;
    b.state.zorg = lim.depthStart;
    b.state.texorg = lim.texStart;
    b.state.texW = 64; b.state.texH = 64; b.state.texPitch = 64;
    b.state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
    b.state.tmr[0] = 0x4000; b.state.tmr[3] = 0x4000;

    for (i = 0; i < (long)OSMGA_HW3D_MAX_TRI; i++) {
        OSMGAHW3DTri *t = &b.tri[i];

        t->dwgctl = 0x4UL | (0x7UL << 4);
        t->alphactrl = 0x0101UL;
        t->y = 0; t->h = 1200;
        /* left edge crosses the whole width over the whole height */
        t->ar0 = 1200; t->ar2 = -1599; t->ar1 = -1599;
        /* right edge stands at the far side */
        t->ar6 = 1200; t->ar5 = 0; t->ar4 = 0;
        t->sgn = 0;
        t->fxbndry = (1600UL << 16) | 0UL;
    }

    v = osmgaHW3DValidate(&b, &lim, &badTri);
    printf("one batch of %lu triangles, %ld rows each, an edge crossing 1600 "
           "columns\n", (unsigned long)OSMGA_HW3D_MAX_TRI, (long)b.tri[0].h);
    printf("   verdict %d (0 is accepted)\n", v);
    if (v != OSMGA_HW3D_OK) { printf("   refused, so this measures nothing\n"); return 1; }

    t0 = time((long *)0);
    for (i = 0; i < reps; i++) {
        badTri = 0;
        (void)osmgaHW3DValidate(&b, &lim, &badTri);
    }
    t1 = time((long *)0);
    printf("   %ld validations in %ld seconds", reps, t1 - t0);
    if (t1 > t0)
        printf("  -> %ld ms each\n", (t1 - t0) * 1000L / reps);
    else
        printf("  -> under %ld ms each\n", 1000L / reps + 1L);
    return 0;
}
