/*
 * test-mesa-trapezoid.c -- does what the builder hands the engine actually
 * cover what OpenGL says it should?
 *
 * The coverage measurement (M1_4A2_COVERAGE_RESULT.md) scored both paths
 * against the rule and found the software rasteriser exact and the engine
 * out by up to two pixels at every run end, dropping a whole row on the
 * shape that splits.  That was measured on hardware.  This checks the same
 * thing on the host, where it is cheap enough to ask it of thousands of
 * triangles instead of seven.
 *
 * Nothing here trusts the software rasteriser.  The rule is written out
 * below in integers -- sample the pixel centre, break ties top-left -- and
 * the builder's registers are unpacked and walked with the recurrence that
 * was confirmed on hardware over 20 rows in D3-2 and then reproduced what
 * the engine drew on 139 rows of 139.  Both sides are independent of Mesa.
 *
 *   cc -O -Wall -I../hw3d -o /tmp/trap test-mesa-trapezoid.c \
 *      OpenStepMGAMesaTriangle.c
 *
 * Written before the fix, and it is meant to fail until the fix lands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "OpenStepMGAMesaTriangle.h"

#define W  320
#define H  240

/* ---------------------------------------------------------------- the rule
 *
 * A pixel is covered when its centre is inside; a centre exactly on an edge
 * belongs to the primitive only if that edge is a top or a left one.
 * Doubling the sample point clears the halves, so this is all integers.
 */
static int
ruleSpan(long ax, long ay, long bx, long by, long cx, long cy,
         long y, long *lo, long *hi)
{
    long x, e0, e1, e2, tx, ty, x0b, x1b;
    int got = 0;

    /* Outside the triangle's own rows there is nothing to find, and the
     * columns outside its own span cost 76800 edge tests a triangle on a
     * machine that does not have them to spare. */
    ty = (ay < by) ? ay : by; if (cy < ty) ty = cy;
    tx = (ay > by) ? ay : by; if (cy > tx) tx = cy;
    if (y < ty || y > tx) return 0;
    x0b = (ax < bx) ? ax : bx; if (cx < x0b) x0b = cx;
    x1b = (ax > bx) ? ax : bx; if (cx > x1b) x1b = cx;
    if (x0b < 0L) x0b = 0L;
    if (x1b > (long)W - 1L) x1b = (long)W - 1L;

    if ((bx - ax) * (cy - ay) - (by - ay) * (cx - ax) < 0L) {
        tx = bx; ty = by; bx = cx; by = cy; cx = tx; cy = ty;
    }
    for (x = x0b; x <= x1b; x++) {
        long px = 2L * x + 1L, py = 2L * y + 1L;
        int in = 1;

#define EDGE(x0, y0, x1, y1, ev)                                       \
        do {                                                           \
            (ev) = ((x1) - (x0)) * (py - 2L * (y0))                    \
                 - ((y1) - (y0)) * (px - 2L * (x0));                   \
            if ((ev) < 0L) in = 0;                                     \
            else if ((ev) == 0L) {                                     \
                long dx = (x1) - (x0), dy = (y1) - (y0);               \
                if (!((dy == 0L && dx < 0L) || dy < 0L)) in = 0;        \
            }                                                          \
        } while (0)

        EDGE(ax, ay, bx, by, e0);
        if (in) EDGE(bx, by, cx, cy, e1);
        if (in) EDGE(cx, cy, ax, ay, e2);
#undef EDGE
        if (in) {
            if (!got) { *lo = x; got = 1; }
            *hi = x;
        }
    }
    return got;
}

/* ------------------------------------------------------- the engine's walk
 *
 *     a = AR1 - AR2 ;  emit row 0 at FXBNDRY
 *     between rows:  a += AR2 ;  while a < 0:  x += sgn ;  a += AR0
 *
 * Written in the registers themselves and as the loop rather than a closed
 * form, because the closed form is where I got it wrong once, and the
 * recurrence is where I got it wrong twice: the first version came from a fit
 * taken entirely with AR1 equal to AR2, which cannot tell this rule from the
 * one it replaced.  Measured on hardware three ways round before being
 * written here.
 */
static void
walk(long x0, long ar0, long ar1, long ar2, long sgn, long n, long *out)
{
    long a = ar1 - ar2, x = x0, k;

    for (k = 0; k < n; k++) {
        if (k > 0L) {
            a += ar2;
            while (a < 0L) { x += sgn; a += ar0; }
        }
        out[k] = x;
    }
}

/* Unpack one trapezoid into the two edges as the engine reads them. */
static void
spansOf(const OSMGAHW3DTri *t, long *L, long *R)
{
    long sgnL = (t->sgn & 0x2L) ? -1L : 1L;
    long sgnR = (t->sgn & 0x20L) ? -1L : 1L;
    long left  = (long)(t->fxbndry & 0xFFFFUL);
    long right = (long)((t->fxbndry >> 16) & 0xFFFFUL);

    walk(left,  t->ar0, t->ar1, t->ar2, sgnL, t->h, L);
    walk(right, t->ar6, t->ar4, t->ar5, sgnR, t->h, R);
}

/*
 * And the kernel has to accept what the builder writes.  Covering the rule is
 * only half of it: the validator used to demand that the edge divisor equal
 * the trapezoid's height, which is exactly what this encoding stopped doing,
 * so a builder that draws correctly and is refused has fixed nothing.
 */
static OSMGAHW3DLimits vlim;
static OSMGAHW3DBatch  vbatch;

static void
limitsFor(long w, long h)
{
    memset(&vlim, 0, sizeof vlim);
    vlim.clipX1 = (unsigned long)w - 1UL;
    vlim.clipY1 = (unsigned long)h - 1UL;
    vlim.pitchBytes = (unsigned long)w * 4UL;
    vlim.colourStart = 4UL * 1024UL * 1024UL;
    vlim.colourEnd   = vlim.colourStart + (unsigned long)(w * h) * 4UL;
    vlim.depthStart  = vlim.colourEnd;
    vlim.depthEnd    = vlim.depthStart + (unsigned long)(w * h) * 2UL;
    vlim.texStart    = vlim.depthEnd;
    vlim.texEnd      = vlim.texStart + 1024UL * 1024UL;
    vlim.batchBytes  = OSMGA_HW3D_BATCH_BYTES;
    vlim.maxEdgeWalk = 16384UL;
}

static int
validateThese(const OSMGAHW3DTri *tri, long n, unsigned long *badTri)
{
    long k;

    memset(&vbatch, 0, sizeof vbatch);
    vbatch.magic = OSMGA_HW3D_MAGIC;
    vbatch.version = OSMGA_HW3D_VERSION;
    vbatch.triCount = (unsigned long)n;
    vbatch.state.dstorg = vlim.colourStart;
    vbatch.state.dstPitch = vlim.pitchBytes / 4UL;
    vbatch.state.dstWidth = vlim.clipX1 + 1UL;
    vbatch.state.dstHeight = vlim.clipY1 + 1UL;
    vbatch.state.zorg = vlim.depthStart;
    vbatch.state.texorg = vlim.texStart;
    vbatch.state.texW = 64; vbatch.state.texH = 64; vbatch.state.texPitch = 64;
    vbatch.state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
    vbatch.state.tmr[0] = 0x4000; vbatch.state.tmr[3] = 0x4000;
    for (k = 0; k < n; k++) vbatch.tri[k] = tri[k];
    return osmgaHW3DValidate(&vbatch, &vlim, badTri);
}

static unsigned long seed = 20260821UL;
static long
pick(long n)
{
    seed = seed * 1103515245UL + 12345UL;
    return (long)((seed >> 16) % (unsigned long)n);
}

/* The builder takes 1/256-pixel coordinates now; this test speaks whole
 * pixels, so it scales.  Integer input through the fixed-point path is
 * exactly the case that must not have changed. */
static void
vert(OSMGAMesaVertex *v, long x, long y)
{
    memset(v, 0, sizeof *v);
    v->x = x * OSMGA_MESA_SUBONE; v->y = y * OSMGA_MESA_SUBONE;
    v->g = 255UL; v->a = 255UL;
}

int
main(int argc, char **argv)
{
    long rounds = (argc > 1) ? atol(argv[1]) : 400L;
    long i, badTri = 0, badRow = 0, totRow = 0, unsupported = 0;
    long shown = 0, shownV = 0, refused = 0;

    /*
     * Random integer vertices almost never share a y, so the shapes that
     * matter most would never appear: the flat-bottomed one is the control
     * that has no split of its own, and it is the shape the hardware
     * measurement actually used.  Collinear, one row tall, and a sliver two
     * columns wide are the other places an edge walk goes wrong.  They are
     * listed rather than hoped for.
     */
    static const long fixed[][6] = {
        {  40,40, 200,40, 120,180 },   /* flat bottom -- one trapezoid */
        { 120,180, 200,40,  40,40 },   /* the same, wound the other way */
        {  40,40, 200,90, 120,180 },   /* splits at the middle row */
        {  40,40, 120,180, 200,180 },  /* flat top */
        {  10,10,  60,60, 110,110 },   /* collinear: no area */
        {  10,10,  10,10, 110,110 },   /* two vertices the same */
        {  30,30, 200,31,  40,32 },    /* two rows, nearly flat */
        {  50,20,  51,200, 52,60 },    /* a sliver two columns wide */
        {   0,0,  319,0,    0,239 },   /* the whole surface corner */
        { 300,10, 319,239,  0,120 },   /* spans the width */
        { 265,10, 313,152, 146,139 }   /* the depth round's shape 4 */
    };
    long nfixed = (long)(sizeof fixed / sizeof fixed[0]);

    limitsFor(W, H);
    printf("builder output vs the OpenGL rule, %ld random plus a fixed corpus\n\n",
           rounds);

    for (i = -nfixed; i < rounds; i++) {
        OSMGAMesaVertex a, b, c;
        OSMGAHW3DTri tri[2];
        long L[H], R[H];
        long y, n, t, off = 0;
        int thisBad = 0;

        if (i < 0L) {
            const long *f = fixed[nfixed + i];

            vert(&a, f[0], f[1]); vert(&b, f[2], f[3]); vert(&c, f[4], f[5]);
        } else {
            vert(&a, pick(W), pick(H));
            vert(&b, pick(W), pick(H));
            vert(&c, pick(W), pick(H));
        }

        n = (long)OSMGAMesaBuildTriangle(&a, &b, &c, &a,
                                         OSMGA_MESA_ZMODE_NONE,
                                         OSMGA_MESA_BLEND_OPAQUE, tri);
        if (n < 0) { unsupported++; continue; }

        /* what the builder would put on the screen, row by row */
        {
            static long gotLo[H], gotHi[H];
            static char gotAny[H];

            memset(gotAny, 0, sizeof gotAny);
            for (t = 0; t < n; t++) {
                spansOf(&tri[t], L, R);
                for (y = 0; y < tri[t].h; y++) {
                    long row = tri[t].y + y;
                    if (row < 0 || row >= H) continue;
                    if (L[y] <= R[y] - 1L) {
                        gotAny[row] = 1;
                        gotLo[row] = L[y];
                        gotHi[row] = R[y] - 1L;
                    }
                }
            }
            for (y = 0; y < H; y++) {
                long rlo = 0, rhi = 0;
                /* the rule speaks whole pixels; the vertices are 1/256 now */
                int want = ruleSpan(a.x / OSMGA_MESA_SUBONE, a.y / OSMGA_MESA_SUBONE,
                                    b.x / OSMGA_MESA_SUBONE, b.y / OSMGA_MESA_SUBONE,
                                    c.x / OSMGA_MESA_SUBONE, c.y / OSMGA_MESA_SUBONE,
                                    y, &rlo, &rhi);

                totRow++;
                if (want && gotAny[y] && rlo == gotLo[y] && rhi == gotHi[y])
                    continue;
                if (!want && !gotAny[y])
                    continue;
                badRow++; thisBad = 1;
                if (shown < 8) {
                    printf("   (%ld,%ld) (%ld,%ld) (%ld,%ld)  row %ld: "
                           "rule %s%ld..%ld%s  builder %s%ld..%ld%s\n",
                           a.x / OSMGA_MESA_SUBONE, a.y / OSMGA_MESA_SUBONE,
                           b.x / OSMGA_MESA_SUBONE, b.y / OSMGA_MESA_SUBONE,
                           c.x / OSMGA_MESA_SUBONE, c.y / OSMGA_MESA_SUBONE, y,
                           want ? "" : "(", rlo, rhi, want ? "" : ")",
                           gotAny[y] ? "" : "(", gotLo[y], gotHi[y],
                           gotAny[y] ? "" : ")");
                    shown++;
                }
            }
        }
        (void)off;
        if (n > 0) {
            unsigned long bt = 0;
            int v = validateThese(tri, n, &bt);

            if (v != OSMGA_HW3D_OK) {
                refused++;
                if (shownV < 6) {
                    printf("   REFUSED %d: (%ld,%ld) (%ld,%ld) (%ld,%ld) "
                           "trap %lu  h=%ld ar0=%ld ar6=%ld\n",
                           v, a.x, a.y, b.x, b.y, c.x, c.y, bt,
                           tri[bt].h, tri[bt].ar0, tri[bt].ar6);
                    shownV++;
                }
            }
        }
        /*
         * Say what the fixed corpus actually exercised.  A degenerate shape
         * and an empty rule agree for free, and a corpus that passes for
         * free is not a corpus.
         */
        if (i < 0L) {
            long rows = 0, y2;

            for (y2 = 0; y2 < H; y2++) {
                long q0 = 0, q1 = 0;
                if (ruleSpan(a.x / OSMGA_MESA_SUBONE, a.y / OSMGA_MESA_SUBONE,
                             b.x / OSMGA_MESA_SUBONE, b.y / OSMGA_MESA_SUBONE,
                             c.x / OSMGA_MESA_SUBONE, c.y / OSMGA_MESA_SUBONE,
                             y2, &q0, &q1)) rows++;
            }
            printf("   corpus (%3ld,%3ld) (%3ld,%3ld) (%3ld,%3ld): "
                   "%ld trapezoid(s), %ld rows in the rule%s\n",
                   a.x / OSMGA_MESA_SUBONE, a.y / OSMGA_MESA_SUBONE,
                   b.x / OSMGA_MESA_SUBONE, b.y / OSMGA_MESA_SUBONE,
                   c.x / OSMGA_MESA_SUBONE, c.y / OSMGA_MESA_SUBONE, n, rows,
                   thisBad ? "   <-- DIFFERS" : "");
        }
        if (thisBad) badTri++;
    }

    printf("\n   triangles that differ anywhere : %ld of %ld\n",
           badTri, rounds + nfixed);
    printf("   rows that differ               : %ld of %ld\n", badRow, totRow);
    printf("   refused as unsupported         : %ld\n", unsupported);
    printf("   refused by the kernel validator: %ld\n", refused);
    printf("\n%s\n", (badTri == 0 && refused == 0)
           ? "the builder covers the rule exactly and the kernel takes it"
           : "the builder does NOT cover the rule, or the kernel refuses it");
    return (badTri == 0 && refused == 0) ? 0 : 1;
}
