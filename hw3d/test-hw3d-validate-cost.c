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
    OSMGAHW3DTexReach reach;
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
    b.state.tmr[0] = 4L; b.state.tmr[3] = 4L;   /* gentle, so the
                                                * batch is accepted and
                                                * every row is walked */

    for (i = 0; i < (long)OSMGA_HW3D_MAX_TRI; i++) {
        OSMGAHW3DTri *t = &b.tri[i];

        /*
         * TEXTURED, because the coordinate work this measures only happens for
         * a textured primitive.  With a plain TRAP the test walked the edges
         * and reported a cost that had nothing to do with the check it was
         * meant to bound.
         */
        t->dwgctl = OSMGA_HW3D_OPCODE_TEX | (0x7UL << 4);
        t->alphactrl = 0x0101UL;
        t->y = 0; t->h = 1200;
        /* left edge crosses the whole width over the whole height */
        t->ar0 = 1200; t->ar2 = -1599; t->ar1 = -1599;
        /* right edge stands at the far side */
        t->ar6 = 1200; t->ar5 = 0; t->ar4 = 0;
        t->sgn = 0;
        t->fxbndry = (1600UL << 16) | 0UL;
    }

    /*
     * The path the driver takes, not the one the older tests take.  Asking
     * for the reach stops the box shortcut ending the walk, so a batch that
     * used to be measured after one walk is now measured after two -- which
     * is exactly the cost worth knowing.
     */
    v = osmgaHW3DValidateReach(&b, &lim, &badTri, &reach,
                                (OSMGAHW3DTexBand *)0);
    printf("one batch of %lu triangles, %ld rows each, an edge crossing 1600 "
           "columns\n", (unsigned long)OSMGA_HW3D_MAX_TRI, (long)b.tri[0].h);
    printf("   verdict %d (0 is accepted)\n", v);
    if (v != OSMGA_HW3D_OK) { printf("   refused, so this measures nothing\n"); return 1; }

    t0 = time((long *)0);
    for (i = 0; i < reps; i++) {
        badTri = 0;
        (void)osmgaHW3DValidateReach(&b, &lim, &badTri, &reach,
                                (OSMGAHW3DTexBand *)0);
    }
    t1 = time((long *)0);
    printf("   %ld validations in %ld seconds", reps, t1 - t0);
    if (t1 > t0)
        printf("  -> %ld ms each\n", (t1 - t0) * 1000L / reps);
    else
        printf("  -> under %ld ms each\n", 1000L / reps + 1L);
    /*
     * And the shape the driver actually submits.  The batch above is a
     * deliberate worst case -- two hundred primitives of twelve hundred rows
     * -- but a textured batch carries ONE primitive, and Mesa's are the size
     * of a quad on a 320 by 240 surface.  Timing only the worst case would
     * report a cost nothing pays.
     */
    {
        long t2, t3;
        int j;
        unsigned long reps2 = 400000UL;

        memset(&b, 0, sizeof b);
        b.magic = OSMGA_HW3D_MAGIC;
        b.version = OSMGA_HW3D_VERSION;
        b.triCount = 1UL;
        b.state.dstorg = lim.colourStart;
        b.state.dstPitch = lim.pitchBytes / 4UL;
        b.state.dstWidth = lim.clipX1 + 1UL;
        b.state.dstHeight = lim.clipY1 + 1UL;
        b.state.zorg = lim.depthStart;
        b.state.texorg = lim.texStart;
        b.state.texW = 16UL; b.state.texH = 16UL; b.state.texPitch = 16UL;
        b.state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
        b.state.tmr[0] = 8192L; b.state.tmr[3] = 8192L;
        b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
        b.tri[0].y = 0L;
        b.tri[0].h = 128L;
        b.tri[0].ar0 = 128L; b.tri[0].ar6 = 128L;
        b.tri[0].fxbndry = (128UL << 16) | 0UL;
        b.tri[0].dr[0] = 200UL << 15;

        badTri = 0;
        v = osmgaHW3DValidateReach(&b, &lim, &badTri, &reach,
                                (OSMGAHW3DTexBand *)0);
        printf("\none textured primitive, 128 rows of 128 columns:"
               " verdict %d\n", v);
        t2 = time((long *)0);
        for (j = 0; j < (int)reps2; j++) {
            badTri = 0;
            (void)osmgaHW3DValidateReach(&b, &lim, &badTri, &reach,
                                (OSMGAHW3DTexBand *)0);
        }
        t3 = time((long *)0);
        printf("   %lu validations in %ld seconds", reps2, t3 - t2);
        if (t3 > t2)
            printf("  -> %ld us each\n",
                   (long)((t3 - t2) * 1000000L / (long)reps2));
        else
            printf("  -> under %ld us each\n", (long)(1000000L / (long)reps2));
    }
    return 0;
}
