/*
 * Hunt a vertex triple the REAL builder turns into a trapezoid the REAL
 * validator refuses as TRICROSS -- so the narrowing test can use a frozen,
 * deterministic sliver instead of hoping one falls out of a scene.
 *
 * The documented natural case is a one-column sliver a few rows tall whose
 * independently rounded edges cross, so the hunt walks that neighbourhood:
 * near-vertical triangles about a pixel wide, several rows high, with
 * subpixel jitter on every coordinate.  Deterministic PRNG; the first few
 * finds are printed in 1/256 units so a test can carry them as constants.
 */
#include <stdio.h>
#include <string.h>
#include "OpenStepMGAMesaTriangle.h"

static unsigned long rs = 12345UL;
static unsigned long rnd(void) { rs = rs * 1103515245UL + 12345UL; return (rs >> 8) & 0xFFFFFFUL; }

static void
vert(OSMGAMesaVertex *v, long fx, long fy)          /* 1/256 pixel units */
{
    memset(v, 0, sizeof *v);
    v->qw = 1.0; v->tq = 1.0;
    v->x = fx; v->y = fy;
    v->r = v->g = v->b = 255UL; v->a = 255UL;
}

int
main(void)
{
    OSMGAHW3DLimits lim;
    OSMGAHW3DBatch batch;
    OSMGAHW3DTri out[4];
    OSMGAMesaVertex a, b, c;
    unsigned long badTri, tries, found = 0UL;
    int n, i, v;

    memset(&lim, 0, sizeof lim);
    lim.pitchBytes = 1024UL * 4UL;
    lim.clipX1 = 319UL; lim.clipY1 = 239UL;
    lim.colourStart = 4UL * 1024UL * 1024UL;
    lim.colourEnd   = 7UL * 1024UL * 1024UL;
    lim.depthStart  = lim.colourStart; lim.depthEnd = lim.colourEnd;
    lim.texStart    = lim.colourStart; lim.texEnd   = lim.colourEnd;
    lim.batchBytes  = OSMGA_HW3D_BATCH_BYTES;
    lim.maxEdgeWalk = OSMGA_HW3D_EDGE_WALK;

    for (tries = 0UL; tries < 6000000UL && found < 6UL; tries++) {
        /* A sliver is three NEARLY COLLINEAR points: a long thin line with
         * the middle vertex a hair off it.  Height up to ~120 px, total
         * lean up to ~4 px, and the middle vertex within a few 1/256
         * steps of the a-c line -- the neighbourhood the real teapot
         * slivers live in. */
        long ax = 2560L + (long)(rnd() % 65536UL);
        long ay = 2560L + (long)(rnd() % 32768UL);
        long h  = 512L  + (long)(rnd() % 30720UL);     /* 2..122 rows */
        long dx = (long)(rnd() % 2048UL) - 1024L;      /* lean +-4 px */
        long t  = (long)(rnd() % 1024UL);              /* b along a-c */
        long by = ay + (long)(((double)h * (double)t) / 1024.0);
        long bxl = ax + (long)(((double)dx * (double)t) / 1024.0);

        vert(&a, ax, ay);
        vert(&b, bxl + (long)(rnd() % 17UL) - 8L,      /* +-8/256 off */
                 by + (long)(rnd() % 17UL) - 8L);
        vert(&c, ax + dx, ay + h);

        n = OSMGAMesaBuildTriangle(&a, &b, &c, &a, OSMGA_MESA_ZMODE_NONE,
                                   0, 0UL, 0.0, out);
        if (n <= 0) continue;
        for (i = 0; i < n; i++) {
            memset(&batch, 0, sizeof batch);
            batch.magic = OSMGA_HW3D_MAGIC;
            batch.version = OSMGA_HW3D_VERSION;
            batch.triCount = 1UL;
            batch.state.dstorg = lim.colourStart;
            batch.state.dstWidth = lim.clipX1 + 1UL;
            batch.state.dstHeight = lim.clipY1 + 1UL;
            batch.state.dstPitch = lim.pitchBytes / 4UL;
            batch.tri[0] = out[i];
            v = osmgaHW3DValidate(&batch, &lim, &badTri);
            if (v == OSMGA_HW3D_E_TRICROSS) {
                printf("FOUND at try %lu, trapezoid %d of %d:\n", tries, i, n);
                printf("  a = (%ld, %ld)  b = (%ld, %ld)  c = (%ld, %ld)"
                       "   (1/256 px)\n",
                       a.x, a.y, b.x, b.y, c.x, c.y);
                printf("  as pixels: a=(%.4f,%.4f) b=(%.4f,%.4f) c=(%.4f,%.4f)\n",
                       a.x/256.0, a.y/256.0, b.x/256.0, b.y/256.0,
                       c.x/256.0, c.y/256.0);
                found++;
            }
        }
    }
    printf("%lu tries, %lu finds\n", tries, found);
    return found ? 0 : 1;
}
