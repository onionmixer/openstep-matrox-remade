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

/*
 * The coordinate range this encoding can carry without leaving 32 bits.
 *
 * The error term below contains 2*|D|*k0, and |D| and k0 are each bounded by
 * twice the coordinate limit, so the product is bounded by eight times its
 * square.  At OSMGA_MESA_COORD_MAX that is 2147418112, which clears the
 * signed range by sixty-five thousand -- a margin of nothing at all.  Half
 * that limit leaves a factor of four, and no surface this driver drives is
 * anywhere near it: the widest mode is 1600 columns.  A triangle outside it
 * is refused as unsupported and drawn the other way, which is the mechanism
 * that already exists for triangles this back end cannot express.
 */
#define OSMGA_MESA_RULE_COORD_MAX  8192L

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
    return (v->x <=  OSMGA_MESA_RULE_COORD_MAX &&
            v->x >= -OSMGA_MESA_RULE_COORD_MAX &&
            v->y <=  OSMGA_MESA_RULE_COORD_MAX &&
            v->y >= -OSMGA_MESA_RULE_COORD_MAX);
}

/*
 * C's division truncates toward zero, which is not the floor when the
 * numerator is negative -- and the numerator below is negative for every
 * edge steeper than one column per row.
 */
static long
osmgaFloorDiv(long a, long b)
{
    long q = a / b;

    if ((a % b) != 0L && ((a < 0L) != (b < 0L)))
        q--;
    return q;
}

/*
 * An edge as the rule needs it: where the edge itself starts, how far it
 * moves over its WHOLE height, and how many rows into it this trapezoid
 * begins.  The lower trapezoid continues the long edge rather than restarting
 * from a rounded position, which is what k0 is for.
 */
typedef struct {
    long xa;                    /* the edge's own start column */
    long dee;                   /* displacement over the whole edge */
    long height;                /* the edge's own height, not the trapezoid's */
    long k0;                    /* rows into the edge this trapezoid starts */
} OSMGAMesaEdge;

/*
 * The registers for one edge.
 *
 * OpenGL samples a pixel at its centre and breaks ties top-left, so the first
 * column of a left edge at row ya+k0+k is
 *
 *     xa + ceil( (2*D*(k0+k) + D - H) / (2*H) )
 *
 * and the same expression is the right edge's boundary, which FXBNDRY treats
 * as exclusive.  Doubling AR0 and AR2 keeps the slope exactly and makes that
 * half pixel an integer -- with the pair left at the trapezoid's own height,
 * as it was, there is no integer that expresses it, which is why biasing the
 * start column alone reached 13 triangles in 300 and this reaches 300.
 *
 * The -1 on a leftward edge is the identity that appears when a ceiling is
 * negated; it is right only because every edge here runs top to bottom.
 *
 * The error term is then folded into [0, 2H) by moving whole columns into the
 * start, since shifting it by the denominator moves the walk exactly one
 * column.  That is NOT a general transformation -- the walk clamps at zero,
 * and folding across the clamp moves every row, as dy=10, |dx|=1, e=15 shows.
 * It is safe here because the term before folding is at most H, and the
 * denominator is 2H, so the fold only ever raises it.  It also leaves nothing
 * to walk on the first row, which is what makes the column written the column
 * drawn -- and the interpolation planes below are anchored to that column.
 */
static void
osmgaEdgeRegs(const OSMGAMesaEdge *e,
              long *x0, long *mag, long *dy, long *err)
{
    long mg = (e->dee < 0L) ? -e->dee : e->dee;
    long den = 2L * e->height;
    long v = e->height - mg - 2L * mg * e->k0 - ((e->dee < 0L) ? 1L : 0L);
    long q = osmgaFloorDiv(v, den);

    *mag = 2L * mg;
    *dy  = den;
    *err = v - q * den;
    *x0  = e->xa - ((e->dee < 0L) ? -q : q);
}

static void
osmgaTrapezoid(OSMGAHW3DTri *t, long y, long h,
               const OSMGAMesaEdge *le, const OSMGAMesaEdge *re,
               const OSMGAMesaVertex *flat,
               const OSMGAColourPlane *plane, const OSMGAMesaVertex *a,
               unsigned long zmode, const OSMGAColourPlane *zplane,
               unsigned long blend, const OSMGAColourPlane *aplane)
{
    int sdxl = (le->dee < 0L) ? 1 : 0;
    int sdxr = (re->dee < 0L) ? 1 : 0;
    long left, lmag, ldy, lerr;
    long right, rmag, rdy, rerr;

    osmgaEdgeRegs(le, &left,  &lmag, &ldy, &lerr);
    osmgaEdgeRegs(re, &right, &rmag, &rdy, &rerr);

    memset(t, 0, sizeof *t);
    t->dwgctl   = (zmode != 0UL) ? (OSMGA_TRI_DWGCTL_Z | zmode)
                                : OSMGA_TRI_DWGCTL;
    t->alphactrl = blend;
    t->y = y;
    t->h = h;

    /*
     * AR0 and AR6 are the heights the two edges divide by, and AR2 and AR5
     * the displacements over that height, so the pair expresses a fractional
     * slope directly.  It is the EDGE's height, not the trapezoid's: pinning
     * it to the trapezoid was what forced the long edge through a rounded
     * restart at the split, and both are doubled so AR1 and AR4 can carry
     * half a pixel.  AR1 and AR4 are the displacement with that error term
     * folded in.
     *
     * What goes in the register is the NEGATED magnitude, never the
     * magnitude: both arms of the expression below come out negative or
     * zero, and the direction is carried entirely by SGN.  Confirmed by
     * reading it back -- a right edge given +40 produced ar5 = -40 and drew
     * the shape that displacement describes.
     */
    t->ar0 = ldy;
    t->ar2 = -lmag;
    t->ar1 = -lmag - lerr;
    t->ar4 = -rmag - rerr;
    t->ar5 = -rmag;
    t->ar6 = rdy;
    t->sgn = ((long)sdxl << 1) | ((long)sdxr << 5);

    /*
     * FXBNDRY is exclusive on the right, and `right` is handed in as the
     * boundary rather than the last column -- so it goes in as it is.
     *
     * It used to have one added to it, which drew one column more than the
     * software rasteriser on every single row.  Measured: the same triangle
     * through both paths gave spans 1..40 against 1..39, 1..39 against 1..38,
     * and so on for all thirty-nine rows, with the left edge and the row
     * extent identical throughout.  Mesa's span is [left, right) -- it takes
     * FixedToInt of each edge and draws right minus left pixels -- and this
     * is what makes ours the same.
     */
    t->fxbndry = (((unsigned long)right) << 16) |
                 ((unsigned long)left & 0xffffUL);

    if (aplane != 0) {
        /*
         * Alpha is a plane too, and at the same scale, and it is written
         * whether or not anything blends with it.
         *
         * Setting it only for blended triangles was tried and measured
         * wrong: an opaque accelerated triangle then left whatever the
         * interpolator happened to hold in the destination's fourth byte --
         * zero -- where the software rasteriser leaves the vertex's alpha.
         * Read back, one path gave 00 and the other ff for the same
         * triangle, and glReadPixels would have reported it.
         */
        double ox = (double)left - (double)a->x;
        double oy = (double)y    - (double)a->y;

        t->a0  = osmgaStartFixed(aplane->at_a + aplane->dx * ox
                                 + aplane->dy * oy);
        t->adx = osmgaFixed(osmgaClampSlope(aplane->dx));
        t->ady = osmgaFixed(osmgaClampSlope(aplane->dy));
    }

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
                       unsigned long blend,
                       OSMGAHW3DTri *out)
{
    const OSMGAMesaVertex *t, *m, *lo, *tmp;
    OSMGAColourPlane plane[3];
    OSMGAColourPlane zplane, aplane;
    const OSMGAMesaVertex *shade = flat;
    long hTop, hBot, span, cross;
    OSMGAMesaEdge longE;
    int n = 0;

    /*
     * Two different answers used to share one value.
     *
     * Zero meant both "there is nothing to draw" and "this is outside what
     * the back end can express", and the caller could only treat them the
     * same way -- so a triangle whose coordinates ran past the range was
     * quietly lost instead of being drawn by the software path.  A negative
     * answer now means UNSUPPORTED and zero keeps its old meaning.
     */
    if (a == 0 || b == 0 || c == 0 || out == 0)
        return OSMGA_MESA_TRI_UNSUPPORTED;
    if (!osmgaCoordOK(a) || !osmgaCoordOK(b) || !osmgaCoordOK(c))
        return OSMGA_MESA_TRI_UNSUPPORTED;

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
    aplane.dx = aplane.dy = 0.0;
    aplane.at_a = 255.0;
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

        /*
         * The same rule the software rasteriser uses for slivers: a slope
         * steeper than the whole depth range in one pixel means the triangle
         * is too thin for the plane to mean anything, and Mesa answers by
         * flattening BOTH derivatives.  Doing anything else here would have
         * the two paths writing wildly different depths for the same thin
         * triangle -- and, at that magnitude, would overflow the fixed point
         * as well.
         */
        if (zplane.dx > 65535.0 || zplane.dx < -65535.0) {
            zplane.dx = 0.0;
            zplane.dy = 0.0;
        }
    }

    /* Always, not only when blending: the destination's fourth byte is
     * written either way, and it has to hold what the software path would
     * have put there. */
    {
        double x1 = (double)(b->x - a->x), y1 = (double)(b->y - a->y);
        double x2 = (double)(c->x - a->x), y2 = (double)(c->y - a->y);
        double den = x1 * y2 - x2 * y1;
        double d1 = (double)b->a - (double)a->a;
        double d2 = (double)c->a - (double)a->a;

        aplane.dx   = (d1 * y2 - d2 * y1) / den;
        aplane.dy   = (d2 * x1 - d1 * x2) / den;
        aplane.at_a = (double)a->a;
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
     * Which side of the long edge the middle vertex is on -- decided once,
     * because the engine cannot exchange left for right partway down a
     * trapezoid.  The sign of this is the sign of the middle vertex's column
     * minus the long edge's column at that row.
     *
     * Zero means the three are collinear, so there is no area and nothing to
     * draw.  Said here rather than left to produce an empty span by
     * arithmetic: relying on two boundaries to come out equal is a weaker
     * thing to depend on than saying what the case is.
     */
    cross = (lo->x - t->x) * (m->y - t->y) - (lo->y - t->y) * (m->x - t->x);
    if (cross == 0L)
        return 0;

    /*
     * Both trapezoids walk the ORIGINAL edges.  The lower one continues the
     * long edge from row hTop rather than restarting from where an integer
     * division put it, which is what used to lose a fraction of a column at
     * the split -- and, on the shape that splits, a whole row.
     */
    longE.xa = t->x; longE.dee = lo->x - t->x; longE.height = span;

    hTop = m->y - t->y;
    if (hTop > 0L) {
        OSMGAMesaEdge shortE;

        shortE.xa = t->x; shortE.dee = m->x - t->x;
        shortE.height = hTop; shortE.k0 = 0L;
        longE.k0 = 0L;
        osmgaTrapezoid(&out[n], t->y, hTop,
                       (cross < 0L) ? &longE : &shortE,
                       (cross < 0L) ? &shortE : &longE,
                       shade, plane, a, zmode, &zplane, blend, &aplane);
        n++;
    }

    hBot = lo->y - m->y;
    if (hBot > 0L) {
        OSMGAMesaEdge shortE;

        shortE.xa = m->x; shortE.dee = lo->x - m->x;
        shortE.height = hBot; shortE.k0 = 0L;
        longE.k0 = hTop;
        osmgaTrapezoid(&out[n], m->y, hBot,
                       (cross < 0L) ? &longE : &shortE,
                       (cross < 0L) ? &shortE : &longE,
                       shade, plane, a, zmode, &zplane, blend, &aplane);
        n++;
    }
    return n;
}
