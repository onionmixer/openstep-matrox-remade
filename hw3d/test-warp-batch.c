/*
 * test-warp-batch.c -- the version 10 WARP payload and its validator.
 *
 * Two things are checked, and the first matters more than it looks.
 *
 * LAYOUT.  The WARP batch is a SECOND shape written into the same mapped
 * buffer as the trapezoid batch, and the kernel reads magic and version
 * before it knows which one it has.  That only works if those fields sit
 * at the same offsets in both, which C89 does not promise for two
 * unrelated structs -- so it is asserted here, where offsetof is
 * available, rather than hoped for in the kernel header.
 *
 * VERDICTS.  Every defect the validator exists to catch is built and fed
 * to it, and the verdict is compared against the one the contract names.
 * A validator that refuses everything passes a test that only checks that
 * bad input is refused, so the good batch is checked first and each
 * defect is a single field changed from it.
 *
 * Hosted C89.
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "OpenStepMGAHW3D.h"

static int failures = 0;

static void
check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static unsigned long
bits(float f)
{
    unsigned int u;

    memcpy(&u, &f, sizeof u);
    return (unsigned long)u;
}

static OSMGAHW3DWarpBatch  wb;
static OSMGAHW3DLimits     lim;

static void
good(void)
{
    unsigned long i;

    memset(&wb, 0, sizeof wb);
    wb.magic    = OSMGA_HW3D_MAGIC;
    wb.version  = OSMGA_HW3D_VERSION_WARP;
    wb.triCount = 0UL;
    wb.runCount = 2U;
    wb.vtxCount = 6U;

    wb.state.texFlags  = 0UL;
    wb.run[0].dwgctl    = 0x000c4074U;
    wb.run[0].alphactrl = 0x00000001U;
    wb.run[0].first     = 0U;
    wb.run[0].count     = 3U;
    wb.run[1].dwgctl    = 0x000c4074U;
    wb.run[1].alphactrl = 0x00000001U;
    wb.run[1].first     = 3U;
    wb.run[1].count     = 3U;

    for (i = 0UL; i < 6UL; i++) {
        wb.vtx[i].x   = (osmga_u32)bits((float)(10 + i));
        wb.vtx[i].y   = (osmga_u32)bits((float)(20 + i));
        wb.vtx[i].z   = (osmga_u32)bits(0.5f);
        wb.vtx[i].rhw = (osmga_u32)bits(1.0f);
        wb.vtx[i].tu0 = (osmga_u32)bits(0.25f);
        wb.vtx[i].tv0 = (osmga_u32)bits(0.75f);
    }
    memset(&lim, 0, sizeof lim);
    lim.pitchBytes  = 4096UL;
    lim.clipX1      = 63UL;
    lim.clipY1      = 63UL;
    lim.colourStart = 4UL * 1024UL * 1024UL;
    lim.colourEnd   = 5UL * 1024UL * 1024UL;
    lim.depthStart  = 5UL * 1024UL * 1024UL;
    lim.depthEnd    = 5UL * 1024UL * 1024UL + 512UL * 1024UL;
    lim.texStart    = 6UL * 1024UL * 1024UL;
    lim.texEnd      = 7UL * 1024UL * 1024UL;
    lim.batchBytes  = OSMGA_HW3D_BATCH_BYTES;
    lim.maxEdgeWalk = 16384UL;

    wb.state.dstorg    = lim.colourStart;
    wb.state.dstPitch  = lim.pitchBytes / 4UL;
    wb.state.dstWidth  = lim.clipX1 + 1UL;
    wb.state.dstHeight = lim.clipY1 + 1UL;
    wb.state.zorg      = lim.depthStart;
    wb.state.texorg    = lim.texStart;
    wb.state.texW      = 64UL;
    wb.state.texH      = 64UL;
    wb.state.texPitch  = 64UL;
    wb.state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
}

static void
expect(int want, const char *what)
{
    unsigned long badRun = 0UL;
    int got = osmgaHW3DValidateWarp(&wb, &lim, &badRun);

    if (got != want) {
        printf("FAIL: %s -- wanted verdict %d, got %d\n", what, want, got);
        failures++;
    }
}

static void
layout(void)
{
    check(sizeof(OSMGAHW3DVertex) == 32, "a vertex is 32 bytes");
    check(sizeof(OSMGAHW3DRun) == 16, "a run is 16 bytes");
    check(sizeof(OSMGAHW3DWarpBatch) <= OSMGA_HW3D_BATCH_BYTES,
          "the WARP batch fits the mapped region");
    check(sizeof(OSMGAHW3DWarpBatch) <= sizeof(OSMGAHW3DBatch),
          "the WARP batch does not move the page split");
    check((OSMGA_HW3D_MAX_VTX % 3UL) == 0UL,
          "the vertex maximum is a whole number of triangles");

    /* The prefix the kernel reads before it knows which shape it has. */
    check(offsetof(OSMGAHW3DWarpBatch, magic) ==
          offsetof(OSMGAHW3DBatch, magic), "magic is at the same offset");
    check(offsetof(OSMGAHW3DWarpBatch, version) ==
          offsetof(OSMGAHW3DBatch, version), "version is at the same offset");
    check(offsetof(OSMGAHW3DWarpBatch, triCount) ==
          offsetof(OSMGAHW3DBatch, triCount), "triCount is at the same offset");
    check(offsetof(OSMGAHW3DWarpBatch, state) ==
          offsetof(OSMGAHW3DBatch, state), "state is at the same offset");

    /* And the command list has to hold what the maxima allow: a state list
     * per run plus every vertex, copied out of the client's reach. */
    check(OSMGA_HW3D_MAX_RUN * 460UL +
          OSMGA_HW3D_MAX_VTX * sizeof(OSMGAHW3DVertex) <=
          OSMGA_HW3D_LIST_BYTES,
          "the runs and vertices fit the command list");
}

static void
structure(void)
{
    good();
    expect(OSMGA_HW3D_OK, "a well formed batch is accepted");

    good(); wb.magic = 0UL;
    expect(OSMGA_HW3D_E_MAGIC, "a wrong magic is refused");

    good(); wb.version = OSMGA_HW3D_VERSION;
    expect(OSMGA_HW3D_E_VERSION, "a version 9 batch is not a WARP batch");

    good(); wb.triCount = 1UL;
    expect(OSMGA_HW3D_E_WARPMIX, "both payloads filled in is refused");

    good(); wb.runCount = 0U;
    expect(OSMGA_HW3D_E_VTXCOUNT, "no runs is refused");
    good(); wb.runCount = (osmga_u32)(OSMGA_HW3D_MAX_RUN + 1UL);
    expect(OSMGA_HW3D_E_VTXCOUNT, "too many runs is refused");
    good(); wb.vtxCount = 0U;
    expect(OSMGA_HW3D_E_VTXCOUNT, "no vertices is refused");
    good(); wb.vtxCount = (osmga_u32)(OSMGA_HW3D_MAX_VTX + 3UL);
    expect(OSMGA_HW3D_E_VTXCOUNT, "too many vertices is refused");
    good(); wb.vtxCount = 5U;
    expect(OSMGA_HW3D_E_VTXCOUNT, "a vertex count that is not a triangle "
                                  "count is refused");

    /* The runs must partition the vertices.  A gap leaves vertices the
     * client thinks were drawn; an overlap draws a primitive twice under
     * two different states. */
    good(); wb.run[1].first = 4U;
    expect(OSMGA_HW3D_E_VTXCOUNT, "a gap between runs is refused");
    good(); wb.run[1].first = 0U;
    expect(OSMGA_HW3D_E_VTXCOUNT, "overlapping runs are refused");
    good(); wb.run[1].count = 6U;
    expect(OSMGA_HW3D_E_VTXCOUNT, "a run past the last vertex is refused");
    good(); wb.run[1].count = 4U; wb.vtxCount = 7U;
    expect(OSMGA_HW3D_E_VTXCOUNT, "a run that is not whole triangles is "
                                  "refused");
    good(); wb.runCount = 1U;
    expect(OSMGA_HW3D_E_VTXCOUNT, "runs that do not cover every vertex are "
                                  "refused");

    good(); lim.batchBytes = 8UL;
    expect(OSMGA_HW3D_E_VTXCOUNT, "a buffer too small for the fixed part is "
                                  "refused");
    lim.batchBytes = OSMGA_HW3D_BATCH_BYTES;
}

static void
vertices(void)
{
    good(); wb.vtx[4].x = (osmga_u32)0x7FC00000UL;
    expect(OSMGA_HW3D_E_VTXFLOAT, "a NaN coordinate is refused");
    good(); wb.vtx[4].y = (osmga_u32)0x7F800000UL;
    expect(OSMGA_HW3D_E_VTXFLOAT, "an infinite coordinate is refused");
    good(); wb.vtx[4].x = (osmga_u32)bits(8192.5f);
    expect(OSMGA_HW3D_E_VTXFLOAT, "a coordinate past the builder's own "
                                  "bound is refused");
    good(); wb.vtx[4].x = (osmga_u32)bits(-8192.5f);
    expect(OSMGA_HW3D_E_VTXFLOAT, "the coordinate bound is symmetric");
    good(); wb.vtx[4].x = (osmga_u32)bits(8192.0f);
    expect(OSMGA_HW3D_OK, "the coordinate bound itself is admitted");

    good(); wb.vtx[2].z = (osmga_u32)bits(1.5f);
    expect(OSMGA_HW3D_E_VTXFLOAT, "a depth past one is refused");
    good(); wb.vtx[2].z = (osmga_u32)bits(-0.5f);
    expect(OSMGA_HW3D_E_VTXFLOAT, "a depth below nought is refused");
    good(); wb.vtx[2].z = (osmga_u32)bits(1.0f);
    expect(OSMGA_HW3D_OK, "a depth of exactly one is admitted");
    good(); wb.vtx[2].z = (osmga_u32)bits(0.0f);
    expect(OSMGA_HW3D_OK, "a depth of exactly nought is admitted");

    /* rhw carries the containment argument: the perspective-corrected
     * interpolation is a convex combination only while every vertex weight
     * is strictly positive. */
    good(); wb.vtx[0].rhw = (osmga_u32)bits(0.0f);
    expect(OSMGA_HW3D_E_VTXFLOAT, "a zero rhw is refused");
    good(); wb.vtx[0].rhw = (osmga_u32)bits(-1.0f);
    expect(OSMGA_HW3D_E_VTXFLOAT, "a negative rhw is refused");
    good(); wb.vtx[0].rhw = (osmga_u32)0x00000001UL;
    expect(OSMGA_HW3D_E_VTXFLOAT, "a denormal rhw is refused");
    good(); wb.vtx[0].rhw = (osmga_u32)bits(0.0625f);
    expect(OSMGA_HW3D_E_VTXFLOAT, "an rhw below the converted Q floor is "
                                  "refused");
    good(); wb.vtx[0].rhw = (osmga_u32)bits(256.0f);
    expect(OSMGA_HW3D_E_VTXFLOAT, "an rhw above the converted Q ceiling is "
                                  "refused");
    good(); wb.vtx[0].rhw = (osmga_u32)bits(0.125f);
    expect(OSMGA_HW3D_OK, "the rhw floor itself is admitted");
    good(); wb.vtx[0].rhw = (osmga_u32)bits(128.0f);
    expect(OSMGA_HW3D_OK, "the rhw ceiling itself is admitted");

    good(); wb.vtx[5].tu0 = (osmga_u32)0x7F800000UL;
    expect(OSMGA_HW3D_E_VTXFLOAT, "an infinite texture coordinate is refused");
    good(); wb.vtx[5].tv0 = (osmga_u32)0xFFC00000UL;
    expect(OSMGA_HW3D_E_VTXFLOAT, "a NaN texture coordinate is refused");
}

static void
policy(void)
{
    good(); wb.state.texFlags = OSMGA_HW3D_TEXF_REPEATU;
    expect(OSMGA_HW3D_E_WARPPOLICY, "repeat on u is not admitted yet");
    good(); wb.state.texFlags = OSMGA_HW3D_TEXF_REPEATV;
    expect(OSMGA_HW3D_E_WARPPOLICY, "repeat on v is not admitted yet");
    good(); wb.run[1].alphactrl = 0x01000154U;
    expect(OSMGA_HW3D_E_WARPPOLICY, "blending is not admitted yet");
    good(); wb.run[0].alphactrl = 0x00000101U;
    expect(OSMGA_HW3D_OK, "the opaque alpha state is admitted");

    /* The policy must be reached for EVERY run, or a batch could smuggle a
     * refused state in behind an admitted one. */
    good(); wb.run[1].alphactrl = 0x01000154U;
    expect(OSMGA_HW3D_E_WARPPOLICY, "a refused state in a later run is "
                                    "still refused");
}

/*
 * The wrap policy, which two encoders now share.  It was ten lines inside
 * the trapezoid encoder and therefore untestable; the cases below are the
 * ones that decide whether a texture may wrap at all.
 *
 * The rule that is easy to get wrong: a pitch that is not the width
 * disqualifies BOTH axes, because a masked index into a padded surface
 * addresses the wrong ROW -- it is not a per-axis property, and reading
 * it as one would let a client repeat u on a padded map.
 */
static void
clampPolicy(void)
{
    OSMGAHW3DState st;
    unsigned long a;

    memset(&st, 0, sizeof st);
    st.texW = 64UL; st.texH = 64UL; st.texPitch = 64UL;

    st.texFlags = 0UL;
    a = osmgaHW3DTexClampAxes(&st);
    check(a == (OSMGA_HW3D_CLAMP_U | OSMGA_HW3D_CLAMP_V),
          "no request means both axes clamp");

    st.texFlags = OSMGA_HW3D_TEXF_REPEATU;
    a = osmgaHW3DTexClampAxes(&st);
    check(a == OSMGA_HW3D_CLAMP_V, "repeat on u frees u alone");

    st.texFlags = OSMGA_HW3D_TEXF_REPEATV;
    a = osmgaHW3DTexClampAxes(&st);
    check(a == OSMGA_HW3D_CLAMP_U, "repeat on v frees v alone");

    st.texFlags = OSMGA_HW3D_TEXF_REPEATU | OSMGA_HW3D_TEXF_REPEATV;
    a = osmgaHW3DTexClampAxes(&st);
    check(a == 0UL, "both requested frees both");

    /* A padded map cannot wrap on EITHER axis. */
    st.texPitch = 80UL;
    a = osmgaHW3DTexClampAxes(&st);
    check(a == (OSMGA_HW3D_CLAMP_U | OSMGA_HW3D_CLAMP_V),
          "a pitch that is not the width disqualifies both axes");
    st.texPitch = 64UL;

    /* And a dimension that is not a power of two cannot wrap on ITS axis. */
    st.texW = 48UL; st.texPitch = 48UL;
    a = osmgaHW3DTexClampAxes(&st);
    check(a == OSMGA_HW3D_CLAMP_U,
          "a width that is not a power of two keeps u clamped");
    st.texW = 64UL; st.texH = 48UL; st.texPitch = 64UL;
    a = osmgaHW3DTexClampAxes(&st);
    check(a == OSMGA_HW3D_CLAMP_V,
          "a height that is not a power of two keeps v clamped");

    check(osmgaHW3DTexClampAxes((const OSMGAHW3DState *)0) ==
          (OSMGA_HW3D_CLAMP_U | OSMGA_HW3D_CLAMP_V),
          "no state at all clamps everything");
}

/*
 * The state the runs will program.  This was the gap: the validator judged
 * the run structure and every vertex word, then let the destination, the
 * depth buffer and the texture through unexamined.
 *
 * The verdicts are version 9's because the CHECKS are version 9's -- the
 * two contracts program the same registers from the same state, so they
 * share the code rather than each carrying a copy that can drift.
 */
static void
stateCases(void)
{
    good(); wb.state.dstPitch = 0UL;
    expect(OSMGA_HW3D_E_DSTPITCH, "a zero pitch is refused");
    good(); wb.state.dstPitch = 999UL;
    expect(OSMGA_HW3D_E_DSTPITCH, "a pitch the limits disagree with is "
                                  "refused");
    good(); wb.state.dstorg = wb.state.dstorg + 1UL;
    expect(OSMGA_HW3D_E_DSTORGAL, "an unaligned destination is refused");
    good(); wb.state.dstorg = 0UL;
    expect(OSMGA_HW3D_E_DSTORG, "a destination outside the window is "
                                "refused");

    /* Depth is examined only when a run addresses it -- the same ANY rule
     * version 9 uses, so one depth run among flat ones still bounds it. */
    good(); wb.state.zorg = 0UL;
    expect(OSMGA_HW3D_OK, "an unused depth origin is ignored");
    good(); wb.state.zorg = 0UL;
    wb.run[1].dwgctl = 0x000c4034U;           /* atype ZI addresses depth */
    expect(OSMGA_HW3D_E_ZORG, "a depth origin a LATER run needs is bounded");

    good(); wb.state.texW = 0UL;
    expect(OSMGA_HW3D_OK, "an unused texture size is ignored");
    good(); wb.run[0].dwgctl = 0x000c4076U;   /* opcode 6 is textured */
    wb.state.texW = 0UL;
    expect(OSMGA_HW3D_E_TEXSIZE, "a texture a run needs is sized");
    good(); wb.run[0].dwgctl = 0x000c4076U;
    wb.state.texorg = 0UL;
    expect(OSMGA_HW3D_E_TEXORG, "a texture outside the window is refused");
    good(); wb.run[0].dwgctl = 0x000c4076U;
    wb.state.texPitch = 32UL;
    expect(OSMGA_HW3D_E_TEXSIZE, "a pitch narrower than the texture is "
                                 "refused");
}

int
main(void)
{
    layout();
    structure();
    vertices();
    policy();
    clampPolicy();
    stateCases();

    if (failures == 0)
        printf("test-warp-batch: layout shared with version 9, every named "
               "defect refused with its own verdict, the state bounded, and "
               "the wrap policy holds (0 failing)\n");
    else
        printf("test-warp-batch: %d failing\n", failures);
    return failures != 0;
}
