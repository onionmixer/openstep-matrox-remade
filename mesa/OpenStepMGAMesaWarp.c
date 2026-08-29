/*
 * OpenStepMGAMesaWarp.c -- see the header.
 */
#include <string.h>
#include "OpenStepMGAMesaWarp.h"

/*
 * A double to an IEEE-754 single's bits.  The conversion is the thing that
 * has to be watched: a double well inside the ranges below can still land
 * on an infinity, and the kernel will only ever see the float.
 */
static int
f32bits(double d, osmga_u32 *out)
{
    float f = (float)d;
    unsigned int u;

    memcpy(&u, &f, sizeof u);
    *out = (osmga_u32)u;
    return osmgaHW3DF32Finite((unsigned long)u);
}

int
OSMGAMesaBuildWarpVertex(const OSMGAMesaVertex *v,
                         const OSMGAMesaTex *tex,
                         double zoffset,
                         OSMGAHW3DVertex *out)
{
    osmga_u32 w;
    double rhw;

    if (v == 0 || out == 0)
        return OSMGA_MESA_TRI_UNSUPPORTED;

    memset(out, 0, sizeof *out);

    /*
     * Screen position, less half a pixel.  The vertex carries 1/256 of a
     * pixel, so half of one is exactly 128 -- an integer subtraction
     * before the single conversion, and no new floating point anywhere.
     *
     * WHY, and what is and is not claimed.  Handed the vertices Mesa
     * computes, this tier draws the picture the rational oracle draws for
     * vertices at V + 1/2.  That was measured, not reasoned: the same 512
     * triangle mesh, the same process, the same surface, only the tier
     * differing -- trapezoid (0 gap, 0 extra, 0 misattributed) against
     * WARP (462, 463, 7707).  Rescoring that one observation against the
     * oracle over a joint 5x5 grid of offsets puts the minimum at exactly
     * (+1/2, +1/2) and nowhere else, at (0, 1, 50).
     *
     * So the claim is a COORDINATE CONTRACT and not a mechanism: this
     * tier behaves as though it sampled where the oracle samples V + 1/2,
     * and handing it V - 1/2 makes the two agree.  Corner sampling would
     * produce exactly this, and so would centre sampling with a bias
     * inside the microcode, or a setup origin half a pixel off; a
     * translation experiment cannot separate them and does not need to.
     *
     * The reference agrees on x to the digit.  mgavb.c:87 adds
     * xoffset = drawX + SUBPIXEL_X with SUBPIXEL_X = -0.5
     * (mgacontext.h:306), and W2's "the real remaining risk -- coordinate
     * convention" wrote that down a year ago as the FIRST thing to
     * suspect if pixels came out wrong.  They are at -0.375 in y, but
     * their y is also flipped (-win[1] + yoffset) and ours is not; ours
     * measures a clean half on both axes.
     *
     * What this is NOT resting on.  The comment here used to say no
     * half-pixel was needed because M4's T4b matched an oracle at all
     * 1176 pixels.  It did -- but that oracle indexes dx = col - TRI_LO,
     * an integer column, over vertices that are themselves on integers,
     * so it agrees with a corner sampler and a centre sampler alike.  T4b
     * measured the DIVIDE, which was its question; it never had the
     * resolution to place the sample point, and citing it for that was
     * over-reading it.
     *
     * The bound below is applied after the shift, so the window of raw
     * coordinates this accepts moves half a pixel with it: raw -8192*256
     * used to convert to -8192.0 and pass, and now converts to -8192.5
     * and is refused.  A refusal here is a decline, and a declined
     * triangle goes down the trapezoid path -- so the effect at the far
     * negative edge of a 16384 pixel coordinate space is a tier choice,
     * not a lost triangle.
     */
    if (!f32bits((double)(v->x - OSMGA_MESA_WARP_XY_BIAS) / 256.0, &w))
        return OSMGA_MESA_TRI_UNSUPPORTED;
    out->x = w;
    if (!osmgaHW3DF32AbsAtMost((unsigned long)w, OSMGA_HW3D_F32_COORD))
        return OSMGA_MESA_TRI_UNSUPPORTED;

    if (!f32bits((double)(v->y - OSMGA_MESA_WARP_XY_BIAS) / 256.0, &w))
        return OSMGA_MESA_TRI_UNSUPPORTED;
    out->y = w;
    if (!osmgaHW3DF32AbsAtMost((unsigned long)w, OSMGA_HW3D_F32_COORD))
        return OSMGA_MESA_TRI_UNSUPPORTED;

    /*
     * Depth, with glPolygonOffset folded in.  See the header: the scale is
     * measured rather than the reference's, and the offset arrives in
     * depth CODES while v->z is in 256ths of one, so it is multiplied by
     * 256 before the shared scale.
     *
     * The range check below is the refusal the trapezoid path makes: a
     * shifted plane that leaves [0,1] goes to software rather than being
     * clamped into a picture neither path draws.
     */
    if (!f32bits(((double)v->z + zoffset * 256.0) / OSMGA_MESA_WARP_ZSCALE,
                 &w))
        return OSMGA_MESA_TRI_UNSUPPORTED;
    out->z = w;
    if (!osmgaHW3DF32InUnit((unsigned long)w))
        return OSMGA_MESA_TRI_UNSUPPORTED;

    /*
     * The vertex weight.  The texture's own divisor is folded in here and
     * divided back out of the coordinates, which is what the reference
     * does (mgavb.c) and what a vertex with no room for a third texture
     * word requires.
     *
     * Strictly positive is not a nicety: the containment argument is that
     * perspective-corrected interpolation is a CONVEX combination of the
     * three vertex values, and that holds only while every weight is
     * positive.  The kernel checks it again.
     */
    if (!(v->qw > 0.0) || !(v->tq > 0.0))
        return OSMGA_MESA_TRI_UNSUPPORTED;
    rhw = v->qw * v->tq;
    if (!f32bits(rhw, &w))
        return OSMGA_MESA_TRI_UNSUPPORTED;
    out->rhw = w;
    if (!osmgaHW3DF32PosNormal((unsigned long)w) ||
        !osmgaHW3DF32Between((unsigned long)w, OSMGA_HW3D_F32_RHW_MIN,
                             OSMGA_HW3D_F32_RHW_MAX))
        return OSMGA_MESA_TRI_UNSUPPORTED;

    /* Colour, packed the way the engine reads a 32 bit pixel: alpha in the
     * top byte, then red, green, blue. */
    out->diffuse = (osmga_u32)(((v->a & 0xFFUL) << 24) |
                               ((v->r & 0xFFUL) << 16) |
                               ((v->g & 0xFFUL) <<  8) |
                                (v->b & 0xFFUL));
    out->specular = 0U;

    if (tex != 0) {
        if (!f32bits(v->s / v->tq, &w))
            return OSMGA_MESA_TRI_UNSUPPORTED;
        out->tu0 = w;
        if (!f32bits(v->tc / v->tq, &w))
            return OSMGA_MESA_TRI_UNSUPPORTED;
        out->tv0 = w;
    }

    return 0;
}

void
OSMGAMesaWarpReset(OSMGAMesaWarpBuilder *w, OSMGAHW3DWarpBatch *b)
{
    if (w == 0)
        return;
    w->b    = b;
    w->open = 0;
    if (b == 0)
        return;
    b->magic    = OSMGA_HW3D_MAGIC;
    b->version  = OSMGA_HW3D_VERSION_WARP;
    b->triCount = 0UL;            /* the other payload is not in use */
    b->runCount = 0U;
    b->vtxCount = 0U;
}

int
OSMGAMesaWarpAdd(OSMGAMesaWarpBuilder *w,
                 unsigned long dwgctl, unsigned long alphactrl,
                 const OSMGAHW3DVertex *v0,
                 const OSMGAHW3DVertex *v1,
                 const OSMGAHW3DVertex *v2)
{
    OSMGAHW3DWarpBatch *b;
    OSMGAHW3DRun *run;
    unsigned long n;
    int needRun;

    if (w == 0 || w->b == 0 || v0 == 0 || v1 == 0 || v2 == 0)
        return OSMGA_MESA_WARP_FULL;
    b = w->b;

    n = (unsigned long)b->vtxCount;
    if (n + 3UL > OSMGA_HW3D_MAX_VTX)
        return OSMGA_MESA_WARP_FULL;

    /*
     * A new run whenever the state moves -- and the state is exactly the
     * two words, because everything else a submission shares lives in
     * OSMGAHW3DState, which is singular for the whole batch.
     */
    needRun = 1;
    if (w->open != 0) {
        run = &b->run[(unsigned long)b->runCount - 1UL];
        if ((unsigned long)run->dwgctl == dwgctl &&
            (unsigned long)run->alphactrl == alphactrl)
            needRun = 0;
    }
    if (needRun) {
        if ((unsigned long)b->runCount >= OSMGA_HW3D_MAX_RUN)
            return OSMGA_MESA_WARP_FULL;
        run = &b->run[(unsigned long)b->runCount];
        run->dwgctl    = (osmga_u32)dwgctl;
        run->alphactrl = (osmga_u32)alphactrl;
        run->first     = (osmga_u32)n;   /* contiguous with the last run */
        run->count     = 0U;
        b->runCount    = (osmga_u32)((unsigned long)b->runCount + 1UL);
        w->open        = 1;
    } else {
        run = &b->run[(unsigned long)b->runCount - 1UL];
    }

    b->vtx[n + 0UL] = *v0;
    b->vtx[n + 1UL] = *v1;
    b->vtx[n + 2UL] = *v2;
    b->vtxCount = (osmga_u32)(n + 3UL);
    run->count  = (osmga_u32)((unsigned long)run->count + 3UL);
    return 0;
}
