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
 * acc = AR1 - 1 ; each row: acc += |dx| ; while acc >= 0: x += sgn, acc -= dy
 * Written as the loop rather than a closed form, because the closed form is
 * where I got it wrong once already.
 */
static void
walk(long x0, long mag, long dy, long e, long sgn, long n, long *out)
{
    long acc = -mag - e - 1L, x = x0, k;

    for (k = 0; k < n; k++) {
        acc += mag;
        while (acc >= 0L) { x += sgn; acc -= dy; }
        out[k] = x;
    }
}

/* Unpack one trapezoid into the two edges as the engine reads them. */
static void
spansOf(const OSMGAHW3DTri *t, long *L, long *R)
{
    long magL = -t->ar2, magR = -t->ar5;
    long eL = -t->ar1 - magL, eR = -t->ar4 - magR;
    long sgnL = (t->sgn & 0x2L) ? -1L : 1L;
    long sgnR = (t->sgn & 0x20L) ? -1L : 1L;
    long left  = (long)(t->fxbndry & 0xFFFFUL);
    long right = (long)((t->fxbndry >> 16) & 0xFFFFUL);

    walk(left,  magL, t->ar0, eL, sgnL, t->h, L);
    walk(right, magR, t->ar6, eR, sgnR, t->h, R);
}

static unsigned long seed = 20260821UL;
static long
pick(long n)
{
    seed = seed * 1103515245UL + 12345UL;
    return (long)((seed >> 16) % (unsigned long)n);
}

static void
vert(OSMGAMesaVertex *v, long x, long y)
{
    memset(v, 0, sizeof *v);
    v->x = x; v->y = y; v->g = 255UL; v->a = 255UL;
}

int
main(int argc, char **argv)
{
    long rounds = (argc > 1) ? atol(argv[1]) : 400L;
    long i, badTri = 0, badRow = 0, totRow = 0, unsupported = 0;
    long shown = 0;

    printf("builder output vs the OpenGL rule, %ld triangles\n\n", rounds);

    for (i = 0; i < rounds; i++) {
        OSMGAMesaVertex a, b, c;
        OSMGAHW3DTri tri[2];
        long L[H], R[H];
        long y, n, t, off = 0;
        int thisBad = 0;

        vert(&a, pick(W), pick(H));
        vert(&b, pick(W), pick(H));
        vert(&c, pick(W), pick(H));

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
                int want = ruleSpan(a.x, a.y, b.x, b.y, c.x, c.y, y, &rlo, &rhi);

                totRow++;
                if (want && gotAny[y] && rlo == gotLo[y] && rhi == gotHi[y])
                    continue;
                if (!want && !gotAny[y])
                    continue;
                badRow++; thisBad = 1;
                if (shown < 8) {
                    printf("   (%ld,%ld) (%ld,%ld) (%ld,%ld)  row %ld: "
                           "rule %s%ld..%ld%s  builder %s%ld..%ld%s\n",
                           a.x, a.y, b.x, b.y, c.x, c.y, y,
                           want ? "" : "(", rlo, rhi, want ? "" : ")",
                           gotAny[y] ? "" : "(", gotLo[y], gotHi[y],
                           gotAny[y] ? "" : ")");
                    shown++;
                }
            }
        }
        (void)off;
        if (thisBad) badTri++;
    }

    printf("\n   triangles that differ anywhere : %ld of %ld\n", badTri, rounds);
    printf("   rows that differ               : %ld of %ld\n", badRow, totRow);
    printf("   refused as unsupported         : %ld\n", unsupported);
    printf("\n%s\n", badTri == 0
           ? "the builder covers exactly what the rule asks for"
           : "the builder does NOT cover what the rule asks for");
    return badTri == 0 ? 0 : 1;
}
