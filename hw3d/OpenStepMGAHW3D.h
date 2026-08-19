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
#define OSMGA_HW3D_MAX_TRI      250UL

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

typedef struct {
    unsigned long dstorg;          /* colour origin, byte offset into VRAM */
    unsigned long zorg;            /* depth origin; ignored unless depth is on */
    unsigned long texorg;          /* texture origin; ignored unless textured */
    unsigned long dwgctl;          /* opcode and atype, checked */
    unsigned long alphactrl;
    unsigned long texctl;
    unsigned long texctl2;
    unsigned long texfilter;
    unsigned long tmr[9];
} OSMGAHW3DState;

typedef struct {
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
    unsigned long texMaxBytes;     /* largest texture we will address */
    unsigned long batchBytes;
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
