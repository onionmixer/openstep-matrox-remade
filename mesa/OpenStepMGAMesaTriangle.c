/*
 * OpenStepMGAMesaTriangle.c - see the header.  Plain C89, 32-bit, no long
 * long: every product below is bounded by the coordinate limits, which are
 * at most a few thousand, so nothing here can leave 32 bits.
 */

#include <string.h>

#include "OpenStepMGAMesaTriangle.h"

/*
 * Masked form.  The client may say only which operation, which access type
 * and which depth mode it wants; everything else in DWGCTL belongs to the
 * kernel, which is what keeps a client from aiming the engine somewhere it
 * should not.  TRAP with access type I is the flat-shaded case.
 */
#define OSMGA_TRI_DWGCTL    (0x4UL | (0x7UL << 4))
#define OSMGA_TRI_OPAQUE    0x00000101UL   /* replace, no blending */

static void
osmgaTrapezoid(OSMGAHW3DTri *t, long y, long h,
               long left, long dxL, long right, long dxR,
               const OSMGAMesaVertex *flat)
{
    int sdxl = (dxL < 0L) ? 1 : 0;
    int sdxr = (dxR < 0L) ? 1 : 0;

    memset(t, 0, sizeof *t);
    t->dwgctl   = OSMGA_TRI_DWGCTL;
    t->alphactrl = OSMGA_TRI_OPAQUE;
    t->y = y;
    t->h = h;

    /*
     * AR0 and AR6 are the heights the two edges divide by, and AR2 and AR5
     * the displacements over that height, so the pair expresses a fractional
     * slope directly.  AR1 and AR4 start the error at the displacement,
     * which is the form the working batches use.  The magnitude goes in the
     * registers and the direction in SGN.
     */
    t->ar0 = h;
    t->ar1 = sdxl ? dxL : -dxL;
    t->ar2 = t->ar1;
    t->ar4 = sdxr ? dxR : -dxR;
    t->ar5 = t->ar4;
    t->ar6 = h;
    t->sgn = ((long)sdxl << 1) | ((long)sdxr << 5);

    /* FXBNDRY is exclusive on the right, so the last column drawn is
     * `right`, not `right + 1`. */
    t->fxbndry = (((unsigned long)(right + 1L)) << 16) |
                 ((unsigned long)left & 0xffffUL);

    /* Flat colour: a start value and no gradient.  The shift is 15, which is
     * measured; the DDX sources write 7 and are wrong for this part. */
    t->dr[0] = flat->r << 15;
    t->dr[3] = flat->g << 15;
    t->dr[6] = flat->b << 15;
}

int
OSMGAMesaBuildTriangle(const OSMGAMesaVertex *a,
                       const OSMGAMesaVertex *b,
                       const OSMGAMesaVertex *c,
                       const OSMGAMesaVertex *flat,
                       OSMGAHW3DTri *out)
{
    const OSMGAMesaVertex *t, *m, *lo, *tmp;
    long hTop, hBot, span, xSplit;
    int n = 0;

    if (a == 0 || b == 0 || c == 0 || flat == 0 || out == 0)
        return 0;

    /* Sort by y.  Three comparisons, written out rather than looped, because
     * a loop over three pointers reads worse than the thing it replaces. */
    t = a; m = b; lo = c;
    if (t->y > m->y)  { tmp = t; t = m; m = tmp; }
    if (m->y > lo->y) { tmp = m; m = lo; lo = tmp; }
    if (t->y > m->y)  { tmp = t; t = m; m = tmp; }

    span = lo->y - t->y;
    if (span <= 0L)
        return 0;               /* no rows: nothing to draw, not an error */

    /*
     * Where the long edge is at the middle vertex's row.  The division is
     * integer and truncating, so the split can be a pixel off the exact
     * edge; that shows as at most one column at the join, which is why the
     * test compares whole shapes rather than this value alone.
     */
    xSplit = t->x + ((lo->x - t->x) * (m->y - t->y)) / span;

    hTop = m->y - t->y;
    if (hTop > 0L) {
        long l1 = (m->x < xSplit) ? m->x : xSplit;
        long r1 = (m->x < xSplit) ? xSplit : m->x;

        osmgaTrapezoid(&out[n], t->y, hTop,
                       t->x, l1 - t->x, t->x, r1 - t->x, flat);
        n++;
    }

    hBot = lo->y - m->y;
    if (hBot > 0L) {
        long l0 = (m->x < xSplit) ? m->x : xSplit;
        long r0 = (m->x < xSplit) ? xSplit : m->x;

        osmgaTrapezoid(&out[n], m->y, hBot,
                       l0, lo->x - l0, r0, lo->x - r0, flat);
        n++;
    }
    return n;
}
