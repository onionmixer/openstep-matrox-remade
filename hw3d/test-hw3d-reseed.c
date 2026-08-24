/*
 * The row index is the primitive's OWN, and a negative gradient is what
 * proves it.
 *
 * While the texture anchors were batch state the hardware ran v's row index
 * on across the textured primitives of a batch, and the validator modelled
 * exactly that.  The anchors are per trapezoid now and the matrix write
 * re-seeds -- measured, probe sections 78 and 78b -- so the model had to
 * follow, and the argument for leaving it alone ("bounding a larger row index
 * refuses more than it must") was wrong: it holds only for a POSITIVE dv/dy.
 *
 * With a negative one a larger row index means a SMALLER coordinate, so the
 * accumulating model evaluated a row the engine never uses and skipped the
 * row it does.  This is that case, and its positive-gradient twin, which the
 * model got right all along and which must still be accepted.
 */
#include <stdio.h>
#include <string.h>
#include "OpenStepMGAHW3D.h"

static OSMGAHW3DBatch b;
static OSMGAHW3DLimits lim;
static int failures;

static void
tri(OSMGAHW3DTri *t, long y, long h, unsigned long x0, unsigned long w)
{
    memset(t, 0, sizeof *t);
    t->dwgctl = OSMGA_HW3D_OPCODE_TEX | (OSMGA_HW3D_ATYPE_I << 4);
    t->alphactrl = 0x00000101UL;
    t->y = y; t->h = h;
    t->ar0 = h; t->ar6 = h;
    t->fxbndry = ((x0 + w) << 16) | x0;
    t->tq0 = OSMGA_HW3D_Q_ONE;
}

/* what the engine forms on the second primitive's own rows */
static long
engineMax(void)
{
    long worst = 0L, dx, row;

    for (row = 0L; row < b.tri[1].h; row++)
        for (dx = 0L; dx < 2L; dx++) {
            long v = b.tri[1].tv0 + b.state.tmr[2] * dx
                     + b.state.tmr[3] * row;

            if (v < 0L) v = -v;
            if (v > worst) worst = v;
        }
    return worst;
}

static void
say(const char *what, int got, int want)
{
    if (got == want)
        printf("   ok    %-56s verdict %d\n", what, got);
    else {
        printf("   FAIL  %-56s verdict %d, wanted %d\n", what, got, want);
        failures++;
    }
}

static void
setup(long dvdy)
{
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

    /* one drawn row ahead of it, which is what used to move the model */
    tri(&b.tri[0], 0L, 1L, 0UL, 2UL);
    tri(&b.tri[1], 4L, 1L, 0UL, 2UL);
    b.tri[1].tv0 = (long)OSMGA_HW3D_TEX_COORD_MAX - 100L;
    b.state.tmr[2] = 200L;
    b.state.tmr[3] = dvdy;
}

int
main(void)
{
    unsigned long badTri = 0UL;
    OSMGAHW3DTexReach reach;
    long CM = (long)OSMGA_HW3D_TEX_COORD_MAX;
    int v;

    lim.colourStart = 0UL;      lim.colourEnd = 16UL * 1024UL * 1024UL;
    lim.depthStart  = 16UL * 1024UL * 1024UL;
    lim.depthEnd    = 24UL * 1024UL * 1024UL;
    lim.texStart    = 24UL * 1024UL * 1024UL;
    lim.texEnd      = 32UL * 1024UL * 1024UL;
    lim.pitchBytes  = 4096UL;
    lim.clipX1 = 1023UL; lim.clipY1 = 767UL;
    lim.maxEdgeWalk = 1UL << 16;
    lim.batchBytes  = OSMGA_HW3D_BATCH_BYTES;

    printf("the row index belongs to the primitive\n\n");

    /*
     * The case that was accepted and should not have been.  Row 0 of the
     * second primitive is where v is largest, and the accumulating model
     * looked at row 1 instead, where a negative gradient had brought it back
     * inside.
     */
    printf("1. a negative dv/dy, with the coordinate near the limit\n");
    setup(-300L);
    v = osmgaHW3DValidateReach(&b, &lim, &badTri, &reach,
                                (OSMGAHW3DTexBand *)0);
    printf("         engine reaches %ld, the limit is %ld\n",
           engineMax(), CM);
    say("past the limit on its own first row, so refused", v,
        OSMGA_HW3D_E_TEXCOORD);
    if (v == OSMGA_HW3D_E_TEXCOORD && badTri != 1UL) {
        printf("   FAIL  %-56s it named %lu\n",
               "and the second trapezoid is named", badTri);
        failures++;
    } else if (v == OSMGA_HW3D_E_TEXCOORD)
        printf("   ok    %-56s\n", "and the second trapezoid is named");

    /*
     * The positive control.  Same shape, gradient the other way, and now the
     * primitive's own rows keep it inside -- this one must still draw, or the
     * fix would have been "refuse everything".
     */
    printf("\n2. the same shape with the gradient the other way\n");
    setup(-100L);
    b.tri[1].tv0 = 100L;
    b.state.tmr[3] = 300L;
    v = osmgaHW3DValidateReach(&b, &lim, &badTri, &reach,
                                (OSMGAHW3DTexBand *)0);
    printf("         engine reaches %ld, the limit is %ld\n",
           engineMax(), CM);
    say("inside on every row it draws, so accepted", v, OSMGA_HW3D_OK);
    if (v == OSMGA_HW3D_OK) {
        long want = engineMax();

        if (reach.vMax == want)
            printf("   ok    %-56s %ld\n",
                   "and the reach is the engine's own maximum", reach.vMax);
        else {
            printf("   FAIL  %-56s %ld, wanted %ld\n",
                   "and the reach is the engine's own maximum",
                   reach.vMax, want);
            failures++;
        }
    }

    /*
     * And the shape that the OLD model refused for a reason that no longer
     * exists: nine primitives of eight rows, whose accumulated height passed
     * the surface even though no primitive is taller than eight.
     */
    printf("\n3. nine primitives of eight rows\n");
    {
        unsigned long i;

        setup(0L);
        b.state.tmr[2] = 0L; b.state.tmr[3] = 4096L;
        b.triCount = 9UL;
        for (i = 0UL; i < 9UL; i++) {
            tri(&b.tri[i], (long)(i * 8UL), 8L, 0UL, 2UL);
            b.tri[i].tv0 = 0L;
        }
        v = osmgaHW3DValidateReach(&b, &lim, &badTri, &reach,
                                (OSMGAHW3DTexBand *)0);
        say("each one stands on its own rows, so accepted", v,
            OSMGA_HW3D_OK);
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
