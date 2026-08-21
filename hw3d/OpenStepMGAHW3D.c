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

        /* The clip bounds x and y, so the furthest coordinate a draw can
         * ask for is computable.  Keep it non-negative and inside the
         * magnification CLAMPUV was actually measured to hold. */
        if (!osmgaHW3DCoord(b->state.tmr[6], b->state.tmr[0],
                            b->state.tmr[2], lim->clipX1, lim->clipY1) ||
            !osmgaHW3DCoord(b->state.tmr[7], b->state.tmr[1],
                            b->state.tmr[3], lim->clipX1, lim->clipY1))
            return OSMGA_HW3D_E_TEXCOORD;
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
        {   /* How far the two edges can travel over this triangle.  ar1
             * and ar4 carry the same slope with the error term folded in,
             * so take whichever is larger of each pair. */
            unsigned long h = (unsigned long)t->h;
            unsigned long sl = (t->ar2 < 0L) ? (unsigned long)(-t->ar2)
                                             : (unsigned long)t->ar2;
            unsigned long s1 = (t->ar1 < 0L) ? (unsigned long)(-t->ar1)
                                             : (unsigned long)t->ar1;
            unsigned long sr = (t->ar5 < 0L) ? (unsigned long)(-t->ar5)
                                             : (unsigned long)t->ar5;
            unsigned long s4 = (t->ar4 < 0L) ? (unsigned long)(-t->ar4)
                                             : (unsigned long)t->ar4;

            if (s1 > sl) sl = s1;
            if (s4 > sr) sr = s4;
            if (sr > sl) sl = sr;
            /*
             * The value IS the travel; it must not be multiplied by the
             * height again.
             *
             * AR2 and AR5 hold an edge's total horizontal displacement over
             * AR0/AR6 rows -- X.Org's own trapezoid setup writes dx into one
             * and dy into the other -- so the distance an edge walks across
             * a triangle is that number, and dividing the budget by the
             * height made the test stricter by a factor of the height.
             *
             * It refused ordinary work.  Measured: a triangle Mesa had
             * clipped to a 320x240 surface, 236 rows tall with an edge moving
             * 179 pixels, was turned away because 179 exceeds 16384/236.  On
             * that surface nothing taller than 51 rows could have an edge
             * crossing the screen.  The constant's own comment gives "a
             * 768-row edge at one pixel per row is 768" as an example of
             * something far inside the limit, and the old form refused that
             * too -- 768 against 16384/768.
             *
             * Bounding the displacement still bounds the excursion, which is
             * what the check is for: an edge starts inside the clip and moves
             * at most this far, so containment does not rest on the clip
             * alone.
             */
            (void)h;
            if (sl > lim->maxEdgeWalk)
                return OSMGA_HW3D_E_TRISLOPE;
        }
        /*
         * AR0 and AR6 are what the edge accumulator divides by, and the
         * bound just above assumes the edge advances by its displacement
         * over that height.  Nothing checked them.  A zero divisor makes the
         * accumulator stop decreasing, so the edge walks on and on and the
         * slope bound stops meaning anything -- a client could pass a
         * displacement of one and still leave the rectangle.
         *
         * Requiring exactly the height is tighter than requiring non-zero,
         * and costs nothing: it is what every caller already writes, because
         * it is what makes the slope the ratio the geometry describes.
         */
        if (t->ar0 != t->h || t->ar6 != t->h)
            return OSMGA_HW3D_E_EDGEDIV;
        {
            unsigned long left  = t->fxbndry & 0xFFFFUL;
            unsigned long right = (t->fxbndry >> 16) & 0xFFFFUL;

            if (left > lim->clipX1 + 1UL || right > lim->clipX1 + 1UL ||
                left > right)
                return OSMGA_HW3D_E_TRICOL;
        }
    }
    if (badTri != 0)
        *badTri = 0UL;
    return OSMGA_HW3D_OK;
}
