/*
 * test-clipbox.c -- the integer clip box against the floating point one it
 * replaces.
 *
 * The driver computed this intersection in `double`, which contradicts the
 * rule the same file states -- "Kernel code must not touch the FPU"
 * (OpenStepMGAReplacementDisplay.m, the vertex builder's comment).  Those
 * four locals were the only floating point in the driver.
 *
 * The replacement is not argued to be equivalent; it is MEASURED against
 * the old expression here, on a host where doubles are allowed, across the
 * whole input space that can reach it -- including the negative and
 * enormous boxes a client is free to send, since the safety argument is
 * that the intersection can only narrow and never that the client's four
 * numbers are sensible.
 *
 * Hosted C89.
 */
#include <stdio.h>
#include "OpenStepMGAHW3D.h"

static int failures = 0;
static unsigned long compared = 0UL;

/*
 * The original, transcribed exactly: clamp in double so a wild box cannot
 * wrap on the way in, then take the half-open intersection.
 */
static int
oldWay(long sx, long sy, unsigned long sw, unsigned long sh,
       unsigned long dstW, unsigned long dstH,
       unsigned long *x0, unsigned long *x1,
       unsigned long *y0, unsigned long *y1)
{
    double lx = (double)sx;
    double ly = (double)sy;
    double hx = lx + (double)sw;
    double hy = ly + (double)sh;

    if (lx < 0.0) lx = 0.0;
    if (ly < 0.0) ly = 0.0;
    if (hx > (double)dstW) hx = (double)dstW;
    if (hy > (double)dstH) hy = (double)dstH;
    if (hx <= lx || hy <= ly)
        return 0;
    *x0 = (unsigned long)lx;
    *x1 = (unsigned long)hx;
    *y0 = (unsigned long)ly;
    *y1 = (unsigned long)hy;
    return 1;
}

static void
one(long sx, long sy, unsigned long sw, unsigned long sh,
    unsigned long dstW, unsigned long dstH)
{
    unsigned long ax0 = 0UL, ax1 = 0UL, ay0 = 0UL, ay1 = 0UL;
    unsigned long bx0 = 0UL, bx1 = 0UL, by0 = 0UL, by1 = 0UL;
    int a, b;

    a = oldWay(sx, sy, sw, sh, dstW, dstH, &ax0, &ax1, &ay0, &ay1);
    b = osmgaHW3DClipBox(1UL, sx, sy, sw, sh, dstW, dstH,
                         &bx0, &bx1, &by0, &by1);
    compared++;
    if (a != b) {
        printf("FAIL: emptiness differs at (%ld,%ld %lu x %lu) in %lu x %lu"
               " -- double says %d, integer says %d\n",
               sx, sy, sw, sh, dstW, dstH, a, b);
        failures++;
        return;
    }
    if (!a)
        return;
    if (ax0 != bx0 || ax1 != bx1 || ay0 != by0 || ay1 != by1) {
        printf("FAIL: box differs at (%ld,%ld %lu x %lu) in %lu x %lu -- "
               "double %lu..%lu %lu..%lu, integer %lu..%lu %lu..%lu\n",
               sx, sy, sw, sh, dstW, dstH,
               ax0, ax1, ay0, ay1, bx0, bx1, by0, by1);
        failures++;
    }
}

int
main(void)
{
    static const long xs[13] = { -100000L, -1024L, -64L, -1L, 0L, 1L, 63L,
                                 64L, 100L, 639L, 640L, 1024L, 100000L };
    static const unsigned long ws[10] = { 0UL, 1UL, 2UL, 63UL, 64UL, 100UL,
                                          640UL, 1024UL, 65536UL,
                                          4000000UL };
    static const unsigned long dsts[4] = { 1UL, 64UL, 640UL, 1600UL };
    unsigned long x0 = 0UL, x1 = 0UL, y0 = 0UL, y1 = 0UL;
    int i, j, k, l, d;

    for (d = 0; d < 4; d++)
        for (i = 0; i < 13; i++)
            for (j = 0; j < 10; j++)
                for (k = 0; k < 13; k++)
                    for (l = 0; l < 10; l++)
                        one(xs[i], xs[k], ws[j], ws[l],
                            dsts[d], dsts[(d + 1) & 3]);

    /* Scissor off is the whole destination, which the old code expressed by
     * simply not entering the branch. */
    if (!osmgaHW3DClipBox(0UL, 999L, -999L, 3UL, 3UL, 640UL, 480UL,
                          &x0, &x1, &y0, &y1) ||
        x0 != 0UL || x1 != 640UL || y0 != 0UL || y1 != 480UL) {
        printf("FAIL: no scissor is not the whole destination\n");
        failures++;
    }
    /* A destination of nothing draws nothing, whatever the box says. */
    if (osmgaHW3DClipBox(0UL, 0L, 0L, 8UL, 8UL, 0UL, 480UL,
                         &x0, &x1, &y0, &y1)) {
        printf("FAIL: an empty destination accepted a box\n");
        failures++;
    }

    if (failures == 0)
        printf("test-clipbox: the integer clip box agrees with the floating "
               "point one it replaces on %lu boxes (0 failing)\n", compared);
    else
        printf("test-clipbox: %d failing of %lu\n", failures, compared);
    return failures != 0;
}
