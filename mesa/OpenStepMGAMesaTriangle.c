/*
 * OpenStepMGAMesaTriangle.c - see the header.  Plain C89, 32-bit, no long
 * long: every product below is bounded by the coordinate limits, which are
 * at most a few thousand, so nothing here can leave 32 bits.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

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

/*
 * The power of two that contains n, exactly as the kernel's encoder works it
 * out (OpenStepMGAReplacementDisplay.m:6853).  The two must agree: the kernel
 * puts this number in the engine's log2 field and the coordinate scale
 * follows from it, so a different answer here would place the texture
 * somewhere the hardware is not looking.
 */
static long
osmgaLog2Ceil(unsigned long n)
{
    long l = 0L;

    while ((1UL << l) < n && l < 31L)
        l++;
    return l;
}

/* Nearest long.  The coordinate registers are plain integers, not the
 * shifted fixed point the colour registers use. */
static long
osmgaRound(double v)
{
    return (long)floor(v + 0.5);
}

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
 * The same for depth, at depth's own scale.
 *
 * This one was missing, and its absence is undefined behaviour rather than a
 * wrong picture: a sliver's plane is ill-conditioned, and over 299939 random
 * triangles 911 of them -- three tenths of a per cent -- produce a depth slope
 * past 65536 per pixel, which osmgaFixed scales past what a long holds before
 * casting it.  Measured at up to five million per pixel, or 1.66e11 scaled.
 *
 * 65535 is the bound because that is where the scaling still fits: 65535 times
 * 32768 is 2147450880 against a signed limit of 2147483647.  Nothing is lost
 * by it for the same reason the colour clamp loses nothing -- a slope that
 * crosses the entire depth range in one column describes a triangle with no
 * interior for the extra precision to land in.
 */
static double
osmgaClampDepthSlope(double v)
{
    if (v >  65535.0) return  65535.0;
    if (v < -65535.0) return -65535.0;
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
    /*
     * Half a level on top, because the engine discards the fifteen low bits
     * rather than rounding them.  Measured as an identity, not a tendency:
     * over 704 pixels of a Gouraud triangle the painted value was exactly
     * floor(the plane's value) at every one of them, in both channels, with
     * zero error -- and the channel whose plane happens to take integer
     * values at integer points came out exactly right, which is the only
     * place a truncation has nothing to discard.
     *
     * Adding it here rather than at the two call sites is why depth is
     * unaffected: depth converts through osmgaFixed directly, and whether its
     * own interpolator truncates has not been measured.
     *
     * The largest start becomes 255.5 scaled, 0x7FC000, against the 0x7F8000
     * this already wrote, and floors back to 255 on the way out.  The
     * clamp above runs first, so the effective range is [0.5, 255.5] and both
     * endpoints still land on their own code.
     */
    return osmgaFixed(v) + (1UL << 14);
}

static int
osmgaCoordOK(const OSMGAMesaVertex *v)
{
    long lim = OSMGA_MESA_RULE_COORD_MAX * OSMGA_MESA_SUBONE;

    return (v->x <= lim && v->x >= -lim && v->y <= lim && v->y >= -lim);
}

/*
 * C's division truncates toward zero, which is not the floor when the
 * numerator is negative -- and the numerator below is negative for every
 * edge steeper than one column per row.
 */
static long
osmgaCeilDiv(long a, long b)            /* b > 0 */
{
    long q = a / b;

    if ((a % b) != 0L && a > 0L)
        q++;
    return q;
}

/*
 * An edge as the rule needs it: where the edge itself starts, how far it
 * moves over its WHOLE height, and how many rows into it this trapezoid
 * begins.  The lower trapezoid continues the long edge rather than restarting
 * from a rounded position, which is what k0 is for.
 */
typedef struct {
    long xa, ya;                /* 1/256 pixel: the edge's own start vertex */
    long dee;                   /* 1/256 pixel: displacement over the edge   */
    long height;                /* 1/256 pixel: the edge's own height        */
} OSMGAMesaEdge;

/* floor division, since C's / rounds toward zero and the numerators here are
 * signed.  Used for quantising a coordinate down onto the chosen grid. */
static long
osmgaFloorDiv(long a, long b)
{
    long q = a / b;

    if ((a % b) != 0L && ((a < 0L) != (b < 0L)))
        q--;
    return q;
}

/*
 * The registers for one edge, with the vertex's fraction carried.
 *
 * Transcribed from spec/subpixel-model.py.  With the vertex on a 1/M grid and
 * XA, YA, DD, HH the coordinates scaled by M, the rule's boundary at sample
 * row r is
 *
 *     B(r) = ceil( P(r) / (2*M*HH) ),
 *     P(r) = 2*XA*HH - M*HH + ((2r+1)*M - 2*YA)*DD
 *
 * so the start column is B at the trapezoid's first row and the error term is
 * what is left over.  The engine is then given the HALVED scaling -- divisor
 * 2*HH rather than 2*M*HH, with the error term divided by M and rounded up --
 * which is exact, not approximate: the coarse ceiling equals the fine one
 * because the half-open interval between a non-integer and the next integer
 * above it contains no integer for a multiple of the divisor to sit in.  That
 * halving is what buys four bits of fraction inside the kernel's edge budget
 * instead of two.
 */
static void
osmgaEdgeRegs(const OSMGAMesaEdge *e, long r0, long s,
              long *x0, long *mag, long *dy, long *err, long *sgn)
{
    long shift = OSMGA_MESA_SUBBITS - s;
    long step = 1L << shift;
    long M = 1L << s;
    long XA = osmgaFloorDiv(e->xa, step);
    long YA = osmgaFloorDiv(e->ya, step);
    long DD = osmgaFloorDiv(e->dee, step);
    long HH = osmgaFloorDiv(e->height, step);
    long Q2, P0, r, e2;

    Q2 = 2L * M * HH;
    P0 = 2L * XA * HH - M * HH + ((2L * r0 + 1L) * M - 2L * YA) * DD;
    *x0 = osmgaCeilDiv(P0, Q2);
    r = P0 - (*x0) * Q2;
    e2 = (DD >= 0L) ? r : (-r - Q2 + 1L);
    *mag = 2L * ((DD >= 0L) ? DD : -DD);
    *dy = 2L * HH;
    *err = osmgaCeilDiv(e2, M);
    *sgn = (DD >= 0L) ? 1L : -1L;
}

/*
 * How many rows at the top of this trapezoid draw nothing.
 *
 * A trapezoid that begins at an apex has an empty first row, and the depth
 * start value is read at that row's boundary -- a pixel the triangle does not
 * cover, where the plane can be outside the buffer's range and get clipped.
 * Clipping it moves every pixel of the trapezoid, because the engine walks
 * from it, and under GL_LESS a fragment pushed to the far value disappears.
 *
 * Starting at the first row that draws something puts the origin on a covered
 * pixel, whose centre is inside the triangle and whose depth is therefore
 * inside the vertex range by construction.  Nothing is drawn differently,
 * since the skipped rows drew nothing.  Counted rather than argued: over
 * 118418 trapezoids the clamp fires 359 times as it stands and zero times
 * after this.
 *
 * The walk here is the engine's own, the one verified against hardware pixel
 * for pixel -- the row has to be empty under that rule and not under an
 * approximation of it.
 */
static long
osmgaFirstDrawn(const OSMGAMesaEdge *le, const OSMGAMesaEdge *re,
                long r0, long h, long sub)
{
    long lx, lmag, ldy, lerr, lsgn, lacc;
    long rx, rmag, rdy, rerr, rsgn, racc;
    long k;

    osmgaEdgeRegs(le, r0, sub, &lx, &lmag, &ldy, &lerr, &lsgn);
    osmgaEdgeRegs(re, r0, sub, &rx, &rmag, &rdy, &rerr, &rsgn);
    lacc = -lerr;
    racc = -rerr;
    for (k = 0L; k < h; k++) {
        if (k > 0L) {
            lacc -= lmag;
            while (lacc < 0L) { lx += lsgn; lacc += ldy; }
            racc -= rmag;
            while (racc < 0L) { rx += rsgn; racc += rdy; }
        }
        if (lx <= rx - 1L)
            return k;
    }
    return h;
}

/* Counted so "the clamp never fires now" is a number and not a claim. */
static unsigned long osmgaDepthClamps = 0UL;

unsigned long
OSMGAMesaDepthClamps(void)
{
    return osmgaDepthClamps;
}

static double
osmgaAbsD(double v)
{
    return (v < 0.0) ? -v : v;
}

/*
 * The three planes a perspective-correct triangle needs, and the one scale
 * that is free to choose.
 *
 * Perspective correction is s/w and 1/w interpolated linearly in screen
 * space and then divided, and the engine performs exactly that division:
 * it samples numerator * 65536 / denominator.  So the numerators carry
 * s/w and t/w, the denominator carries 1/w, and the quotient is s -- the
 * projective scale cancels out of it entirely.
 *
 * That freedom is worth spending.  The engine adds to the numerator an
 * amount bounded by 512, measured, which costs 2048/q texels, so the larger
 * the denominator the smaller the error.  The scale is therefore taken as
 * large as every bound will allow rather than merely large enough:
 *
 *      the denominator's floor and ceiling, over the whole triangle;
 *      the numerator start, which the validator holds to the coordinate
 *          limit -- bounded here over the BOUNDING BOX, since the trapezoid
 *          anchor the emitter uses lies inside it and not at vertex a;
 *      the numerator slopes, against the limit over each span;
 *      the denominator slopes, against what evaluating q may hold.
 *
 * If the floor asks for more than the ceilings allow, this returns nought
 * and the triangle is drawn in software.  A refusal is cheap; a wrong
 * picture is not.
 */
static int
osmgaPerspPlanes(const OSMGAMesaVertex *a, const OSMGAMesaVertex *b,
                 const OSMGAMesaVertex *c,
                 double ua, double ub, double uc,
                 double va, double vb, double vc,
                 double x1, double y1, double x2, double y2, double den,
                 OSMGAColourPlane *up, OSMGAColourPlane *vp,
                 OSMGAColourPlane *qp)
{
    double qa = a->qw, qb = b->qw, qc = c->qw;
    double na = ua * qa, nb = ub * qb, nc = uc * qc;
    double ma = va * qa, mb = vb * qb, mc = vc * qc;
    double ndx, ndy, mdx, mdy, qdx, qdy;
    double qlo, qhi, dxlo, dxhi, dylo, dyhi, ex, ey;
    double lamLo, lamHi, t, room, budget;
    int k;

    (void)c;
    ndx = ((nb - na) * y2 - (nc - na) * y1) / den;
    ndy = ((nc - na) * x1 - (nb - na) * x2) / den;
    mdx = ((mb - ma) * y2 - (mc - ma) * y1) / den;
    mdy = ((mc - ma) * x1 - (mb - ma) * x2) / den;
    qdx = ((qb - qa) * y2 - (qc - qa) * y1) / den;
    qdy = ((qc - qa) * x1 - (qb - qa) * x2) / den;

    qlo = qhi = qa;
    if (qb < qlo) qlo = qb;   if (qb > qhi) qhi = qb;
    if (qc < qlo) qlo = qc;   if (qc > qhi) qhi = qc;
    if (qlo <= 0.0)
        return 0;

    dxlo = dxhi = 0.0;
    if (x1 < dxlo) dxlo = x1;   if (x1 > dxhi) dxhi = x1;
    if (x2 < dxlo) dxlo = x2;   if (x2 > dxhi) dxhi = x2;
    dylo = dyhi = 0.0;
    if (y1 < dylo) dylo = y1;   if (y1 > dyhi) dyhi = y1;
    if (y2 < dylo) dylo = y2;   if (y2 > dyhi) dyhi = y2;
    ex = dxhi - dxlo;   if (ex < 1.0) ex = 1.0;
    ey = dyhi - dylo;   if (ey < 1.0) ey = 1.0;

    room   = (double)OSMGA_HW3D_TEX_COORD_MAX;
    budget = ((double)(1L << 30) - (double)OSMGA_HW3D_Q_MAX) / 2.0;

    lamLo = (double)OSMGA_HW3D_Q_MIN / (qlo * 65536.0);
    lamHi = (double)OSMGA_HW3D_Q_MAX / (qhi * 65536.0);

    /* the numerator start, over the box rather than at one vertex */
    for (k = 0; k < 4; k++) {
        double dx = (k & 1) ? dxhi : dxlo;
        double dy = (k & 2) ? dyhi : dylo;
        double n = osmgaAbsD(na + ndx * dx + ndy * dy);
        double m = osmgaAbsD(ma + mdx * dx + mdy * dy);

        if (m > n) n = m;
        if (n > 0.0) { t = room / n; if (t < lamHi) lamHi = t; }
    }
    /* the numerator slopes, each against its own span */
    t = osmgaAbsD(ndx); if (t > 0.0) { t = (room / ex) / t; if (t < lamHi) lamHi = t; }
    t = osmgaAbsD(mdx); if (t > 0.0) { t = (room / ex) / t; if (t < lamHi) lamHi = t; }
    t = osmgaAbsD(ndy); if (t > 0.0) { t = (room / ey) / t; if (t < lamHi) lamHi = t; }
    t = osmgaAbsD(mdy); if (t > 0.0) { t = (room / ey) / t; if (t < lamHi) lamHi = t; }
    /* the denominator slopes */
    t = osmgaAbsD(qdx) * 65536.0;
    if (t > 0.0) { t = (budget / ex) / t; if (t < lamHi) lamHi = t; }
    t = osmgaAbsD(qdy) * 65536.0;
    if (t > 0.0) { t = (budget / ey) / t; if (t < lamHi) lamHi = t; }

    if (lamHi < lamLo)
        return 0;

    up->dx   = ndx * lamHi;
    up->dy   = ndy * lamHi;
    up->at_a = na  * lamHi;
    vp->dx   = mdx * lamHi;
    vp->dy   = mdy * lamHi;
    vp->at_a = ma  * lamHi;
    qp->dx   = qdx * lamHi * 65536.0;
    qp->dy   = qdy * lamHi * 65536.0;
    qp->at_a = qa  * lamHi * 65536.0;
    return 1;
}

static void
osmgaTrapezoid(OSMGAHW3DTri *t, long y, long h, long sub,
               const OSMGAMesaEdge *le, const OSMGAMesaEdge *re,
               const OSMGAMesaVertex *flat,
               const OSMGAColourPlane *plane, const OSMGAMesaVertex *a,
               unsigned long zmode, const OSMGAColourPlane *zplane,
               unsigned long blend, const OSMGAColourPlane *aplane,
               const OSMGAColourPlane *uplane, const OSMGAColourPlane *vplane,
               const OSMGAColourPlane *qplane, long *tmr)
{
    long left, lmag, ldy, lerr, lsgn;
    long right, rmag, rdy, rerr, rsgn;
    int sdxl, sdxr;

    osmgaEdgeRegs(le, y, sub, &left,  &lmag, &ldy, &lerr, &lsgn);
    osmgaEdgeRegs(re, y, sub, &right, &rmag, &rdy, &rerr, &rsgn);
    sdxl = (lsgn < 0L) ? 1 : 0;
    sdxr = (rsgn < 0L) ? 1 : 0;

    memset(t, 0, sizeof *t);
    t->dwgctl   = (zmode != 0UL) ? (OSMGA_TRI_DWGCTL_Z | zmode)
                                : OSMGA_TRI_DWGCTL;
    /*
     * The textured opcode, when there is a texture.  Getting the coordinates
     * right and leaving the opcode alone would have been arithmetic nobody
     * ever used.
     */
    if (tmr != 0)
        t->dwgctl = (t->dwgctl & ~0xFUL) | OSMGA_HW3D_OPCODE_TEX;
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
        /*
         * Half a pixel into the pixel, because OpenGL asks for the value at
         * the fragment's CENTRE and `left` and `y` are its top-left corner.
         *
         * Measured, not reasoned: one Gouraud triangle, every covered pixel,
         * scored against the plane solved exactly from the vertices.  The
         * software path landed within four hundredths of a level of the
         * centre and the engine within half a level of the corner, on two
         * shapes whose middle vertex falls on opposite sides of the long
         * edge.  A third channel built so its two gradients cancel could not
         * move under this and did not, which is what says anchoring rather
         * than a colour bias.
         *
         * Depth uses the same expression below and is deliberately NOT
         * changed here: half a pixel of depth is many 16-bit codes on a steep
         * polygon, it flips comparisons at ties, and with the depth test on
         * it would move coverage.  It gets its own measurement.
         */
        double ox = (double)left - (double)a->x / (double)OSMGA_MESA_SUBONE + 0.5;
        double oy = (double)y    - (double)a->y / (double)OSMGA_MESA_SUBONE + 0.5;

        t->a0  = osmgaStartFixed(aplane->at_a + aplane->dx * ox
                                 + aplane->dy * oy);
        t->adx = osmgaFixed(osmgaClampSlope(aplane->dx));
        t->ady = osmgaFixed(osmgaClampSlope(aplane->dy));
    }

    if (tmr != 0 && uplane != 0 && vplane != 0) {
        /*
         * The same origin the colour and depth starts use: this trapezoid's
         * own first row and that row's left edge, at the pixel's centre.
         *
         * The engine samples the coordinate at the pixel's LEFT EDGE and
         * steps once per column -- measured, two texels per pixel from a zero
         * start gave 0, 2, 4 at columns 0, 1, 2 rather than 1, 3, 5.  GL wants
         * the sample at the centre.  Putting the plane's value at the centre
         * into the start makes the engine's value at every pixel equal the
         * plane's value at that pixel's centre, and since the texel index is
         * a truncation of the same real number on both sides, the truncation
         * does not change the answer.
         */
        double ox = (double)left - (double)a->x / (double)OSMGA_MESA_SUBONE + 0.5;
        double oy = (double)y    - (double)a->y / (double)OSMGA_MESA_SUBONE + 0.5;

        /*
         * TMR1 is s per Y and TMR2 is t per X, not the other way round.
         *
         * They went in swapped, and the first textured triangle came out with
         * v wrong on half its pixels.  Isolating the terms did not settle it
         * either, because the test that was supposed to tell them apart gave
         * both the same value and so could not.  The DDX names them in its
         * own comments -- mga_storm.c:332-335 writes TMR0 "sx inc", TMR1
         * "sy inc", TMR2 "tx inc", TMR3 "ty inc" -- and putting them that way
         * round takes the disagreement from 2157 pixels of 4410 to none.
         */
        tmr[0] = osmgaRound(uplane->dx);
        tmr[1] = osmgaRound(uplane->dy);
        tmr[2] = osmgaRound(vplane->dx);
        tmr[3] = osmgaRound(vplane->dy);
        tmr[6] = osmgaRound(uplane->at_a + uplane->dx * ox + uplane->dy * oy);
        tmr[7] = osmgaRound(vplane->at_a + vplane->dx * ox + vplane->dy * oy);
        /*
         * The denominator at the same anchor, in the same fixed point the
         * engine reads it in.  Nought here is what tells the caller this was
         * an affine solve and the H family is the kernel's again.
         */
        if (qplane != 0 && qplane->at_a != 0.0) {
            tmr[4] = osmgaRound(qplane->dx);
            tmr[5] = osmgaRound(qplane->dy);
            tmr[8] = osmgaRound(qplane->at_a + qplane->dx * ox
                                + qplane->dy * oy);
        } else {
            tmr[4] = tmr[5] = tmr[8] = 0L;
        }
        if (tmr[8] != 0L && getenv("OSMGA_TMR_DUMP") != 0)
            fprintf(stderr,
                    "# tmr y=%ld h=%ld fx=%08lx  u %ld %ld %ld  v %ld %ld %ld"
                    "  q %ld %ld %ld\n",
                    (long)t->y, (long)t->h, (unsigned long)t->fxbndry,
                    tmr[6], tmr[0], tmr[1], tmr[7], tmr[2], tmr[3],
                    tmr[8], tmr[4], tmr[5]);
    }

    if (zmode != 0UL && zplane != 0) {
        /*
         * Depth is a plane like any other, at the same fifteen-bit scale --
         * measured, along with colour and alpha.  Its values run to 65535
         * rather than 255, so the start is held to that range instead: the
         * engine reads sixteen bits, and Mesa's software depth stores the
         * same range in the same buffer, which is what lets the two agree.
         */
        /*
         * Half a pixel, as for colour and alpha above: OpenGL wants the value
         * at the fragment's centre and `left` and `y` are its corner.
         *
         * Depth waited for its own measurement rather than inheriting theirs,
         * and it needed to.  The anchoring is worth 2662 and 2613 depth codes
         * on the two shapes measured -- six and a half percent of the
         * triangle's own span -- against four tenths of a code for the vertex
         * truncation and half a code for the output truncation, so the three
         * causes are nothing alike in size and only this one is fixed here.
         *
         * It also does more than bend a value: the start is clamped to the
         * buffer's range, and on a steep shape reaching the far end the corner
         * computed 67461.8, clamped, and GL_LESS then refused 65535 < 65535 --
         * the pixel was simply not drawn.  Measured against the same shape
         * with depth switched off, which drew it.
         *
         * The move is not strictly safer.  Over 236940 trapezoids it removes
         * 1093 clamps and introduces 475; counted in pixels, 253822 against
         * 58502.  Both populations are slivers -- median twice-area 292 and
         * 127 against 8983 where nothing clamps -- whose depth plane is
         * ill-conditioned wherever it is sampled.  A shape from the
         * introduced class is in the test, not just one from the removed.
         */
        double ox = (double)left - (double)a->x / (double)OSMGA_MESA_SUBONE + 0.5;
        double oy = (double)y    - (double)a->y / (double)OSMGA_MESA_SUBONE + 0.5;
        double at = zplane->at_a + zplane->dx * ox + zplane->dy * oy;

        if (at < 0.0 || at > 65535.0)
            osmgaDepthClamps++;
        if (at < 0.0)     at = 0.0;
        if (at > 65535.0) at = 65535.0;
        /*
         * Half a code on top, so the engine's truncating shift rounds
         * instead -- the same half-level the colour and alpha starts carry,
         * which depth missed because it converts through osmgaFixed directly
         * rather than through the shared start conversion.
         *
         * This CHOOSES a tie rule: a fragment landing exactly on a half now
         * rounds up.  That is a decision, not a neutral repair, and saying so
         * is the point of writing it here.
         *
         * The headroom is 16383 units, not 16384: 65535 scaled is 2147450880,
         * plus this half-code is 2147467264, against a signed limit of
         * 2147483647.  And nothing walked can exceed 65535.5, because the
         * trapezoid now starts on a covered pixel and every value it walks to
         * lies inside the triangle's own depth range.
         */
        t->z0  = osmgaFixed(at) + (1L << 14);
        t->zdx = osmgaFixed(osmgaClampDepthSlope(zplane->dx));
        t->zdy = osmgaFixed(osmgaClampDepthSlope(zplane->dy));
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
        /*
         * Half a pixel into the pixel, because OpenGL asks for the value at
         * the fragment's CENTRE and `left` and `y` are its top-left corner.
         *
         * Measured, not reasoned: one Gouraud triangle, every covered pixel,
         * scored against the plane solved exactly from the vertices.  The
         * software path landed within four hundredths of a level of the
         * centre and the engine within half a level of the corner, on two
         * shapes whose middle vertex falls on opposite sides of the long
         * edge.  A third channel built so its two gradients cancel could not
         * move under this and did not, which is what says anchoring rather
         * than a colour bias.
         *
         * Depth uses the same expression above and is deliberately NOT
         * changed there: half a pixel of depth is many 16-bit codes on a steep
         * polygon, it flips comparisons at ties, and with the depth test on
         * it would move coverage.  It gets its own measurement.
         */
        double ox = (double)left - (double)a->x / (double)OSMGA_MESA_SUBONE + 0.5;
        double oy = (double)y    - (double)a->y / (double)OSMGA_MESA_SUBONE + 0.5;
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
OSMGAMesaBuildTriangleTex(const OSMGAMesaVertex *a,
                       const OSMGAMesaVertex *b,
                       const OSMGAMesaVertex *c,
                       const OSMGAMesaVertex *flat,
                       unsigned long zmode,
                       unsigned long blend,
                       const OSMGAMesaTex *tex,
                       OSMGAHW3DTri *out,
                       long tmrOut[][9])
{
    const OSMGAMesaVertex *t, *m, *lo, *tmp;
    OSMGAColourPlane plane[3];
    OSMGAColourPlane zplane, aplane, uplane, vplane, qplane;
    const OSMGAMesaVertex *shade = flat;
    long span, sub, rT, rM, rL;
    double crossD;
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
        double x1 = (double)(b->x - a->x) / (double)OSMGA_MESA_SUBONE;
        double y1 = (double)(b->y - a->y) / (double)OSMGA_MESA_SUBONE;
        double x2 = (double)(c->x - a->x) / (double)OSMGA_MESA_SUBONE;
        double y2 = (double)(c->y - a->y) / (double)OSMGA_MESA_SUBONE;

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
        double x1 = (double)(b->x - a->x) / (double)OSMGA_MESA_SUBONE;
        double y1 = (double)(b->y - a->y) / (double)OSMGA_MESA_SUBONE;
        double x2 = (double)(c->x - a->x) / (double)OSMGA_MESA_SUBONE;
        double y2 = (double)(c->y - a->y) / (double)OSMGA_MESA_SUBONE;
        double den = x1 * y2 - x2 * y1;
        double d1 = ((double)b->z - (double)a->z) / (double)OSMGA_MESA_SUBONE;
        double d2 = ((double)c->z - (double)a->z) / (double)OSMGA_MESA_SUBONE;

        /* den is not zero: the caller above has already refused a triangle
         * with no area. */
        zplane.dx   = (d1 * y2 - d2 * y1) / den;
        zplane.dy   = (d2 * x1 - d1 * x2) / den;
        zplane.at_a = (double)a->z / (double)OSMGA_MESA_SUBONE;

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

    /*
     * The texture coordinates, as the engine counts them.
     *
     * A texel is 1 << (20 - ceil(log2 size)) and NOT 2^20 over the size: the
     * engine's log2 field names the power of two that CONTAINS the texture,
     * and the exact size travels beside it, so a texture that is not a power
     * of two spans less than the whole range.  The two axes scale
     * independently, because the width and the height have their own fields.
     */
    if (tex != 0) {
        double x1 = (double)(b->x - a->x) / (double)OSMGA_MESA_SUBONE;
        double y1 = (double)(b->y - a->y) / (double)OSMGA_MESA_SUBONE;
        double x2 = (double)(c->x - a->x) / (double)OSMGA_MESA_SUBONE;
        double y2 = (double)(c->y - a->y) / (double)OSMGA_MESA_SUBONE;
        double den = x1 * y2 - x2 * y1;
        double us = (double)tex->w * (double)(1L << (20L - osmgaLog2Ceil(tex->w)));
        double vs = (double)tex->h * (double)(1L << (20L - osmgaLog2Ceil(tex->h)));
        double ua = a->s * us, ub = b->s * us, uc = c->s * us;
        double va = a->tc * vs, vb = b->tc * vs, vc = c->tc * vs;
        double lo, hi;

        /*
         * The coordinate the ENGINE will sample is the same either way -- an
         * affine plane through the three vertex values, or the quotient of
         * two planes that reproduces it -- so the range check below is on the
         * unweighted values in both cases, and it is exact for the pixels
         * this triangle draws because every one of their centres is inside
         * the triangle.
         *
         * The kernel checks a wider box and may still refuse; that is what
         * the software fallback is for.
         */
        lo = hi = ua;
        if (ub < lo) lo = ub;   if (ub > hi) hi = ub;
        if (uc < lo) lo = uc;   if (uc > hi) hi = uc;
        if (lo < 0.0 || hi > (double)OSMGA_HW3D_TEX_COORD_MAX)
            return OSMGA_MESA_TRI_UNSUPPORTED;
        lo = hi = va;
        if (vb < lo) lo = vb;   if (vb > hi) hi = vb;
        if (vc < lo) lo = vc;   if (vc > hi) hi = vc;
        if (lo < 0.0 || hi > (double)OSMGA_HW3D_TEX_COORD_MAX)
            return OSMGA_MESA_TRI_UNSUPPORTED;

        if (a->qw == b->qw && a->qw == c->qw) {
            /* w is the same at all three, so the divide is a constant and
             * the coordinate interpolates straight.  Unchanged. */
            uplane.dx   = ((ub - ua) * y2 - (uc - ua) * y1) / den;
            uplane.dy   = ((uc - ua) * x1 - (ub - ua) * x2) / den;
            uplane.at_a = ua;
            vplane.dx   = ((vb - va) * y2 - (vc - va) * y1) / den;
            vplane.dy   = ((vc - va) * x1 - (vb - va) * x2) / den;
            vplane.at_a = va;
            qplane.at_a = 0.0;          /* nought says "affine" downstream */
            qplane.dx = qplane.dy = 0.0;
        } else if (!osmgaPerspPlanes(a, b, c, ua, ub, uc, va, vb, vc,
                                     x1, y1, x2, y2, den,
                                     &uplane, &vplane, &qplane)) {
            return OSMGA_MESA_TRI_UNSUPPORTED;
        }
    }

    /* Always, not only when blending: the destination's fourth byte is
     * written either way, and it has to hold what the software path would
     * have put there. */
    {
        double x1 = (double)(b->x - a->x) / (double)OSMGA_MESA_SUBONE;
        double y1 = (double)(b->y - a->y) / (double)OSMGA_MESA_SUBONE;
        double x2 = (double)(c->x - a->x) / (double)OSMGA_MESA_SUBONE;
        double y2 = (double)(c->y - a->y) / (double)OSMGA_MESA_SUBONE;
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
        double x1 = (double)(b->x - a->x) / (double)OSMGA_MESA_SUBONE;
        double y1 = (double)(b->y - a->y) / (double)OSMGA_MESA_SUBONE;
        double x2 = (double)(c->x - a->x) / (double)OSMGA_MESA_SUBONE;
        double y2 = (double)(c->y - a->y) / (double)OSMGA_MESA_SUBONE;
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
    /*
     * The side the middle vertex falls on, computed in WHOLE PIXELS.
     *
     * In 1/256 units this product overflows: a 191-row, 199-column triangle
     * gives 2490957824, which is past the signed range, and the sign flips --
     * measured, as a triangle drawn with its left and right edges exchanged.
     * The coordinates reach 8192 pixels, so no 32-bit product of two
     * differences can be exact at this scale.
     *
     * The overflow is real and the answer to it was not.  Reducing to whole
     * pixels threw away the fraction that decides the sides of a narrow
     * triangle, in two ways at once: the difference rounds to zero, and C's
     * division truncates toward zero rather than flooring, so a difference of
     * minus a third of a pixel came out as nought and minus one and a fifth
     * came out as minus one.  A coarse cross of ZERO was then answered with
     * zero -- "there is nothing to draw" -- and the hook takes that at its
     * word and draws nothing, with no software fallback.  Measured: slivers a
     * quarter, a half, three quarters and nine tenths of a pixel wide put 25,
     * 51, 34 and 27 pixels on the screen through the software path and NONE
     * through this one, with no counter recording it.
     *
     * A double does not overflow.  The coordinates are held to 8192 pixels by
     * osmgaCoordOK above, which is 2^21 in sixteenths of a sixteenth of a
     * pixel, so a difference is at most 2^22 and this determinant at most
     * 2^44 -- integers that far are exact in a double, whose limit is 2^53.
     * So the sign is exact and no triangle is lost or classified by a
     * rounding.  Casts before the multiplications, so that nothing is formed
     * in a long on the way.
     *
     * Mesa's own rasteriser decides the same thing the same way, from the
     * same sorted vertices: tritemp.h computes area = eMaj.dx * eBot.dy -
     * eBot.dx * eMaj.dy and reads ltor off its sign.  It uses a float, where
     * this uses a double on exact integers.
     */
    crossD = (double)(lo->x - t->x) * (double)(m->y - t->y)
           - (double)(lo->y - t->y) * (double)(m->x - t->x);
    /*
     * Unreachable: the area was tested exactly further up, and collinearity
     * does not depend on which vertex is named first.  Kept as a guard, and
     * answering UNSUPPORTED rather than zero, because zero is the answer that
     * loses the triangle.
     */
    if (crossD == 0.0)
        return OSMGA_MESA_TRI_UNSUPPORTED;

    /*
     * How much of the vertex fraction the registers can hold.
     *
     * The divisor is 2*(M*H) and the displacement 2*(M*|D|), and the kernel
     * holds the displacement and the error term to its edge budget, so the
     * binding condition is on their sum.  One value for the whole triangle,
     * never one per edge: two edges quantising a shared vertex differently
     * would put it in two places and open the seam the split runs along.
     */
    sub = OSMGA_MESA_SUBBITS;
    while (sub > 0L) {
        long step = 1L << (OSMGA_MESA_SUBBITS - sub);
        long worst = 0L;
        int i2, j2;

        for (i2 = 0; i2 < 3; i2++)
            for (j2 = 0; j2 < 3; j2++) {
                const OSMGAMesaVertex *p = (i2 == 0) ? t : ((i2 == 1) ? m : lo);
                const OSMGAMesaVertex *q = (j2 == 0) ? t : ((j2 == 1) ? m : lo);
                long dd = (q->x - p->x) / step, hh = (q->y - p->y) / step;

                if (dd < 0L) dd = -dd;
                if (hh < 0L) hh = -hh;
                if (2L * dd > worst) worst = 2L * dd;
                if (2L * (dd + hh) > worst) worst = 2L * (dd + hh);
            }
        if (worst <= (long)OSMGA_HW3D_EDGE_WALK) break;
        sub--;
    }

    /*
     * Which rows belong to which trapezoid, in SAMPLE ROW space rather than
     * in vertex coordinates: row r belongs where r + 1/2 falls.  With a
     * fractional vertex the first row and the height are no longer the
     * vertex's own numbers, which is the thing that made this rewrite
     * necessary in the first place.
     */
    rT = osmgaCeilDiv(t->y  - OSMGA_MESA_SUBONE / 2L, OSMGA_MESA_SUBONE);
    rM = osmgaCeilDiv(m->y  - OSMGA_MESA_SUBONE / 2L, OSMGA_MESA_SUBONE);
    rL = osmgaCeilDiv(lo->y - OSMGA_MESA_SUBONE / 2L, OSMGA_MESA_SUBONE);

    longE.xa = t->x; longE.ya = t->y;
    longE.dee = lo->x - t->x; longE.height = lo->y - t->y;

    if (rM > rT && m->y > t->y) {
        OSMGAMesaEdge shortE;
        const OSMGAMesaEdge *le, *re;
        long skip;

        shortE.xa = t->x; shortE.ya = t->y;
        shortE.dee = m->x - t->x; shortE.height = m->y - t->y;
        le = (crossD < 0.0) ? &longE : &shortE;
        re = (crossD < 0.0) ? &shortE : &longE;
        skip = osmgaFirstDrawn(le, re, rT, rM - rT, sub);
        if (skip < rM - rT) {
            osmgaTrapezoid(&out[n], rT + skip, rM - rT - skip, sub, le, re,
                           shade, plane, a, zmode, &zplane, blend, &aplane,
                           (tex != 0) ? &uplane : (const OSMGAColourPlane *)0,
                           (tex != 0) ? &vplane : (const OSMGAColourPlane *)0,
                           (tex != 0) ? &qplane : (const OSMGAColourPlane *)0,
                           (tex != 0 && tmrOut != 0) ? tmrOut[n] : (long *)0);
            n++;
        }
    }

    if (rL > rM && lo->y > m->y) {
        OSMGAMesaEdge shortE;
        const OSMGAMesaEdge *le, *re;
        long skip;

        shortE.xa = m->x; shortE.ya = m->y;
        shortE.dee = lo->x - m->x; shortE.height = lo->y - m->y;
        le = (crossD < 0.0) ? &longE : &shortE;
        re = (crossD < 0.0) ? &shortE : &longE;
        skip = osmgaFirstDrawn(le, re, rM, rL - rM, sub);
        if (skip < rL - rM) {
            osmgaTrapezoid(&out[n], rM + skip, rL - rM - skip, sub, le, re,
                           shade, plane, a, zmode, &zplane, blend, &aplane,
                           (tex != 0) ? &uplane : (const OSMGAColourPlane *)0,
                           (tex != 0) ? &vplane : (const OSMGAColourPlane *)0,
                           (tex != 0) ? &qplane : (const OSMGAColourPlane *)0,
                           (tex != 0 && tmrOut != 0) ? tmrOut[n] : (long *)0);
            n++;
        }
    }
    return n;
}

/*
 * The untextured entry point, which is every caller there was.  Kept so that
 * the shape of the old contract is unchanged: a texture is something a caller
 * asks for, not something it has to say no to.
 */
int
OSMGAMesaBuildTriangle(const OSMGAMesaVertex *a,
                       const OSMGAMesaVertex *b,
                       const OSMGAMesaVertex *c,
                       const OSMGAMesaVertex *flat,
                       unsigned long zmode,
                       unsigned long blend,
                       OSMGAHW3DTri *out)
{
    return OSMGAMesaBuildTriangleTex(a, b, c, flat, zmode, blend,
                                     (const OSMGAMesaTex *)0, out,
                                     (long (*)[9])0);
}
