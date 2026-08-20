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
#define OSMGA_TRI_DWGCTL    (0x4UL | (0x7UL << 4))   /* TRAP, access type I */
#define OSMGA_TRI_DWGCTL_Z  (0x4UL | (0x3UL << 4))   /* TRAP, access type ZI */
#define OSMGA_TRI_OPAQUE    0x00000101UL   /* replace, no blending */

/*
 * A colour component as a plane over the triangle: its rate along x, its
 * rate down y, and the value at vertex a.  Doubles, which the kernel could
 * not use but this can -- nothing in mesa/ is compiled into the driver, and
 * the alternative is a fixed-point solve whose intermediate products leave
 * 32 bits (255 << 15 times a coordinate of a couple of thousand is already
 * eight times the signed range).
 */
typedef struct {
    double dx, dy, at_a;
} OSMGAColourPlane;

/* (component << 15), rounded, as the two's complement the engine reads.
 * Increments are signed there even though the field is unsigned here. */
static unsigned long
osmgaFixed(double v)
{
    double s = v * 32768.0;

    return (unsigned long)(long)((s >= 0.0) ? (s + 0.5) : (s - 0.5));
}

/*
 * An increment has to be held to the range a component can actually cover in
 * one step.  A near-degenerate triangle -- a sliver one column wide with
 * opposite colours along it -- solves to a gradient of tens of thousands per
 * pixel, and at the fifteen-bit scale that no longer fits the field the
 * engine reads: 255 * 119 becomes 0x3B448000, of which the hardware would
 * see 0x448000, or +137, and paint a gradient nobody asked for.
 *
 * Nothing legitimate is lost by holding it to 255.  A component spans its
 * whole range in a single pixel at that rate, so any larger value describes
 * a triangle too thin to have an interior for the extra precision to land
 * in.  The clamped form also stays inside 24 bits, whatever the field's
 * exact width turns out to be.
 */
static double
osmgaClampSlope(double v)
{
    if (v >  255.0) return  255.0;
    if (v < -255.0) return -255.0;
    return v;
}

/*
 * Coordinates far outside any real destination are refused rather than
 * packed.  FXBNDRY keeps sixteen bits, so a multiple of 65536 would survive
 * the mask as a small number and pass the kernel's column check while naming
 * a pixel nowhere near the screen; and the products in the split overflow a
 * signed long long before that.  This bound is far above any mode and far
 * below where the arithmetic stops being exact.
 */
#define OSMGA_MESA_COORD_MAX 16384L

static unsigned long
osmgaStartFixed(double v)
{
    /*
     * The start is evaluated at the trapezoid's own corner, which the
     * integer split can put a fraction of a pixel outside the triangle.  A
     * component that went negative would wrap into a very large unsigned
     * value and paint the opposite of what was asked, so it is held to the
     * representable range; the cost is at most one level at one corner.
     */
    if (v < 0.0)
        v = 0.0;
    if (v > 255.0)
        v = 255.0;
    return osmgaFixed(v);
}

static int
osmgaCoordOK(const OSMGAMesaVertex *v)
{
    return (v->x <=  OSMGA_MESA_COORD_MAX && v->x >= -OSMGA_MESA_COORD_MAX &&
            v->y <=  OSMGA_MESA_COORD_MAX && v->y >= -OSMGA_MESA_COORD_MAX);
}

static void
osmgaTrapezoid(OSMGAHW3DTri *t, long y, long h,
               long left, long dxL, long right, long dxR,
               const OSMGAMesaVertex *flat,
               const OSMGAColourPlane *plane, const OSMGAMesaVertex *a,
               unsigned long zmode, const OSMGAColourPlane *zplane)
{
    int sdxl = (dxL < 0L) ? 1 : 0;
    int sdxr = (dxR < 0L) ? 1 : 0;

    memset(t, 0, sizeof *t);
    t->dwgctl   = (zmode != 0UL) ? (OSMGA_TRI_DWGCTL_Z | zmode)
                                : OSMGA_TRI_DWGCTL;
    t->alphactrl = OSMGA_TRI_OPAQUE;
    t->y = y;
    t->h = h;

    /*
     * AR0 and AR6 are the heights the two edges divide by, and AR2 and AR5
     * the displacements over that height, so the pair expresses a fractional
     * slope directly.  AR1 and AR4 start the error at the displacement,
     * which is the form the working batches use.
     *
     * What goes in the register is the NEGATED magnitude, never the
     * magnitude: both arms of the expression below come out negative or
     * zero, and the direction is carried entirely by SGN.  Confirmed by
     * reading it back -- a right edge given +40 produced ar5 = -40 and drew
     * the shape that displacement describes.
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

    if (zmode != 0UL && zplane != 0) {
        /*
         * Depth is a plane like any other, at the same fifteen-bit scale --
         * measured, along with colour and alpha.  Its values run to 65535
         * rather than 255, so the start is held to that range instead: the
         * engine reads sixteen bits, and Mesa's software depth stores the
         * same range in the same buffer, which is what lets the two agree.
         */
        double ox = (double)left - (double)a->x;
        double oy = (double)y    - (double)a->y;
        double at = zplane->at_a + zplane->dx * ox + zplane->dy * oy;

        if (at < 0.0)     at = 0.0;
        if (at > 65535.0) at = 65535.0;
        t->z0  = osmgaFixed(at);
        t->zdx = osmgaFixed(zplane->dx);
        t->zdy = osmgaFixed(zplane->dy);
    }

    /* The shift is 15, which is measured; the DDX sources write 7 and are
     * wrong for this part. */
    if (flat != 0) {
        t->dr[0] = flat->r << 15;
        t->dr[3] = flat->g << 15;
        t->dr[6] = flat->b << 15;
        return;
    }
    {
        /* Start values belong at this trapezoid's first pixel, because that
         * is where the engine begins counting -- measured, see the header. */
        double ox = (double)left - (double)a->x;
        double oy = (double)y    - (double)a->y;
        int i;

        for (i = 0; i < 3; i++) {
            const OSMGAColourPlane *p = &plane[i];

            t->dr[i * 3 + 0] =
                osmgaStartFixed(p->at_a + p->dx * ox + p->dy * oy);
            t->dr[i * 3 + 1] = osmgaFixed(osmgaClampSlope(p->dx));
            t->dr[i * 3 + 2] = osmgaFixed(osmgaClampSlope(p->dy));
        }
    }
}

int
OSMGAMesaBuildTriangle(const OSMGAMesaVertex *a,
                       const OSMGAMesaVertex *b,
                       const OSMGAMesaVertex *c,
                       const OSMGAMesaVertex *flat,
                       unsigned long zmode,
                       OSMGAHW3DTri *out)
{
    const OSMGAMesaVertex *t, *m, *lo, *tmp;
    OSMGAColourPlane plane[3];
    OSMGAColourPlane zplane;
    const OSMGAMesaVertex *shade = flat;
    long hTop, hBot, span, xSplit;
    int n = 0;

    if (a == 0 || b == 0 || c == 0 || out == 0)
        return 0;
    if (!osmgaCoordOK(a) || !osmgaCoordOK(b) || !osmgaCoordOK(c))
        return 0;

    {
        /*
         * Twice the signed area.  Zero means the three vertices are on one
         * line, and a triangle with no area covers no pixels -- without this
         * the two edges walk together and the engine draws the line itself,
         * which is a shape nobody asked for.
         */
        double x1 = (double)(b->x - a->x), y1 = (double)(b->y - a->y);
        double x2 = (double)(c->x - a->x), y2 = (double)(c->y - a->y);

        if (x1 * y2 - x2 * y1 == 0.0)
            return 0;
    }

    /*
     * The depth plane, solved whenever depth is asked for, and independently
     * of whether colour is interpolated -- flat shading says nothing about
     * depth, which varies across a triangle in either case.
     */
    zplane.dx = zplane.dy = 0.0;
    zplane.at_a = 0.0;
    if (zmode != 0UL) {
        double x1 = (double)(b->x - a->x), y1 = (double)(b->y - a->y);
        double x2 = (double)(c->x - a->x), y2 = (double)(c->y - a->y);
        double den = x1 * y2 - x2 * y1;
        double d1 = (double)b->z - (double)a->z;
        double d2 = (double)c->z - (double)a->z;

        /* den is not zero: the caller above has already refused a triangle
         * with no area. */
        zplane.dx   = (d1 * y2 - d2 * y1) / den;
        zplane.dy   = (d2 * x1 - d1 * x2) / den;
        zplane.at_a = (double)a->z;
    }

    if (shade == 0) {
        /*
         * Solve each component as a plane through the three vertices.  The
         * denominator is twice the signed area, which the caller above has
         * already established is not zero.
         */
        double x1 = (double)(b->x - a->x), y1 = (double)(b->y - a->y);
        double x2 = (double)(c->x - a->x), y2 = (double)(c->y - a->y);
        double denom = x1 * y2 - x2 * y1;

        {
            unsigned long ca[3], cb[3], cc[3];
            int i;

            ca[0] = a->r; ca[1] = a->g; ca[2] = a->b;
            cb[0] = b->r; cb[1] = b->g; cb[2] = b->b;
            cc[0] = c->r; cc[1] = c->g; cc[2] = c->b;
            for (i = 0; i < 3; i++) {
                double d1 = (double)cb[i] - (double)ca[i];
                double d2 = (double)cc[i] - (double)ca[i];

                plane[i].dx   = (d1 * y2 - d2 * y1) / denom;
                plane[i].dy   = (d2 * x1 - d1 * x2) / denom;
                plane[i].at_a = (double)ca[i];
            }
        }
    }

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
                       t->x, l1 - t->x, t->x, r1 - t->x, shade, plane, a,
                       zmode, &zplane);
        n++;
    }

    hBot = lo->y - m->y;
    if (hBot > 0L) {
        long l0 = (m->x < xSplit) ? m->x : xSplit;
        long r0 = (m->x < xSplit) ? xSplit : m->x;

        osmgaTrapezoid(&out[n], m->y, hBot,
                       l0, lo->x - l0, r0, lo->x - r0, shade, plane, a,
                       zmode, &zplane);
        n++;
    }
    return n;
}
