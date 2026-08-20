/*
 * test-hw3d-validate.c -- host test for the batch validator.
 *
 * Runs where a mistake costs nothing, before the same code goes into the
 * kernel.  Every bound is exercised on BOTH sides: the last accepted value
 * and the first refused one.  Checking only that good input passes would
 * have missed every off-by-one, and this project has already had one
 * verdict that only looked at the case it expected to fail.
 *
 * Build 32-bit: the shared struct uses unsigned long, which is four bytes on
 * the OPENSTEP target and eight on a modern host, and the buffer-capacity
 * check is computed from sizeof.  A 64-bit build measures a different struct
 * and rejects counts the target would accept.
 *
 *   cc -m32 -Wall -o test-hw3d-validate test-hw3d-validate.c OpenStepMGAHW3D.c
 */
#include <stdio.h>
#include <string.h>
#include "OpenStepMGAHW3D.h"

static OSMGAHW3DLimits lim;
static OSMGAHW3DBatch b;
static int failures;

static void
reset(void)
{
    memset(&b, 0, sizeof b);
    b.magic = OSMGA_HW3D_MAGIC;
    b.version = OSMGA_HW3D_VERSION;
    b.triCount = 1;
    b.state.dstorg = lim.colourStart;
    b.state.zorg = lim.depthStart;
    b.state.texorg = lim.texStart;
    b.state.texW = 64; b.state.texH = 64; b.state.texPitch = 64;
    b.state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
    b.state.tmr[0] = 0x4000; b.state.tmr[3] = 0x4000;   /* identity, 64 wide */
    b.state.texW = 64; b.state.texH = 64; b.state.texPitch = 64;
    b.state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
    b.state.tmr[0] = 0x4000; b.state.tmr[3] = 0x4000;   /* identity, 64 wide */
    b.tri[0].dwgctl = 0x0004UL | 0x0070UL;  /* TRAP | atype I, masked form */
    b.tri[0].alphactrl = 0x0101UL;          /* ALPHACHANNEL | SRC_ONE */
    b.tri[0].y = 0;
    b.tri[0].h = 1;
    b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h;
    b.tri[0].fxbndry = (64UL << 16) | 0UL;
}

static void
expect(const char *what, int want)
{
    unsigned long badTri = 0;
    int got = osmgaHW3DValidate(&b, &lim, &badTri);

    if (got != want) {
        printf("  FAIL  %-46s got %d, wanted %d\n", what, got, want);
        failures++;
    } else {
        printf("  ok    %-46s %d\n", what, got);
    }
}

int
main(void)
{
    unsigned long rows, pitch;

    /* The same geometry the driver reports: 1024x768x4, offscreen colour at
     * 4 MiB, depth at 5 MiB, texture at 6 MiB, proven VRAM bound 7 MiB. */
    lim.pitchBytes = 4096;
    lim.clipY1 = 63;
    lim.clipX1 = 63;
    lim.colourStart = 4UL * 1024 * 1024;
    lim.colourEnd   = 5UL * 1024 * 1024;
    lim.depthStart  = 5UL * 1024 * 1024;
    lim.depthEnd    = 5UL * 1024 * 1024 + 512UL * 1024;
    lim.texStart    = 6UL * 1024 * 1024;
    lim.texEnd      = 7UL * 1024 * 1024;
    lim.batchBytes  = OSMGA_HW3D_BATCH_BYTES;
    lim.maxEdgeWalk = 16384;          /* pixels of x travel per triangle */
    rows = lim.clipY1 + 1;
    pitch = lim.pitchBytes;

    printf("struct sizes: batch %lu, state %lu, tri %lu (target wants "
           "4-byte long)\n",
           (unsigned long)sizeof(OSMGAHW3DBatch),
           (unsigned long)sizeof(OSMGAHW3DState),
           (unsigned long)sizeof(OSMGAHW3DTri));
    if (sizeof(unsigned long) != 4) {
        printf("  this host has %lu-byte longs; build with -m32\n",
               (unsigned long)sizeof(unsigned long));
        return 2;
    }

    printf("header fields\n");
    reset();                                    expect("valid batch", OSMGA_HW3D_OK);
    reset(); b.magic ^= 1;                      expect("wrong magic", OSMGA_HW3D_E_MAGIC);
    reset(); b.version = OSMGA_HW3D_VERSION + 1;expect("wrong version", OSMGA_HW3D_E_VERSION);
    {   unsigned long k;                        /* the cap needs real triangles */
        reset();
        b.triCount = OSMGA_HW3D_MAX_TRI;
        for (k = 0; k < OSMGA_HW3D_MAX_TRI; k++) {
            b.tri[k] = b.tri[0];        /* dwgctl and alphactrl included */
            b.tri[k].y = 0; b.tri[k].h = 1;
 b.tri[k].ar0 = b.tri[k].ar6 = b.tri[k].h;
            b.tri[k].fxbndry = (64UL << 16) | 0UL;
        }
        expect("triCount at the cap", OSMGA_HW3D_OK);
    }
    reset(); b.triCount = OSMGA_HW3D_MAX_TRI+1; expect("triCount one past the cap", OSMGA_HW3D_E_COUNT);
    reset(); b.triCount = 0;                    expect("triCount zero", OSMGA_HW3D_OK);
    reset(); lim.batchBytes = 128;              expect("buffer too small for the count", OSMGA_HW3D_E_COUNT);
    lim.batchBytes = OSMGA_HW3D_BATCH_BYTES;

    printf("colour origin -- reach is origin + %lu rows * %lu bytes\n", rows, pitch);
    reset(); b.state.dstorg = lim.colourEnd - rows * pitch;
                                                expect("dstorg at the last fitting origin", OSMGA_HW3D_OK);
    reset(); b.state.dstorg = lim.colourEnd - rows * pitch + 1;
                                                expect("dstorg one byte past that", OSMGA_HW3D_E_DSTORG);
    reset(); b.state.dstorg = lim.colourStart - 1;
                                                expect("dstorg one byte below the window", OSMGA_HW3D_E_DSTORG);
    reset(); b.state.dstorg = 0;                expect("dstorg at the visible framebuffer", OSMGA_HW3D_E_DSTORG);
    reset(); b.state.dstorg = lim.colourEnd;    expect("dstorg at the window end", OSMGA_HW3D_E_DSTORG);
    reset(); b.state.dstorg = 0xFFFFF000UL;     expect("dstorg near the 32-bit ceiling", OSMGA_HW3D_E_DSTORG);

    printf("drawing control -- per triangle now, and masked\n");
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
                                                expect("TEXTURE_TRAP | atype I", OSMGA_HW3D_OK);
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL; b.state.texorg = 0;
                                                expect("the same with texorg at the framebuffer", OSMGA_HW3D_E_TEXORG);
    reset(); b.tri[0].dwgctl = 0x0004UL | 0x0030UL;
                                                expect("TRAP | atype ZI", OSMGA_HW3D_OK);
    reset(); b.tri[0].dwgctl = 0x0004UL | 0x0000UL;
                                                expect("atype RPL is not an option", OSMGA_HW3D_E_DWGCTL);
    reset(); b.tri[0].dwgctl = 0x0008UL | 0x0070UL;
                                                expect("BITBLT opcode", OSMGA_HW3D_E_DWGCTL);
    reset(); b.tri[0].dwgctl = 0x0009UL | 0x0070UL;
                                                expect("ILOAD opcode", OSMGA_HW3D_E_DWGCTL);
    reset(); b.tri[0].dwgctl |= 0x0080UL;       expect("linear bit is masked away, not refused", OSMGA_HW3D_OK);
    reset(); b.tri[0].dwgctl |= 0x0800UL;       expect("SOLID is masked away", OSMGA_HW3D_OK);
    reset(); b.tri[0].dwgctl |= 0x3000UL;       expect("ARZERO and SGNZERO are masked away", OSMGA_HW3D_OK);
    reset(); b.tri[0].dwgctl |= 0x0700UL;       expect("any zmode is allowed", OSMGA_HW3D_OK);

    printf("alpha control -- undefined encodings\n");
    reset(); b.tri[0].alphactrl = 0x0008UL;     expect("src factor 8, the last defined", OSMGA_HW3D_OK);
    reset(); b.tri[0].alphactrl = 0x0009UL;     expect("src factor 9, undefined", OSMGA_HW3D_E_ALPHA);
    reset(); b.tri[0].alphactrl = 0x0070UL;     expect("dst factor 7, the last defined", OSMGA_HW3D_OK);
    reset(); b.tri[0].alphactrl = 0x0080UL;     expect("dst factor 8, undefined", OSMGA_HW3D_E_ALPHA);
    reset(); b.tri[0].alphactrl = 0x0200UL;     expect("amode 2, video alpha", OSMGA_HW3D_OK);
    reset(); b.tri[0].alphactrl = 0x0300UL;     expect("amode 3, reserved", OSMGA_HW3D_E_ALPHA);
    reset(); b.tri[0].alphactrl = 0x0000UL;     expect("atmode 0, no compare", OSMGA_HW3D_OK);
    reset(); b.tri[0].alphactrl = 0x2000UL;     expect("atmode 1, no macro exists", OSMGA_HW3D_E_ALPHA);
    reset(); b.tri[0].alphactrl = 0x4000UL;     expect("atmode 2, defined", OSMGA_HW3D_OK);
    reset(); b.tri[0].alphactrl = 0xFC000400UL; expect("bits with no field are masked away", OSMGA_HW3D_OK);

    printf("origins follow ANY triangle, not all of them\n");
    reset(); b.triCount = 3;
             b.tri[1] = b.tri[0]; b.tri[2] = b.tri[0];
             b.state.zorg = 0;                  expect("bad zorg, no triangle is ZI", OSMGA_HW3D_OK);
    reset(); b.triCount = 3;
             b.tri[1] = b.tri[0]; b.tri[2] = b.tri[0];
             b.tri[1].dwgctl = 0x0004UL | 0x0030UL;
             b.state.zorg = 0;                  expect("bad zorg, ONE middle triangle is ZI", OSMGA_HW3D_E_ZORG);
    reset(); b.triCount = 3;
             b.tri[1] = b.tri[0]; b.tri[2] = b.tri[0];
             b.tri[2].dwgctl = 0x0004UL | 0x0030UL;
             b.state.zorg = 0;                  expect("bad zorg, the LAST triangle is ZI", OSMGA_HW3D_E_ZORG);
    reset(); b.triCount = 3;
             b.tri[1] = b.tri[0]; b.tri[2] = b.tri[0];
             b.tri[1].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texorg = 0;                expect("bad texorg, ONE triangle is textured", OSMGA_HW3D_E_TEXORG);
    reset(); b.triCount = 3;
             b.tri[1] = b.tri[0]; b.tri[2] = b.tri[0];
             b.state.texorg = 0;                expect("bad texorg, none is textured", OSMGA_HW3D_OK);

    printf("depth origin -- reach, when any triangle is ZI\n");
    reset(); b.tri[0].dwgctl = 0x0004UL | 0x0030UL;
             b.state.zorg = lim.depthEnd - rows * (pitch / 4) * 2;
                                                expect("zorg at the last fitting origin", OSMGA_HW3D_OK);
    reset(); b.tri[0].dwgctl = 0x0004UL | 0x0030UL;
             b.state.zorg = lim.depthEnd - rows * (pitch / 4) * 2 + 1;
                                                expect("zorg one byte past that", OSMGA_HW3D_E_ZORG);
    reset(); b.tri[0].dwgctl = 0x0004UL | 0x0030UL;
             b.state.zorg = 0;                  expect("zorg at the visible framebuffer", OSMGA_HW3D_E_ZORG);

    printf("texture origin -- reach from the size the client gave\n");
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texorg = lim.texEnd - 64 * 64 * 4;
                                                expect("texorg at the last fitting origin", OSMGA_HW3D_OK);
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texorg = lim.texEnd - 64 * 64 * 4 + 1;
                                                expect("texorg one byte past that", OSMGA_HW3D_E_TEXORG);
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texPitch = 128;
             b.state.texorg = lim.texEnd - 64 * 128 * 4;
                                                expect("a padded pitch moves the reach", OSMGA_HW3D_OK);
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texPitch = 128;
             b.state.texorg = lim.texEnd - 64 * 64 * 4;
                                                expect("that origin no longer fits at the wider pitch", OSMGA_HW3D_E_TEXORG);

    printf("texture size -- powers of two are NOT required\n");
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texW = 48; b.state.texH = 17; b.state.texPitch = 48;
                                                expect("48 by 17, neither a power of two", OSMGA_HW3D_OK);
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texW = 2047; b.state.texH = 1; b.state.texPitch = 2047;
                                                expect("width 2047, the widest expressible", OSMGA_HW3D_OK);
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texW = 2048; b.state.texH = 1; b.state.texPitch = 2047;
                                                expect("width 2048 needs a pitch the field cannot hold", OSMGA_HW3D_E_TEXSIZE);
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texW = 2049;               expect("width past the stated bound", OSMGA_HW3D_E_TEXSIZE);
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texW = 0;                  expect("zero width", OSMGA_HW3D_E_TEXSIZE);
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texH = 0;                  expect("zero height", OSMGA_HW3D_E_TEXSIZE);
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texPitch = 63;             expect("pitch narrower than the width", OSMGA_HW3D_E_TEXSIZE);
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texPitch = 2048;           expect("pitch past the 11-bit field", OSMGA_HW3D_E_TEXSIZE);
    reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
             b.state.texFormat = 3;             expect("a format we do not allow yet", OSMGA_HW3D_E_TEXSIZE);
    reset(); b.state.texW = 0; b.state.texPitch = 0; b.state.texFormat = 3;
                                                expect("nonsense texture, but nothing is textured", OSMGA_HW3D_OK);

    printf("texture coordinates -- bounded, because CLAMPUV was measured once\n");
    {   long span = 0x4000L;            /* one texel for a 64-texel texture */
        long full = (long)OSMGA_HW3D_TEX_COORD_MAX;

        reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
                                                expect("the identity mapping", OSMGA_HW3D_OK);
        reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
                 b.state.tmr[0] = span * 8;     expect("magnified eight times, as measured", OSMGA_HW3D_OK);
        reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
                 b.state.tmr[6] = -1;           expect("a negative u start", OSMGA_HW3D_E_TEXCOORD);
        reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
                 b.state.tmr[7] = -1;           expect("a negative v start", OSMGA_HW3D_E_TEXCOORD);
        reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
                 b.state.tmr[0] = -span;        expect("a negative u increment", OSMGA_HW3D_E_TEXCOORD);
        /* The budget covers the start AND what the increments add across
         * the clip, so a start at the whole budget only fits when the
         * increments are zero. */
        reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
                 b.state.tmr[0] = 0; b.state.tmr[6] = full;
                                                expect("a start at the whole budget, no increment", OSMGA_HW3D_OK);
        reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
                 b.state.tmr[0] = 0; b.state.tmr[6] = full + 1;
                                                expect("a start one past it", OSMGA_HW3D_E_TEXCOORD);
        reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
                 b.state.tmr[6] = full;         expect("that start with the identity increment no longer fits", OSMGA_HW3D_E_TEXCOORD);
        reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
                 b.state.tmr[0] = full;         expect("an increment that overshoots across the clip", OSMGA_HW3D_E_TEXCOORD);
        reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
                 b.state.tmr[2] = full;         expect("a y increment that overshoots", OSMGA_HW3D_E_TEXCOORD);
        reset(); b.tri[0].dwgctl = 0x0006UL | 0x0070UL;
                 b.state.tmr[4] = -1; b.state.tmr[8] = -1;
                                                expect("the H family is ignored, not refused", OSMGA_HW3D_OK);
        reset(); b.state.tmr[6] = -1;           expect("a bad coordinate, but nothing is textured", OSMGA_HW3D_OK);
    }

    printf("per-triangle geometry\n");
    reset(); b.tri[0].y = lim.clipY1; b.tri[0].h = 1;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h;
                                                expect("last row, one row tall", OSMGA_HW3D_OK);
    reset(); b.tri[0].y = lim.clipY1; b.tri[0].h = 2;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h;
                                                expect("one row past the clip", OSMGA_HW3D_E_TRIROW);
    reset(); b.tri[0].y = lim.clipY1 + 1;       expect("first row past the clip", OSMGA_HW3D_E_TRIROW);
    reset(); b.tri[0].y = -1;                   expect("negative first row", OSMGA_HW3D_E_TRIROW);
    reset(); b.tri[0].h = 0;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h;                    expect("zero rows", OSMGA_HW3D_E_TRIROW);
    reset(); b.tri[0].h = -1;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h;                   expect("negative rows", OSMGA_HW3D_E_TRIROW);
    reset(); b.tri[0].fxbndry = ((lim.clipX1 + 1) << 16) | 0;
                                                expect("span to the clip edge", OSMGA_HW3D_OK);
    reset(); b.tri[0].fxbndry = ((lim.clipX1 + 2) << 16) | 0;
                                                expect("span one past the clip edge", OSMGA_HW3D_E_TRICOL);
    reset(); b.tri[0].fxbndry = (0UL << 16) | 8UL;
                                                expect("right edge left of the left edge", OSMGA_HW3D_E_TRICOL);
    reset(); b.triCount = 3; b.tri[1] = b.tri[0]; b.tri[2] = b.tri[0];
             b.tri[2].y = lim.clipY1 + 1;       expect("a bad triangle late in the batch", OSMGA_HW3D_E_TRIROW);

    printf("edge slopes -- bounded so containment does not rest on the clip alone\n");
    reset(); b.tri[0].h = 20;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h; b.tri[0].ar2 = -(long)(lim.maxEdgeWalk / 20);
                                                expect("slope at the walk limit", OSMGA_HW3D_OK);
    reset(); b.tri[0].h = 20;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h; b.tri[0].ar2 = -(long)(lim.maxEdgeWalk / 20) - 1;
                                                expect("slope one past the limit", OSMGA_HW3D_E_TRISLOPE);
    reset(); b.tri[0].h = 20;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h; b.tri[0].ar5 = (long)(lim.maxEdgeWalk / 20) + 1;
                                                expect("right edge past the limit", OSMGA_HW3D_E_TRISLOPE);
    reset(); b.tri[0].h = 20;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h; b.tri[0].ar1 = -(long)(lim.maxEdgeWalk / 20) - 1;
                                                expect("ar1 carries the slope too", OSMGA_HW3D_E_TRISLOPE);
    reset(); b.tri[0].h = 20;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h; b.tri[0].ar2 = -131071L;
                                                expect("the widest an 18-bit field holds", OSMGA_HW3D_E_TRISLOPE);
    reset(); b.tri[0].h = 1;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h;  b.tri[0].ar2 = -(long)lim.maxEdgeWalk;
                                                expect("one row, the whole budget", OSMGA_HW3D_OK);
    reset(); b.tri[0].h = 1;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h;  b.tri[0].ar2 = -(long)lim.maxEdgeWalk - 1;
                                                expect("one row, one past it", OSMGA_HW3D_E_TRISLOPE);


    /*
     * The edge accumulator divides by AR0 and AR6, and the slope bound above
     * assumes the edge advances by its displacement over that height.  A zero
     * divisor stops the accumulator decreasing, so the edge walks on without
     * limit and a displacement of one is enough to leave the rectangle -- the
     * bound stops meaning anything.  These exist because nothing checked the
     * divisors at all.
     */
    reset(); b.tri[0].ar6 = 0;
                                                expect("a zero right-edge divisor", OSMGA_HW3D_E_EDGEDIV);
    reset(); b.tri[0].ar0 = 0;
                                                expect("a zero left-edge divisor", OSMGA_HW3D_E_EDGEDIV);
    reset(); b.tri[0].h = 8;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h; b.tri[0].ar6 = 4;
                                                expect("a divisor that is not the height", OSMGA_HW3D_E_EDGEDIV);
    reset(); b.tri[0].h = 8;
 b.tri[0].ar0 = b.tri[0].ar6 = b.tri[0].h;
                                                expect("divisors that are the height", OSMGA_HW3D_OK);

    printf("\n%s (%d failing)\n", failures ? "FAILURES" : "all cases behave as specified",
           failures);
    return failures != 0;
}
