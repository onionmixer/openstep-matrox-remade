/*
 * A batch may ask its trapezoids onto one ladder rung, and the kernel keeps
 * the last word.
 *
 * The bias belongs to the trapezoid and comes from its own reach, so two
 * neighbours can see the same coordinate through different biases and it
 * lands in two places.  Probe section 82 measured that on a texture of 2048
 * rows: a window of 112 units, and twelve of sixty-four samples at an
 * ordinary tiling rate reading a different row.  Asking every primitive of a
 * surface onto one rung removes it.
 *
 * The rule is  used = max(own, min(request, headroom)):
 *
 *   the request may only ever raise the rung, because a higher rung
 *   subtracts less and leaves a bigger residual, and the residual is what
 *   must never go negative;
 *
 *   the headroom is the range: at a trapezoid's own rung the residual at its
 *   farthest coordinate is exactly nought, and raising the rung stops that
 *   being nought -- enough of it and the coordinate leaves the range the
 *   checks admit;
 *
 *   the outer max is because a perspective numerator may exceed COORD_MAX
 *   legally, which can put the headroom below the trapezoid's own rung.
 */
#include <stdio.h>
#include <string.h>
#include "OpenStepMGAHW3D.h"

static OSMGAHW3DBatch b;
static OSMGAHW3DLimits lim;
static OSMGAHW3DTexBand bands[OSMGA_HW3D_MAX_TRI];
static int failures;

static void
say(const char *what, int ok)
{
    if (ok)
        printf("   ok    %s\n", what);
    else {
        printf("   FAIL  %s\n", what);
        failures++;
    }
}

/* one textured trapezoid whose u reaches about the given value */
static void
build(long reach, unsigned long reqU, unsigned long reqV)
{
    OSMGAHW3DTri *t;

    memset(&b, 0, sizeof b);
    b.magic = OSMGA_HW3D_MAGIC;
    b.version = OSMGA_HW3D_VERSION;
    b.triCount = 1UL;
    b.state.dstorg = 0UL;
    b.state.dstWidth = 1024UL; b.state.dstHeight = 768UL;
    b.state.dstPitch = 1024UL;
    b.state.zorg = lim.depthStart;
    b.state.texorg = lim.texStart;
    b.state.texW = 64UL; b.state.texH = 64UL; b.state.texPitch = 64UL;
    b.state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
    b.state.texFlags = 0UL;
    b.state.texBiasReqU = reqU;
    b.state.texBiasReqV = reqV;

    t = &b.tri[0];
    t->dwgctl = OSMGA_HW3D_OPCODE_TEX | (OSMGA_HW3D_ATYPE_I << 4);
    t->alphactrl = 0x00000101UL;
    t->y = 0L; t->h = 1L;
    t->ar0 = 1L; t->ar6 = 1L;
    t->fxbndry = (2UL << 16) | 0UL;
    t->tq0 = OSMGA_HW3D_Q_ONE;
    t->tu0 = reach;
    t->tv0 = 0L;
}

int
main(void)
{
    unsigned long badTri = 0UL;
    OSMGAHW3DTexReach reach;
    static const long reaches[6] = {
        1L << 19, 1L << 20, (1L << 21) - 4L, (1L << 22) - 4L,
        (1L << 23) - 4L, 1L << 23
    };
    int i, q;

    lim.colourStart = 0UL;      lim.colourEnd = 16UL * 1024UL * 1024UL;
    lim.depthStart  = 16UL * 1024UL * 1024UL;
    lim.depthEnd    = 24UL * 1024UL * 1024UL;
    lim.texStart    = 24UL * 1024UL * 1024UL;
    lim.texEnd      = 32UL * 1024UL * 1024UL;
    lim.pitchBytes  = 4096UL;
    lim.clipX1 = 1023UL; lim.clipY1 = 767UL;
    lim.maxEdgeWalk = 1UL << 16;
    lim.batchBytes  = OSMGA_HW3D_BATCH_BYTES;

    printf("the requested rung, and what the kernel does with it\n\n");

    printf("1. no request leaves every trapezoid on its own rung\n");
    for (i = 0; i < 6; i++) {
        unsigned char want;
        char name[80];

        build(reaches[i], OSMGA_HW3D_TEX_BIAS_NONE,
              OSMGA_HW3D_TEX_BIAS_NONE);
        if (osmgaHW3DValidateReach(&b, &lim, &badTri, &reach, bands)
            != OSMGA_HW3D_OK) {
            printf("   FAIL  reach %ld was refused\n", reaches[i]);
            failures++;
            continue;
        }
        want = osmgaHW3DTexBandFor(reach.uMax);
        sprintf(name, "reach %8ld -> rung %u", reaches[i], (unsigned)want);
        say(name, bands[0].u == want);
    }

    printf("\n2. every request against every reach, and the rule\n");
    for (i = 0; i < 6; i++) {
        for (q = 0; q < (int)OSMGA_HW3D_TEX_BANDS; q++) {
            unsigned char own, head, want;
            int v;

            build(reaches[i], (unsigned long)(q + 1),
                  OSMGA_HW3D_TEX_BIAS_NONE);
            v = osmgaHW3DValidateReach(&b, &lim, &badTri, &reach, bands);
            if (v != OSMGA_HW3D_OK) {
                printf("   FAIL  reach %ld request %d refused (%d)\n",
                       reaches[i], q, v);
                failures++;
                continue;
            }
            own  = osmgaHW3DTexBandFor(reach.uMax);
            head = osmgaHW3DTexBandHeadroom(reach.uMax);
            want = (unsigned char)q;
            if (want > head) want = head;
            if (want < own)  want = own;
            if (bands[0].u != want) {
                printf("   FAIL  reach %8ld own %u head %u request %d ->"
                       " %u, wanted %u\n", reaches[i], (unsigned)own,
                       (unsigned)head, q, (unsigned)bands[0].u,
                       (unsigned)want);
                failures++;
            }
            /*
             * And the property the rule exists for, checked directly rather
             * than inferred from the arithmetic above.
             */
            if (bands[0].u < own) {
                printf("   FAIL  the rung went BELOW the trapezoid's own\n");
                failures++;
            }
            if (reach.uMax + osmgaHW3DTexBiasFor(reach.uMax)
                - osmgaHW3DTexBiasOfBand(bands[0].u)
                > (long)OSMGA_HW3D_TEX_COORD_MAX) {
                printf("   FAIL  reach %ld request %d leaves the range\n",
                       reaches[i], q);
                failures++;
            }
        }
    }
    printf("   ok    %d reaches by %d requests, all on the rule\n",
           6, (int)OSMGA_HW3D_TEX_BANDS);

    printf("\n3. the two axes are asked separately\n");
    build(1L << 19, (unsigned long)OSMGA_HW3D_TEX_BANDS,
          OSMGA_HW3D_TEX_BIAS_NONE);
    if (osmgaHW3DValidateReach(&b, &lim, &badTri, &reach, bands)
        == OSMGA_HW3D_OK) {
        say("a request on u alone moves u", bands[0].u > 0U);
        say("and leaves v where it was", bands[0].v == 0U);
    } else {
        printf("   FAIL  the split-axis batch was refused\n");
        failures++;
    }

    printf("\n4. a rung that is not on the ladder is refused, not clamped\n");
    build(1L << 19, OSMGA_HW3D_TEX_BANDS + 1UL, OSMGA_HW3D_TEX_BIAS_NONE);
    say("one past the top",
        osmgaHW3DValidateReach(&b, &lim, &badTri, &reach, bands)
        == OSMGA_HW3D_E_TEXSIZE);
    build(1L << 19, OSMGA_HW3D_TEX_BIAS_NONE, 0xFFFFFFFFUL);
    say("and a wild one on the other axis",
        osmgaHW3DValidateReach(&b, &lim, &badTri, &reach, bands)
        == OSMGA_HW3D_E_TEXSIZE);

    printf("\n5. the headroom really bites at the top of the range\n");
    {
        unsigned char h = osmgaHW3DTexBandHeadroom(1L << 23);

        printf("   a reach of 2^23 allows at most rung %u; rung 4 would put"
               " it at %ld\n", (unsigned)h,
               (1L << 23) + osmgaHW3DTexBiasFor(1L << 23)
               - osmgaHW3DTexBiasOfBand(4U));
        say("which is past COORD_MAX, so the headroom stops short of it",
            h < 4U);
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
