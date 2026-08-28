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

/*
 * The ALPHACTRL combinations.  See the header for which three and why the
 * fourth is missing.
 */
int
osmgaHW3DAlphaCross(unsigned long ac)
{
    unsigned long src  = ac & 0xFUL;
    unsigned long dst  = (ac >> 4) & 0xFUL;
    unsigned long mode = (ac >> 8) & 0x3UL;
    unsigned long stip = (ac >> 11) & 0x1UL;

    /* 2: video alpha needs somewhere to blend into. */
    if (mode == OSMGA_HW3D_AC_AM_VIDEO && dst == OSMGA_HW3D_AC_BF_ZERO)
        return 0;

    /* 3: and it cannot be approximated by the stipple at the same time. */
    if (mode == OSMGA_HW3D_AC_AM_VIDEO && stip != 0UL)
        return 0;

    /* 4: the stipple supports four pairs and no others.  Written as the
     * four rather than as a rule, because the spec gives four rows and
     * inventing the rule behind them would be inventing. */
    if (stip != 0UL) {
        int ok =
            (src == OSMGA_HW3D_AC_BF_ZERO   && dst == OSMGA_HW3D_AC_BF_ONE)    ||
            (src == OSMGA_HW3D_AC_BF_ONE    && dst == OSMGA_HW3D_AC_BF_ZERO)   ||
            (src == OSMGA_HW3D_AC_BF_SRCA   && dst == OSMGA_HW3D_AC_BF_OMSRCA) ||
            (src == OSMGA_HW3D_AC_BF_OMSRCA && dst == OSMGA_HW3D_AC_BF_SRCA);
        if (!ok)
            return 0;
    }

    /* Rule 1 -- SRC_ALPHA_SATURATE with dst ZERO -- is deliberately not
     * here.  It is measured; see the header. */
    (void)OSMGA_HW3D_AC_SRC_SAT;
    return 1;
}

/*
 * One field, checked and encoded.  See the header for the contract.
 *
 * The mask is not a repair of a wrong value: for a 22-bit field, -1 IS
 * 0x3fffff, and the thirty-two-bit 0xffffffff the producer hands over is
 * the same number wearing a wider coat.  What the mask removes is the
 * sign extension that would otherwise land in bits the spec says must be
 * zero.  A value that does NOT fit is a different matter and is refused,
 * because masking it would hand the engine exactly the truncated number it
 * already computes for itself -- this repository has that measured, in the
 * Mesa clamp's own comment: 255 * 119 becomes 0x3B448000 "of which the
 * hardware would see 0x448000, or +137, and paint a gradient nobody asked
 * for".
 */
int
osmgaHW3DField(long v, unsigned bits, unsigned long *out)
{
    unsigned long span;
    long hi, lo;

    if (bits == 0U || bits >= 32U)
        return 0;

    span = 1UL << (bits - 1U);
    hi   = (long)(span - 1UL);
    lo   = -hi - 1L;                    /* two's complement is asymmetric */

    if (v < lo || v > hi)
        return 0;

    if (out != 0)
        *out = ((unsigned long)v) & ((1UL << bits) - 1UL);
    return 1;
}

/*
 * The secondary DMA range check.  See the header.
 *
 * Every bound is a subtraction.  An earlier draft of this said in its own
 * comment that it wrote them that way and then wrote
 * `ringPhys + SEC_OFF + SEC_BYTES`, which is the exact form the sentence
 * claimed to avoid -- on a 32-bit target that sum is not a proof of
 * anything.  So: no expression here adds two values that could carry.
 */
int
osmgaHW3DSecRange(unsigned long ringPhys,
                  unsigned long secStart, unsigned long secEnd)
{
    unsigned long base, len, into;

    /* The region base itself must not wrap. */
    if (ringPhys > 0xFFFFFFFFUL - OSMGA_HW3D_SEC_OFF)
        return 0;
    base = ringPhys + OSMGA_HW3D_SEC_OFF;

    if (secEnd <= secStart)          return 0;   /* 4-13: length 0 forbidden */
    if (secStart < base)             return 0;

    len  = secEnd - secStart;
    into = secStart - base;

    if (len > OSMGA_HW3D_SEC_BYTES)  return 0;
    if (into > OSMGA_HW3D_SEC_BYTES - len)
        return 0;                                /* subtraction, not a sum */

    /* Whole packets only.  A length that is dword-aligned but not a
     * multiple of twenty leaves the channel ending partway through one,
     * and the parser resumes at "the last Pseudo-DMA location" (4-16). */
    if (len < OSMGA_HW3D_SEC_PACKET) return 0;
    if ((len % OSMGA_HW3D_SEC_PACKET) != 0UL) return 0;

    if ((secStart & 3UL) != 0UL)     return 0;
    if ((secEnd   & 3UL) != 0UL)     return 0;
    return 1;
}

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
     * multiplier below is ALLOW / 65536.  The build refuses a width that is
     * not a whole number of those, since then the two forms would part
     * company.  q is at most 2^23 and the multiplier sixteen, so the product
     * is 2^27 and stays inside a long.
     */
    if (p < -(q * (long)(OSMGA_HW3D_TEX_NEG_ALLOW / OSMGA_HW3D_Q_ONE)))
        return 0;
    return p <= roomHi * q;
}

/* the denominator plane at an offset from the primitive's anchor */
static long
osmgaHW3DQAt(const OSMGAHW3DBatch *b, const OSMGAHW3DTri *t,
             int persp, long dx, long dy)
{
    if (!persp)
        return OSMGA_HW3D_Q_ONE;
    /* The anchor is the trapezoid's; the slopes are the triangle's. */
    return t->tq0 + b->state.tmr[4] * dx + b->state.tmr[5] * dy;
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
    return osmgaHW3DTexBiasOfBand(osmgaHW3DTexBandFor(maxCoord));
}

/* The same ladder, as the rung and as what the rung is worth. */
static const long osmgaHW3DBiasLadder[OSMGA_HW3D_TEX_BANDS] = {
    OSMGA_HW3D_TEX_BIAS, 480L, 448L, 384L, 256L, 0L
};
static long osmgaHW3DTexAddend(long v);

unsigned char
osmgaHW3DTexBandFor(long maxCoord)
{
    if (maxCoord <= (long)(1UL << 20)) return 0U;
    if (maxCoord <= (long)(1UL << 21)) return 1U;
    if (maxCoord <= (long)(1UL << 22)) return 2U;
    if (maxCoord <= (long)(1UL << 23)) return 3U;
    if (maxCoord <= (long)(1UL << 24)) return 4U;
    return 5U;
}

/*
 * Does this triangle address the depth buffer?  The header carries the
 * reasoning; this is the one implementation of it.
 */
int
osmgaHW3DAddressesDepth(unsigned long dwgctl)
{
    unsigned long d = dwgctl & OSMGA_HW3D_DWG_CLIENT;
    unsigned long atype = (d >> 4) & 0x7UL;
    unsigned long zmode = (d >> 8) & 0x7UL;

    if (atype == OSMGA_HW3D_ATYPE_ZI)
        return 1;
    if (atype == OSMGA_HW3D_ATYPE_I && zmode != OSMGA_HW3D_ZMODE_NOZCMP)
        return 1;
    return 0;
}

/*
 * What the engine ADDS to a coordinate of this magnitude before it picks a
 * texel.  The same ladder the bias walks, indexed by the value rather than by
 * the rung -- and a flat 511 below nought, which is measured and is not
 * symmetric with the positive side.
 */
static long
osmgaHW3DTexAddend(long v)
{
    unsigned char r;

    if (v < 0L)
        return 511L;
    r = osmgaHW3DTexBandFor(v);
    if (r >= (unsigned char)OSMGA_HW3D_TEX_BANDS)
        r = (unsigned char)(OSMGA_HW3D_TEX_BANDS - 1);
    return osmgaHW3DBiasLadder[r];
}

unsigned char
osmgaHW3DTexBandHeadroom(long reach)
{
    long a = osmgaHW3DTexAddend(reach);
    unsigned char r = (unsigned char)(OSMGA_HW3D_TEX_BANDS - 1);

    /*
     * The largest rung whose bias still leaves the farthest coordinate inside
     * the range.  Walked down from the top rather than solved, because the
     * ladder is six entries and a loop cannot get a boundary wrong the way an
     * inequality can.
     */
    for (;;) {
        if (reach + a - osmgaHW3DTexBiasOfBand(r) <=
            (long)OSMGA_HW3D_TEX_COORD_MAX)
            return r;
        if (r == 0U)
            return 0U;
        r--;
    }
}

long
osmgaHW3DTexBiasOfBand(unsigned char band)
{
    /* A rung outside the ladder takes the smallest bias, which is the one
     * that cannot make a residual negative whatever the addend turns out to
     * be.  A caller handing over a rung it was not given is a bug, and this
     * is what it costs rather than what it corrupts. */
    if (band >= (unsigned char)OSMGA_HW3D_TEX_BANDS)
        return 0L;
    return osmgaHW3DBiasLadder[band];
}

int
osmgaHW3DValidate(const OSMGAHW3DBatch *b, const OSMGAHW3DLimits *lim,
                  unsigned long *badTri)
{
    return osmgaHW3DValidateReach(b, lim, badTri, (OSMGAHW3DTexReach *)0,
                                  (OSMGAHW3DTexBand *)0);
}

/*
 * The batch state, judged once for both contracts.
 *
 * The trapezoid path and the WARP path program the SAME registers from the
 * SAME OSMGAHW3DState, so they need the same bounds -- and this is the
 * containment argument itself, not a convenience.  Written twice it would
 * drift, and the half that drifted would be the half nobody was reading.
 *
 * `anyDepth` and `anyTex` come from the caller because they are derived
 * from the primitives, and the two contracts keep those in different
 * shapes: version 9 reads tri[].dwgctl, version 10 reads run[].dwgctl.
 * Deriving them is a pure loop that cannot fail, so doing it before this
 * call rather than inside leaves the order of every verdict below
 * unchanged.
 */
static int
osmgaHW3DValidateStateCommon(const OSMGAHW3DState *st,
                             const OSMGAHW3DLimits *lim,
                             unsigned long rows, int anyDepth, int anyTex)
{
    if (st->dstPitch == 0UL ||
        st->dstWidth > st->dstPitch ||
        lim->pitchBytes / 4UL != st->dstPitch)
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
    if ((st->dstPitch % OSMGA_HW3D_PITCH_ALIGN) != 0UL)
        return OSMGA_HW3D_E_DSTPITCH;

    /*
     * Alignment before reach, because an unaligned origin is not an address
     * at all -- its low bits are the memory-space and access selectors --
     * and measuring the reach of a value that is partly a mode field would
     * be bounding the wrong number.
     *
     * Inside this function only.  The submit path range-checks dstorg
     * against the window before ever calling here, so an origin that is
     * both outside the window and unaligned still reports E_DSTORG from
     * there.  The precedence is local, not global, and saying otherwise
     * would be a promise this file cannot keep.
     */
    if ((st->dstorg & (OSMGA_HW3D_DSTORG_ALIGN - 1UL)) != 0UL)
        return OSMGA_HW3D_E_DSTORGAL;

    if (!osmgaHW3DReach(st->dstorg, rows, lim->pitchBytes,
                        lim->colourStart, lim->colourEnd))
        return OSMGA_HW3D_E_DSTORG;

    /*
     * Which origins have to be bounded depends on what the triangles ask
     * for, and the test is ANY rather than ALL: one depth triangle in a
     * batch of otherwise flat ones still addresses depth.  Writing this as
     * "all of them" would let exactly that triangle draw against an origin
     * nobody checked.
     */
    /*
     * "Addresses depth" is osmgaHW3DAddressesDepth and nothing else -- the
     * same call the encoder and the submit path make.  It used to be atype
     * ZI spelled out here, which missed atype I asking for a real
     * comparison: that reads depth, and read from where nobody bounded.
     */

    if (anyDepth) {
        unsigned long zstride = (lim->pitchBytes / 4UL) * OSMGA_HW3D_DEPTH_BYTES;

        /* Conditional, like the reach check beside it: when nothing in the
         * batch addresses depth the field is ignored, and refusing a stale
         * value nobody is going to use would reject working batches. */
        if ((st->zorg & (OSMGA_HW3D_ZORG_ALIGN - 1UL)) != 0UL)
            return OSMGA_HW3D_E_ZORGAL;

        if (!osmgaHW3DReach(st->zorg, rows, zstride,
                            lim->depthStart, lim->depthEnd))
            return OSMGA_HW3D_E_ZORG;
    }
    if (anyTex) {
        unsigned long w = st->texW, h = st->texH;
        unsigned long pitch = st->texPitch;

        if (w == 0UL || h == 0UL ||
            w > OSMGA_HW3D_TEX_MAX_DIM || h > OSMGA_HW3D_TEX_MAX_DIM ||
            pitch < w || pitch > OSMGA_HW3D_TEX_MAX_PIT)
            return OSMGA_HW3D_E_TEXSIZE;
        if (st->texFormat != OSMGA_HW3D_TEXFMT_TW32)
            return OSMGA_HW3D_E_TEXSIZE;
        /*
         * The requested rungs, which are the rung PLUS ONE so that nought is
         * the inert value -- see the note by the fields.  Anything above the
         * ladder is a client that has not read the header, and is refused
         * rather than clamped: clamping would let a wrong number look as
         * though it had worked.
         */
        if (st->texBiasReqU > OSMGA_HW3D_TEX_BANDS ||
            st->texBiasReqV > OSMGA_HW3D_TEX_BANDS)
            return OSMGA_HW3D_E_TEXSIZE;
        /*
         * The scissor box is NOT checked here, and that is deliberate.
         *
         * The submit path draws the intersection of it with the destination
         * window, so no value in it can widen anything: a box of nonsense
         * either narrows the clip or empties it, and an empty one skips the
         * draw.  Checking it would be checking something containment does
         * not rest on, and would turn a harmless client mistake into a
         * refusal.
         *
         * The row and column checks below are unchanged and still measured
         * against the whole window.  A narrow scissor therefore makes this
         * validation conservative -- it admits only what could be drawn
         * without one -- and never relaxed.
         */
        /*
         * The diagnostic minification selector, and what it costs.
         *
         * Only the four modes the generated register description names are
         * let through.  The rest of the field is unnamed there -- including
         * the 0xd the hand-written header calls MIN_ANISO -- and an unnamed
         * fetch footprint is not something to hand the engine on a guess.
         */
        {
            unsigned long mm = (st->texFlags
                                & OSMGA_HW3D_TEXF_MINMODE_MASK)
                               >> OSMGA_HW3D_TEXF_MINMODE_SHIFT;

            if (mm != 0UL &&
                mm != OSMGA_HW3D_TEXF_MINMODE_MM1S &&
                mm != OSMGA_HW3D_TEXF_MINMODE_MM2S &&
                mm != OSMGA_HW3D_TEXF_MINMODE_MM4S &&
                mm != OSMGA_HW3D_TEXF_MINMODE_MM8S)
                return OSMGA_HW3D_E_TEXSIZE;
            /*
             * Reach from the size the client gave, which is the size the
             * kernel will program: pitch texels of four bytes, h rows.  With
             * a mipmap mode asked for it is TWICE that -- a whole chain is
             * four thirds of the base, and the engine may walk one to an
             * address this driver has not worked out yet.  Reading beyond the
             * texture would be reading VRAM nobody proved.
             */
            /* Conditional for the same reason as zorg above. */
            if ((st->texorg & (OSMGA_HW3D_TEXORG_ALIGN - 1UL)) != 0UL)
                return OSMGA_HW3D_E_TEXORGAL;

            if (!osmgaHW3DReach(st->texorg, (mm != 0UL) ? h * 2UL : h,
                                pitch * 4UL, lim->texStart, lim->texEnd))
                return OSMGA_HW3D_E_TEXORG;
        }

        /* The coordinate check is after the triangle loop, because what it
         * needs -- how far the drawing actually reaches -- is what that loop
         * works out. */
    }
    return OSMGA_HW3D_OK;
}

int
osmgaHW3DValidateReach(const OSMGAHW3DBatch *b, const OSMGAHW3DLimits *lim,
                       unsigned long *badTri, OSMGAHW3DTexReach *reach,
                       OSMGAHW3DTexBand *bands)
{
    unsigned long i, rows, opcode, atype;
    int anyDepth, anyTex;
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
     *   v  is re-seeded too, and so is q.  Both were measured NOT to be,
     *      while the matrix was written once before all the primitives:
     *      heights of five, eleven and three, with a flat primitive
     *      interposed, gave first rows of 3, 8 and 19 from a start of 3.
     *      With the matrix written ahead of each primitive, three of them
     *      given anchors that DIFFER read back exactly what each was given
     *      (probe section 78), and a pair with the same numerators and q of
     *      one then two read the quotient and then half of it (78b).
     *
     *      Anchors that AGREE cannot show this: they read the same value
     *      whether each primitive re-seeds from its own or the first one is
     *      latched and the rest of the writes ignored.  The older sections
     *      set them equal, so they are the weaker corollary and 78 is the
     *      proof.
     *
     *   So both axes' vertical reach is one primitive's height, and they are
     *      checked with the same span.
     *
     * A batch USED to write TMR6 and TMR7 once, before its primitives, and
     * the accumulator was seen to start afresh every submission -- read in
     * the encoder and then measured, twice through the same batch.  It no
     * longer does: the anchors are the trapezoid's now, so the matrix is
     * written again before each one.
     *
     * AND THE WRITE RE-SEEDS -- probe sections 78 and 78b.  The accumulating
     * model was kept for one cycle on the argument that its error had a known
     * direction: bounding a LARGER row index than the hardware uses refuses
     * more than it must and takes a smaller addend off.  That argument holds
     * only for a POSITIVE gradient.  With a negative dv/dy a larger row index
     * means a SMALLER coordinate, so the model evaluated a row the engine
     * never uses and skipped the one it does; test-hw3d-reseed.c is the batch
     * that was accepted with its own coordinate a hundred past the limit.
     */
    unsigned long texSpanLo = 0UL, texSpanHi = 0UL;
    int texDrawn = 0, texBad = 0;
    unsigned long texBadTri = 0UL;   /* which trapezoid set it */

    if (reach != 0) {
        reach->uMax = 0L;
        reach->vMax = 0L;
    }
    if (bands != 0) {
        unsigned long z;

        for (z = 0UL; z < OSMGA_HW3D_MAX_TRI; z++) {
            bands[z].u = 0U;
            bands[z].v = 0U;
        }
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
    {
        int vs;

        anyDepth = 0;
        anyTex = 0;
        for (i = 0UL; i < b->triCount; i++) {
            if (osmgaHW3DAddressesDepth(b->tri[i].dwgctl))            anyDepth = 1;
            if ((b->tri[i].dwgctl & 0xFUL) == OSMGA_HW3D_OPCODE_TEX)  anyTex = 1;
        }
    
        vs = osmgaHW3DValidateStateCommon(&b->state, lim, rows,
                                          anyDepth, anyTex);
        if (vs != OSMGA_HW3D_OK)
            return vs;
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
            /* Fields apart, the combination.  Its own verdict: reusing
             * E_ALPHA would hide one behind the other, because the driver
             * logs a verdict number once and never again. */
            if (!osmgaHW3DAlphaCross(ac))
                return OSMGA_HW3D_E_ALPHACROSS;
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
         * And an upper bound, which they never had.  A divisor larger than
         * the field cannot be told apart from a smaller one once the engine
         * truncates, so the value the hardware acts on stops being the value
         * the client sent.  Not tied to the trapezoid's height: the
         * validator already withdrew that once, because a triangle split at
         * its middle vertex has one edge spanning both halves.
         */
        if (!osmgaHW3DField(t->ar0, OSMGA_HW3D_F_AR, (unsigned long *)0) ||
            !osmgaHW3DField(t->ar6, OSMGA_HW3D_F_AR, (unsigned long *)0))
            return OSMGA_HW3D_E_TRIFIELD;

        /*
         * Every other per-triangle value that reaches a register with a
         * reserved field.  ar1/ar2/ar4/ar5 are already held to the edge
         * walk, far inside their fields; these are the ones nothing bounded
         * at all -- the nine emitted DRs and the three alpha values.
         *
         * dr[9..11] are structure padding and are never emitted, so they
         * are not checked: refusing a value the card never sees would
         * reject working batches for a field that does not exist.
         *
         * z0/zdx/zdy go to DR0/DR2/DR3, which are <31:0> with no reserved
         * field, and are deliberately absent here.
         */
        {
            int k;

            if (!osmgaHW3DField(t->ar1, OSMGA_HW3D_F_AR1, (unsigned long *)0) ||
                !osmgaHW3DField(t->ar2, OSMGA_HW3D_F_AR, (unsigned long *)0) ||
                !osmgaHW3DField(t->ar4, OSMGA_HW3D_F_AR, (unsigned long *)0) ||
                !osmgaHW3DField(t->ar5, OSMGA_HW3D_F_AR, (unsigned long *)0))
                return OSMGA_HW3D_E_TRIFIELD;

            for (k = 0; k < 9; k++)
                if (!osmgaHW3DField((long)t->dr[k], OSMGA_HW3D_F_DR,
                                    (unsigned long *)0))
                    return OSMGA_HW3D_E_TRIFIELD;

            if (!osmgaHW3DField((long)t->a0,  OSMGA_HW3D_F_ALPHA, (unsigned long *)0) ||
                !osmgaHW3DField((long)t->adx, OSMGA_HW3D_F_ALPHA, (unsigned long *)0) ||
                !osmgaHW3DField((long)t->ady, OSMGA_HW3D_F_ALPHA, (unsigned long *)0))
                return OSMGA_HW3D_E_TRIFIELD;
        }
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
                /*
                 * v's row index is this primitive's own, the same as u's.
                 *
                 * It used to be the batch's running total, because that is
                 * what the hardware did while the matrix was written once per
                 * batch.  The matrix is written per primitive now and the
                 * write RE-SEEDS -- measured, with three primitives given
                 * anchors that differ (probe section 78) and again for the
                 * denominator (78b).
                 *
                 * Keeping the accumulating form was not the conservative
                 * choice it looked like.  That argument holds only for a
                 * POSITIVE dv/dy: with a negative one a larger row index
                 * means a SMALLER coordinate, so the model skipped the row
                 * the engine actually uses.  A batch with one row ahead of
                 * it, an anchor a hundred below the limit, dv/dx +200 and
                 * dv/dy -300 was accepted with a reach inside the range while
                 * the engine's own coordinate landed a hundred past it.
                 */
                long vy = ey;
                /* this trapezoid's own reach, which is what its anchor is
                 * biased with; the label at the end of this block reads them,
                 * so they cannot live in the walk's own scope */
                long triU = 0L, triV = 0L;

                long room = (long)OSMGA_HW3D_TEX_COORD_MAX;
                long roomHi = room >> 16;
                int persp = (b->state.texFlags
                             & OSMGA_HW3D_TEXF_PERSP) != 0UL;

                /*
                 * The anchors, BEFORE anything is evaluated with them.
                 *
                 * They used to be held to the range after the primitive loop,
                 * on the grounds that they bound the arithmetic whether or not
                 * a pixel samples them.  That is true of what they bound and
                 * false of when: every coordinate below is the anchor plus two
                 * bounded products, so an anchor near the end of a long
                 * overflows inside the walk and would only be rejected
                 * afterwards, by which time the value that was checked is the
                 * wrapped one.  A long is four bytes here, which the build
                 * asserts, so this is not hypothetical.
                 *
                 * Now that the anchors are the trapezoid's, the check comes
                 * with them: it is the first thing this block does, ahead of
                 * the gradient bounds and far ahead of the walk.  The
                 * denominator's anchor is NOT checked here -- it is held to
                 * [Q_MIN, Q_MAX] below, which is a different range, and
                 * folding the two would admit a q the divider has never been
                 * looked at over.
                 */
                if (t->tu0 < -(long)OSMGA_HW3D_TEX_COORD_MAX ||
                    t->tu0 > (long)OSMGA_HW3D_TEX_COORD_MAX ||
                    t->tv0 < -(long)OSMGA_HW3D_TEX_COORD_MAX ||
                    t->tv0 > (long)OSMGA_HW3D_TEX_COORD_MAX) {
                    if (badTri != 0)
                        *badTri = i;
                    return OSMGA_HW3D_E_TEXCOORD;
                }

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
                     * can sit far to the left of, and the row index used to
                     * accumulate across every earlier textured primitive in
                     * the batch and so passed dstHeight as soon as there were
                     * a few of them.  The check would have admitted a slope
                     * whose evaluation leaves a long.
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
                    mrow = ey;          /* this primitive's rows, not the
                                         * batch's -- see the note by vy.
                                         * The greatest index the walk below
                                         * evaluates is h - 1, and bounding
                                         * against h would be one row wider
                                         * than the claim this makes. */
                    if (mdx < 1L) mdx = 1L;
                    if (mrow < 1L) mrow = 1L;
                    /*
                     * Refused here and not deferred.  Setting the flag and
                     * carrying on fell through to the walk, which calls the q
                     * evaluator with the very slopes this just found too
                     * large -- the check would have been made and then the
                     * overflow taken anyway.
                     */
                    if (t->tq0 < OSMGA_HW3D_Q_MIN ||
                        t->tq0 > OSMGA_HW3D_Q_MAX ||
                        a4 > budget / mdx ||
                        a5 > budget / mrow) {
                        if (badTri != 0)
                            *badTri = i;
                        return OSMGA_HW3D_E_TEXCOORD;
                    }
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
                    { texBad = 1; texBadTri = i; }
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

                        ux  = t->tu0 + b->state.tmr[0] * dx
                              + b->state.tmr[1] * dy;
                        vx2 = t->tv0 + b->state.tmr[2] * dx
                              + b->state.tmr[3] * dy;
                        /*
                         * The denominator's row index is this primitive's
                         * own, as v's is.  It accumulated once, when the
                         * matrix was written per batch; probe section 78b
                         * shows the per-primitive write re-seeds q too.
                         */
                        qa = osmgaHW3DQAt(b, t, persp, dx, dy);
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
                     * Perspective used to keep the shortcut, on the grounds
                     * that the row walk checks the quotient at per-row
                     * denominators where the box checks it at the corners',
                     * so walking a primitive the box had passed might refuse
                     * it.  That was reasoning and it was wrong.  p/q <= m is
                     * p - m*q <= 0, and p - m*q is AFFINE in the two offsets,
                     * so it is non-positive over the whole box exactly when
                     * it is non-positive at the four corners -- and q stays
                     * positive throughout for the same reason.  A ratio of
                     * two affine functions has no strict interior extremum.
                     * Two hundred thousand random boxes in python agree: not
                     * one interior point beat every corner.
                     */
                    if (boxOK && reach == 0 && bands == 0)
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
                        ux  = t->tu0 + b->state.tmr[0] * (lx - lx0)
                              + b->state.tmr[1] * row;
                        vx2 = t->tv0 + b->state.tmr[2] * (lx - lx0)
                              + b->state.tmr[3] * row;
                        ly = ux + b->state.tmr[0] * (rx - 1L - lx);
                        ry = vx2 + b->state.tmr[2] * (rx - 1L - lx);
                        /* This primitive's own row here too, the same index
                          * the numerators above use. */
                        qa = osmgaHW3DQAt(b, t, persp, lx - lx0, row);
                        qb = osmgaHW3DQAt(b, t, persp, rx - 1L - lx0, row);
                        if (!osmgaHW3DRatioOK(ux,  qa, roomHi) ||
                            !osmgaHW3DRatioOK(vx2, qa, roomHi) ||
                            !osmgaHW3DRatioOK(ly,  qb, roomHi) ||
                            !osmgaHW3DRatioOK(ry,  qb, roomHi))
                            { texBad = 1; texBadTri = i; }
                        /*
                         * And how far it reaches, from the same four values:
                         * the coordinate is linear along a row, so a row's
                         * extremes are its two ends.  The slopes have been
                         * bounded above, so these products are inside a long;
                         * the walk that builds the box runs before that bound
                         * and could not have formed them.
                         */
                        /*
                         * These are NUMERATORS.  In affine q is one, so the
                         * numerator and the coordinate are the same number
                         * and the distinction never came up; in perspective
                         * they part company, and it is the numerator the
                         * engine picks its band from -- measured, by holding
                         * the coordinate at 2^19 and sweeping the denominator
                         * so only the numerator's band moved, and again with
                         * a matched numerator reached from two different
                         * coordinates.  So the same reach serves both.
                         */
                        if (reach != 0 || bands != 0) {
                            /*
                             * The MAGNITUDE, not the signed maximum.  The
                             * engine picks its band from how large the
                             * numerator is, and a coordinate is allowed to go
                             * below nought -- so a batch whose numerators are
                             * all negative would otherwise report a reach of
                             * nought and be given the bias for a tiny
                             * coordinate.
                             *
                             * Today that cannot bite: a negative numerator is
                             * held to q/16, so its magnitude is at most 2^19,
                             * and every bias from biasFor(0) to biasFor(2^20)
                             * is the same 496.  python checks every reachable
                             * combination and finds no case where the two
                             * choose differently.  It has to be the magnitude
                             * before the negative range is widened, not
                             * after.
                             */
                            long au  = (ux  < 0L) ? -ux  : ux;
                            long al  = (ly  < 0L) ? -ly  : ly;
                            long av  = (vx2 < 0L) ? -vx2 : vx2;
                            long ar  = (ry  < 0L) ? -ry  : ry;

                            if (al > au) au = al;
                            if (ar > av) av = ar;
                            if (reach != 0) {
                                if (au > reach->uMax) reach->uMax = au;
                                if (av > reach->vMax) reach->vMax = av;
                            }
                            /*
                             * And this trapezoid's own, which is what the
                             * encoder biases its anchor with.  Kept as the
                             * running maximum in the same two variables the
                             * batch uses, so the two cannot drift apart.
                             */
                            if (au > triU) triU = au;
                            if (av > triV) triV = av;
                        }
                    }
                }
              texDone:
                    /*
                     * The rung this trapezoid sits on.
                     *
                     * Written even when the box shortcut answered, because
                     * asking for the bands forces the walk above -- so triU
                     * and triV are the walked maxima whenever bands is not
                     * nought.  A trapezoid that reaches nowhere keeps rung
                     * nought, which is a bias of 496 and what an anchor of
                     * nought wants.
                     */
                    if (bands != 0) {
                        unsigned char ou = osmgaHW3DTexBandFor(triU);
                        unsigned char ov = osmgaHW3DTexBandFor(triV);

                        bands[i].u = ou;
                        bands[i].v = ov;
                        /*
                         * A batch may ask every trapezoid onto one rung, so
                         * that a surface drawn as several of them shares a
                         * residual and shows no seam.
                         *
                         *    used = max(own, min(request, headroom))
                         *
                         * The inner min is the range: raising the rung raises
                         * the residual at the farthest coordinate, which at
                         * its own rung is exactly nought, and enough of it
                         * would push that coordinate past what the check
                         * above admits.
                         *
                         * The outer max is not decoration.  A perspective
                         * numerator may exceed COORD_MAX quite legally -- the
                         * cap on it is three times that -- so the headroom
                         * can come out BELOW the trapezoid's own rung, and
                         * its own rung is always safe because the residual
                         * there is nought.
                         */
                        if (b->state.texBiasReqU != OSMGA_HW3D_TEX_BIAS_NONE) {
                            unsigned char q =
                                (unsigned char)(b->state.texBiasReqU - 1UL);
                            unsigned char h = osmgaHW3DTexBandHeadroom(triU);

                            if (q > h) q = h;
                            if (q > ou) bands[i].u = q;
                        }
                        if (b->state.texBiasReqV != OSMGA_HW3D_TEX_BIAS_NONE) {
                            unsigned char q =
                                (unsigned char)(b->state.texBiasReqV - 1UL);
                            unsigned char h = osmgaHW3DTexBandHeadroom(triV);

                            if (q > h) q = h;
                            if (q > ov) bands[i].v = q;
                        }
                    }
                    ;
            }

            if (opcode == OSMGA_HW3D_OPCODE_TEX) {
                /*
                 * A textured primitive that draws nothing anywhere is
                 * refused.  It is still encoded and still executed, so
                 * the texture unit is running for it, and
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
     * The deferred texture verdict.
     *
     * The vertical spans that used to be worked out here went with the
     * accumulating model: u and v wanted different ones, because u re-seeded
     * at every primitive and v did not.  Both re-seed now, so there is one
     * span and it is the primitive's own, which the loop above already used.
     */
    if (anyTex) {
        unsigned long spanLo = 0UL, spanHi = lim->clipX1;

        if (texDrawn) {
            spanLo = texSpanLo;
            spanHi = texSpanHi;
        }
        (void)spanLo; (void)spanHi;
        if (texBad) {
            if (badTri != 0)
                *badTri = texBadTri;
            return OSMGA_HW3D_E_TEXCOORD;
        }
    }
    if (badTri != 0)
        *badTri = 0UL;
    return OSMGA_HW3D_OK;
}

/*
 * TEXWIDTH/TEXHEIGHT for WARP.  NOT the trapezoid path's encoding.
 *
 * osmgaTextureSetup builds those words as ((dim-1)<<18) | ((8-log2)<<9) |
 * log2, which is hardware-verified -- for the CPU-fed coordinate matrix.
 * Mesa builds a different pair under a comment reading "warp texture
 * registers" (mgatex.c:395):
 *
 *      ofs  = G200 ? 28 : 11
 *      rfw  = (10 - log2 - 8) & 63          i.e. (2 - log2)
 *      tw   = (log2 + ofs) | 0x40
 *
 * For an 8x8 texture that is rfw 63 against 5 and tw 14 against 3 -- both
 * fields differ, so copying the trapezoid words across would have fed
 * WARP nonsense.  Spec 3-228 gives the reason: tw and rfw depend on the
 * fractional format of whatever produces the coordinate, and for WARP
 * that is not the CPU.
 */
#define OSMGA_HW3D_WARP_TEX_OFS   11UL      /* G400/G450; the G200 value is 28 */

unsigned long
osmgaHW3DWarpTexDim(unsigned long dim, unsigned long log2dim)
{
    return ((dim - 1UL) << 18)
         | ((((10UL - log2dim - 8UL)) & 63UL) << 9)
         | ((((log2dim + OSMGA_HW3D_WARP_TEX_OFS) | 0x40UL)) & 63UL);
}

/*
 * ---- IEEE-754 singles, judged with integer operations only ----
 *
 * The kernel cannot touch the FPU, and the WARP vertex form is nothing but
 * float bit patterns: x, y, z, rhw, tu0 and tv0.  So every question the
 * validator has to ask about them -- is it a number, is it positive, is it
 * in range -- is asked of the BITS.
 *
 * Two facts make that exact rather than approximate, and both were checked
 * in python against real floats before a line of this was written:
 *
 *   - NaN and both infinities are exactly the patterns whose exponent
 *     field is all ones, so one mask answers "is this a number".
 *
 *   - for values that are not negative, the bit pattern ORDERS THE SAME WAY
 *     the value does.  A range check is therefore an unsigned comparison of
 *     the patterns, with no arithmetic and no rounding anywhere in it.
 *
 * Negative values reverse that order, which is why the sign is taken out
 * first everywhere below rather than being folded into a comparison.
 */
#define OSMGA_F32_EXP    0x7F800000UL
#define OSMGA_F32_MAG    0x7FFFFFFFUL
#define OSMGA_F32_SIGN   0x80000000UL

int
osmgaHW3DF32Finite(unsigned long p)
{
    return ((p & OSMGA_F32_EXP) != OSMGA_F32_EXP) ? 1 : 0;
}

/*
 * Strictly positive and normal.  Denormals are excluded deliberately: a
 * denominator of 1e-40 is not a coordinate anyone meant to send, and
 * admitting it would put the containment argument -- which needs every
 * vertex weight strictly positive -- on a value the engine may flush to
 * nought.
 */
int
osmgaHW3DF32PosNormal(unsigned long p)
{
    unsigned long exp = p & OSMGA_F32_EXP;

    if ((p & OSMGA_F32_SIGN) != 0UL)
        return 0;
    return (exp != 0UL && exp != OSMGA_F32_EXP) ? 1 : 0;
}

/*
 * 0 <= v <= 1.  Negative zero passes, because it IS zero and a client that
 * computed it did nothing wrong; no other negative does.
 */
int
osmgaHW3DF32InUnit(unsigned long p)
{
    if (!osmgaHW3DF32Finite(p))
        return 0;
    if ((p & OSMGA_F32_SIGN) != 0UL)
        return ((p & OSMGA_F32_MAG) == 0UL) ? 1 : 0;
    return (p <= 0x3F800000UL) ? 1 : 0;
}

/* |v| <= |limit|, sign irrelevant.  `limit` is a bit pattern, so the
 * caller states the bound as a float and not as a number this code has to
 * convert. */
int
osmgaHW3DF32AbsAtMost(unsigned long p, unsigned long limit)
{
    if (!osmgaHW3DF32Finite(p))
        return 0;
    return ((p & OSMGA_F32_MAG) <= (limit & OSMGA_F32_MAG)) ? 1 : 0;
}

/* lo <= v <= hi, for a lo and hi that are not negative.  Used for rhw,
 * whose admitted band is the Q range converted: 0.125 to 128.0. */
int
osmgaHW3DF32Between(unsigned long p, unsigned long lo, unsigned long hi)
{
    if (!osmgaHW3DF32Finite(p))
        return 0;
    if ((p & OSMGA_F32_SIGN) != 0UL)
        return 0;
    return (p >= lo && p <= hi) ? 1 : 0;
}

/*
 * ---- the WARP tier's admission policy, and the batch validator ----
 */

/*
 * What the WARP tier admits TODAY.  Alone in one function because it is
 * the part that moves as bands qualify, and because a reader asking "why
 * did that triangle go the slow way" should have one place to look.
 *
 * Everything refused here is drawn by the trapezoid path, which has been
 * qualified for all of it.  So a refusal costs speed and never a picture,
 * and the conservative direction is the safe one.
 *
 * Repeat and blending are refused pending T7 and T8.  Both are qualified
 * on the TRAPEZOID path already -- M1_4CB opened GL_REPEAT for nearest and
 * for linear, and M1_4E3 matched Mesa's blending on nine factor pairs with
 * worst difference nought -- but neither has been shown to behave the same
 * when WARP produces the coordinates and the setup.  Those bands are
 * built and waiting on a reboot; when they report, this function is what
 * changes.
 */
int
osmgaHW3DWarpAdmits(const OSMGAHW3DState *st, const OSMGAHW3DRun *run)
{
    if (st == 0 || run == 0)
        return OSMGA_HW3D_E_WARPPOLICY;

    /* T7 has not run.  CLAMPUV is what M4's T5 measured, on both filters. */
    if ((st->texFlags & (OSMGA_HW3D_TEXF_REPEATU |
                         OSMGA_HW3D_TEXF_REPEATV)) != 0UL)
        return OSMGA_HW3D_E_WARPPOLICY;

    /* T8 has not run.  ALPHACTRL is admitted only at the value that does
     * not blend -- src ONE, dst ZERO, both alpha tests off -- which is the
     * state every WARP band so far has drawn under. */
    if (run->alphactrl != (osmga_u32)0x00000101UL &&
        run->alphactrl != (osmga_u32)0x00000001UL)
        return OSMGA_HW3D_E_WARPPOLICY;

    return OSMGA_HW3D_OK;
}

int
osmgaHW3DValidateWarp(const OSMGAHW3DWarpBatch *b,
                      const OSMGAHW3DLimits *lim,
                      unsigned long *badRun)
{
    unsigned long r, i, seen;

    if (badRun != 0)
        *badRun = 0UL;
    if (b == 0 || lim == 0)
        return OSMGA_HW3D_E_MAGIC;
    if (b->magic != OSMGA_HW3D_MAGIC)
        return OSMGA_HW3D_E_MAGIC;
    if (b->version != OSMGA_HW3D_VERSION_WARP)
        return OSMGA_HW3D_E_VERSION;
    /*
     * A client that filled in both payloads did not know what it was
     * sending, and resolving it in its favour would be guessing.
     */
    if (b->triCount != 0UL)
        return OSMGA_HW3D_E_WARPMIX;

    if ((unsigned long)b->runCount == 0UL ||
        (unsigned long)b->runCount > OSMGA_HW3D_MAX_RUN)
        return OSMGA_HW3D_E_VTXCOUNT;
    if ((unsigned long)b->vtxCount == 0UL ||
        (unsigned long)b->vtxCount > OSMGA_HW3D_MAX_VTX)
        return OSMGA_HW3D_E_VTXCOUNT;
    if (((unsigned long)b->vtxCount % 3UL) != 0UL)
        return OSMGA_HW3D_E_VTXCOUNT;
    if (lim->batchBytes < sizeof(OSMGAHW3DWarpBatch) -
                          sizeof(OSMGAHW3DVertex) * OSMGA_HW3D_MAX_VTX)
        return OSMGA_HW3D_E_VTXCOUNT;

    /*
     * The runs must PARTITION the vertices: contiguous, in order, and
     * covering every one of them.  Overlapping runs would draw a primitive
     * twice under two states, and a gap would leave vertices the client
     * believes were drawn.  Both are cheaper to refuse than to explain.
     */
    seen = 0UL;
    for (r = 0UL; r < (unsigned long)b->runCount; r++) {
        const OSMGAHW3DRun *run = &b->run[r];
        unsigned long first = (unsigned long)run->first;
        unsigned long count = (unsigned long)run->count;
        int policy;

        if (badRun != 0)
            *badRun = r;
        if (first != seen)
            return OSMGA_HW3D_E_VTXCOUNT;
        if (count == 0UL || (count % 3UL) != 0UL)
            return OSMGA_HW3D_E_VTXCOUNT;
        if (count > (unsigned long)b->vtxCount - first)
            return OSMGA_HW3D_E_VTXCOUNT;
        seen = first + count;

        policy = osmgaHW3DWarpAdmits(&b->state, run);
        if (policy != OSMGA_HW3D_OK)
            return policy;
    }
    if (seen != (unsigned long)b->vtxCount)
        return OSMGA_HW3D_E_VTXCOUNT;
    if (badRun != 0)
        *badRun = 0UL;

    /*
     * The batch state, through the same checks version 9 uses -- the pitch,
     * the origins and their alignments, the texture's size and reach.
     *
     * It was missing.  The validator judged the run structure and every
     * vertex word and then let DSTORG, ZORG, TEXORG, the pitch and the
     * texture size reach the registers unexamined, which is the whole
     * containment argument going out the door behind a well-formed
     * vertex array.
     *
     * anyDepth and anyTex are derived from the RUNS here, where version 9
     * derives them from its triangles; the state checks themselves are the
     * same code.
     */
    {
        int anyDepth = 0, anyTex = 0, vs;

        for (r = 0UL; r < (unsigned long)b->runCount; r++) {
            unsigned long dw = (unsigned long)b->run[r].dwgctl;

            if (osmgaHW3DAddressesDepth(dw))                anyDepth = 1;
            if ((dw & 0xFUL) == OSMGA_HW3D_OPCODE_TEX)      anyTex = 1;
        }
        vs = osmgaHW3DValidateStateCommon(&b->state, lim, lim->clipY1 + 1UL,
                                          anyDepth, anyTex);
        if (vs != OSMGA_HW3D_OK)
            return vs;
    }

    /*
     * Every vertex word, by its bits.  The containment argument rests on
     * rhw being strictly positive at every vertex -- with that, the
     * perspective-corrected interpolation of any coordinate is a convex
     * combination of the three vertex values, so three vertices in range
     * put every pixel in range.  It is the one check the geometry cannot
     * do without.
     */
    for (i = 0UL; i < (unsigned long)b->vtxCount; i++) {
        const OSMGAHW3DVertex *v = &b->vtx[i];

        if (!osmgaHW3DF32AbsAtMost((unsigned long)v->x,
                                   OSMGA_HW3D_F32_COORD) ||
            !osmgaHW3DF32AbsAtMost((unsigned long)v->y,
                                   OSMGA_HW3D_F32_COORD))
            return OSMGA_HW3D_E_VTXFLOAT;
        if (!osmgaHW3DF32InUnit((unsigned long)v->z))
            return OSMGA_HW3D_E_VTXFLOAT;
        if (!osmgaHW3DF32PosNormal((unsigned long)v->rhw) ||
            !osmgaHW3DF32Between((unsigned long)v->rhw,
                                 OSMGA_HW3D_F32_RHW_MIN,
                                 OSMGA_HW3D_F32_RHW_MAX))
            return OSMGA_HW3D_E_VTXFLOAT;
        if (!osmgaHW3DF32Finite((unsigned long)v->tu0) ||
            !osmgaHW3DF32Finite((unsigned long)v->tv0))
            return OSMGA_HW3D_E_VTXFLOAT;
    }

    return OSMGA_HW3D_OK;
}

int
osmgaHW3DIsPow2(unsigned long n)
{
    return (n != 0UL) && ((n & (n - 1UL)) == 0UL);
}

unsigned long
osmgaHW3DTexClampAxes(const OSMGAHW3DState *st)
{
    unsigned long axes = OSMGA_HW3D_CLAMP_U | OSMGA_HW3D_CLAMP_V;

    if (st == 0)
        return axes;
    /*
     * A masked index into a padded surface addresses the wrong row, so a
     * pitch that is not the width disqualifies BOTH axes at once -- it is
     * not a per-axis property.
     */
    if (st->texPitch != st->texW)
        return axes;
    if ((st->texFlags & OSMGA_HW3D_TEXF_REPEATU) != 0UL &&
        osmgaHW3DIsPow2(st->texW))
        axes &= ~OSMGA_HW3D_CLAMP_U;
    if ((st->texFlags & OSMGA_HW3D_TEXF_REPEATV) != 0UL &&
        osmgaHW3DIsPow2(st->texH))
        axes &= ~OSMGA_HW3D_CLAMP_V;
    return axes;
}

/*
 * One axis of the clip box.  Written once and used twice because the two
 * axes are the same problem and a second copy is a second place to get the
 * negative side wrong.
 *
 * The order of the guards is what keeps it inside a long:
 *
 *   - a width of nought is empty before anything is added;
 *   - the width is clamped to the destination FIRST, so every later sum is
 *     bounded by twice the destination;
 *   - a low edge at or past the destination is empty before it is used;
 *   - a low edge at or before -dstSize is empty before it is negated,
 *     which is what keeps LONG_MIN from overflowing on the way.
 */
static int
osmgaHW3DClipAxis(long lo, unsigned long size, unsigned long dst,
                  unsigned long *o0, unsigned long *o1)
{
    unsigned long hi;

    if (size == 0UL || dst == 0UL)
        return 0;
    /* Starts at or past the end. */
    if (lo >= (long)dst)
        return 0;

    if (lo < 0L) {
        /*
         * |lo|, computed so that LONG_MIN cannot overflow on the way:
         * negating lo directly is undefined there, negating lo+1 is not.
         */
        unsigned long neg = (unsigned long)(-(lo + 1L)) + 1UL;

        if (size <= neg)
            return 0;               /* ends at or before nought */
        *o0 = 0UL;
        hi  = size - neg;           /* only the part above nought */
    } else {
        unsigned long room = dst - (unsigned long)lo;   /* lo < dst, so > 0 */

        *o0 = (unsigned long)lo;
        hi  = (size >= room) ? dst : ((unsigned long)lo + size);
    }
    if (hi > dst)
        hi = dst;
    *o1 = hi;
    return (hi > *o0) ? 1 : 0;
}

int
osmgaHW3DClipBox(unsigned long scissorOn,
                 long sx, long sy, unsigned long sw, unsigned long sh,
                 unsigned long dstW, unsigned long dstH,
                 unsigned long *x0, unsigned long *x1,
                 unsigned long *y0, unsigned long *y1)
{
    if (x0 == 0 || x1 == 0 || y0 == 0 || y1 == 0)
        return 0;
    if (dstW == 0UL || dstH == 0UL)
        return 0;
    if (scissorOn == 0UL) {
        *x0 = 0UL; *x1 = dstW;
        *y0 = 0UL; *y1 = dstH;
        return 1;
    }
    if (!osmgaHW3DClipAxis(sx, sw, dstW, x0, x1))
        return 0;
    if (!osmgaHW3DClipAxis(sy, sh, dstH, y0, y1))
        return 0;
    return 1;
}

unsigned long
osmgaHW3DTexFilter(unsigned long texFlags)
{
    unsigned long f = OSMGA_HW3D_TEXFILTER_ALPHA |
                      OSMGA_HW3D_TEXFILTER_FTHRES1;

    if ((texFlags & OSMGA_HW3D_TEXF_BILIN) != 0UL)
        f |= OSMGA_HW3D_TEXFILTER_MAGBILIN;
    /*
     * The diagnostic selector wins the minification field when it is set;
     * the validator has already held it to the four named mipmap modes, so
     * nothing unnamed reaches the register from here.
     */
    if ((texFlags & OSMGA_HW3D_TEXF_MINMODE_MASK) != 0UL)
        f |= (texFlags & OSMGA_HW3D_TEXF_MINMODE_MASK)
             >> OSMGA_HW3D_TEXF_MINMODE_SHIFT;
    else if ((texFlags & OSMGA_HW3D_TEXF_BILINMIN) != 0UL)
        f |= OSMGA_HW3D_TEXFILTER_MINBILIN;
    return f;
}

/*
 * The PRODUCTION rule only.  Version 9 has two diagnostic flags on top of
 * this -- one that zeroes the second lane and one that swaps them -- and
 * they stay with version 9: they exist to prove the lanes are the even and
 * odd screen columns, which is a thing to demonstrate once and not a state
 * to offer the WARP tier.
 */
unsigned long
osmgaHW3DTexDualStage(unsigned long texFlags, int textured)
{
    unsigned long tds;

    if (!textured)
        return OSMGA_HW3D_TDS_COLOR_MUL;

    tds = ((texFlags & OSMGA_HW3D_TEXF_MODULATE) != 0UL)
              ? OSMGA_HW3D_TDS_COLOR_MUL : 0UL;
    if ((texFlags & OSMGA_HW3D_TEXF_TEXALPHA) != 0UL)
        tds |= ((texFlags & OSMGA_HW3D_TEXF_MODULATE) != 0UL)
                   ? OSMGA_HW3D_TDS_ALPHA_MUL : 0UL;
    else
        tds |= OSMGA_HW3D_TDS_ALPHA_ARG2;
    return tds;
}
