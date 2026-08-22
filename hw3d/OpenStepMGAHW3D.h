/*
 * OpenStepMGAHW3D.h -- the contract between a userland 3D client and the
 * kernel driver.
 *
 * Userland never names a register.  It fills the structures below, and the
 * kernel turns them into a DMA command list with its own encoder.  That is
 * the shape the MGA DRM uses (mga_drm.h's typed SAREA structs, validated in
 * mga_state.c and emitted by the kernel), and it is chosen over letting
 * userland write raw command lists for one reason: validating a raw list is
 * only safe if the register whitelist is complete, whereas here the kernel
 * decides which registers are written at all, so there is no enumeration to
 * get wrong.
 *
 * This header and its .c compile both in the driver and on a host, with no
 * kernel dependencies, so the validation can be tested where a mistake
 * costs nothing.  C89 only, and no long long: this target's cc 2.7.2.1
 * miscompiles long long comparisons under -O.
 */
#ifndef OPENSTEP_MGA_HW3D_H
#define OPENSTEP_MGA_HW3D_H

#define OSMGA_HW3D_MAGIC        0x4D474133UL   /* 'MGA3' */
/*
 * 4: the batch declares its row pitch as well as its size.  It used to be
 * forced to the display's, which meant a drawing surface had to be laid out
 * a screen-width at a time -- and, worse, that the software rasteriser's
 * depth buffer could never share ours, because Mesa addresses depth at the
 * surface's own width and cannot be told otherwise.
 *
 * 3: the capabilities report the display's row stride, which a library
 * needs in order to lay a drawing surface out in video memory the way the
 * engine will read it -- the engine takes the destination pitch from one
 * register and that register holds the display's.
 *
 * 2: the batch declares how big its destination is.  Before this the kernel
 * clipped every batch to a fixed 64 by 120, which was a development bound
 * and far too small for a real drawing surface.  The layout changed, so the
 * version had to move -- the probe demands an exact match precisely so that
 * a library and a driver disagreeing about where the fields are cannot draw.
 */
#define OSMGA_HW3D_VERSION      4UL

/* The 64 KiB IOMallocLow block is split: the client writes the batch at the
 * start, the kernel builds the command list after it.  28 KiB and 36 KiB
 * balance the two -- both hold about 255 triangles (scratchpad/m1_size.py). */
#define OSMGA_HW3D_BATCH_BYTES  (28UL * 1024UL)
#define OSMGA_HW3D_RING_OFFSET  OSMGA_HW3D_BATCH_BYTES
#define OSMGA_HW3D_MAX_TRI      200UL

/*
 * Pixels of x travel an edge may accumulate over one triangle.  Chosen to
 * be far larger than any real triangle needs -- a 768-row edge at one
 * pixel per row is 768 -- and far smaller than the margin between the
 * offscreen window and anything else.  The proper form derives it from
 * the distance between the destination origin and the window edges;
 * until the origin is client-chosen at runtime this constant is both
 * simpler and stricter.
 */
#define OSMGA_HW3D_EDGE_WALK    16384UL

/* Rejection reasons.  A client that is refused should be told which field
 * was wrong rather than just "no". */
#define OSMGA_HW3D_OK           0
#define OSMGA_HW3D_E_MAGIC      1
#define OSMGA_HW3D_E_VERSION    2
#define OSMGA_HW3D_E_COUNT      3
#define OSMGA_HW3D_E_DSTORG     4
#define OSMGA_HW3D_E_ZORG       5
#define OSMGA_HW3D_E_TEXORG     6
#define OSMGA_HW3D_E_DWGCTL     7
#define OSMGA_HW3D_E_TRIROW     8
#define OSMGA_HW3D_E_TRICOL     9
#define OSMGA_HW3D_E_TRISLOPE  10
#define OSMGA_HW3D_E_ALPHA     11
#define OSMGA_HW3D_E_TEXSIZE   12
#define OSMGA_HW3D_E_TEXCOORD  13
#define OSMGA_HW3D_E_DSTSIZE   14   /* destination not inside the window */
#define OSMGA_HW3D_E_EDGEDIV   15   /* AR0/AR6 is not a usable divisor */
#define OSMGA_HW3D_E_DSTPITCH  16   /* pitch absent, too small, too wide,
                                           or not a multiple of 32 pixels */
#define OSMGA_HW3D_E_TRIEMPTY  19   /* a textured primitive that draws no
                                     * pixel at all: it is still executed and
                                     * where it would fetch cannot be observed,
                                     * so it is refused rather than guessed at */
#define OSMGA_HW3D_E_TRISGN    18   /* a direction bit the validator does not
                                     * model, and therefore cannot predict the
                                     * drawn columns from */
#define OSMGA_HW3D_E_TRICROSS  17   /* the two edges cross partway down, or
                                           one leaves the clip rectangle */

/*
 * What a client may say in DWGCTL, and what the kernel says for it.
 *
 * Handing over the whole register would hand over BOP, transparency,
 * BLTMOD, SOLID, SHIFTZERO, ARZERO and SGNZERO as well.  None of those
 * changes an address, but none of them has been reasoned about either,
 * and a mask is stronger than a check: bits outside it cannot be
 * expressed, so there is no check to forget.  Deriving every field from
 * mgareg_flags.h by computation confirms this mask covers exactly opcode
 * (bits 0-3), atype (4-6) and zmode (8-10), and that bit 7 -- linear
 * addressing, which would turn x and y into a flat offset and step past
 * the clip -- is outside it.
 */
/*
 * How coarse a destination pitch this hardware can hold, IN PIXELS.
 *
 * Measured on a G450 across twelve widths, with no exception: 64, 320, 352,
 * 640 and 800 draw what the software path draws, while 322, 324, 328, 333,
 * 336, 369 and 655 cover between one and fourteen per cent of it.  Only 32
 * separates those two sets -- 16 fails on 336, 8 on 328, 4 on 324, 2 on 322,
 * and 64 fails because 352 and 800 are clean without being multiples of it.
 *
 * X.Org's mga driver says the same in two places: mga_exa.c sets
 * pixmapPitchAlign to 128 bytes for "sets of 32 pixels ... to cover 32bpp",
 * and mga_dacG.c fills a rounding table whose entry for four bytes a pixel
 * is 32.
 *
 * It is PIXELS and it varies with the depth -- that table reads 64, 32, 64,
 * 32 for one through four bytes a pixel, which is 64, 64, 192 and 128 bytes,
 * not a constant.  A byte rule cannot even describe what was measured: 336
 * pixels is 1344 bytes, a multiple of 64, and 336 is one of the widths that
 * failed.  This constant belongs to THIS contract, which the driver admits
 * only at four bytes a pixel.
 */
#define OSMGA_HW3D_PITCH_ALIGN  32UL

#define OSMGA_HW3D_DWG_CLIENT   0x0000077FUL
#define OSMGA_HW3D_DWG_FIXED    0x000C4000UL   /* bop/trans, SHIFTZERO */

#define OSMGA_HW3D_OPCODE_TRAP  0x4UL
#define OSMGA_HW3D_OPCODE_TEX   0x6UL
#define OSMGA_HW3D_ATYPE_I      0x7UL
#define OSMGA_HW3D_ATYPE_ZI     0x3UL

/*
 * ALPHACTRL likewise.  Its named fields cover bits 0-9, 11-25; bits 10
 * and 26-31 have no field at all and leave the mask.  Four fields also
 * have encodings the header never names, and refusing them costs a
 * comparison each: the source factor above 8, the destination factor
 * above 7, the reserved alpha mode 3, and alpha-test mode 1, which has no
 * macro whatsoever.
 */
#define OSMGA_HW3D_AC_CLIENT    0x03FFFBFFUL
#define OSMGA_HW3D_AC_SRC_MAX   8UL
#define OSMGA_HW3D_AC_DST_MAX   7UL

/*
 * Texture description.
 *
 * The client says what it means -- a format, a filter, a size -- and the
 * kernel builds TEXCTL, TEXCTL2 and TEXFILTER from that.  Taking the
 * registers directly would let a client clear CLAMPUV, and the reason we
 * do not bound the coordinate matrix at all is that CLAMPUV was measured
 * to hold the fetched ADDRESS inside the texture, not merely the
 * coordinate.  That argument only survives if the bit is ours.
 *
 * Sizes need not be powers of two: the log2 field says which power of two
 * contains the texture, and the exact size travels separately in bits
 * 18 and up.  The bound the DDX states is 2048 (mga_storm.c:300-304).
 * Pitch is in TEXELS, is the texture's own row stride, and may legally
 * exceed the width -- EXA uses a pixmap's real stride and Storm rounds
 * its scratch texture up to a multiple of sixteen.  The kernel writes the
 * client's pitch into TEXCTL and uses the same number for the reach
 * check, so the check and the hardware cannot disagree.
 */
#define OSMGA_HW3D_TEX_MAX_DIM  2048UL
#define OSMGA_HW3D_TEX_MAX_PIT  2047UL   /* TEXCTL's field is 11 bits */
#define OSMGA_HW3D_TEXFMT_TW32  6UL      /* the only format we allow yet */
#define OSMGA_HW3D_TEXF_BILIN   0x1UL    /* client flag, not a register bit */
/*
 * Two diagnostic flags, and what they are for.
 *
 * The engine draws in two lanes: the even columns of a row and the odd ones
 * are handled by different halves of it, and TDUALSTAGE0 and TDUALSTAGE1 hold
 * one lane's texture-environment word each rather than two stages of a serial
 * combiner.  That was worked out from a failure -- selecting the interpolated
 * alpha in stage nought alone fixed exactly the even columns -- and a reading
 * arrived at from a failure is worth proving before it is believed.
 *
 * So the encoder can be asked to put the two words back the way they were, or
 * to swap them.  Neither flag lets a client PUT anything in a register: the
 * two words are the kernel's own constants and the flags only choose which
 * lane gets which.  What they buy is the control the reading needs --
 *
 *      neither     every column takes the interpolated alpha
 *      TDS1ZERO    the even columns do and the odd ones take the texture's
 *      TDSSWAP     the odd columns do and the even ones take the texture's
 *
 * -- and the third line is the one that cannot be explained any other way.
 */
#define OSMGA_HW3D_TEXF_TDS1ZERO 0x2UL   /* stage 1 left at zero, as it was */
#define OSMGA_HW3D_TEXF_TDSSWAP  0x4UL   /* the two lanes' words exchanged */
/*
 * Where the destination's alpha comes from.
 *
 * Clear, the engine is told to take the INTERPOLATED alpha -- what the
 * triangle carries in ALPHASTART -- which is what GL_REPLACE means for a
 * texture with no alpha of its own: Av = Af.  Set, it takes the TEXTURE's,
 * which is what the same mode means for one that has: Av = At.
 *
 * It is one bit rather than a mode because that is the whole of the
 * difference between the two formats here.  The colour is Ct either way.
 */
#define OSMGA_HW3D_TEXF_TEXALPHA 0x8UL   /* Av = At rather than Av = Af */
/*
 * GL_MODULATE rather than GL_REPLACE: the texel multiplies the interpolated
 * colour instead of standing in for it.
 *
 *      REPLACE   Cv = Ct      MODULATE   Cv = Cf Ct
 *      and the alpha follows OSMGA_HW3D_TEXF_TEXALPHA, except that under
 *      MODULATE a texture that HAS an alpha multiplies it too:
 *
 *          RGB   REPLACE   Av = Af      RGBA  REPLACE   Av = At
 *          RGB   MODULATE  Av = Af      RGBA  MODULATE  Av = Af At
 *
 * which is Mesa's own table (texture.c, apply_texture) and four words of the
 * combiner, not four code paths.
 */
#define OSMGA_HW3D_TEXF_MODULATE 0x10UL
/*
 * Wrapping, one axis at a time.
 *
 * The kernel has always programmed TEXCTL's two CLAMPUV bits on, and the
 * coordinate bound the validator applies was justified on top of that: with
 * clamping, any coordinate it admits addresses a texel inside the texture
 * whatever it says.  Clearing a bit is repeat -- X.Org's mga driver selects it
 * the same way, "if (!repeat) texctl |= MGA_CLAMPUV".
 *
 * WHICH bit is u and which is v is measured, not assumed; the names below are
 * what the measurement says.  Two flags rather than one because GL lets the
 * two axes differ.
 *
 * The safety that clamping used to provide has to come from somewhere else,
 * and it comes from three things together: the validator still refuses a
 * coordinate that goes NEGATIVE at any drawn pixel, so repeat only ever sees
 * a coordinate between nought and eight texture spans; the dimension being
 * wrapped must be a power of two, so the reduction is a mask; and the pitch
 * must equal the width, since a masked index into a padded surface would
 * land in the wrong row.  All three are required below, not assumed.
 */
#define OSMGA_HW3D_TEXF_REPEATU  0x20UL
#define OSMGA_HW3D_TEXF_REPEATV  0x40UL
/*
 * Perspective.  Set, tmr[4], tmr[5] and tmr[8] stop being ignored and become
 * the DENOMINATOR plane -- dq/dx, dq/dy and q at the primitive's anchor --
 * and the encoder clears TEXCTL's NOPERSPECTIVE so the engine samples s/q
 * rather than s.
 *
 * The format is not a guess.  xf86-video-mga's mga_exa.c documents the nine
 * TMR registers as a 3x3 projective matrix whose elements are all 16.16, and
 * converts only the two numerator rows into the texture unit's own fixed
 * point by shifting them by 20 - log2(width) - 16; the denominator row it
 * writes unconverted, and its identity case is 0, 0, 1<<16.  That conversion
 * also cross-checks this driver's own measurement: for a 64-wide texture the
 * shift is -2, so one texel, 1<<16 in 16.16, becomes 1<<14 = 16384 register
 * units, which is the texel size measured here on the machine.
 *
 * So q is 16.16 with 1.0 = 65536, and the coordinate the engine samples is
 *
 *      coordinate = s * 65536 / q
 *
 * which the validator bounds WITHOUT dividing.  The existing rule is
 * 0 <= coordinate <= OSMGA_HW3D_TEX_COORD_MAX, and that span is a multiple
 * of 65536, so the rule is exactly
 *
 *      0 <= s  and  s <= (OSMGA_HW3D_TEX_COORD_MAX >> 16) * q
 *
 * with nothing wider than a long anywhere in it.  At q = 65536 it reduces to
 * the affine rule it replaces, term for term, so the affine path does not
 * change behaviour by being written this way.
 *
 * The corners are enough: q is affine and the region is convex, so q inside
 * is a convex combination of its corner values and cannot dip below them,
 * and s/q is likewise a convex combination of the corner values of s/q.
 *
 * The row the denominator is evaluated at is the accumulated count of
 * TEXTURED rows in the batch, the same index v uses -- measured, by leaving a
 * gap between two primitives and finding that the far one reads what the
 * near one's rows left behind rather than what its own screen position would
 * give.  So it is an accumulator and not a plane in screen coordinates.
 *
 * Q_MIN and Q_MAX are NOT the safety argument.  What keeps the address inside
 * the texture is the addressing -- clamped it saturates, repeating it is
 * masked, measured out to eight texture spans in both.  The two bounds keep
 * the divider's inputs inside the range whose behaviour has been looked at,
 * which is the same rule the coordinate bound has always been, and they keep
 * the validator's own arithmetic inside a long.
 */
#define OSMGA_HW3D_TEXF_PERSP    0x80UL
#define OSMGA_HW3D_Q_ONE         65536L         /* q = 1.0, 16.16 */
/*
 * The smallest denominator, and it is an ACCURACY budget rather than a place
 * the divider fails -- the divider does not fail.  Measured: the divisor is
 * exactly q at all twelve denominators tried, and what goes wrong is an
 * addend on the numerator of 512 times q's normalised fraction, which has no
 * exponent term and so is bounded by 512 whatever q is.  One texel is q/4 in
 * numerator units, so the error is at most 2048/q texels:
 *
 *      q = 2048   one texel        q = 8192   a quarter
 *      q = 32768  a sixteenth
 *
 * The old value of 256 was chosen with no reason and permitted eight texels.
 *
 * 8192 buys a quarter of a texel.  It is not free: a projective triple can be
 * scaled by any constant without changing the quotient, so a builder makes q
 * as large as the numerator bound allows, and for a primitive spanning one
 * whole texture that bound is s <= 2^23, which caps q at 2^39 / 2^20.  The
 * depth ratio a single primitive may then span is 2^39 / (2^20 * Q_MIN), and
 *
 *      depth ratio  x  1/error-in-texels  =  256
 *
 * always -- the register widths fix the product, and Q_MIN only says where on
 * that curve to sit.  A quarter texel buys 64 to one, which is comfortable
 * for one triangle; a sixteenth would buy only 16 to one.
 *
 * Widening the numerator bound under perspective would move the whole curve,
 * but it cannot be done alone: the slope bounds are the affine ones, written
 * when tmr[6] WAS the coordinate, and they would have to become aware of q
 * as well.  That is its own design.
 */
#define OSMGA_HW3D_Q_MIN         8192L          /* a quarter of a texel */
#define OSMGA_HW3D_Q_MAX         (1L << 23)     /* 128.0 */
/*
 * dq/dx and dq/dy are bounded only so that EVALUATING q cannot leave a long,
 * and the bound is taken against the surface the batch DECLARES rather than
 * against a span written down here -- see the check in the validator.  The
 * first attempt used Q_MAX/4096, a thirty-second of a unit per pixel, which
 * was the arithmetic done backwards: it bounded the slope by the coordinate
 * range instead of by the overflow, and turned away a denominator climbing a
 * sixteenth a row, which is an ordinary one.  What limits the slope in any
 * real sense is the corner check keeping q inside [Q_MIN, Q_MAX] over the
 * pixels the primitive actually draws.
 */

/*
 * How far a texture coordinate may reach.
 *
 * The first draft did not bound the coordinate matrix at all, on the
 * grounds that CLAMPUV was measured to hold the fetched address inside
 * the texture.  That measurement covered one case: magnifying eight times
 * so the coordinate ran off the high end of both axes.  It says nothing
 * about a negative start, and nothing about the H registers, which the
 * sources do not show to be ignored under NOPERSPECTIVE.
 *
 * So the H family is taken out of the client's hands entirely -- the
 * kernel writes TMR4, TMR5 and TMR8 -- and the remaining six are required
 * to be non-negative and to reach no further than the magnification that
 * was actually measured.  Beyond that the answer would rest on a property
 * nobody has tested.
 *
 * A full texture spans about 1 << 20 in this fixed point regardless of
 * its size, since one texel is 1 << (20 - log2(size)).
 */
#define OSMGA_HW3D_TEX_SPAN     (1UL << 20)
#define OSMGA_HW3D_TEX_COORD_MAX (8UL * OSMGA_HW3D_TEX_SPAN)

/*
 * What the engine adds to a texture coordinate before it picks a texel, and
 * what the encoder takes back off.
 *
 * The engine's addend is not one number.  Sweeping the start with every
 * gradient at zero -- so the coordinate at each pixel IS the start -- and
 * bisecting the turnover at all sixty-three texel boundaries of a 64-texel
 * texture, it adds
 *
 *      511 below 2^16      510 below 2^17      508 below 2^18
 *      504 below 2^19      496 below 2^20
 *
 * that is, 512 less one, two, four, eight, sixteen, stepping at the powers of
 * two of the coordinate: the coordinate is carried with about sixteen
 * significant bits and the addend is what survives of 512.  Above 2^20 it
 * stops mattering, because a coordinate past the last texel is clamped and no
 * addend this small can change which texel that is.
 *
 * What is subtracted here is the SMALLEST of them, not the one that matches
 * the band the primitive starts in.  Correcting per band is exact for a
 * primitive that stays inside one, and it is what the first attempt did --
 * but a primitive that crosses a band gets back less than was taken off, and
 * a coordinate sitting exactly ON a texel boundary then lands one unit below
 * it and reads the texel before.  Coordinates exactly on boundaries are not a
 * corner case; they are what a texture drawn at its own size is made of.
 * That is what took the first attempt out, and it was read at the time as the
 * accumulation arriving short.
 *
 * Subtracting the smallest instead can only ever leave the coordinate at or
 * ABOVE where the caller put it, by at most 511 - 496 = 15 units against a
 * texel of 16384.  So boundary-aligned drawing stays exact, and a coordinate
 * just below a boundary is misread only in the last fifteen units of the
 * texel instead of the last 511.
 *
 * Measured on the machine, from userland, before it went in: six hundred
 * coordinates each sitting just below a texel boundary, all of which must
 * read the texel BELOW.  Uncorrected they came back 511 wrong of 600 in the
 * first band and 497 wrong of 600 past 2^19.
 *
 * The fine structure of at most fifteen units on top of the ladder, which
 * involves the column and which no rule tried so far fits, is written down in
 * docs/M1_4C7_TEXBIAS_PLAN.md rather than guessed at.  It is the same size as
 * the residual above and smaller than what is being corrected.
 */
#define OSMGA_HW3D_TEX_BIAS     496L

typedef struct {
    unsigned long dstorg;          /* colour origin, byte offset into VRAM */
    /*
     * How much of the destination this batch may touch, in pixels.  The
     * kernel clips to it, so a client cannot reach past what it declared;
     * and it proves the declared rectangle lies inside the window it owns
     * before believing any of it, so a client cannot declare its way out
     * either.  The pitch is declared too, below.
     */
    unsigned long dstWidth;        /* columns; must be <= dstPitch */
    unsigned long dstHeight;       /* rows */
    /*
     * Pixels between one row and the next.  The engine has a single pitch
     * register and uses it for colour and for depth alike, so this is what
     * decides how BOTH are laid out -- which is the reason it is here at all
     * rather than fixed at the display's: Mesa addresses its depth buffer at
     * the surface's own width, so a surface at any other pitch is one whose
     * depth the software path cannot share.
     */
    unsigned long dstPitch;
    unsigned long zorg;            /* depth origin; ignored unless depth is on */
    unsigned long texorg;          /* texture origin; ignored unless textured */
    unsigned long texW, texH;      /* texels; need not be powers of two */
    unsigned long texPitch;        /* texels per row, >= texW */
    unsigned long texFormat;       /* OSMGA_HW3D_TEXFMT_* */
    unsigned long texFlags;        /* OSMGA_HW3D_TEXF_* */
    /* tmr[0..3] are the increments, tmr[6] and tmr[7] the starts.  All six
     * are bounded, and MAY BE NEGATIVE: what is required is that the
     * coordinate stays inside the measured range at every pixel, which for a
     * plane means at each of the four corners.  They were required
     * non-negative once, which turned away roughly half of all real texture
     * mapping -- a triangle whose texture runs the other way across the
     * screen has a negative gradient and is in no way exotic.
     *
     * tmr[4], tmr[5] and tmr[8] are the H family and are IGNORED -- the
     * kernel writes them, see the note above. */
    long tmr[9];
} OSMGAHW3DState;

typedef struct {
    /* Per triangle, because Mesa varies depth mode, blending and texture
     * enable between primitives; when these were per batch, four depth
     * modes needed four submissions. */
    unsigned long dwgctl;          /* masked to opcode, atype and zmode */
    unsigned long alphactrl;       /* masked, and undefined encodings refused */
    long y, h;                     /* first row and row count */
    long ar0, ar1, ar2, ar4, ar5, ar6, sgn;   /* both edges */
    unsigned long fxbndry;         /* (right << 16) | left */
    /* Colour interpolators, in the order the encoder emits them:
     *   0,1,2 = red   start, x increment, y increment  (DR4,  DR6,  DR7)
     *   3,4,5 = green                                  (DR8,  DR10, DR11)
     *   6,7,8 = blue                                   (DR12, DR14, DR15)
     *   9,10,11 reserved, must be zero.
     * Values are (component << 15); the << 7 the DDX writes is wrong for
     * this part, measured three times now (colour, depth, alpha). */
    unsigned long dr[12];
    unsigned long z0, zdx, zdy;
    unsigned long a0, adx, ady;
} OSMGAHW3DTri;

typedef struct {
    unsigned long magic;
    unsigned long version;
    unsigned long triCount;
    OSMGAHW3DState state;
    OSMGAHW3DTri tri[OSMGA_HW3D_MAX_TRI];
} OSMGAHW3DBatch;

/*
 * What the kernel owns and userland cannot influence.  Containment rests on
 * these: the clip and the pitch are set by the kernel, so a triangle can only
 * reach rows 0..clipY1 of whatever origin is allowed, and the reach check
 * below turns that into a byte bound.
 */
typedef struct {
    unsigned long pitchBytes;      /* destination row stride */
    unsigned long clipY1;          /* last row the kernel will allow */
    unsigned long clipX1;          /* last column */
    unsigned long colourStart, colourEnd;
    unsigned long depthStart, depthEnd;
    unsigned long texStart, texEnd;
    /* No longer a constant ceiling: the reach is computed from the size
     * the client gave, which is the same size the kernel programs. */
    unsigned long batchBytes;
    /* Largest |x| an edge may accumulate over a triangle, in pixels.  The
     * hardware clip clamps the span per pixel and a measurement showed it
     * holding for an edge walking 800 px/row outside the window, but AR2
     * and AR5 are 18-bit fields, so a client can ask for 131071 px/row and
     * that magnitude has never been tried -- trying it would put the
     * unclipped address in the visible framebuffer.  Bounding the slope
     * here means containment does not rest on the clip alone. */
    unsigned long maxEdgeWalk;
} OSMGAHW3DLimits;

/*
 * The batch is a shared layout, so a change that made it outgrow its half of
 * the ring, or that changed the word size, has to fail the build rather than
 * be discovered at run time.  A negative array size is the C89 way to say so.
 */
typedef int OSMGAHW3DFitsCheck[
    (sizeof(OSMGAHW3DBatch) <= OSMGA_HW3D_BATCH_BYTES) ? 1 : -1];
typedef int OSMGAHW3DWordCheck[(sizeof(unsigned long) == 4) ? 1 : -1];

/*
 * M1-3a: the capability parameter the Mesa backend probes.
 *
 * Identity is established by the parameter itself.  Another display driver
 * does not know this name, so its getIntValues fails and the backend falls
 * back to software without needing any other way to tell whose card it is.
 *
 * The driver answers this parameter even when acceleration is switched off,
 * because "not our driver" and "our driver, switch off" are different things
 * to a person reading a log, and reporting them the same way would make the
 * switch impossible to diagnose.  The backend gates on ENABLED.
 */
#define OSMGA_HW3D_CAPS_PARAM   "OSMGAHW3DCaps"
#define OSMGA_HW3D_CAPS_COUNT   9U

/* caps[OSMGA_HW3D_CAP_FLAGS] */
#define OSMGA_HW3D_CAP_ENABLED  0x00000001UL /* Configure.app switch is on */
#define OSMGA_HW3D_CAP_MMAP     0x00000002UL /* the VRAM window is published */
#define OSMGA_HW3D_CAP_CMD      0x00000004UL /* the command window exists */
#define OSMGA_HW3D_CAP_READY    0x00000008UL /* linear mode, engine usable */

/*
 * All four are required before the backend may accelerate.  ENABLED alone is
 * not enough: without the VRAM window there is nowhere to put a batch, and
 * without the command window there is no list to submit.
 */
#define OSMGA_HW3D_CAP_REQUIRED (OSMGA_HW3D_CAP_ENABLED | \
                                 OSMGA_HW3D_CAP_MMAP    | \
                                 OSMGA_HW3D_CAP_CMD     | \
                                 OSMGA_HW3D_CAP_READY)

#define OSMGA_HW3D_CAP_MAGIC    0U  /* OSMGA_HW3D_MAGIC */
#define OSMGA_HW3D_CAP_VERSION  1U  /* OSMGA_HW3D_VERSION */
#define OSMGA_HW3D_CAP_FLAGS    2U
#define OSMGA_HW3D_CAP_MAXTRI   3U  /* OSMGA_HW3D_MAX_TRI */
#define OSMGA_HW3D_CAP_BATCH    4U  /* OSMGA_HW3D_BATCH_BYTES */
#define OSMGA_HW3D_CAP_MAJOR    5U  /* character major of the VRAM device */
#define OSMGA_HW3D_CAP_VRAMOFF  6U  /* window start, byte offset into VRAM */
#define OSMGA_HW3D_CAP_VRAMLEN  7U  /* window length in bytes */
#define OSMGA_HW3D_CAP_STRIDE   8U  /* display row stride, in pixels */

/*
 * The same capabilities, reachable from plain C.
 *
 * The parameter above needs IODeviceMaster, which is Objective-C.  An archive
 * holding it cannot be linked by a C program: measured on the target, a C main
 * linking such an archive fails with
 *
 *     /bin/ld: Undefined symbols:
 *     .objc_class_name_IODeviceMaster
 *
 * and libGL has to be usable from an unmodified C application given nothing
 * but -lGL.  So the library asks through the character device it must open
 * for mmap anyway, and never links Objective-C.
 *
 * The parameter form stays, because Objective-C tools -- the Configure.app
 * inspector above all -- already speak it, and both forms report the same
 * flags from the same state.
 */
typedef struct {
    unsigned long caps[OSMGA_HW3D_CAPS_COUNT];
} OSMGAHW3DCapsBlock;

/*
 * Encoding taken from the target's <sys/ioctl.h>, not from memory of a more
 * recent BSD: on this system IOCPARM_MASK is 0x7f, so a parameter block must
 * stay under 128 bytes.  Assuming the modern 0x1fff would have encoded a
 * length the kernel then truncated, and the copyout would have been short
 * rather than refused.
 */
#define OSMGA_IOC_OUT_BIT   0x40000000UL
#define OSMGA_IOC_IN_BIT    0x80000000UL
#define OSMGA_IOC_PARM_MASK 0x7fUL
#define OSMGA_IOC_GROUP     'M'
#define OSMGA_IOC_CAPS      ((unsigned long)(OSMGA_IOC_OUT_BIT | \
    ((sizeof(OSMGAHW3DCapsBlock) & OSMGA_IOC_PARM_MASK) << 16) | \
    ((unsigned long)OSMGA_IOC_GROUP << 8) | 1UL))

/*
 * Running a batch, from plain C, for the same reason the capabilities are
 * reachable that way: the parameter form needs IODeviceMaster and libGL
 * cannot link Objective-C.
 *
 * Nothing goes in -- the batch is already in the mapped command window --
 * and what comes back is the same four words the status parameter reports,
 * so a caller learns which triangle was refused and why without a second
 * call that could race another client's submission.
 *
 * The outcome travels in `status` rather than in the ioctl's own return,
 * because a 4.3BSD ioctl copies its block back only when it returns zero:
 * refusing there would have thrown away the very explanation the block
 * exists to carry.  Measured, not reasoned about -- an over-long batch came
 * back as EINVAL with a verdict of OSMGA_HW3D_OK, which is the block never
 * having been copied at all.  So the ioctl now returns zero whenever it
 * managed to attempt a submission, and non-zero only when it could not try.
 */
/*
 * A verdict of this means the batch was never examined -- the driver stopped
 * before the validator ran.  It has to be distinct from OSMGA_HW3D_OK, which
 * is zero, or a submission refused before validation would report the code
 * for "drew fine".
 */
#define OSMGA_HW3D_NOT_RUN  0xFFFFFFFFUL

typedef struct {
    unsigned long status;    /* 0 drew; else an errno explaining why not */
    unsigned long verdict;   /* OSMGA_HW3D_OK or one of the E_ codes */
    unsigned long triangle;  /* which one, when the verdict names one */
    unsigned long dwords;    /* the encoded list's length */
    unsigned long spins;     /* how long the engine was waited for */
} OSMGAHW3DSubmitBlock;

#define OSMGA_IOC_SUBMIT    ((unsigned long)(OSMGA_IOC_OUT_BIT | \
    ((sizeof(OSMGAHW3DSubmitBlock) & OSMGA_IOC_PARM_MASK) << 16) | \
    ((unsigned long)OSMGA_IOC_GROUP << 8) | 2UL))

/*
 * Copying a drawn surface out of video memory, done by the driver.
 *
 * A client's first read after a submission returns can still hold what was
 * there before the draw.  Measured since this was written: it depends on
 * where the client last READ before submitting -- inside the first 64 bytes
 * of the window and the next submission's first 64 bytes arrive late, past
 * them and they do not, and writes make no difference either way.  The
 * remark that once stood here, that an uncached read from the driver changed
 * nothing, was measured at the window's first word, which is inside that
 * range and is the one offset that cannot settle anything.
 *
 * The copy still goes through an alias the driver CAN make uncached, so a
 * client using it never reads video memory at all and none of this applies.
 */
typedef struct {
    unsigned long srcOffset;    /* byte offset into video memory */
    unsigned long rows;
    unsigned long srcPitch;     /* bytes between rows in video memory */
    unsigned long width;        /* bytes copied from each row, <= srcPitch */
    unsigned long dst;          /* where to put them, in the caller */
    unsigned long dstPitch;     /* bytes between rows there */
} OSMGAHW3DMirrorBlock;

#define OSMGA_IOC_MIRROR    ((unsigned long)(OSMGA_IOC_IN_BIT | \
    ((sizeof(OSMGAHW3DMirrorBlock) & OSMGA_IOC_PARM_MASK) << 16) | \
    ((unsigned long)OSMGA_IOC_GROUP << 8) | 3UL))

typedef int OSMGAHW3DMirrorFits[
    (sizeof(OSMGAHW3DMirrorBlock) <= OSMGA_IOC_PARM_MASK) ? 1 : -1];

typedef int OSMGAHW3DSubmitFits[
    (sizeof(OSMGAHW3DSubmitBlock) <= OSMGA_IOC_PARM_MASK) ? 1 : -1];

/* If the block ever outgrows the mask the encoded length wraps silently. */
typedef int OSMGAHW3DCapsFits[
    (sizeof(OSMGAHW3DCapsBlock) <= OSMGA_IOC_PARM_MASK) ? 1 : -1];

int osmgaHW3DValidate(const OSMGAHW3DBatch *b, const OSMGAHW3DLimits *lim,
                      unsigned long *badTri);

#endif /* OPENSTEP_MGA_HW3D_H */
