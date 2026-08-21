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


int
osmgaHW3DValidate(const OSMGAHW3DBatch *b, const OSMGAHW3DLimits *lim,
                  unsigned long *badTri)
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

                if ((ex > 0L && (b->state.tmr[0] > room / ex ||
                                 b->state.tmr[0] < -(room / ex) ||
                                 b->state.tmr[1] > room / ex ||
                                 b->state.tmr[1] < -(room / ex))) ||
                    (ey > 0L && (b->state.tmr[2] > room / ey ||
                                 b->state.tmr[2] < -(room / ey))) ||
                    (vy > 0L && (b->state.tmr[3] > room / vy ||
                                 b->state.tmr[3] < -(room / vy))))
                    texBad = 1;
                else {
                    long ux, vx2, ly, ry;
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
                              + b->state.tmr[2] * dy;
                        vx2 = b->state.tmr[7] + b->state.tmr[1] * dx
                              + b->state.tmr[3] * (texSpanY + dy);
                        if (ux < 0L || ux > room || vx2 < 0L || vx2 > room)
                            boxOK = 0;
                    }
                    if (boxOK)
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
                              + b->state.tmr[2] * row;
                        vx2 = b->state.tmr[7] + b->state.tmr[1] * (lx - lx0)
                              + b->state.tmr[3] * (texSpanY + row);
                        ly = ux + b->state.tmr[0] * (rx - 1L - lx);
                        ry = vx2 + b->state.tmr[1] * (rx - 1L - lx);
                        if (ux < 0L || ux > room || ly < 0L || ly > room ||
                            vx2 < 0L || vx2 > room || ry < 0L || ry > room)
                            texBad = 1;
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
        /*
         * The starts bound the arithmetic above whether or not any pixel
         * samples them, so they are held to the range here; everything else
         * was decided per row, over the pixels that exist.
         */
        if (b->state.tmr[6] < -(long)OSMGA_HW3D_TEX_COORD_MAX ||
            b->state.tmr[6] > (long)OSMGA_HW3D_TEX_COORD_MAX ||
            b->state.tmr[7] < -(long)OSMGA_HW3D_TEX_COORD_MAX ||
            b->state.tmr[7] > (long)OSMGA_HW3D_TEX_COORD_MAX)
            texBad = 1;
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
