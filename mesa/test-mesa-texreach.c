/*
 * What the validator reports as a batch's texture reach, against what the
 * batch's pixels actually reach.
 *
 * The reach decides which addend the encoder takes off, and taking off the
 * wrong one moves the phase of every pixel in the primitive.  The validator
 * gets its reach from the primitive's bounding BOX, and its own comments
 * warn that a primitive is not its rectangle -- so the question is how far
 * the box overshoots, and in particular whether it can carry a coordinate
 * that really stops at 2^20 across into the band above it.
 *
 * The oracle here does not share the validator's arithmetic: it walks the
 * trapezoid the way the engine does, a row at a time, and takes the largest
 * coordinate at the two ends of each row's span.
 *
 *   cc -O -Wall -I../hw3d -o /tmp/tr test-mesa-texreach.c \
 *      OpenStepMGAMesaTriangle.c ../hw3d/OpenStepMGAHW3D.c
 */
#include <stdio.h>
#include <string.h>
#include "OpenStepMGAMesaTriangle.h"

#define SUB (double)OSMGA_MESA_SUBONE

static OSMGAHW3DLimits lim;

static void
vert(OSMGAMesaVertex *v, double x, double y, double s, double t)
{
    memset(v, 0, sizeof *v);
    /* A zeroed vertex has neither a w nor a texture q, and the
     * builder divides by both.  One each is what "no perspective and
     * no projective texture" means. */
    v->qw = 1.0;
    v->tq = 1.0;
    v->x = (long)(x * SUB + 0.5);
    v->y = (long)(y * SUB + 0.5);
    v->r = v->g = v->b = 255UL; v->a = 255UL;
    v->s = s; v->tc = t;
}

/*
 * The largest u and v over the pixels the primitive really covers, by
 * walking it exactly as osmgaHW3DStep does.
 */
static void
trueReach(const OSMGAHW3DBatch *b, const OSMGAHW3DTri *t, long *uOut, long *vOut)
{
    long lx = (long)(t->fxbndry & 0xFFFFUL);
    long rx = (long)((t->fxbndry >> 16) & 0xFFFFUL);
    long lx0 = lx;
    long lacc = t->ar1 - t->ar2, racc = t->ar4 - t->ar5;
    /* the same bits the validator reads: 0x2 and 0x20, not 1 and 4 */
    long lsgn = (t->sgn & 0x2L)  ? -1L : 1L;
    long rsgn = (t->sgn & 0x20L) ? -1L : 1L;
    long row;

    *uOut = *vOut = 0L;
    for (row = 0L; row < t->h; row++) {
        long u0, u1, v0, v1;

        if (row > 0L) {
            lacc += t->ar2;
            while (lacc < 0L) { lx += lsgn; lacc += t->ar0; }
            racc += t->ar5;
            while (racc < 0L) { rx += rsgn; racc += t->ar6; }
        }
        if (lx >= rx) continue;
        u0 = t->tu0 + b->state.tmr[0] * (lx - lx0)
             + b->state.tmr[1] * row;
        u1 = u0 + b->state.tmr[0] * (rx - 1L - lx);
        v0 = t->tv0 + b->state.tmr[2] * (lx - lx0)
             + b->state.tmr[3] * row;
        v1 = v0 + b->state.tmr[2] * (rx - 1L - lx);
        if (u0 > *uOut) *uOut = u0;
        if (u1 > *uOut) *uOut = u1;
        if (v0 > *vOut) *vOut = v0;
        if (v1 > *vOut) *vOut = v1;
    }
}

static void
one(const char *name, double x0, double y0, double x1, double y1,
    double smax, unsigned long tw, unsigned long th)
{
    OSMGAMesaVertex a, b2, c;
    OSMGAHW3DTri out[4];
    OSMGAMesaTex tex;
    long tmr[4][9];
    int n, i, k;
    static const double vx[2][3] = { { 0.0, 1.0, 1.0 }, { 0.0, 1.0, 0.0 } };
    static const double vy[2][3] = { { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 1.0 } };

    printf("\n%s   quad %.0f,%.0f .. %.0f,%.0f   s,t 0..%.0f   texture %lu x %lu\n",
           name, x0, y0, x1, y1, smax, tw, th);
    for (k = 0; k < 2; k++) {
        vert(&a,  x0 + (x1 - x0) * vx[k][0], y0 + (y1 - y0) * vy[k][0],
             smax * vx[k][0], smax * vy[k][0]);
        vert(&b2, x0 + (x1 - x0) * vx[k][1], y0 + (y1 - y0) * vy[k][1],
             smax * vx[k][1], smax * vy[k][1]);
        vert(&c,  x0 + (x1 - x0) * vx[k][2], y0 + (y1 - y0) * vy[k][2],
             smax * vx[k][2], smax * vy[k][2]);
        tex.w = tw; tex.h = th;
        memset(tmr, 0, sizeof tmr);
        n = OSMGAMesaBuildTriangleTex(&a, &b2, &c, (const OSMGAMesaVertex *)0,
                                      OSMGA_MESA_ZMODE_NONE,
                                      OSMGA_MESA_BLEND_OPAQUE, &tex,
                                      0.0 /* no polygon offset */,
                                      out, tmr);
        if (n < 0) { printf("   triangle %d refused %d\n", k, n); continue; }
        for (i = 0; i < n; i++) {
            static OSMGAHW3DBatch batch;
            OSMGAHW3DTexReach reach;
            unsigned long badTri = 0UL;
            long tu, tv;
            int v;

            memset(&batch, 0, sizeof batch);
            batch.magic = OSMGA_HW3D_MAGIC;
            batch.version = OSMGA_HW3D_VERSION;
            batch.triCount = 1UL;
            batch.state.dstorg = lim.colourStart;
            batch.state.dstWidth = lim.clipX1 + 1UL;
            batch.state.dstHeight = lim.clipY1 + 1UL;
            batch.state.dstPitch = lim.pitchBytes / 4UL;
            batch.state.texorg = lim.texStart;
            batch.state.texW = tw; batch.state.texH = th;
            batch.state.texPitch = tw;
            batch.state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
            batch.state.texFlags = OSMGA_HW3D_TEXF_REPEATU
                                 | OSMGA_HW3D_TEXF_REPEATV;
            batch.state.tmr[0] = tmr[i][0]; batch.state.tmr[1] = tmr[i][1];
            batch.state.tmr[2] = tmr[i][2]; batch.state.tmr[3] = tmr[i][3];
            batch.tri[0] = out[i];

            v = osmgaHW3DValidateReach(&batch, &lim, &badTri, &reach,
                                (OSMGAHW3DTexBand *)0);
            trueReach(&batch, &out[i], &tu, &tv);
            printf("   tri %d.%d  v=%d   reach u %9ld  oracle u %9ld"
                   "   bias %3ld vs %3ld %s\n",
                   k, i, v, reach.uMax, tu,
                   osmgaHW3DTexBiasFor(reach.uMax),
                   osmgaHW3DTexBiasFor(tu),
                   (osmgaHW3DTexBiasFor(reach.uMax)
                      != osmgaHW3DTexBiasFor(tu)) ? "  <-- DIFFERENT BAND" : "");
            printf("            "
                   "        reach v %9ld  oracle v %9ld   bias %3ld vs %3ld %s\n",
                   reach.vMax, tv,
                   osmgaHW3DTexBiasFor(reach.vMax),
                   osmgaHW3DTexBiasFor(tv),
                   (osmgaHW3DTexBiasFor(reach.vMax)
                      != osmgaHW3DTexBiasFor(tv)) ? "  <-- DIFFERENT BAND" : "");
        }
    }
}

int
main(void)
{
    lim.pitchBytes = 1024UL * 4UL;
    lim.clipX1 = 319UL; lim.clipY1 = 239UL;
    lim.colourStart = 4UL * 1024UL * 1024UL;
    lim.colourEnd   = 7UL * 1024UL * 1024UL;
    lim.depthStart  = lim.colourStart; lim.depthEnd = lim.colourEnd;
    lim.texStart    = lim.colourStart; lim.texEnd   = lim.colourEnd;
    lim.batchBytes  = OSMGA_HW3D_BATCH_BYTES;
    lim.maxEdgeWalk = OSMGA_HW3D_EDGE_WALK;

    printf("the validator's reach against an independent walk of the same shape\n");
    /*
     * The plain dump draws this alongside the quad.  It is a general
     * triangle, so its bounding box has corners the triangle does not cover
     * -- which is the case the validator's own comments warn about, and the
     * one the quad cannot show because a right triangle's box corner is a
     * vertex.
     */
    {
        OSMGAMesaVertex a, b2, c;
        OSMGAHW3DTri out[4];
        OSMGAMesaTex tex;
        long tmr[4][9];
        int n, i;

        printf("\n   the split triangle the plain dump also draws\n");
        vert(&a,  180.0,  30.0, 0.0, 0.0);
        vert(&b2, 300.0,  70.0, 1.0, 0.2);
        vert(&c,  210.0, 200.0, 0.3, 1.0);
        tex.w = 16UL; tex.h = 16UL;
        memset(tmr, 0, sizeof tmr);
        n = OSMGAMesaBuildTriangleTex(&a, &b2, &c, (const OSMGAMesaVertex *)0,
                                      OSMGA_MESA_ZMODE_NONE,
                                      OSMGA_MESA_BLEND_OPAQUE, &tex,
                                      0.0 /* no polygon offset */,
                                      out, tmr);
        for (i = 0; i < n; i++) {
            static OSMGAHW3DBatch batch;
            OSMGAHW3DTexReach reach;
            unsigned long badTri = 0UL;
            long tu, tv;
            int v;

            memset(&batch, 0, sizeof batch);
            batch.magic = OSMGA_HW3D_MAGIC;
            batch.version = OSMGA_HW3D_VERSION;
            batch.triCount = 1UL;
            batch.state.dstorg = lim.colourStart;
            batch.state.dstWidth = lim.clipX1 + 1UL;
            batch.state.dstHeight = lim.clipY1 + 1UL;
            batch.state.dstPitch = lim.pitchBytes / 4UL;
            batch.state.texorg = lim.texStart;
            batch.state.texW = 16UL; batch.state.texH = 16UL;
            batch.state.texPitch = 16UL;
            batch.state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
            batch.state.tmr[0] = tmr[i][0]; batch.state.tmr[1] = tmr[i][1];
            batch.state.tmr[2] = tmr[i][2]; batch.state.tmr[3] = tmr[i][3];
            batch.tri[0] = out[i];
            v = osmgaHW3DValidateReach(&batch, &lim, &badTri, &reach,
                                (OSMGAHW3DTexBand *)0);
            trueReach(&batch, &out[i], &tu, &tv);
            printf("   part %d  v=%d  reach u %9ld oracle u %9ld  bias %3ld/%3ld %s\n",
                   i, v, reach.uMax, tu, osmgaHW3DTexBiasFor(reach.uMax),
                   osmgaHW3DTexBiasFor(tu),
                   (osmgaHW3DTexBiasFor(reach.uMax) != osmgaHW3DTexBiasFor(tu))
                       ? "  <-- DIFFERENT BAND" : "");
            printf("            reach v %9ld oracle v %9ld  bias %3ld/%3ld %s\n",
                   reach.vMax, tv, osmgaHW3DTexBiasFor(reach.vMax),
                   osmgaHW3DTexBiasFor(tv),
                   (osmgaHW3DTexBiasFor(reach.vMax) != osmgaHW3DTexBiasFor(tv))
                       ? "  <-- DIFFERENT BAND" : "");
        }
    }
    one("the ordinary scene", 40.0, 40.0, 168.0, 168.0, 1.0, 16UL, 16UL);
    one("the tiling scene",   40.0, 40.0, 168.0, 168.0, 3.0, 16UL, 16UL);
    one("the boundary scene", 40.0, 40.0,  96.0,  72.0, 7.0, 16UL, 16UL);
    return 0;
}
