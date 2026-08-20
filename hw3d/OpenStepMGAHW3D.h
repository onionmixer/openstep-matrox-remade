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
#define OSMGA_HW3D_VERSION      1UL

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

typedef struct {
    unsigned long dstorg;          /* colour origin, byte offset into VRAM */
    unsigned long zorg;            /* depth origin; ignored unless depth is on */
    unsigned long texorg;          /* texture origin; ignored unless textured */
    unsigned long texW, texH;      /* texels; need not be powers of two */
    unsigned long texPitch;        /* texels per row, >= texW */
    unsigned long texFormat;       /* OSMGA_HW3D_TEXFMT_* */
    unsigned long texFlags;        /* OSMGA_HW3D_TEXF_* */
    /* tmr[0..3] are the increments, tmr[6] and tmr[7] the starts; all six
     * are bounded.  tmr[4], tmr[5] and tmr[8] are the H family and are
     * IGNORED -- the kernel writes them, see the note above. */
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

int osmgaHW3DValidate(const OSMGAHW3DBatch *b, const OSMGAHW3DLimits *lim,
                      unsigned long *badTri);

#endif /* OPENSTEP_MGA_HW3D_H */
