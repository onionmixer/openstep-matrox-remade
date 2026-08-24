/*
 * Asking for the reach must not change the verdict, and the reach must be
 * the largest coordinate the primitive's pixels really see.
 *
 * Two properties, over a lot of shapes rather than a few:
 *
 *   1. Validate(b) == ValidateReach(b, &r) for every batch.  Collecting the
 *      reach makes the walk run where the box shortcut used to end it, and
 *      the walk sets the same flag the box does -- so if the two ever
 *      disagree the driver would refuse work it used to accept.
 *
 *   2. r.uMax and r.vMax equal an oracle that does not share the validator's
 *      arithmetic: it walks the trapezoid the way the engine does and takes
 *      the coordinate at both ends of each row's span.
 *
 * Shapes are generated with both edge directions, both gradient signs, thin
 * and wide spans, and starts placed so the maximum lands just below, exactly
 * on, and just above each band edge -- which is the only place where getting
 * the reach wrong changes which addend the encoder takes off.
 *
 *   cc -O -Wall -o test-hw3d-reach test-hw3d-reach.c OpenStepMGAHW3D.c
 */
#include <stdio.h>
#include <string.h>
#include "OpenStepMGAHW3D.h"

static OSMGAHW3DBatch b;
static OSMGAHW3DLimits lim;
static int failures;

/* a small deterministic generator, so a failure can be reproduced */
static unsigned long seed = 20260824UL;
static unsigned long
rnd(unsigned long n)
{
    seed = seed * 1103515245UL + 12345UL;
    return ((seed >> 16) & 0x7FFFUL) % n;
}

/* the oracle: the walk, without the validator's help */
static void
oracle(const OSMGAHW3DTri *t, long *uOut, long *vOut, int *any)
{
    long lx = (long)(t->fxbndry & 0xFFFFUL);
    long rx = (long)((t->fxbndry >> 16) & 0xFFFFUL);
    long lx0 = lx;
    long lacc = t->ar1 - t->ar2, racc = t->ar4 - t->ar5;
    long lsgn = (t->sgn & 0x2L)  ? -1L : 1L;
    long rsgn = (t->sgn & 0x20L) ? -1L : 1L;
    long row;

    *uOut = *vOut = 0L; *any = 0;
    for (row = 0L; row < t->h; row++) {
        long ua, ub, va, vb;

        if (row > 0L) {
            lacc += t->ar2;
            while (lacc < 0L) { lx += lsgn; lacc += t->ar0; }
            racc += t->ar5;
            while (racc < 0L) { rx += rsgn; racc += t->ar6; }
        }
        if (lx >= rx)
            continue;
        ua = b.state.tmr[6] + b.state.tmr[0] * (lx - lx0)
             + b.state.tmr[1] * row;
        ub = ua + b.state.tmr[0] * (rx - 1L - lx);
        va = b.state.tmr[7] + b.state.tmr[2] * (lx - lx0)
             + b.state.tmr[3] * row;
        vb = va + b.state.tmr[2] * (rx - 1L - lx);
        if (ua < 0L) ua = -ua;
        if (ub < 0L) ub = -ub;
        if (va < 0L) va = -va;
        if (vb < 0L) vb = -vb;
        if (!*any) { *uOut = ua; *vOut = va; *any = 1; }
        if (ua > *uOut) *uOut = ua;
        if (ub > *uOut) *uOut = ub;
        if (va > *vOut) *vOut = va;
        if (vb > *vOut) *vOut = vb;
    }
}

/* what the reach USED to be: the signed maximum, floored at nought */
static void
signedOracle(const OSMGAHW3DTri *t, long *uOut, long *vOut)
{
    long lx = (long)(t->fxbndry & 0xFFFFUL);
    long rx = (long)((t->fxbndry >> 16) & 0xFFFFUL);
    long lx0 = lx;
    long lacc = t->ar1 - t->ar2, racc = t->ar4 - t->ar5;
    long lsgn = (t->sgn & 0x2L)  ? -1L : 1L;
    long rsgn = (t->sgn & 0x20L) ? -1L : 1L;
    long row;

    *uOut = 0L; *vOut = 0L;
    for (row = 0L; row < t->h; row++) {
        long ua, ub, va, vb;

        if (row > 0L) {
            lacc += t->ar2;
            while (lacc < 0L) { lx += lsgn; lacc += t->ar0; }
            racc += t->ar5;
            while (racc < 0L) { rx += rsgn; racc += t->ar6; }
        }
        if (lx >= rx)
            continue;
        ua = b.state.tmr[6] + b.state.tmr[0] * (lx - lx0)
             + b.state.tmr[1] * row;
        ub = ua + b.state.tmr[0] * (rx - 1L - lx);
        va = b.state.tmr[7] + b.state.tmr[2] * (lx - lx0)
             + b.state.tmr[3] * row;
        vb = va + b.state.tmr[2] * (rx - 1L - lx);
        if (ua > *uOut) *uOut = ua;
        if (ub > *uOut) *uOut = ub;
        if (va > *vOut) *vOut = va;
        if (vb > *vOut) *vOut = vb;
    }
}

int
main(void)
{
    unsigned long trial;
    unsigned long agreed = 0UL, checked = 0UL, accepted = 0UL, boundary = 0UL;
    unsigned long perspOK = 0UL, magDiff = 0UL;

    lim.pitchBytes = 1024UL * 4UL;
    lim.clipX1 = 255UL; lim.clipY1 = 63UL;
    lim.colourStart = 4UL * 1024UL * 1024UL;
    lim.colourEnd   = 7UL * 1024UL * 1024UL;
    lim.depthStart  = lim.colourStart; lim.depthEnd = lim.colourEnd;
    lim.texStart    = lim.colourStart; lim.texEnd   = lim.colourEnd;
    lim.batchBytes  = OSMGA_HW3D_BATCH_BYTES;
    lim.maxEdgeWalk = OSMGA_HW3D_EDGE_WALK;

    printf("asking for the reach must change nothing, and must be exact\n\n");

    for (trial = 0UL; trial < 40000UL; trial++) {
        OSMGAHW3DTri *t;
        OSMGAHW3DTexReach r;
        unsigned long bad1 = 0UL, bad2 = 0UL;
        unsigned long h, x0, w;
        int v1, v2, any;
        long ou, ov, ou2, ov2, osig, vsig;
        long band;

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
        b.state.texW = 64UL; b.state.texH = 64UL; b.state.texPitch = 64UL;
        b.state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
        b.state.texFlags = 0UL;

        h  = 1UL + rnd(48UL);
        x0 = rnd(180UL);
        w  = 1UL + rnd(70UL);
        t = &b.tri[0];
        t->dwgctl = 0x0006UL | 0x0070UL;
        t->y = (long)rnd(lim.clipY1 + 1UL - h);
        t->h = (long)h;
        t->fxbndry = ((x0 + w) << 16) | x0;
        t->ar0 = (long)(1UL + rnd(8UL));
        t->ar6 = (long)(1UL + rnd(8UL));
        t->ar1 = (long)rnd((unsigned long)t->ar0);
        t->ar4 = (long)rnd((unsigned long)t->ar6);
        t->ar2 = -(long)rnd((unsigned long)t->ar0 + 1UL);
        t->ar5 = -(long)rnd((unsigned long)t->ar6 + 1UL);
        t->sgn = (long)((rnd(2UL) ? 0x2UL : 0UL) | (rnd(2UL) ? 0x20UL : 0UL));
        t->dr[0] = 200UL << 15;

        /*
         * Gradients small enough that most shapes are accepted -- a test that
         * mostly measures refusals would compare two refusals and prove
         * nothing -- and both signs, since a negative one puts the maximum at
         * the other end of the row.
         */
        b.state.tmr[0] = (long)rnd(20000UL) - 10000L;
        b.state.tmr[1] = (long)rnd(20000UL) - 10000L;
        b.state.tmr[2] = (long)rnd(20000UL) - 10000L;
        b.state.tmr[3] = (long)rnd(20000UL) - 10000L;

        /*
         * Put the start where the maximum will land near a band edge: that
         * is where an inexact reach picks a different addend, and a test that
         * never goes near one would pass on a reach that is merely close.
         */
        band = (long)(1UL << (19U + (unsigned)rnd(4UL)));
        b.state.tmr[6] = band - (long)rnd(4096UL) + (long)rnd(2048UL);
        b.state.tmr[7] = band - (long)rnd(4096UL) + (long)rnd(2048UL);
        /*
         * A third of them are pushed below nought instead of being clamped to
         * it.  The clamp was hiding the whole question this file now has to
         * answer: the reach used to be a signed maximum and is now a
         * magnitude, and a batch whose numerators never go positive is
         * exactly where the two could part company.  With every start clamped
         * non-negative such a batch could not occur.
         */
        if ((trial % 3UL) == 0UL) {
            b.state.tmr[6] = -(long)rnd(4096UL);
            b.state.tmr[7] = -(long)rnd(4096UL);
            /*
             * And with no gradient for half of those, so the numerators stay
             * negative at every pixel.  With a gradient they climb back above
             * nought and the signed reach sees the same maximum the magnitude
             * does -- which made the comparison below vacuous the first time
             * this ran: not one batch in forty thousand had the two differ.
             */
            if ((trial % 6UL) == 0UL) {
                b.state.tmr[0] = 0L; b.state.tmr[1] = 0L;
                b.state.tmr[2] = 0L; b.state.tmr[3] = 0L;
            }
        } else {
            if (b.state.tmr[6] < 0L) b.state.tmr[6] = 0L;
            if (b.state.tmr[7] < 0L) b.state.tmr[7] = 0L;
        }

        /*
         * Half of them in perspective.  Forcing the row walk there is new --
         * the box shortcut used to end it -- and the reason for forcing it is
         * that the reach holds NUMERATORS, which is what the engine picks its
         * band from.  The box check is sound for perspective too (p/q <= m is
         * the affine p - m*q <= 0), so no verdict should move; this is here to
         * find the implementation disagreeing with that arithmetic, not to
         * re-derive it.
         */
        if ((trial & 1UL) != 0UL) {
            b.state.texFlags |= OSMGA_HW3D_TEXF_PERSP;
            b.state.tmr[8] = (long)(OSMGA_HW3D_Q_ONE
                                    + (long)rnd(1UL << 20));
            b.state.tmr[4] = (long)rnd(4096UL) - 2048L;
            b.state.tmr[5] = (long)rnd(4096UL) - 2048L;
        }

        memcpy(&b.tri[1], &b.tri[0], sizeof b.tri[0]);   /* unused; keeps h */

        v1 = osmgaHW3DValidate(&b, &lim, &bad1);
        memset(&r, 0xEE, sizeof r);            /* poisoned, so a miss shows */
        v2 = osmgaHW3DValidateReach(&b, &lim, &bad2, &r);
        checked++;
        if (v1 != v2 || bad1 != bad2) {
            if (failures < 5)
                printf("   FAIL  verdict moved: %d/%lu vs %d/%lu"
                       " (trial %lu)\n", v1, bad1, v2, bad2, trial);
            failures++;
            continue;
        }
        agreed++;
        if (v1 != OSMGA_HW3D_OK)
            continue;
        accepted++;
        if ((b.state.texFlags & OSMGA_HW3D_TEXF_PERSP) != 0UL) perspOK++;
        oracle(t, &ou, &ov, &any);
        signedOracle(t, &osig, &vsig);
        ou2 = ou; ov2 = ov;
        if (r.uMax != ou || r.vMax != ov) {
            if (failures < 5)
                printf("   FAIL  reach u %ld want %ld, v %ld want %ld"
                       " (trial %lu)\n", r.uMax, ou, r.vMax, ov, trial);
            failures++;
            continue;
        }
        if (osmgaHW3DTexBiasFor(r.uMax) != OSMGA_HW3D_TEX_BIAS ||
            osmgaHW3DTexBiasFor(r.vMax) != OSMGA_HW3D_TEX_BIAS)
            boundary++;
        /*
         * The property the magnitude change exists for, and it has to hold
         * whatever the negative range is.
         *
         * It began life as "the magnitude picks the same bias as the signed
         * maximum would have", which was true while a negative numerator was
         * held to a quarter of a texel -- and it stopped being true the moment
         * the range was widened to a whole texture, which is exactly what the
         * change was preparing for.  So the assertion is the SAFETY property
         * rather than the neutrality one: the magnitude is never smaller than
         * the signed maximum, so the bias it picks is never LARGER, and a
         * bias that is never larger is a residual that is never negative.
         */
        {
            long su = (osig > 0L) ? osig : 0L;
            long sv = (vsig > 0L) ? vsig : 0L;

            if (r.uMax < su || r.vMax < sv ||
                osmgaHW3DTexBiasFor(r.uMax) > osmgaHW3DTexBiasFor(su) ||
                osmgaHW3DTexBiasFor(r.vMax) > osmgaHW3DTexBiasFor(sv)) {
                if (failures < 5)
                    printf("   FAIL  the magnitude reach is smaller, or its"
                           " bias larger: signed %ld/%ld -> %d/%d, magnitude"
                           " %ld/%ld -> %d/%d (trial %lu)\n",
                           su, sv, (int)osmgaHW3DTexBiasFor(su),
                           (int)osmgaHW3DTexBiasFor(sv),
                           r.uMax, r.vMax, (int)osmgaHW3DTexBiasFor(r.uMax),
                           (int)osmgaHW3DTexBiasFor(r.vMax), trial);
                failures++;
            }
            if (osmgaHW3DTexBiasFor(r.uMax) != osmgaHW3DTexBiasFor(su) ||
                osmgaHW3DTexBiasFor(r.vMax) != osmgaHW3DTexBiasFor(sv))
                magDiff++;
        }
    }

    printf("   %lu batches, %lu verdicts identical, %lu accepted\n",
           checked, agreed, accepted);
    printf("   %lu of the accepted reach past 2^20, so the bias really moves\n",
           boundary);
    printf("   %lu of the accepted were perspective\n", perspOK);
    printf("   %lu chose a SMALLER bias than the signed reach would have,\n"
           "   which is the widened negative range doing what it is for\n",
           magDiff);
    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
