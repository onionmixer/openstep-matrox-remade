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

/* Is start + spanX*incX + spanY*incY non-negative and within the reach we
 * have measured?  Written as divisions so no product can overflow. */
static int
osmgaHW3DCoord(long start, long incX, long incY,
               unsigned long spanX, unsigned long spanY)
{
    long room = (long)OSMGA_HW3D_TEX_COORD_MAX;
    long cx, cy, v;

    /*
     * The coordinate is a plane, so it is monotone in x and in y and its
     * extremes over the rectangle are at the four corners.  Checking those
     * settles every pixel between them.
     *
     * Increments used to be required non-negative, which was a bound from
     * when only increasing coordinates had been measured -- and it refused
     * roughly half of all real texture mapping, since a triangle whose
     * texture runs the other way across the screen has a negative gradient
     * and is in no way exotic.  Sign is allowed now; staying inside the
     * range is what is still required.
     *
     * The displacement across a span is checked before it is formed, and by
     * comparing against a bound and its negation rather than by taking the
     * increment's magnitude: negating the most negative long is undefined,
     * and a client supplies these.  That is not merely to avoid overflow: if it exceeds the whole legal range
     * then the values at the two ends differ by more than that range, so one
     * of them lies outside it whatever the start may be.  Having refused
     * that, every term below is at most the range plus a span, and three of
     * them together stay well inside a signed long.
     */
    if (spanX != 0UL) {
        long bound = room / (long)spanX;

        if (incX > bound || incX < -bound)
            return 0;
    }
    if (spanY != 0UL) {
        long bound = room / (long)spanY;

        if (incY > bound || incY < -bound)
            return 0;
    }

    cx = incX * (long)spanX;
    cy = incY * (long)spanY;
    for (v = 0L; v < 4L; v++) {
        long at = start + ((v & 1L) ? cx : 0L) + ((v & 2L) ? cy : 0L);

        if (at < 0L || at > room)
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
     * accepted at 256 and refused at 320.  And the coordinate restarts at
     * every primitive -- measured, by drawing one textured rectangle at two
     * different columns with the same TMR and getting the same ramp twice --
     * so the span that matters is the primitive's own.
     *
     * texEmpty is the honest fallback: a triangle with height but no columns
     * on any row is still encoded and still executed, so there is no ground
     * for saying it fetches nothing.  Such a batch keeps the old, wider
     * check rather than being trusted or waved through.
     */
    unsigned long texSpanX = 0UL, texSpanY = 0UL;
    int texEmpty = 0, texDrawn = 0;

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
            long bx0 = 0L, bx1 = 0L;
            int haveBox = 0;

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
            lx = (long)left;   rx = (long)right;
            lacc = t->ar1 - t->ar2;
            racc = t->ar4 - t->ar5;
            lsgn = (t->sgn & 0x2L)  ? -1L : 1L;
            rsgn = (t->sgn & 0x20L) ? -1L : 1L;

            if (opcode == OSMGA_HW3D_OPCODE_TEX) {
                bx0 = lx; bx1 = lx; haveBox = 0;
            }
            for (row = 0L; row < t->h; row++) {
                if (row > 0L) {
                    lacc += t->ar2;
                    while (lacc < 0L) {
                        lx += lsgn;
                        lacc += t->ar0;
                        if (lx < 0L || (unsigned long)lx > lim->clipX1 + 1UL)
                            return OSMGA_HW3D_E_TRICROSS;
                    }
                    racc += t->ar5;
                    while (racc < 0L) {
                        rx += rsgn;
                        racc += t->ar6;
                        if (rx < 0L || (unsigned long)rx > lim->clipX1 + 1UL)
                            return OSMGA_HW3D_E_TRICROSS;
                    }
                }
                if (lx > rx)
                    return OSMGA_HW3D_E_TRICROSS;
                /*
                 * Only rows that have columns.  lx == rx is a legal row with
                 * nothing in it, and rx - 1 there would run below zero.
                 */
                if (opcode == OSMGA_HW3D_OPCODE_TEX && lx < rx) {
                    if (!haveBox) { bx0 = lx; bx1 = rx - 1L; haveBox = 1; }
                    else {
                        if (lx < bx0)      bx0 = lx;
                        if (rx - 1L > bx1) bx1 = rx - 1L;
                    }
                }
            }
            if (opcode == OSMGA_HW3D_OPCODE_TEX) {
                if (!haveBox) {
                    texEmpty = 1;
                } else {
                    unsigned long sx = (unsigned long)(bx1 - bx0);
                    unsigned long sy = (unsigned long)t->h - 1UL;

                    texDrawn = 1;
                    if (sx > texSpanX) texSpanX = sx;
                    if (sy > texSpanY) texSpanY = sy;
                }
            }
        }
    }

    /*
     * The texture coordinate, now that the reach is known.
     *
     * Batch-global state, so the widest primitive in the batch decides -- and
     * a verdict about batch-global state belongs to no triangle, which is why
     * badTri goes back to zero before it is reported.
     */
    if (anyTex) {
        unsigned long spanX = lim->clipX1, spanY = lim->clipY1;

        if (texDrawn && !texEmpty) {
            spanX = texSpanX;
            spanY = texSpanY;
        }
        if (!osmgaHW3DCoord(b->state.tmr[6], b->state.tmr[0],
                            b->state.tmr[2], spanX, spanY) ||
            !osmgaHW3DCoord(b->state.tmr[7], b->state.tmr[1],
                            b->state.tmr[3], spanX, spanY)) {
            if (badTri != 0)
                *badTri = 0UL;
            return OSMGA_HW3D_E_TEXCOORD;
        }
    }
    if (badTri != 0)
        *badTri = 0UL;
    return OSMGA_HW3D_OK;
}
