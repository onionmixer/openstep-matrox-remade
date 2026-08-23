/*
 * OpenStepMGAHW3D.c -- validation of a userland batch.  See the header.
 *
 * Every bound is written so no expression can overflow: comparisons are
 * arranged as `a > limit - b` rather than `a + b > limit`, because the
 * second wraps and the first does not.  The same discipline the S4a mmap
 * handler uses, and for the same reason -- being wrong here hands the
 * engine an address of the client's choosing.
 */
#include "OpenStepMGAHW3D.h"

#define OSMGA_HW3D_DEPTH_BYTES  2UL    /* MACCESS leaves depth 16-bit */

/* Does [org, org + rows*stride) fit inside [lo, hi)? */
static int
osmgaHW3DReach(unsigned long org, unsigned long rows, unsigned long stride,
               unsigned long lo, unsigned long hi)
{
    unsigned long span;

    if (hi <= lo || org < lo || org >= hi)
        return 0;
    if (stride != 0UL && rows > 0xFFFFFFFFUL / stride)
        return 0;
    span = rows * stride;
    if (span > hi - org)
        return 0;
    return 1;
}


/*
 * One row of the engine's edge walk, and the ONLY copy of the recurrence.
 *
 * The rule is measured, not fitted: between rows a += AR2, and while a is
 * negative x steps by the sign and a += AR0.  It used to be written out twice
 * in this file and once in the builder, and when the fitted version turned out
 * to be wrong all three were wrong together and the tests agreed with them.
 * Two passes over a triangle now share this.
 *
 * Returns 0 when an edge leaves the rectangle, which is the caller's cue to
 * refuse: the check is inside the loop because it is also what bounds the
 * number of steps.
 */
/*
 * Is 0 <= p * 65536 / q <= room, without dividing and without leaving a long?
 *
 * roomHi is room >> 16, exact because the coordinate bound is a multiple of
 * 65536, and q is bounded so the product cannot overflow.  At q = 65536 this
 * is p >= 0 && p <= room, which is the affine rule it replaces.
 */
static int
osmgaHW3DRatioOK(long p, long q, long roomHi)
{
    if (q < OSMGA_HW3D_Q_MIN || q > OSMGA_HW3D_Q_MAX)
        return 0;
    /*
     * Below nought by a sliver is admitted; the width is
     * OSMGA_HW3D_TEX_NEG_ALLOW, and it is written HERE in terms of that
     * constant rather than as the sixteen it works out to.  It used to be the
     * bare sixteen, with the constant named only in this comment -- so the
     * constant was documentation that nothing read, and changing it would
     * have changed nothing at all.
     *
     * The bound is against q so that it is the same COORDINATE at every
     * scale: p / q * 65536 >= -ALLOW is p >= -q * ALLOW / 65536, and the
     * divisor below is 65536 / ALLOW.  Written as a division of q it cannot
     * overflow, and the build refuses a width that does not divide 65536
     * exactly, since then the two forms would part company.
     */
    if (p < -(q / (long)(OSMGA_HW3D_Q_ONE / OSMGA_HW3D_TEX_NEG_ALLOW)))
        return 0;
    return p <= roomHi * q;
}

/* the denominator plane at an offset from the primitive's anchor */
static long
osmgaHW3DQAt(const OSMGAHW3DBatch *b, int persp, long dx, long dy)
{
    if (!persp)
        return OSMGA_HW3D_Q_ONE;
    return b->state.tmr[8] + b->state.tmr[4] * dx + b->state.tmr[5] * dy;
}


static int
osmgaHW3DStep(const OSMGAHW3DTri *t, long *lx, long *rx,
              long *lacc, long *racc, long lsgn, long rsgn,
              unsigned long clipX1)
{
    *lacc += t->ar2;
    while (*lacc < 0L) {
        *lx += lsgn;
        *lacc += t->ar0;
        if (*lx < 0L || (unsigned long)*lx > clipX1 + 1UL)
            return 0;
    }
    *racc += t->ar5;
    while (*racc < 0L) {
        *rx += rsgn;
        *racc += t->ar6;
        if (*rx < 0L || (unsigned long)*rx > clipX1 + 1UL)
            return 0;
    }
    return 1;
}


/*
 * The smallest addend a coordinate reaching this far can meet.
 *
 * Measured, in docs/M1_4D5_LADDER_ABOVE_2E20.md: the ladder that the header
 * writes down for the bands below 2^20 keeps stepping above it, so the flat
 * 496 stops being the smallest once a coordinate passes 2^20 and a
 * boundary-aligned one is pushed into the texel below.  The edges are the
 * measured ones -- exactly 2^20 still reads as the lower band, and the drop
 * happens inside the 512 units above each power of two -- so `<=` is right
 * here and is also the conservative side: naming a band too high only ever
 * takes off LESS than the engine puts back, which is the safe direction.
 */
long
osmgaHW3DTexBiasFor(long maxCoord)
{
    if (maxCoord <= (long)(1UL << 20)) return OSMGA_HW3D_TEX_BIAS;
    if (maxCoord <= (long)(1UL << 21)) return 480L;
    if (maxCoord <= (long)(1UL << 22)) return 448L;
    return 384L;
}

int
osmgaHW3DValidate(const OSMGAHW3DBatch *b, const OSMGAHW3DLimits *lim,
                  unsigned long *badTri)
{
    return osmgaHW3DValidateReach(b, lim, badTri, (OSMGAHW3DTexReach *)0);
}

int
osmgaHW3DValidateReach(const OSMGAHW3DBatch *b, const OSMGAHW3DLimits *lim,
                       unsigned long *badTri, OSMGAHW3DTexReach *reach)
{
    unsigned long i, rows, opcode, atype;
    int anyZI, anyTex;
    /*
     * How far a texture coordinate actually travels in this batch.
     *
     * It used to be extrapolated to the last column and row of the whole
     * destination, which is not where the coordinate is defined: a probe that
     * changed nothing but the declared width saw the same 32-pixel triangle
     * accepted at 256 and refused at 320.
     *
     * The two coordinates then turned out to behave differently, which is why
     * they are tracked apart here.  Measured, in one batch, at positions that
     * are not multiples of a texture:
     *
     *   u  is re-seeded at every primitive, at its own first row's left edge.
     *      Three primitives at three different columns all began at the same
     *      texel.
     *   v  is NOT re-seeded.  It runs on across the TEXTURED primitives of
     *      the batch: heights of five, eleven and three, with a flat
     *      primitive interposed, gave first rows of 3, 8 and 19 from a start
     *      of 3 -- the running sum of the textured heights, and the flat one
     *      did not move it.  An EMPTY textured primitive does move it: five
     *      drawn rows then six empty ones put the next primitive at 14.
     *
     *   u's ROW index re-seeds as well: with a gradient of one texel per row
     *      in u and nothing in x, two primitives both began at the same
     *      texel.  So u's vertical reach is one primitive's height and v's is
     *      the batch's total, and they are checked with different spans.
     *
     * Each batch writes TMR6 and TMR7 once, before its primitives, so the
     * accumulator starts afresh every submission -- read in the encoder and
     * then measured, twice through the same batch.
     */
    unsigned long texSpanLo = 0UL, texSpanHi = 0UL, texSpanY = 0UL;
    unsigned long texMaxH = 0UL;
    int texDrawn = 0, texBad = 0;

    if (reach != 0) {
        reach->uMax = 0L;
        reach->vMax = 0L;
    }
    if (badTri != 0)
        *badTri = 0UL;
    if (b == 0 || lim == 0)
        return OSMGA_HW3D_E_MAGIC;
    if (b->magic != OSMGA_HW3D_MAGIC)
        return OSMGA_HW3D_E_MAGIC;
    if (b->version != OSMGA_HW3D_VERSION)
        return OSMGA_HW3D_E_VERSION;

    /* The count is checked against both the compiled-in array and the
     * buffer the client was actually given, because the two can differ if
     * the window is ever resized. */
    if (b->triCount > OSMGA_HW3D_MAX_TRI)
        return OSMGA_HW3D_E_COUNT;
    if (lim->batchBytes < sizeof(OSMGAHW3DBatch) -
                          sizeof(OSMGAHW3DTri) * OSMGA_HW3D_MAX_TRI)
        return OSMGA_HW3D_E_COUNT;
    {
        unsigned long fixed = sizeof(OSMGAHW3DBatch) -
                              sizeof(OSMGAHW3DTri) * OSMGA_HW3D_MAX_TRI;
        unsigned long room = (lim->batchBytes - fixed) / sizeof(OSMGAHW3DTri);

        if (b->triCount > room)
            return OSMGA_HW3D_E_COUNT;
    }

    rows = lim->clipY1 + 1UL;

    /* Colour: the kernel's clip bounds the rows, so the origin plus that
     * many rows must stay inside the window we own. */
    /*
     * The pitch decides how far apart the rows are and therefore everything
     * below, so it is checked here rather than only by whoever calls this.
     * The equality is the point: the caller passes the pitch in bytes and the
     * batch carries it in pixels, and a validator that trusted one while the
     * engine was programmed from the other would be measuring a rectangle
     * nobody was going to draw.  Compared by dividing, so a pitch large
     * enough to overflow a multiply is refused rather than wrapping into
     * agreement.
     */
    if (b->state.dstPitch == 0UL ||
        b->state.dstWidth > b->state.dstPitch ||
        lim->pitchBytes / 4UL != b->state.dstPitch)
        return OSMGA_HW3D_E_DSTPITCH;
    /*
     * And a pitch the hardware can actually hold.
     *
     * Without this the engine accepts the batch and draws somewhere else --
     * measured, it covered as little as one per cent of what the software
     * path covered, and nothing anywhere said no.  It is refused HERE rather
     * than only in the library because the library is not the only caller:
     * everything the checks below promise about staying inside the window is
     * computed from this pitch, and if the hardware is walking a different
     * one those promises are about a picture nobody is drawing.
     *
     * The width is deliberately not constrained -- 333 pixels inside a pitch
     * of 352 is a perfectly good surface.  It is the PITCH the engine walks.
     */
    if ((b->state.dstPitch % OSMGA_HW3D_PITCH_ALIGN) != 0UL)
        return OSMGA_HW3D_E_DSTPITCH;

    if (!osmgaHW3DReach(b->state.dstorg, rows, lim->pitchBytes,
                        lim->colourStart, lim->colourEnd))
        return OSMGA_HW3D_E_DSTORG;

    /*
     * Which origins have to be bounded depends on what the triangles ask
     * for, and the test is ANY rather than ALL: one depth triangle in a
     * batch of otherwise flat ones still addresses depth.  Writing this as
     * "all of them" would let exactly that triangle draw against an origin
     * nobody checked.
     */
    anyZI = 0;
    anyTex = 0;
    for (i = 0UL; i < b->triCount; i++) {
        unsigned long d = b->tri[i].dwgctl & OSMGA_HW3D_DWG_CLIENT;

        if (((d >> 4) & 0x7UL) == OSMGA_HW3D_ATYPE_ZI) anyZI = 1;
        if ((d & 0xFUL) == OSMGA_HW3D_OPCODE_TEX)      anyTex = 1;
    }

    if (anyZI) {
        unsigned long zstride = (lim->pitchBytes / 4UL) * OSMGA_HW3D_DEPTH_BYTES;

        if (!osmgaHW3DReach(b->state.zorg, rows, zstride,
                            lim->depthStart, lim->depthEnd))
            return OSMGA_HW3D_E_ZORG;
    }
    if (anyTex) {
        unsigned long w = b->state.texW, h = b->state.texH;
        unsigned long pitch = b->state.texPitch;

        if (w == 0UL || h == 0UL ||
            w > OSMGA_HW3D_TEX_MAX_DIM || h > OSMGA_HW3D_TEX_MAX_DIM ||
            pitch < w || pitch > OSMGA_HW3D_TEX_MAX_PIT)
            return OSMGA_HW3D_E_TEXSIZE;
        if (b->state.texFormat != OSMGA_HW3D_TEXFMT_TW32)
            return OSMGA_HW3D_E_TEXSIZE;
        /*
         * The starts, BEFORE anything is evaluated with them.
         *
         * They used to be held to the range after the primitive loop, on the
         * grounds that they bound the arithmetic whether or not a pixel
         * samples them.  That is true of what they bound and false of when:
         * every coordinate the loop forms is the start plus two bounded
         * products, so a start near the end of a long overflows inside the
         * loop and is only rejected afterwards, by which time the value that
         * was checked is the wrapped one.  A long is four bytes here, which
         * the build asserts, so this is not hypothetical.
         */
        if (b->state.tmr[6] < -(long)OSMGA_HW3D_TEX_COORD_MAX ||
            b->state.tmr[6] > (long)OSMGA_HW3D_TEX_COORD_MAX ||
            b->state.tmr[7] < -(long)OSMGA_HW3D_TEX_COORD_MAX ||
            b->state.tmr[7] > (long)OSMGA_HW3D_TEX_COORD_MAX)
            return OSMGA_HW3D_E_TEXCOORD;
        /* Reach from the size the client gave, which is the size the
         * kernel will program: pitch texels of four bytes, h rows. */
        if (!osmgaHW3DReach(b->state.texorg, h, pitch * 4UL,
                            lim->texStart, lim->texEnd))
            return OSMGA_HW3D_E_TEXORG;

        /* The coordinate check is after the triangle loop, because what it
         * needs -- how far the drawing actually reaches -- is what that loop
         * works out. */
    }

    for (i = 0UL; i < b->triCount; i++) {
        const OSMGAHW3DTri *t = &b->tri[i];

        if (badTri != 0)
            *badTri = i;

        /* The mask has already removed everything outside opcode, atype
         * and zmode; what is left is to reject values those fields do not
         * define.  zmode is not checked: every value only decides whether
         * a pixel is written, never where. */
        opcode = t->dwgctl & 0xFUL;
        atype  = (t->dwgctl >> 4) & 0x7UL;
        if ((opcode != OSMGA_HW3D_OPCODE_TRAP &&
             opcode != OSMGA_HW3D_OPCODE_TEX) ||
            (atype != OSMGA_HW3D_ATYPE_I && atype != OSMGA_HW3D_ATYPE_ZI))
            return OSMGA_HW3D_E_DWGCTL;

        {   /* Encodings the register documentation never names.  None of
             * them moves a write, but a field whose meaning is unknown is
             * not something to hand to a client. */
            unsigned long ac = t->alphactrl & OSMGA_HW3D_AC_CLIENT;

            if ((ac & 0xFUL) > OSMGA_HW3D_AC_SRC_MAX ||
                ((ac >> 4) & 0xFUL) > OSMGA_HW3D_AC_DST_MAX ||
                ((ac >> 8) & 0x3UL) == 0x3UL ||          /* amode RSVD */
                ((ac >> 13) & 0x7UL) == 0x1UL)           /* atmode has no macro */
                return OSMGA_HW3D_E_ALPHA;
        }

        if (t->y < 0L || t->h <= 0L)
            return OSMGA_HW3D_E_TRIROW;
        if ((unsigned long)t->y > lim->clipY1)
            return OSMGA_HW3D_E_TRIROW;
        if ((unsigned long)t->h > lim->clipY1 + 1UL - (unsigned long)t->y)
            return OSMGA_HW3D_E_TRIROW;
        {
            /*
             * How far each edge may travel across this triangle.
             *
             * AR2 and AR5 hold an edge's total horizontal displacement over
             * AR0/AR6 rows -- X.Org's own trapezoid setup writes dx into one
             * and dy into the other -- so the distance an edge walks is that
             * number.  It used to be compared against the budget divided by
             * the height, which bounds the displacement TIMES the height and
             * is stricter by a factor of it.  Measured on hardware: a
             * triangle Mesa had clipped to a 320x240 surface, 236 rows tall
             * with an edge moving 179 pixels, was refused because 179 exceeds
             * 16384/236.  The constant's own comment offers "a 768-row edge
             * at one pixel per row is 768" as comfortably inside the limit,
             * and the old form refused that too.
             *
             * Compared against the bound AND ITS NEGATION rather than by
             * taking a magnitude, for the reason this file already gives
             * where texture increments are checked: negating the most
             * negative long is undefined, and every one of these comes from
             * a client.
             *
             * AR1 and AR4 carry the same displacement with the edge
             * accumulator's error term folded in, so they are bounded too.
             */
            long lim2 = (long)lim->maxEdgeWalk;

            if (t->ar2 > lim2 || t->ar2 < -lim2 ||
                t->ar5 > lim2 || t->ar5 < -lim2 ||
                t->ar1 > lim2 || t->ar1 < -lim2 ||
                t->ar4 > lim2 || t->ar4 < -lim2)
                return OSMGA_HW3D_E_TRISLOPE;
        }
        /*
         * AR0 and AR6 are what the edge accumulator divides by.  A divisor of
         * zero makes it stop decreasing, so the edge walks on and on and the
         * slope bound above stops meaning anything -- a client could pass a
         * displacement of one and still leave the rectangle.
         *
         * This used to demand exactly the trapezoid's height, on the grounds
         * that it was tighter and cost nothing.  It was not free.  The
         * divisor belongs to the EDGE, and a triangle split at its middle
         * vertex has one edge spanning both halves; forcing the two to agree
         * is what made the lower half restart from a rounded position and
         * leave the rasterisation rule (3-12).
         */
        if (t->ar0 <= 0L || t->ar6 <= 0L)
            return OSMGA_HW3D_E_EDGEDIV;
        /*
         * Displacements go in negated, always.  A positive one is not a
         * direction -- SGN carries that -- it is a value this walk cannot
         * read, and taking its magnitude below would then be wrong.
         */
        if (t->ar2 > 0L || t->ar5 > 0L)
            return OSMGA_HW3D_E_TRISLOPE;
        {
            unsigned long left  = t->fxbndry & 0xFFFFUL;
            unsigned long right = (t->fxbndry >> 16) & 0xFFFFUL;
            long lx, rx, lacc, racc, lsgn, rsgn, row;
            long bx0 = 0L, bx1 = 0L, lx0;
            int haveBox = 0, drewSome = 0;

            if (left > lim->clipX1 + 1UL || right > lim->clipX1 + 1UL ||
                left > right)
                return OSMGA_HW3D_E_TRICOL;

            /*
             * Then walk both edges the way the engine does, and require a
             * span on every row rather than only at the ends.
             *
             *     a = AR1 - AR2 ;  row 0 is emitted where FXBNDRY says
             *     between rows:  a += AR2 ;  while a < 0:  x += sgn ; a += AR0
             *
             * That recurrence is measured, and the one this first carried was
             * not.  Its predecessor came from a fit taken entirely with AR1
             * equal to AR2, which every batch this driver had ever sent
             * satisfied -- and under that constraint the two rules emit the
             * same pixels, so the fit could not have chosen.  A three-way
             * probe separated them: 8 rows of 8 for this one against 7 and 6,
             * and 20 of 20 against 11 and 1 on a control.  A validator that
             * predicts the wrong columns refuses correct work and passes
             * incorrect work, so this is not a detail.
             *
             * Bounding each edge's travel on its own is not enough either:
             * the difference of two monotone sequences is not monotone, so
             * two edges whose first and last rows are in order can still
             * cross in between.  Constructed rather than imagined.
             *
             * The work is bounded.  Every column step is checked against the
             * rectangle and refused the moment it leaves it, and a walk only
             * ever moves one way, so no edge can take more steps than the
             * rectangle is wide.
             */
            /*
             * Only the two direction bits this walk models.
             *
             * The encoder passes sgn to the engine unmasked, and everything
             * the texture reach now rests on comes from predicting which
             * columns get drawn.  A bit that changes how an edge walks would
             * make that prediction describe a different shape than the one
             * the hardware draws, so a bit that is not modelled is refused
             * rather than assumed inert.
             */
            if ((t->sgn & ~0x22L) != 0L)
                return OSMGA_HW3D_E_TRISGN;

            lx = (long)left;   rx = (long)right;
            lacc = t->ar1 - t->ar2;
            racc = t->ar4 - t->ar5;
            lsgn = (t->sgn & 0x2L)  ? -1L : 1L;
            rsgn = (t->sgn & 0x20L) ? -1L : 1L;

            /* Where this primitive's coordinate is anchored: the left edge
             * of its first row, before any stepping. */
            lx0 = lx;
            if (opcode == OSMGA_HW3D_OPCODE_TEX) {
                bx0 = lx; bx1 = lx; haveBox = 0;
            }
            for (row = 0L; row < t->h; row++) {
                if (row > 0L &&
                    !osmgaHW3DStep(t, &lx, &rx, &lacc, &racc, lsgn, rsgn,
                                   lim->clipX1))
                    return OSMGA_HW3D_E_TRICROSS;
                if (lx > rx)
                    return OSMGA_HW3D_E_TRICROSS;
                /*
                 * Only rows that have columns.  lx == rx is a legal row with
                 * nothing in it, and rx - 1 there would run below zero.
                 */
                /*
                 * Every row, not only the rows with columns in them.
                 *
                 * A row where lx equals rx draws nothing, but its span still
                 * sits somewhere, and the rows around it can be elsewhere
                 * entirely -- an edge that walks out to column a hundred while
                 * empty and comes back to draw at zero would have left the box
                 * describing a shape the primitive does not have.  The empty
                 * row contributes its position and no width.
                 */
                if (opcode == OSMGA_HW3D_OPCODE_TEX) {
                    long hiCol = (lx < rx) ? rx - 1L : lx;

                    if (!haveBox) { bx0 = lx; bx1 = hiCol; haveBox = 1; }
                    else {
                        if (lx < bx0)    bx0 = lx;
                        if (hiCol > bx1) bx1 = hiCol;
                    }
                    if (lx < rx)
                        drewSome = 1;
                }
            }
            if (opcode == OSMGA_HW3D_OPCODE_TEX && haveBox && !texBad) {
                /*
                 * The coordinate, over the pixels this primitive actually
                 * draws.
                 *
                 * It used to be read at the four corners of the bounding
                 * rectangle, and a primitive is not its rectangle: the first
                 * real textured triangle drawn through this driver was refused
                 * because a negative dv/dx put v at -56742 in the corner above
                 * its widest row, where the triangle has no pixel at all.  Over
                 * the 4410 pixels it does draw, v ran from 8615 to 1027632 --
                 * inside the range with room to spare.
                 *
                 * Within a row the coordinate is linear in x, so its extremes
                 * there are at the two ends of the span; over every row, that
                 * is exact for the whole primitive.  Checked against a brute
                 * force over sixty thousand random trapezoids.
                 *
                 * The increments are bounded first, against THIS primitive's
                 * own extent rather than the surface's -- bounding them
                 * against the surface is the mistake this replaces.  A
                 * gradient that moves the coordinate by more than the whole
                 * permitted range across the primitive is refused, whether or
                 * not the two axes would have cancelled: that bound is what
                 * keeps every sum below inside a long, and no texture mapping
                 * this back end offers comes near it.
                 */
                long ex = bx1 - bx0, ey = t->h - 1L;
                long vy = texSpanY + ey;    /* v's row index runs on */

                long room = (long)OSMGA_HW3D_TEX_COORD_MAX;
                long roomHi = room >> 16;
                int persp = (b->state.texFlags
                             & OSMGA_HW3D_TEXF_PERSP) != 0UL;

                /*
                 * The denominator plane's own bounds, before it is evaluated
                 * anywhere: the anchor inside the range the divider has been
                 * looked at over, and the two slopes small enough that
                 * evaluating q across a surface cannot leave a long.  The
                 * corner checks below then work on values that are known to
                 * be representable.
                 */
                if (persp) {
                    /*
                     * Bound the slopes by the indices q is ACTUALLY evaluated
                     * at, not by the surface.
                     *
                     * The first version of this used dstWidth and dstHeight,
                     * and that is not a bound: dx is measured from the
                     * primitive's own left edge, which a clipped primitive
                     * can sit far to the left of, and the row index is
                     * texSpanY + row, which accumulates across every earlier
                     * textured primitive in the batch and so passes dstHeight
                     * as soon as there are a few of them.  The check would
                     * have admitted a slope whose evaluation leaves a long.
                     *
                     * Half the budget to each axis, each as a division so the
                     * check cannot overflow in doing its job.
                     */
                    long budget = ((1L << 30) - OSMGA_HW3D_Q_MAX) / 2L;
                    long dxlo = (long)bx0 - lx0, dxhi = (long)bx1 - lx0;
                    long mdx, mrow;
                    long a4 = (b->state.tmr[4] < 0L)
                              ? -b->state.tmr[4] : b->state.tmr[4];
                    long a5 = (b->state.tmr[5] < 0L)
                              ? -b->state.tmr[5] : b->state.tmr[5];

                    if (dxlo < 0L) dxlo = -dxlo;
                    if (dxhi < 0L) dxhi = -dxhi;
                    mdx = (dxlo > dxhi) ? dxlo : dxhi;
                    mrow = (long)texSpanY + t->h;
                    if (mdx < 1L) mdx = 1L;
                    if (mrow < 1L) mrow = 1L;
                    if (b->state.tmr[8] < OSMGA_HW3D_Q_MIN ||
                        b->state.tmr[8] > OSMGA_HW3D_Q_MAX ||
                        a4 > budget / mdx ||
                        a5 > budget / mrow)
                        texBad = 1;
                }

                /*
                 * A slope is bounded against the span of ITS OWN axis: what a
                 * slope in y can displace is itself times the height.  This
                 * had tmr[1] against the width and tmr[2] against the height,
                 * which is the same transposition as below and just as wrong.
                 *
                 *      tmr[0] = ds/dx   width      tmr[1] = ds/dy   height
                 *      tmr[2] = dt/dx   width      tmr[3] = dt/dy   height
                 */
                if ((ex > 0L && (b->state.tmr[0] > room / ex ||
                                 b->state.tmr[0] < -(room / ex) ||
                                 b->state.tmr[2] > room / ex ||
                                 b->state.tmr[2] < -(room / ex))) ||
                    (ey > 0L && (b->state.tmr[1] > room / ey ||
                                 b->state.tmr[1] < -(room / ey))) ||
                    (vy > 0L && (b->state.tmr[3] > room / vy ||
                                 b->state.tmr[3] < -(room / vy))))
                    texBad = 1;
                else {
                    long ux, vx2, ly, ry, qa, qb;
                    int boxOK = 1;
                    long k;

                    /*
                     * The cheap answer first.  The box contains every pixel
                     * the primitive draws, so a coordinate that stays in
                     * range over the box stays in range over the pixels --
                     * and then there is nothing to walk.  Only when the box
                     * says no is the exact walk needed, which is the case the
                     * box is wrong about.
                     */
                    for (k = 0L; k < 4L; k++) {
                        long dx = (k & 1L) ? (bx1 - lx0) : (bx0 - lx0);
                        long dy = (k & 2L) ? ey : 0L;

                        ux  = b->state.tmr[6] + b->state.tmr[0] * dx
                              + b->state.tmr[1] * dy;
                        vx2 = b->state.tmr[7] + b->state.tmr[2] * dx
                              + b->state.tmr[3] * (texSpanY + dy);
                        /*
                         * The denominator's row index is the accumulated
                         * count of textured rows in the batch, exactly as v's
                         * is -- measured, by leaving a gap between two
                         * primitives and finding that the far one reads what
                         * the near one's rows left behind rather than what
                         * its own screen position would give.
                         */
                        qa = osmgaHW3DQAt(b, persp, dx,
                                          (long)texSpanY + dy);
                        if (!osmgaHW3DRatioOK(ux, qa, roomHi) ||
                            !osmgaHW3DRatioOK(vx2, qa, roomHi))
                            boxOK = 0;
                    }
                    /*
                     * The shortcut stays for validation, but it cannot be
                     * taken when the reach is wanted: the reach decides which
                     * addend the encoder takes off, and the BOX is not the
                     * primitive.  Measured -- the split triangle the texture
                     * test draws has a box that carries v to 1052846 while
                     * its pixels stop at 1039076, one side of 2^20 each, so
                     * the box picks a band the primitive never enters and
                     * moves the phase of every pixel in it.
                     *
                     * Perspective still takes the shortcut.  Its bias is left
                     * at 496 either way, and the row walk checks the quotient
                     * at per-row denominators where the box checks it at the
                     * corners' -- so walking a primitive the box has already
                     * passed could refuse it, which would be a change in what
                     * the driver accepts rather than in what it encodes.
                     */
                    if (boxOK && (reach == 0 || persp))
                        goto texDone;

                    lx = (long)left; rx = (long)right;
                    lacc = t->ar1 - t->ar2;
                    racc = t->ar4 - t->ar5;
                    for (row = 0L; row < t->h; row++) {
                        if (row > 0L)
                            (void)osmgaHW3DStep(t, &lx, &rx, &lacc, &racc,
                                                lsgn, rsgn, lim->clipX1);
                        if (lx >= rx)
                            continue;           /* no pixel on this row */
                        ux  = b->state.tmr[6] + b->state.tmr[0] * (lx - lx0)
                              + b->state.tmr[1] * row;
                        vx2 = b->state.tmr[7] + b->state.tmr[2] * (lx - lx0)
                              + b->state.tmr[3] * (texSpanY + row);
                        ly = ux + b->state.tmr[0] * (rx - 1L - lx);
                        ry = vx2 + b->state.tmr[2] * (rx - 1L - lx);
                        /*
                         * The accumulated row index here too.  This walk had
                         * the primitive's own row, which is the reading the
                         * measurement rules out -- and unlike the box above
                         * it checked only that one, so it was checking a
                         * denominator the engine does not use.
                         */
                        qa = osmgaHW3DQAt(b, persp, lx - lx0,
                                          (long)texSpanY + row);
                        qb = osmgaHW3DQAt(b, persp, rx - 1L - lx0,
                                          (long)texSpanY + row);
                        if (!osmgaHW3DRatioOK(ux,  qa, roomHi) ||
                            !osmgaHW3DRatioOK(vx2, qa, roomHi) ||
                            !osmgaHW3DRatioOK(ly,  qb, roomHi) ||
                            !osmgaHW3DRatioOK(ry,  qb, roomHi))
                            texBad = 1;
                        /*
                         * And how far it reaches, from the same four values:
                         * the coordinate is linear along a row, so a row's
                         * extremes are its two ends.  The slopes have been
                         * bounded above, so these products are inside a long;
                         * the walk that builds the box runs before that bound
                         * and could not have formed them.
                         */
                        /*
                         * Affine only, and that is a contract rather than an
                         * optimisation: a perspective batch that passes the
                         * box jumps out with the reach still at nought, so
                         * one that fails the box and walks would otherwise
                         * come back with numerators in it and the two would
                         * mean different things.  Nought always, for
                         * perspective, and the encoder's own check that it is
                         * affine is then a second lock rather than the only
                         * one.
                         */
                        if (reach != 0 && !persp) {
                            if (ux  > reach->uMax) reach->uMax = ux;
                            if (ly  > reach->uMax) reach->uMax = ly;
                            if (vx2 > reach->vMax) reach->vMax = vx2;
                            if (ry  > reach->vMax) reach->vMax = ry;
                        }
                    }
                }
              texDone: ;
            }

            if (opcode == OSMGA_HW3D_OPCODE_TEX) {
                /*
                 * v does not restart at a primitive; it runs on.
                 *
                 * Measured: three textured primitives with one start of three
                 * texels and eight rows each began at v = 3, 11 and 19, and a
                 * flat primitive in front of a textured one left it at 3.  So
                 * the vertical coordinate is an accumulator over the rows of
                 * the TEXTURED primitives in the batch, and the span to check
                 * is their total height, not the tallest of them.  Taking the
                 * maximum was an under-check by a factor of the batch's
                 * length -- two hundred, at the cap.
                 *
                 * Counted for an empty primitive too: it is still executed,
                 * and whether the accumulator steps for a row that draws
                 * nothing is not measured.  Counting it is the conservative
                 * of the two answers.
                 */
                texSpanY += (unsigned long)t->h;
                if ((unsigned long)t->h > texMaxH)
                    texMaxH = (unsigned long)t->h;
                /*
                 * A textured primitive that draws nothing anywhere is
                 * refused.  It is still encoded and still executed -- it
                 * steps the vertical accumulator like any other, which is
                 * measured -- so the texture unit is running for it, and
                 * where it fetches cannot be observed, because a fetch that
                 * writes no pixel leaves no trace.  The old answer was to
                 * widen the horizontal check to the whole clip, which
                 * covered only the columns to the RIGHT of where it started:
                 * an empty span whose edges walk left sits at negative
                 * offsets that the widening never reached.  Refusing makes
                 * no claim about it, and nothing is lost, since it draws no
                 * pixel by construction.
                 */
                if (!drewSome) {
                    if (badTri != 0)
                        *badTri = i;
                    return OSMGA_HW3D_E_TRIEMPTY;
                }
                {
                    /* Both sides of the anchor.  A left edge that opens
                     * leftward puts pixels before it; one that closes puts
                     * none, and the clamp keeps that at zero. */
                    long lo = lx0 - bx0, hi = bx1 - lx0;

                    if (lo < 0L) lo = 0L;
                    if (hi < 0L) hi = 0L;
                    texDrawn = 1;
                    if ((unsigned long)lo > texSpanLo) texSpanLo = (unsigned long)lo;
                    if ((unsigned long)hi > texSpanHi) texSpanHi = (unsigned long)hi;
                }
            }
        }
    }

    /*
     * The texture coordinate, now that the reach is known.
     *
     * Batch-global state.  Horizontally the widest primitive decides, because
     * u restarts at each one; vertically the TOTAL decides, because v does
     * not.  A verdict about batch-global state belongs to no triangle, which
     * is why badTri goes back to zero before it is reported.
     */
    if (anyTex) {
        unsigned long spanLo = 0UL, spanHi = lim->clipX1;
        unsigned long spanY = lim->clipY1;   /* v: the batch's total */
        unsigned long spanUY = lim->clipY1;  /* u: one primitive's height */

        /*
         * Vertically the total always, because that is what was measured and
         * an empty primitive steps the accumulator like any other.  The
         * fallback below is horizontal only.
         */
        /*
         * u and v want different vertical spans, and both were measured.
         *
         * u re-seeds at every primitive -- with a gradient of one texel per
         * row in u, two primitives both began at the same texel -- so its
         * row index only ever runs the height of one primitive.  v does not
         * re-seed, so its row index runs the batch's total.
         */
        if (texMaxH > 0UL)
            spanUY = texMaxH - 1UL;
        if (texSpanY > 0UL)
            spanY = texSpanY - 1UL;     /* the accumulator's last step */
        if (texDrawn) {
            spanLo = texSpanLo;
            spanHi = texSpanHi;
        }
        (void)spanLo; (void)spanHi; (void)spanUY; (void)spanY;
        if (texBad) {
            if (badTri != 0)
                *badTri = 0UL;
            return OSMGA_HW3D_E_TEXCOORD;
        }
    }
    if (badTri != 0)
        *badTri = 0UL;
    return OSMGA_HW3D_OK;
}
