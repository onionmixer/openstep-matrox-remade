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
    b.state.dwgctl = 0x000C7074UL;          /* TRAP | atype I, our Gouraud */
    b.tri[0].y = 0;
    b.tri[0].h = 1;
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
    lim.texMaxBytes = 256UL * 1024;
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
            b.tri[k].y = 0; b.tri[k].h = 1;
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

    printf("drawing control\n");
    reset(); b.state.dwgctl = 0x000C7076UL;     expect("TEXTURE_TRAP | atype I", OSMGA_HW3D_OK);
    reset(); b.state.dwgctl = (0x000C7074UL & ~0x70UL) | 0x30UL;
                                                expect("TRAP | atype ZI", OSMGA_HW3D_OK);
    reset(); b.state.dwgctl = 0x000C7804UL;     expect("TRAP | atype RPL (solid fill)", OSMGA_HW3D_E_DWGCTL);
    reset(); b.state.dwgctl = (0x000C7074UL & ~0xFUL) | 0x8UL;
                                                expect("BITBLT opcode", OSMGA_HW3D_E_DWGCTL);
    reset(); b.state.dwgctl = (0x000C7074UL & ~0xFUL) | 0x9UL;
                                                expect("ILOAD opcode", OSMGA_HW3D_E_DWGCTL);
    reset(); b.state.dwgctl = 0x000C7074UL | 0x80UL;
                                                expect("linear addressing bit set", OSMGA_HW3D_E_DWGCTL);

    printf("depth origin -- only checked when the access type is ZI\n");
    reset(); b.state.zorg = 0;                  expect("bad zorg but atype I, so unused", OSMGA_HW3D_OK);
    reset(); b.state.dwgctl = (b.state.dwgctl & ~0x70UL) | 0x30UL;
             b.state.zorg = lim.depthEnd - rows * (pitch / 4) * 2;
                                                expect("zorg at the last fitting origin", OSMGA_HW3D_OK);
    reset(); b.state.dwgctl = (b.state.dwgctl & ~0x70UL) | 0x30UL;
             b.state.zorg = lim.depthEnd - rows * (pitch / 4) * 2 + 1;
                                                expect("zorg one byte past that", OSMGA_HW3D_E_ZORG);
    reset(); b.state.dwgctl = (b.state.dwgctl & ~0x70UL) | 0x30UL;
             b.state.zorg = 0;                  expect("zorg at the visible framebuffer", OSMGA_HW3D_E_ZORG);

    printf("texture origin -- only checked for the textured opcode\n");
    reset(); b.state.texorg = 0;                expect("bad texorg but untextured", OSMGA_HW3D_OK);
    reset(); b.state.dwgctl = 0x000C7076UL;
             b.state.texorg = lim.texEnd - lim.texMaxBytes;
                                                expect("texorg at the last fitting origin", OSMGA_HW3D_OK);
    reset(); b.state.dwgctl = 0x000C7076UL;
             b.state.texorg = lim.texEnd - lim.texMaxBytes + 1;
                                                expect("texorg one byte past that", OSMGA_HW3D_E_TEXORG);
    reset(); b.state.dwgctl = 0x000C7076UL;
             b.state.texorg = 0;                expect("texorg at the visible framebuffer", OSMGA_HW3D_E_TEXORG);

    printf("per-triangle geometry\n");
    reset(); b.tri[0].y = lim.clipY1; b.tri[0].h = 1;
                                                expect("last row, one row tall", OSMGA_HW3D_OK);
    reset(); b.tri[0].y = lim.clipY1; b.tri[0].h = 2;
                                                expect("one row past the clip", OSMGA_HW3D_E_TRIROW);
    reset(); b.tri[0].y = lim.clipY1 + 1;       expect("first row past the clip", OSMGA_HW3D_E_TRIROW);
    reset(); b.tri[0].y = -1;                   expect("negative first row", OSMGA_HW3D_E_TRIROW);
    reset(); b.tri[0].h = 0;                    expect("zero rows", OSMGA_HW3D_E_TRIROW);
    reset(); b.tri[0].h = -1;                   expect("negative rows", OSMGA_HW3D_E_TRIROW);
    reset(); b.tri[0].fxbndry = ((lim.clipX1 + 1) << 16) | 0;
                                                expect("span to the clip edge", OSMGA_HW3D_OK);
    reset(); b.tri[0].fxbndry = ((lim.clipX1 + 2) << 16) | 0;
                                                expect("span one past the clip edge", OSMGA_HW3D_E_TRICOL);
    reset(); b.tri[0].fxbndry = (0UL << 16) | 8UL;
                                                expect("right edge left of the left edge", OSMGA_HW3D_E_TRICOL);
    reset(); b.triCount = 3; b.tri[1].h = 1; b.tri[2].h = 1;
             b.tri[1].fxbndry = b.tri[2].fxbndry = (64UL << 16);
             b.tri[2].y = lim.clipY1 + 1;       expect("a bad triangle late in the batch", OSMGA_HW3D_E_TRIROW);

    printf("edge slopes -- bounded so containment does not rest on the clip alone\n");
    reset(); b.tri[0].h = 20; b.tri[0].ar2 = -(long)(lim.maxEdgeWalk / 20);
                                                expect("slope at the walk limit", OSMGA_HW3D_OK);
    reset(); b.tri[0].h = 20; b.tri[0].ar2 = -(long)(lim.maxEdgeWalk / 20) - 1;
                                                expect("slope one past the limit", OSMGA_HW3D_E_TRISLOPE);
    reset(); b.tri[0].h = 20; b.tri[0].ar5 = (long)(lim.maxEdgeWalk / 20) + 1;
                                                expect("right edge past the limit", OSMGA_HW3D_E_TRISLOPE);
    reset(); b.tri[0].h = 20; b.tri[0].ar1 = -(long)(lim.maxEdgeWalk / 20) - 1;
                                                expect("ar1 carries the slope too", OSMGA_HW3D_E_TRISLOPE);
    reset(); b.tri[0].h = 20; b.tri[0].ar2 = -131071L;
                                                expect("the widest an 18-bit field holds", OSMGA_HW3D_E_TRISLOPE);
    reset(); b.tri[0].h = 1;  b.tri[0].ar2 = -(long)lim.maxEdgeWalk;
                                                expect("one row, the whole budget", OSMGA_HW3D_OK);
    reset(); b.tri[0].h = 1;  b.tri[0].ar2 = -(long)lim.maxEdgeWalk - 1;
                                                expect("one row, one past it", OSMGA_HW3D_E_TRISLOPE);

    printf("\n%s (%d failing)\n", failures ? "FAILURES" : "all cases behave as specified",
           failures);
    return failures != 0;
}
