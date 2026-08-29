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
 *
 * 3: the split below moved, which moves sizeof(OSMGAHW3DBatch).  The batch
 * had to shrink so that the region a client may map is a whole number of
 * pages -- see the note on the split -- and the triangle count came down
 * with it.  A library built against version 2 would lay its triangles out
 * at the wrong offsets, so the version is what stops the pair from drawing
 * at all rather than drawing wrongly.
 */
#define OSMGA_HW3D_VERSION      9UL

/*
 * The 64 KiB IOMallocLow block is split: the client writes the batch at the
 * start, the kernel builds the command list after it.
 *
 * 24 KiB and 40 KiB, and the first number is the one that is not free to
 * move.  The kernel maps a WHOLE PAGE per mmap call, and PAGE_SIZE here is
 * 8192, so if the batch did not end on a page boundary the last page a
 * client may map would reach past it into the command list -- which it did:
 * the split was 28 KiB, three and a half pages, and every accelerated
 * process had 4 KiB of the list mapped read-write.  Measured on the machine,
 * not deduced: a client read the list back and watched it change as batches
 * were submitted.  So the batch is three whole pages and the mmap handler
 * refuses anything that would reach the list.
 *
 * 64 KiB is a hard ceiling, not a choice: dma_buf_alloc refuses a larger
 * request (see OSMGA_DMA_RING_BYTES in the driver), so the two halves have
 * to be traded against each other rather than both made bigger.
 *
 * The triangle count follows from what is left.  sizeof(OSMGAHW3DBatch) is
 * 108 + 132 * MAX_TRI and has to fit in the batch's three pages, while the
 * list has to hold the worst case the encoder can produce for that many
 * primitives; 180 satisfies both with room over on each side, and 185 is the
 * most that would fit at all.  It costs the Mesa path nothing: that path
 * submits one source triangle's one to four trapezoids per batch and has
 * never come near either number.
 */
#define OSMGA_HW3D_BATCH_BYTES  (24UL * 1024UL)
#define OSMGA_HW3D_RING_OFFSET  OSMGA_HW3D_BATCH_BYTES
#define OSMGA_HW3D_MAX_TRI      180UL

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

/* Appended, never renumbered: the verdict is observable to clients that
 * already know 1..19, and reusing a number would tell an old client the
 * wrong thing.  An origin whose low bits are not zero is refused here
 * rather than by the reach checks, because those bits are not part of the
 * address -- see the alignment note below. */
#define OSMGA_HW3D_E_DSTORGAL  20
#define OSMGA_HW3D_E_ZORGAL    21
#define OSMGA_HW3D_E_TEXORGAL  22
#define OSMGA_HW3D_E_TRIFIELD  23   /* a per-triangle value that does not fit
                                     * the signed field its register keeps */
#define OSMGA_HW3D_E_ALPHACROSS 24  /* an ALPHACTRL combination the spec
                                     * says is not supported -- see the
                                     * note below for which three, and for
                                     * the one that is deliberately absent */

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

/*
 * The origins are not plain byte offsets.  Each of these registers keeps a
 * memory-space bit in bit 0 (1 selects SYSTEM memory), an access bit in bit
 * 1, and the actual origin in a high field -- DSTORG <31:6>, ZORG <31:2>
 * with a note requiring multiples of 128, TEXORG <31:5>.  So an offset whose
 * low bits are not zero does not merely land somewhere slightly different:
 * it can move the destination, the depth buffer or the texture out of the
 * frame buffer entirely.
 *
 * The validator bounded these numerically and nothing else, which let every
 * unaligned value through.  It never fired because the only producer is an
 * allocator that returns aligned bases, not because anything stopped it.
 *
 *   DSTORG  64  (3-129)   ZORG  128  (3-286, "the seven LSBs = 0")
 *   TEXORG  32  (3-221)
 *
 * DSTORG has a further rule in PW24 -- a multiple of three 64-byte units --
 * which does not apply here because this path programs PW32.  It would have
 * to come back if a 24-bit mode ever did.
 */
#define OSMGA_HW3D_DSTORG_ALIGN 64UL
#define OSMGA_HW3D_ZORG_ALIGN   128UL
#define OSMGA_HW3D_TEXORG_ALIGN 32UL

#define OSMGA_HW3D_DWG_CLIENT   0x0000077FUL
#define OSMGA_HW3D_DWG_FIXED    0x000C4000UL   /* bop/trans, SHIFTZERO */

#define OSMGA_HW3D_OPCODE_TRAP  0x4UL
#define OSMGA_HW3D_OPCODE_TEX   0x6UL
#define OSMGA_HW3D_ATYPE_I      0x7UL
#define OSMGA_HW3D_ATYPE_ZI     0x3UL
#define OSMGA_HW3D_ZMODE_NOZCMP 0x0UL

/*
 * Does this triangle address the depth buffer?
 *
 * ZI is "depth mode with gouraud" and I is "Gouraud (with depth compare)" --
 * Matrox's own register decoder says so, in those words
 * (xf86-video-mga-2.0.0/util/stormdwg.c:32 and :35).  So atype alone does not
 * answer it: I with a real comparison reads depth, and only I with NOZCMP --
 * a comparison that always passes, which is what "no depth" has always been
 * spelled as here -- does not.
 *
 * This lives in one place because three copies of it existed, written out by
 * hand, in the validator, the encoder and the submit path's dead-zone test,
 * and the third one carried a comment promising it was "the condition the
 * validator and the encoder both use, and by the same expression".  A promise
 * is not a mechanism.  The masking is inside on purpose: a caller that hands
 * over an unmasked dwgctl must not be able to make the answer wrong.
 *
 * Reserved zmode 1 counts as a comparison.  The validator lets it through --
 * deliberately, since no zmode can move a write -- so containment has to
 * assume it addresses depth rather than assume it does not.
 */
int osmgaHW3DAddressesDepth(unsigned long dwgctl);

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
 * ---- ALPHACTRL combinations, as opposed to ALPHACTRL fields
 *
 * The checks above ask whether each field holds a value its own table
 * defines.  The register's Notes forbid four COMBINATIONS of fields that
 * are individually legal (3-32..3-34):
 *
 *   1  srcblendf = SRC_ALPHA_SATURATE  =>  dstblendf must not be ZERO
 *   2  alphamode = video alpha         =>  dstblendf must not be ZERO
 *   3  alphamode = video alpha         =>  astipple must not be 1
 *   4  astipple  = 1                   =>  only four (src, dst) pairs
 *
 * THREE OF THEM ARE REFUSED.  The first is not, and the reason is not
 * squeamishness: this repository has measured it.  test/…-blendsat-client
 * exists to send exactly src 8 with dst ZERO and report what comes back,
 * and its answer is stable and published -- "source factor 8 is source
 * alpha on colour and ONE on alpha, so it is not GL_SRC_ALPHA_SATURATE".
 * Refusing it would retire a measurement and break the test that made it.
 *
 * The rule the four share is the one this validator already applies to
 * reserved zmode 1, which it lets through because no zmode can move a
 * write: refuse what is undefined to us, not what has been measured.  None
 * of the four moves a write; what refusing buys is that a client is not
 * handed semantics nobody knows.  For the first, we know them.
 *
 * CAREFUL.  Rules 1 and 2 are about dstblendf = ZERO *in combination*.
 * dstblendf = ZERO on its own is not merely legal, it is what the spec
 * recommends: "To disable alpha blending, srcblendf must be programmed
 * with 1 and dstblendf with 0" -- which is precisely the word Mesa sends
 * for every opaque triangle.  A check written as "dstblendf must not be
 * zero" would refuse all of them.
 */
#define OSMGA_HW3D_AC_SRC_SAT   8UL   /* SRC_ALPHA_SATURATE */
#define OSMGA_HW3D_AC_AM_VIDEO  2UL   /* alphamode: video alpha */
#define OSMGA_HW3D_AC_BF_ZERO   0UL
#define OSMGA_HW3D_AC_BF_ONE    1UL
#define OSMGA_HW3D_AC_BF_SRCA   4UL
#define OSMGA_HW3D_AC_BF_OMSRCA 5UL

/*
 * Is this ALPHACTRL a combination the specification supports?  Takes the
 * value already masked to OSMGA_HW3D_AC_CLIENT.  Returns 1 when allowed.
 * Portable; the host tests link it.
 */
int osmgaHW3DAlphaCross(unsigned long ac);

/*
 * TEXWIDTH/TEXHEIGHT as WARP wants them, which is NOT how the trapezoid
 * path wants them.  Portable arithmetic; the host tests link it, which is
 * the point -- the two encodings differ in both variable fields and the
 * difference is invisible to reading.
 */
unsigned long osmgaHW3DWarpTexDim(unsigned long dim, unsigned long log2dim);

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
 * The row the denominator is evaluated at is the primitive's own, the same
 * index v uses.  It was an accumulator across the batch while the matrix was
 * written once before all the primitives -- measured, by leaving a gap
 * between two and finding the far one reading what the near one's rows left
 * behind.  The matrix is written ahead of each primitive now and the write
 * re-seeds q as well as the numerators: probe section 78b gives two
 * primitives the same numerators and q of one then two, and reads the
 * quotient and then half of it.
 *
 * Q_MIN and Q_MAX are NOT the safety argument.  What keeps the address inside
 * the texture is the addressing -- clamped it saturates, repeating it is
 * masked, measured out to eight texture spans in both.  The two bounds keep
 * the divider's inputs inside the range whose behaviour has been looked at,
 * which is the same rule the coordinate bound has always been, and they keep
 * the validator's own arithmetic inside a long.
 */
#define OSMGA_HW3D_TEXF_PERSP    0x80UL

/*
 * The MINIFICATION filter, which is a different register field from the
 * magnification one.
 *
 * The chooser used to say the engine had a single filter switch and require
 * MinFilter to equal MagFilter on those grounds.  It has two: TEXFILTER holds
 * a MIN field and a MAG field (xf86-video-mga mga_reg.h:576-580), and the
 * encoder was writing only MAG.  So a GL_LINEAR texture that MINIFIED was
 * point sampled -- measured, a scene at two texels to the pixel differed from
 * the software rasteriser on all 1024 of its pixels, the hardware reading
 * texel one exactly where the software blended nought and one.
 *
 * Kept a separate flag from BILIN rather than folded into it, because the two
 * being independent is what lets the probe ask whether the hardware chooses
 * between them per fragment -- which is lambda, and which decides whether
 * mipmapping is reachable on this path at all.
 */
#define OSMGA_HW3D_TEXF_BILINMIN 0x100UL

/*
 * A DIAGNOSTIC selector for the minification field, four bits wide.
 *
 * Nought means the ordinary behaviour, where BILIN and BILINMIN decide the
 * two fields.  Anything else goes straight into TEXFILTER's minification
 * field, and only the four mipmap modes the generated register description
 * names are accepted -- mm1s, mm2s, mm4s and mm8s.  0xd is MIN_ANISO in the
 * hand-written header and absent from the generated one; whatever it selects
 * is a different fetch footprint and it is not an input to this experiment.
 *
 * This exists so the probe can ask WHERE a mipmap mode reads from, which is
 * the one thing standing between this driver and mipmapping: the half that
 * chooses a level per fragment is already there and measured, and where the
 * levels live is not.
 *
 * The builder never sets it.  When it is set the validator requires twice the
 * base texture's rows to be inside the proven texture window, because a whole
 * mip chain is four thirds of the base and the engine may walk one.
 */
#define OSMGA_HW3D_TEXF_MINMODE_SHIFT 9
#define OSMGA_HW3D_TEXF_MINMODE_MASK  0x1E00UL
#define OSMGA_HW3D_TEXF_MINMODE_MM1S  0x8UL
#define OSMGA_HW3D_TEXF_MINMODE_MM2S  0x9UL
#define OSMGA_HW3D_TEXF_MINMODE_MM4S  0xAUL
#define OSMGA_HW3D_TEXF_MINMODE_MM8S  0xCUL

/*
 * How far below nought a coordinate may go before it is refused.
 *
 * Not a licence to sample outside the texture -- the addressing does not let
 * it, clamped or repeating -- but an acknowledgement that the edge walk's
 * integer x sits a fraction of a pixel outside the true edge, so a coordinate
 * that is nought along that edge comes out a hair below it.  Measured on a
 * perspective quad: the walk was 0.00135 of a pixel out and the coordinate
 * 0.00088 of a texel below nought, and refusing that sent a whole triangle to
 * software.
 *
 * A quarter of a texel, which is two hundred and ninety times the excursion
 * that was measured and narrow enough that the whole of it can be swept and
 * compared against the software path rather than assumed.  Expressed against
 * the denominator so the same coordinate means the same thing at any scale:
 * a coordinate of -SPAN/256 is a numerator of -q/16.
 */
/*
 * One whole texture below nought.
 *
 * It was a quarter of a texel, which is all the edge walk needs: the walk's
 * integer x sits a fraction of a pixel outside the true edge and puts the
 * coordinate a thousandth of a texel under.  A whole texture is not for that
 * -- it is for ordinary tiling, glTexCoord2f(-1, 2), which the engine draws
 * exactly as GL says and which still falls back to software.
 *
 * The builder is NOT opened with it.  Until the engine's addend has been
 * measured for large negative numerators -- every measurement of the ladder
 * so far used positive ones -- the only thing that can reach down here is the
 * raw probe, so a wrong guess stays in an instrument instead of a picture.
 */
#define OSMGA_HW3D_TEX_NEG_ALLOW  OSMGA_HW3D_TEX_SPAN

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
 * How far a SLOPE may carry a coordinate across a trapezoid's own bounding
 * box.  This is not the coordinate policy above -- a box corner is not a
 * pixel, and what the pixels are allowed is judged separately and exactly:
 * at the four corners, at each drawn row's two ends, and at each empty
 * row's position.
 *
 * It was the policy limit, and that refused a great deal it had no cause
 * to.  A slope is a coordinate span over a screen extent, so the barycentric
 * solve dividing by a small area gives a sliver a steep one however small
 * its coordinates are; the box then swings past the policy at corners no
 * pixel occupies.  Measured over 19972 random built triangles, the policy
 * as a slope bound refused 6054 of them and the exact checks that follow
 * objected to two.
 *
 * Sixty-four repeats.  That takes the refusals to 548, keeps the slack on
 * the extrapolation to a box corner rather than on any coordinate a texel
 * is fetched at, and stays eight times nearer the magnification that was
 * actually measured on the card than a budget large enough to matter for
 * overflow would.
 */
#ifndef OSMGA_HW3D_TEX_SLOPE_ROOM
#define OSMGA_HW3D_TEX_SLOPE_ROOM (64UL * OSMGA_HW3D_TEX_SPAN)
#endif

/*
 * The row walk forms tu0 + slope*dx + slope*dy and then adds one more
 * slope*dx to reach the row's far end.  The anchor is held to
 * TEX_COORD_MAX before any of it, so the largest value it can build is
 * TEX_COORD_MAX + 3 * SLOPE_ROOM, and a long here is four bytes.  Checked
 * rather than asserted in a comment, because the budget is the thing most
 * likely to be raised by somebody who has not re-done this arithmetic.
 */
typedef int OSMGAHW3DSlopeRoomCheck[
    (OSMGA_HW3D_TEX_SLOPE_ROOM
       <= ((2147483647UL - OSMGA_HW3D_TEX_COORD_MAX) / 3UL)) ? 1 : -1];

/*
 * Here rather than beside the allowance itself, because the allowance is
 * written in terms of the span and the span is declared below it.
 *
 * The allowance has to divide the fixed point's one exactly, or the bound the
 * validator writes as a division of q stops being the same coordinate at
 * every scale.  A negative array size is the C89 way to fail the build.
 */
typedef int OSMGAHW3DNegAllowCheck[
    ((OSMGA_HW3D_TEX_NEG_ALLOW % (unsigned long)OSMGA_HW3D_Q_ONE) == 0UL)
        ? 1 : -1];

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
    /*
     * WHICH INCREMENT IS WHICH.  Everything that reads these must use these
     * four equations and not its own recollection:
     *
     *      u = tmr[6] + tmr[0] * dx + tmr[1] * dy
     *      v = tmr[7] + tmr[2] * dx + tmr[3] * dy
     *
     * so tmr[0] is ds/dx, tmr[1] ds/dy, tmr[2] dt/dx, tmr[3] dt/dy.
     *
     * It is written here because the two sides of this interface once
     * disagreed about it.  The builder had it right -- and said so with a
     * measurement, since the other arrangement disagreed with the software
     * rasteriser on 2157 pixels of 4410 and this one on none -- while the
     * validator had tmr[1] and tmr[2] the other way round and so bounded the
     * wrong slope for each axis.  It went unseen because almost every probe
     * leaves both at nought, where the two readings agree.
     *
     * The references disagree too, which is how the wrong reading survived a
     * check: xf86-video-mga's mga_exa.c makes TMR1 the second row's x
     * increment, while mga_storm.c's own comment calls it "sy inc".  The
     * machine settles it in favour of mga_storm.c.
     *
     * tmr[0..3] are the numerators' increments and tmr[4], tmr[5] the
     * denominator's, which are read only when the batch says perspective and
     * are nought otherwise.  All of them are bounded, and MAY BE NEGATIVE:
     * what is required is that the coordinate stays inside the measured range
     * at every pixel, which for a plane means at each of the four corners.
     * They were required non-negative once, which turned away roughly half of
     * all real texture mapping -- a triangle whose texture runs the other way
     * across the screen has a negative gradient and is in no way exotic.
     *
     * The STARTS are not here.  They belong to the trapezoid, not the batch,
     * and live in OSMGAHW3DTri as tu0, tv0 and tq0; the array shrank from
     * nine so that anything still reaching for the old index fails the build
     * rather than reading a gradient as an anchor. */
    long tmr[6];
    /*
     * A ladder rung the whole batch asks for, per axis.  Nought asks for
     * nothing; 1 through OSMGA_HW3D_TEX_BANDS mean that rung plus one.
     *
     * The bias belongs to the trapezoid and comes from its own reach, so two
     * neighbours can see the same coordinate through different biases and it
     * lands in two places.  On a sixteen-texel texture that window is a sixth
     * of a percent of a texel and nothing ever lands in it; on one of 2048
     * rows it is nearly a quarter, and twelve of sixty-four samples at an
     * ordinary tiling rate read a different row -- measured, probe section
     * 82.  That is a visible seam on a tiled floor.
     *
     * Asking for a rung lets a client put every primitive of a surface on the
     * same one, so they share a residual and the seam goes.  The kernel does
     * not trust the number: it takes the LARGER of the request and the
     * trapezoid's own, because a larger rung subtracts less and leaves a
     * bigger residual, and the residual is what must never go negative.  So
     * the client can buy continuity with accuracy and cannot sell safety.
     *
     * Nought means no request FOR A REASON.  The batch is a mapped buffer
     * that clients reuse and write field by field, so a field left alone
     * keeps whatever was in it; nought being the inert value means a stale
     * one cannot switch the policy on.
     */
    unsigned long texBiasReqU;
    unsigned long texBiasReqV;
    /*
     * A scissor box, HALF OPEN: x and y are its low corner and w and h its
     * size, so a width of nought is an empty box and not a malformed one.
     *
     * Half open because glScissor is, and because the alternative wrote the
     * plan into a contradiction: an inclusive box wants x0 <= x1, and
     * glScissor(0,0,0,0) is a legal call that must draw nothing, which as an
     * inclusive box is x1 = x0 - 1.
     *
     * scissorOn is nought for no scissor at all, which is what a client that
     * has never heard of one leaves behind -- the same reason the bias
     * request's inert value is nought.
     *
     * The kernel does NOT trust the box.  It draws the INTERSECTION of this
     * and the destination window it already clips to, so a client can only
     * ever narrow what it could already reach: containment does not rest on
     * these four numbers being sensible, and the validator's row and column
     * checks are unchanged and still measured against the whole window.
     */
    unsigned long scissorOn;
    long scissorX, scissorY;
    unsigned long scissorW, scissorH;
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
    /*
     * The texture anchors, which are this trapezoid's and not the batch's.
     *
     * The gradients stay in the state because they are the triangle's planes
     * and every trapezoid cut from it shares them; only the value AT the
     * anchor moves, because the anchor is the trapezoid's own first row and
     * that row's left edge.  Holding them here is what lets both trapezoids
     * of a split triangle go out in one batch, so that a refusal of the
     * second one draws neither.
     *
     * Ignored unless the primitive is textured.  tq0 is ignored again unless
     * the batch says perspective, and then it is the denominator's anchor and
     * is held to [Q_MIN, Q_MAX] rather than to the coordinate range.
     */
    long tu0, tv0, tq0;
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
 * ================= The WARP batch (version 10) =================
 *
 * A SECOND payload at the same address, not a change to the first.
 *
 * The trapezoid batch is 108 + 132 * 180 = 23,868 bytes in a 24 KiB
 * client-mapped region, which leaves 708 free -- and the 24 KiB itself is
 * the number that must not move, because the kernel maps whole 8 KiB pages
 * and a batch that did not end on a page boundary once left every
 * accelerated process with 4 KiB of the command list mapped read-write.
 * So nothing is appended to OSMGAHW3DBatch.  A client writes EITHER shape
 * into the buffer, and the kernel reads magic and version -- which sit at
 * the same offsets in both -- to know which one it is looking at.
 *
 * Version 9 keeps working untouched.  Nothing about the trapezoid layout
 * moves, so an existing library needs no rebuild to go on drawing.
 *
 * Every wire field is an explicit 32-bit type.  The batch has always been
 * an i386 layout in practice, but a contract should say so rather than
 * inherit it from a word size assertion.
 */
typedef unsigned int osmga_u32;

/*
 * One WARP vertex, 32 bytes, in the order the microcode consumes them.
 * All six coordinates are IEEE-754 single bit patterns: the kernel has no
 * FPU, so the client -- which does -- performs every conversion, and the
 * kernel judges the bits (see the osmgaHW3DF32* family).
 *
 * x and y are what the ENGINE is to be given, not what GL computed.  This
 * tier draws the picture a centre-sampling oracle draws for vertices half
 * a pixel higher, so a client that wants GL's own picture sends GL's
 * coordinates less a half (mesa/OpenStepMGAMesaWarp.h explains the
 * measurement, and the reference's SUBPIXEL_X = -0.5 agrees).  The kernel
 * does not apply it and could not -- it has no FPU -- and a harness that
 * supplies its own vertices to ask what the ENGINE does with them wants
 * no bias at all.  Stating it here is what keeps those two uses from
 * being confused for one.
 *
 * z is normalised to [0,1].  Measured: the engine multiplies by 65536 and
 * saturates, so a Mesa window depth code becomes code / 65536.0 and the
 * round trip is exact for every code.  NOT the reference DRI's 1/65535.
 *
 * rhw is qw * tq and tu0/tv0 are s/tq and t/tq -- the texture's own
 * denominator folded into the vertex weight, which is what the reference
 * does (mgavb.c) and what the vertex form has room for.
 *
 * diffuse is a packed byte per channel and there is nowhere in it to put
 * half a level.  That is not a detail: the drawing engine DISCARDS the low
 * fifteen bits of an interpolated colour rather than rounding them (see
 * osmgaStartFixed in mesa/OpenStepMGAMesaTriangle.c, which adds half a
 * level to its start to compensate), and a WARP start comes out of the
 * microcode with no such compensation.  Measured: this tier's colour is
 * nought or one level BELOW the trapezoid tier's, never more, constant
 * across the triangle, and exactly equal on a channel whose plane takes
 * integer values at integer points -- which is where a truncation has
 * nothing to discard.  It is not correctable through this struct.
 */
typedef struct {
    osmga_u32 x, y;
    osmga_u32 z;
    osmga_u32 rhw;
    osmga_u32 diffuse;      /* BGRA, packed */
    osmga_u32 specular;     /* BGR + fog, packed */
    osmga_u32 tu0, tv0;
} OSMGAHW3DVertex;

/*
 * A maximal stretch of primitives that share their engine state.
 *
 * The trapezoid contract carries dwgctl and alphactrl PER TRIANGLE,
 * because Mesa varies them between primitives.  WARP cannot: everything in
 * one submission shares the state, so the batch is cut into runs and a new
 * GENERAL state list goes out at each boundary.
 *
 * That is affordable because it was measured.  Over 109,803 trapezoids
 * dwgctl changed on 1.8% and the alpha block on 1.8%
 * (REMAINING_WORK.md), so a compatible run averages between 28 primitives
 * (if the two are independent) and 55 (if they move together).  A 240
 * triangle batch therefore needs at most nine runs, and sixteen is not the
 * binding constraint on anything.
 *
 * Nothing else varies within a batch: OSMGAHW3DState is singular, so the
 * destination, the depth buffer, the scissor and the whole texture state
 * are fixed for its lifetime by construction.
 */
typedef struct {
    osmga_u32 dwgctl;
    osmga_u32 alphactrl;
    osmga_u32 first;        /* index into vtx[] */
    osmga_u32 count;        /* vertices, a multiple of three */
} OSMGAHW3DRun;

#define OSMGA_HW3D_VERSION_WARP  10UL
#define OSMGA_HW3D_MAX_RUN       16UL
#define OSMGA_HW3D_MAX_VTX      720UL   /* 240 triangles */

/*
 * The header is declared identically to OSMGAHW3DBatch's, so magic,
 * version and triCount and the state block sit at the same offsets in
 * both and the kernel can read the version before it knows which shape it
 * has.  The host suite asserts that with offsetof; the kernel header
 * cannot, so it is stated here and checked there.
 *
 * triCount is nought in a WARP batch.  Both counts nonzero is refused
 * rather than resolved: a client that filled in both did not know what it
 * was sending.
 */
typedef struct {
    unsigned long magic;
    unsigned long version;
    unsigned long triCount;
    OSMGAHW3DState state;
    osmga_u32 runCount;
    osmga_u32 vtxCount;
    OSMGAHW3DRun    run[OSMGA_HW3D_MAX_RUN];
    OSMGAHW3DVertex vtx[OSMGA_HW3D_MAX_VTX];
} OSMGAHW3DWarpBatch;

/*
 * Verdicts, appended and never renumbered.
 */
#define OSMGA_HW3D_E_VTXCOUNT   25  /* vtxCount, runCount, or a run's span */
#define OSMGA_HW3D_E_VTXFLOAT   26  /* a vertex word that is not a number,
                                     * not positive, or out of range */
#define OSMGA_HW3D_E_WARPMIX    27  /* both payload counts are nonzero */
#define OSMGA_HW3D_E_WARPPOLICY 28  /* a state the WARP tier has not been
                                     * qualified for -- it declines DOWN to
                                     * the trapezoid path, which can draw it */

/*
 * Judge a WARP batch.  Structure, then every vertex word, then the tier's
 * admission policy.  Returns OSMGA_HW3D_OK or a verdict; `badRun` names
 * the run when the verdict names one.
 */
int osmgaHW3DValidateWarp(const OSMGAHW3DWarpBatch *b,
                          const OSMGAHW3DLimits *lim,
                          unsigned long *badRun);

/*
 * What the WARP tier currently admits, alone in one function because it is
 * the part that MOVES as bands qualify.  Everything it refuses is drawn by
 * the trapezoid path instead, so a refusal costs speed and never a
 * picture.
 */
int osmgaHW3DWarpAdmits(const OSMGAHW3DState *st, const OSMGAHW3DRun *run);

/*
 * One primitive's opcode, access type and alpha control.  Both contracts
 * hand these to the same registers -- version 9 per trapezoid, version 10
 * per run -- so the rule lives in one place.
 */
int osmgaHW3DValidatePrimState(unsigned long dwgctl, unsigned long alphactrl);

/*
 * The batch is a shared layout, so a change that made it outgrow its half of
 * the ring, or that changed the word size, has to fail the build rather than
 * be discovered at run time.  A negative array size is the C89 way to say so.
 */
typedef int OSMGAHW3DFitsCheck[
    (sizeof(OSMGAHW3DBatch) <= OSMGA_HW3D_BATCH_BYTES) ? 1 : -1];
typedef int OSMGAHW3DWordCheck[(sizeof(unsigned long) == 4) ? 1 : -1];
/*
 * And the WARP payload, which shares the buffer rather than extending it.
 * It is SMALLER than the trapezoid batch (23,412 against 23,868), so the
 * page split does not move -- but that is a fact about today's maxima and
 * exactly the kind of fact that stops being true in an edit.
 */
typedef int OSMGAHW3DWarpFitsCheck[
    (sizeof(OSMGAHW3DWarpBatch) <= OSMGA_HW3D_BATCH_BYTES) ? 1 : -1];
typedef int OSMGAHW3DWarpVtxCheck[
    ((OSMGA_HW3D_MAX_VTX % 3UL) == 0UL) ? 1 : -1];
typedef int OSMGAHW3DVertexSizeCheck[
    (sizeof(OSMGAHW3DVertex) == 32) ? 1 : -1];
typedef int OSMGAHW3DRunSizeCheck[(sizeof(OSMGAHW3DRun) == 16) ? 1 : -1];

/*
 * And the OTHER half of the ring, which the batch's own size says nothing
 * about.  The encoder writes fixed-size blocks: a few for the state, a run
 * per primitive, and a short tail.  A batch that fits in its half can still
 * encode into more command list than there is, and the failure would be a
 * refused submission at the worst moment rather than a build error.
 *
 * The per-primitive count rose by one when the anchors moved here -- the
 * matrix is now written inside the loop.  Counted from the encoder, with
 * room to spare rather than exactly.
 */
#define OSMGA_HW3D_ENC_BLOCK_DW   5UL   /* index dword + four values */
#define OSMGA_HW3D_ENC_STATE_BLK  8UL   /* before the loop; 6 today */
/*
 * Nine, not eight.  Counted from the encoder rather than from memory: a
 * primitive emits seven blocks unconditionally, one more when it is
 * textured, and one to start it -- and the eight that stood here was short
 * by exactly that textured one.  The bound it produced was therefore an
 * UNDER-estimate, which is the one direction a bound must not be wrong in:
 * it would have let the list be sized too small and the encoder would have
 * refused whole batches at the far end.  It never did, because the list
 * happened to be large enough anyway.
 */
#define OSMGA_HW3D_ENC_TRI_BLK    9UL   /* per primitive; 8 unconditional + tex */
#define OSMGA_HW3D_ENC_TAIL_BLK   4UL   /* after it; 3 today */
#define OSMGA_HW3D_ENC_DWORDS \
    (((OSMGA_HW3D_ENC_STATE_BLK + OSMGA_HW3D_ENC_TAIL_BLK) + \
      OSMGA_HW3D_MAX_TRI * OSMGA_HW3D_ENC_TRI_BLK) * OSMGA_HW3D_ENC_BLOCK_DW)
/*
 * 64 KiB is the whole block; what is left after the batch is the list.  The
 * literal is here rather than shared with the driver's OSMGA_DMA_RING_BYTES
 * because this header is compiled by clients that have no business knowing
 * how the kernel allocates -- but the two must agree, and the driver checks
 * at init that they do.
 */
/*
 * The tail of the ring is no longer all list.
 *
 * A secondary DMA payload has to live somewhere the card owns and the
 * client cannot map, and the only such place is this same region.  Parking
 * it in "the space left over" reserves nothing: the encoder is handed the
 * whole region, and how much of it a batch uses is a function of how many
 * triangles the client sent.  So the region is split, and the list's bound
 * is stated against its own half.
 *
 * The numbers, since guessing them is how the last attempt went wrong:
 *
 *   region                40960 B
 *   encoder worst case    32640 B   ((8 + 4) + 180 * 9) * 5 dwords
 *   secondary region       4080 B   204 general packets exactly
 *   list gets             36880 B   leaving 4240 B of margin
 *
 * About a page rather than the whole 8320 B of slack, so the list keeps
 * real headroom instead of exactly none.
 */
/*
 * A whole number of general-mode packets, not a round number of bytes.
 * 4096 would leave sixteen bytes at the end that are neither a packet nor
 * anything else, and the point of this region is that every byte of it is
 * a complete DMAPAD packet -- a partial one at the end is exactly what the
 * guard exists to prevent being read.
 *
 *   4080 = 204 packets * 20 bytes, remainder 0
 */
#define OSMGA_HW3D_SEC_BYTES    4080UL
#define OSMGA_HW3D_LIST_BYTES \
    ((64UL * 1024UL - OSMGA_HW3D_RING_OFFSET) - OSMGA_HW3D_SEC_BYTES)
/* Offset from the ring base, not from RING_OFFSET. */
#define OSMGA_HW3D_SEC_OFF \
    (OSMGA_HW3D_RING_OFFSET + OSMGA_HW3D_LIST_BYTES)

/*
 * ---- how a WARP batch lays out the SAME list region ----
 *
 * The trapezoid path fills the list region with one command list.  A WARP
 * batch needs two different things in it: a state list per run, and the
 * vertices themselves -- which cannot be DMA'd out of the client-mapped
 * batch, because after validation and before the card reads, the client
 * can still write.  The encoder already copies for that reason and the
 * vertices are copied for the same one.
 *
 * The state lists come first and the vertices after them, at a FIXED
 * offset rather than after however many lists this batch happened to
 * need: a base that moved with the run count would make PRIMADDRESS
 * arithmetic depend on a number the client chose, and the whole point of
 * copying is that the card's addresses do not.
 *
 * The per-run figure is the state list the driver builds -- pipe,
 * context, texture, global, clip and the trap.  Counted from the builder
 * it is twenty-two blocks of twenty bytes, so 440; the allowance is 512
 * because a list that outgrew its slot would be a refusal at submission
 * time, and three spare blocks cost 1,152 bytes of a region with six
 * thousand to spare.
 */
#define OSMGA_HW3D_WARP_STATE_BYTES  512UL
#define OSMGA_HW3D_WARP_VTX_OFF \
    (OSMGA_HW3D_MAX_RUN * OSMGA_HW3D_WARP_STATE_BYTES)
#define OSMGA_HW3D_WARP_VTX_BYTES \
    (OSMGA_HW3D_MAX_VTX * 32UL)

/*
 * Both halves have to fit the list region, and neither number is free to
 * drift: the maxima come from the batch's own budget and the state list's
 * length comes from the builder.  A negative array size is the C89 way of
 * saying so at build time rather than at the far end of a submission.
 */
typedef int OSMGAHW3DWarpListCheck[
    ((OSMGA_HW3D_WARP_VTX_OFF + OSMGA_HW3D_WARP_VTX_BYTES) <=
     OSMGA_HW3D_LIST_BYTES) ? 1 : -1];
/* And the vertex base is 32-byte aligned, so PRIMADDRESS needs no mask. */
typedef int OSMGAHW3DWarpVtxAlign[
    ((OSMGA_HW3D_WARP_VTX_OFF % 32UL) == 0UL) ? 1 : -1];

/* One general-mode packet: an index dword and four values. */
#define OSMGA_HW3D_SEC_PACKET   20UL

/*
 * Is [secStart, secEnd) a legal secondary DMA range?
 *
 * The values are ADDRESSES.  SECADDRESS keeps secmod in bits 1:0 and SECEND
 * keeps SAGPXFER in bit 1, so the register words are built by OR-ing the
 * mode in AFTER this returns.  Validating the raw words instead would
 * either reject every legal submission or invite somebody to "fix" it by
 * masking after the check, which is the same thing as not checking.
 *
 * Returns 1 when legal.  Portable; the host tests link it.
 */
int osmgaHW3DSecRange(unsigned long ringPhys,
                      unsigned long secStart, unsigned long secEnd);

/*
 * ---- What an unsigned batch field carries, and how it reaches a register
 *
 * Several registers keep their value in less than thirty-two bits and
 * reserve the rest, and the spec is explicit about the representation:
 * ar2 is "a 22-bit signed value in two's complement notation" (3-40), dr4
 * and the alpha increments hold "a signed 9.15 value in two's complement
 * notation" (3-121, 3-37), and each says of the bits above that they "must
 * be set to '0'".
 *
 *   AR0 AR2 AR4 AR5 AR6              signed <21:0>
 *   AR1                              signed <23:0>
 *   DR4 6 7 8 10 11 12 14 15         signed <23:0>   (9.15)
 *   ALPHASTART ALPHAXINC ALPHAYINC   signed <23:0>   (9.15)
 *   DR0 DR2 DR3                      signed <31:0>   -- no reserved field
 *
 * THE CARRIER.  dr[], a0, adx and ady are `unsigned long`, so a word like
 * 0x00ffffff is ambiguous on its face: the raw 24-bit encoding of -1, or
 * the number 16,777,215, which does not fit a signed 24-bit field.  The
 * producer settles it -- osmgaFixed returns (unsigned long)(long) -- so:
 *
 *   an unsigned batch field carries a SIGN-EXTENDED signed value.
 *
 * Read it back as (long), and 0x00ffffff is +16,777,215 and is refused.
 * Saying this out loud is not pedantry: without it, "does it fit" has no
 * meaning and neither does any test of it.
 *
 * WHY BOTH HALVES.  Fitting the field and having the reserved bits clear
 * are different properties -- a negative value fits and still has every
 * high bit set once it is widened to thirty-two.  So one helper answers
 * both, and the validator and the encoder call the same one rather than
 * keeping two copies of the widths that are free to drift apart.
 *
 * Returns 1 when v is representable in `bits` as two's complement, and
 * then stores the canonical register word -- v masked to the field.
 */
int osmgaHW3DField(long v, unsigned bits, unsigned long *out);

#define OSMGA_HW3D_F_AR     22U   /* AR0 AR2 AR4 AR5 AR6 */
#define OSMGA_HW3D_F_AR1    24U
#define OSMGA_HW3D_F_DR     24U   /* the nine emitted DRs, 9.15 */
#define OSMGA_HW3D_F_ALPHA  24U

typedef int OSMGAHW3DListCheck[
    ((OSMGA_HW3D_ENC_DWORDS * 4UL) <= OSMGA_HW3D_LIST_BYTES) ? 1 : -1];
/* And the split itself has to land inside the ring. */
typedef int OSMGAHW3DSplitCheck[
    ((OSMGA_HW3D_SEC_OFF + OSMGA_HW3D_SEC_BYTES) == 64UL * 1024UL) ? 1 : -1];

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
 * The operator would rather the WARP setup engine drew the triangles.
 *
 * A PREFERENCE, and named as one.  The engine's WARP path does not appear or
 * disappear with this bit -- the kernel takes either kind of batch whatever
 * it says -- so it is deliberately not called CAP_WARP, which would read as
 * "this card cannot do WARP" when the checkbox is off.  It is not in
 * CAP_REQUIRED for the same reason: nothing about acceleration depends on it.
 *
 * An old library ignores the bit and behaves as it always did; a new library
 * against an old kernel reads nought, which is the safe answer because
 * nought is the setting WARP already ships with.
 */
#define OSMGA_HW3D_CAP_WARP_PREFERRED 0x00000010UL

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
 * Presenting a drawn surface ON THE SCREEN, by the engine, without the
 * picture ever crossing the bus.
 *
 * This is the one deliberate opening of the visible framebuffer to a client,
 * and it exists because the alternative was measured to be structurally
 * hopeless: delivering a frame to system memory costs 229 ms at 640x480
 * (5.49 MB/s uncached reads), which is fifteen times what software takes to
 * DRAW the same frame.  A VRAM-to-VRAM engine blit moves the same frame in
 * a few milliseconds and touches system memory not at all.
 *
 * The client says where its picture is -- origin and stride, the same two
 * numbers its batches already declare -- and where on the screen it wants
 * the rectangle.  The kernel checks BOTH ends: the source must lie entirely
 * inside the offscreen window (checked in the division form, so nothing can
 * overflow first), the destination entirely inside the visible mode.  Source
 * and destination cannot overlap by construction: the window begins a guard
 * above the visible surface, and both containments are re-checked here
 * rather than assumed.
 *
 * What this deliberately does NOT decide: whose rectangle of the screen it
 * is.  There is no window-ownership authority to consult -- the window
 * server does not arbitrate for us (measured; it never sends blits) -- so
 * any client of this device may paint any screen rectangle, and the same
 * shared-surface caveats recorded for the batch path apply.  Tearing is also
 * accepted: the blit is not synchronised to scanout.
 *
 * The blit itself is the EXA shape, verified against mga_exa.c: PITCH is the
 * DESTINATION's pitch, AR5 the SOURCE's own stride (they are separate
 * registers, and X.Org copies between differently-pitched pixmaps with
 * exactly this encoding), SRCORG the source origin, DSTORG the screen.
 */
#define OSMGA_HW3D_PRESENT_MAGIC 0x4d474150UL   /* 'MGAP' */

typedef struct {
    unsigned long magic;     /* OSMGA_HW3D_PRESENT_MAGIC */
    unsigned long srcOrg;    /* byte origin of the surface, in the window */
    unsigned long srcStride; /* the surface's row stride, in PIXELS */
    unsigned long srcX, srcY;/* top-left of the rectangle, in the surface */
    unsigned long w, h;      /* rectangle size in pixels; both > 0 */
    unsigned long dstX, dstY;/* top-left on the visible screen */
    /* out */
    unsigned long status;    /* 0 presented; else an errno-style reason */
    unsigned long verdict;   /* OSMGA_PRESENT_* saying which check refused */
} OSMGAHW3DPresentBlock;

#define OSMGA_PRESENT_OK        0UL
#define OSMGA_PRESENT_E_MAGIC   1UL   /* wrong magic (or size skew) */
#define OSMGA_PRESENT_E_SRC     2UL   /* source rect leaves the window */
#define OSMGA_PRESENT_E_DST     3UL   /* dest rect leaves the visible mode */
#define OSMGA_PRESENT_E_GEOM    4UL   /* zero size, or a packed field would
                                       * not hold the value */
#define OSMGA_PRESENT_E_BUSY    5UL   /* engine busy; try again */
#define OSMGA_PRESENT_E_LATCH   6UL   /* acceleration disabled permanently */
#define OSMGA_PRESENT_E_MODE    7UL   /* mode changed under the request, or
                                       * no window is offered */

/*
 * MEASUREMENT ONLY.  Same block, same batch, same validation -- but the
 * driver returns after encoding instead of ringing the doorbell.  It exists
 * to separate the kernel's per-trapezoid work from the engine's, which no
 * sweep from the outside can do because both scale with the same count.
 *
 * A NEW COMMAND rather than a field in the batch, deliberately: no struct
 * moves, so OSMGA_HW3D_VERSION does not change and the probe's exact-match
 * gate does not fire.  An older driver simply refuses the command.
 */
/*
 * Gated with the driver's handler.  A shipped driver does not answer this
 * command, so a client that could still name the number would only be able to
 * fail; the number stays reserved either way, since removing it would let a
 * later command reuse 4 and mean something else to an old client.
 */
#ifdef OSMGA_HW3D_SUBMIT_DRY
#define OSMGA_IOC_SUBMIT_DRY ((unsigned long)(OSMGA_IOC_OUT_BIT | \
    ((sizeof(OSMGAHW3DSubmitBlock) & OSMGA_IOC_PARM_MASK) << 16) | \
    ((unsigned long)OSMGA_IOC_GROUP << 8) | 4UL))
#endif /* OSMGA_HW3D_SUBMIT_DRY */

#define OSMGA_IOC_PRESENT   ((unsigned long)(OSMGA_IOC_OUT_BIT | \
    OSMGA_IOC_IN_BIT | \
    ((sizeof(OSMGAHW3DPresentBlock) & OSMGA_IOC_PARM_MASK) << 16) | \
    ((unsigned long)OSMGA_IOC_GROUP << 8) | 3UL))

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

/*
 * How far each texture coordinate actually reaches in a batch.
 *
 * The engine's addend depends on which band the coordinate is in, and the
 * encoder has to take off the SMALLEST addend the batch can meet or a
 * coordinate sitting on a texel boundary is pushed into the texel below.
 * Below 2^20 that smallest is 496 and always has been; above it the ladder
 * carries on -- 480, 448, 384, measured in docs/M1_4D5_LADDER_ABOVE_2E20.md --
 * so the encoder needs to know how high this batch goes.
 *
 * Only the maximum is carried: the addend does not rise again, so the
 * smallest one the batch can meet is the one its largest coordinate implies.
 */
typedef struct {
    long uMax;      /* the largest |numerator| this batch's pixels reach */
    long vMax;
} OSMGAHW3DTexReach;

/*
 * What the encoder must subtract for a coordinate that reaches this far.
 *
 * 496 for anything at or below 2^20, which is every batch the driver drew
 * before this was measured, so those are unchanged.  The band edges are the
 * measured ones: exactly 2^20 still gets 496, and the addend drops within the
 * 512 units above each power of two (probe sections 60 and 61).
 */
long osmgaHW3DTexBiasFor(long maxCoord);

/*
 * The reach of ONE trapezoid, as the rung of the ladder its bias sits on.
 *
 * The batch-wide reach above decides one bias for every anchor the encoder
 * writes, and that was right while the anchors were the batch's.  They are
 * the trapezoid's now, and a batch holding a near trapezoid beside a far one
 * takes the far one's bias for both -- which leaves the near one's residual
 * larger than it was alone and can move it up a texel.  Measured: a
 * coordinate twenty units below a texel boundary reads column 7 submitted
 * alone and column 8 submitted beside a trapezoid two bands further out
 * (probe section 80).
 *
 * A rung rather than the reach itself, because there are six of them and the
 * encoder wants no more than that: two bytes a trapezoid, four hundred at the
 * cap, against sixteen hundred for the reaches.  osmgaHW3DTexBiasOfBand turns
 * one back into what the encoder subtracts.
 */
typedef struct {
    unsigned char u;
    unsigned char v;
} OSMGAHW3DTexBand;

#define OSMGA_HW3D_TEX_BANDS 6      /* 496, 480, 448, 384, 256, 0 */
#define OSMGA_HW3D_TEX_BIAS_NONE 0UL   /* the inert request */

/*
 * The largest rung that keeps a trapezoid's farthest coordinate inside the
 * range the coordinate check admits.
 *
 * At a trapezoid's own rung the addend and the bias at that coordinate are
 * the same ladder value, so the residual there is exactly nought and the
 * effective coordinate is the reach itself.  A higher rung subtracts less and
 * the residual stops being nought -- a reach of 2^23 asked to sit on rung
 * four lands at 8388736, past COORD_MAX.  This is what stops that.
 */
unsigned char osmgaHW3DTexBandHeadroom(long reach);

long osmgaHW3DTexBiasOfBand(unsigned char band);
unsigned char osmgaHW3DTexBandFor(long maxCoord);

/*
 * The validator, with the reach handed back.  osmgaHW3DValidate is this with
 * nowhere to put it -- the tests and the self-checks do not need it and are
 * left alone.
 */
/*
 * bands, when it is not nought, is an array of OSMGA_HW3D_MAX_TRI that
 * receives each trapezoid's own rung.  It belongs to the caller and must
 * outlive the encode that reads it; the kernel keeps one beside the batch
 * snapshot rather than on its stack.  Entries for untextured or undrawn
 * primitives are left at the widest rung, which is what a bias of 496 means
 * and what an anchor of nought wants.
 */
int osmgaHW3DValidateReach(const OSMGAHW3DBatch *b, const OSMGAHW3DLimits *lim,
                           unsigned long *badTri, OSMGAHW3DTexReach *reach,
                           OSMGAHW3DTexBand *bands);

/*
 * The same, and it also says WHICH check refused.  Zero means no texture
 * check spoke -- the verdict came from somewhere else, or there was none.
 * The numbers are the file's own and are listed beside the macro that
 * writes them.
 */
int osmgaHW3DValidateReachSite(const OSMGAHW3DBatch *b,
                               const OSMGAHW3DLimits *lim,
                               unsigned long *badTri,
                               OSMGAHW3DTexReach *reach,
                               OSMGAHW3DTexBand *bands,
                               unsigned long *texSite);

int osmgaHW3DValidate(const OSMGAHW3DBatch *b, const OSMGAHW3DLimits *lim,
                      unsigned long *badTri);

#endif /* OPENSTEP_MGA_HW3D_H */

/*
 * IEEE-754 singles as bit patterns, judged without an FPU -- see the block
 * comment in the .c.  The WARP vertex form carries nothing else, and the
 * kernel has to bound all of it before the card reads any of it.
 *
 * The bounds are stated as bit patterns so that a caller names a float and
 * this code never converts one:
 */
#define OSMGA_HW3D_F32_ONE      0x3F800000UL   /*   1.0f */
#define OSMGA_HW3D_F32_COORD    0x46000000UL   /* 8192.0f -- the same bound
                                                * the Mesa builder already
                                                * enforces on vertices */
#define OSMGA_HW3D_F32_RHW_MIN  0x3E000000UL   /*   0.125f = Q_MIN / 65536 */
#define OSMGA_HW3D_F32_RHW_MAX  0x43000000UL   /* 128.0f   = Q_MAX / 65536 */

int osmgaHW3DF32Finite(unsigned long p);
int osmgaHW3DF32PosNormal(unsigned long p);
int osmgaHW3DF32InUnit(unsigned long p);
int osmgaHW3DF32AbsAtMost(unsigned long p, unsigned long limit);
int osmgaHW3DF32Between(unsigned long p, unsigned long lo, unsigned long hi);

/*
 * Which axes must CLAMP rather than wrap.
 *
 * The rule is not "what the client asked for": repeat is granted only when
 * the map can safely be wrapped, which needs a power-of-two dimension so
 * the reduction is a mask, and a pitch equal to the width, since a masked
 * index into a padded surface addresses the wrong row.  Anything else stays
 * clamped whatever the client said.
 *
 * It lives here, in abstract flags rather than TEXCTL bits, because two
 * encoders now need the SAME policy: the trapezoid path and the WARP path
 * write the texture dimensions in different formats but must agree about
 * wrapping, and a policy written twice is a policy that drifts.  Putting it
 * here also makes it testable on the host, which it was not when it was
 * ten lines inside the encoder.
 */
#define OSMGA_HW3D_CLAMP_U  0x1UL
#define OSMGA_HW3D_CLAMP_V  0x2UL

unsigned long osmgaHW3DTexClampAxes(const OSMGAHW3DState *st);

/* A power of two, and not nought.  It used to be a static in the driver;
 * the wrap policy needs it and the policy lives here now. */
int osmgaHW3DIsPow2(unsigned long n);

/*
 * The clip box: the destination window narrowed by whatever scissor the
 * client asked for, as an INTERSECTION -- so a client can only ever reduce
 * what it could already reach, and containment does not rest on these four
 * numbers being sensible.
 *
 * Integer only.  The driver computed this in `double` (four locals in the
 * submit path), which contradicts the rule the same file states at :8367,
 * "Kernel code must not touch the FPU".  It was the only floating point in
 * the driver.  The replacement is proved equivalent to it on the host,
 * where doubles are allowed, across the whole input space that matters.
 *
 * Returns 1 with the half-open box in *x0,*x1,*y0,*y1 (x1 and y1
 * EXCLUSIVE), or 0 when the intersection is empty and nothing should be
 * drawn at all.
 */
int osmgaHW3DClipBox(unsigned long scissorOn,
                     long sx, long sy, unsigned long sw, unsigned long sh,
                     unsigned long dstW, unsigned long dstH,
                     unsigned long *x0, unsigned long *x1,
                     unsigned long *y0, unsigned long *y1);

/*
 * TEXFILTER, derived from the client's flags.
 *
 * The register's own layout: magfilter is bits 7:4 and minfilter 3:0, both
 * naming NRST as 0 and BILIN as 2; filteralpha is bit 20 and fthres bits
 * 28:21, an unsigned 4.4 holding the SQUARE of the step at which the
 * engine changes from magnifying to minifying, so 0x10 is a step of one.
 *
 * Two fields, and only the magnification one was ever written -- so a
 * texture drawn smaller than itself was point sampled however GL had asked
 * for it to be filtered.  The diagnostic minification selector wins that
 * field when it is set, and the validator has already held it to the four
 * named mipmap modes.
 *
 * Here rather than inside an encoder for the reason the wrap policy is:
 * the WARP path needs the same answer, and a derivation written twice
 * drifts.  Pure integer arithmetic, so the host tests it.
 */
unsigned long osmgaHW3DTexFilter(unsigned long texFlags);

/*
 * TDUALSTAGE0, and TDUALSTAGE1 takes the SAME value.
 *
 * They are not two serial stages: they are the even and odd screen
 * columns.  Setting the alpha selector in stage nought alone fixed exactly
 * half the pixels, measured as a row reading `20 ab 28 ab 30 ab 38 ab`
 * where 0xab was the texture's own alpha -- so both lanes take this one
 * word, and a caller that writes it to one of them has written a stripe.
 *
 * Selector nought is ARG1, the texture's alpha; ARG2 with the operand
 * fields left at zero is the interpolated one.  Which of them GL wants
 * depends on whether the texture has an alpha at all, which the client
 * says through OSMGA_HW3D_TEXF_TEXALPHA.
 */
unsigned long osmgaHW3DTexDualStage(unsigned long texFlags, int textured);

/* The register fields these two produce, named here because both encoders
 * write them and neither owns them. */
#define OSMGA_HW3D_TDS_COLOR_MUL   0x00600000UL
#define OSMGA_HW3D_TDS_ALPHA_MUL   0xC0000000UL
#define OSMGA_HW3D_TDS_ALPHA_ARG2  0x40000000UL
#define OSMGA_HW3D_TEXFILTER_ALPHA 0x00100000UL
#define OSMGA_HW3D_TEXFILTER_FTHRES1 (0x10UL << 21)   /* a step of one */
#define OSMGA_HW3D_TEXFILTER_MAGBILIN 0x20UL
#define OSMGA_HW3D_TEXFILTER_MINBILIN 0x02UL

/*
 * Does the destination the batch declared fit the window the driver owns?
 *
 * The last byte the engine can touch is
 *
 *      dstorg + (h-1) * pitch * 4 + w * 4 - 1
 *
 * and this compares it WITHOUT forming the product, because a height a
 * caller may legitimately ask for overflows a 32-bit multiply long before
 * it stops being plausible -- and a check that overflows is not a check.
 *
 * It lives here because both contracts declare the same destination and
 * program the same registers from it.  The version 9 submit path had it
 * inline; drafting the version 10 path skipped it, which would have
 * admitted a batch whose declared destination runs past the window --
 * the containment argument leaving by the back door behind a well formed
 * vertex array.
 *
 * Returns one of OSMGA_HW3D_OK, E_DSTSIZE, E_DSTORG.
 */
int osmgaHW3DDestFits(unsigned long dstorg, unsigned long w,
                      unsigned long h, unsigned long pitch,
                      unsigned long winStart, unsigned long winEnd);
