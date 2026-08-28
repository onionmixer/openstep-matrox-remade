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

    /*
     * The command list has to hold what the maxima allow: a state list per
     * run plus every vertex, copied out of the client's reach.  The header
     * asserts this at build time; here it is checked against the sizes the
     * compiler actually produced, which is not the same statement -- the
     * header's 32 is a literal and this one is sizeof.
     */
    check(OSMGA_HW3D_WARP_VTX_OFF +
          OSMGA_HW3D_MAX_VTX * sizeof(OSMGAHW3DVertex) <=
          OSMGA_HW3D_LIST_BYTES,
          "the runs and vertices fit the command list");
    check(OSMGA_HW3D_WARP_VTX_BYTES ==
          OSMGA_HW3D_MAX_VTX * sizeof(OSMGAHW3DVertex),
          "the vertex span matches the vertex size");
    check((OSMGA_HW3D_WARP_VTX_OFF % 32UL) == 0UL,
          "the vertex base needs no mask in PRIMADDRESS");
    /*
     * The vertex base does NOT move with the run count.  A base that did
     * would make the card's addresses depend on a number the client chose,
     * which is the opposite of why the vertices are copied at all.
     */
    check(OSMGA_HW3D_WARP_VTX_OFF ==
          OSMGA_HW3D_MAX_RUN * OSMGA_HW3D_WARP_STATE_BYTES,
          "the vertex base is past the WORST case state lists, not this "
          "batch's");
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
    /*
     * Repeat and blending are ADMITTED now.  Until the hardware answered
     * they were refused here, and these cases asserted the refusal; the
     * measurements that changed it are named in osmgaHW3DWarpAdmits.
     */
    good(); wb.state.texFlags = OSMGA_HW3D_TEXF_REPEATU;
    expect(OSMGA_HW3D_OK, "repeat on u is admitted");
    good(); wb.state.texFlags = OSMGA_HW3D_TEXF_REPEATV;
    expect(OSMGA_HW3D_OK, "repeat on v is admitted");
    good(); wb.state.texFlags = OSMGA_HW3D_TEXF_REPEATU |
                                OSMGA_HW3D_TEXF_REPEATV;
    expect(OSMGA_HW3D_OK, "repeat on both axes is admitted");
    good(); wb.run[1].alphactrl = 0x01000154U;
    expect(OSMGA_HW3D_OK, "blending is admitted");
    good(); wb.run[0].alphactrl = 0x00000101U;
    expect(OSMGA_HW3D_OK, "the opaque alpha state is admitted");

    /*
     * What each RUN asks the engine to do is judged by the same rule
     * version 9 applies to each trapezoid -- one function, two callers.
     */
    good(); wb.run[0].dwgctl = 0x000c4075U;      /* opcode 5 */
    expect(OSMGA_HW3D_E_DWGCTL, "an opcode the engine does not define is "
                                "refused");
    good(); wb.run[0].dwgctl = 0x000c4004U;      /* atype 0 */
    expect(OSMGA_HW3D_E_DWGCTL, "an access type outside I and ZI is "
                                "refused");
    good(); wb.run[0].dwgctl = 0x000c4076U;      /* opcode 6, textured */
    expect(OSMGA_HW3D_OK, "a textured opcode is admitted");
    good(); wb.run[0].dwgctl = 0x000c4034U;      /* atype ZI */
    expect(OSMGA_HW3D_OK, "the depth access type is admitted");

    good(); wb.run[0].alphactrl = 0x0000010FU;   /* src 15 > 8 */
    expect(OSMGA_HW3D_E_ALPHA, "a source blend factor past the table is "
                               "refused");
    good(); wb.run[0].alphactrl = 0x000001F1U;   /* dst 15 > 7 */
    expect(OSMGA_HW3D_E_ALPHA, "a destination factor past the table is "
                               "refused");
    good(); wb.run[0].alphactrl = 0x00000301U;   /* alphamode RSVD */
    expect(OSMGA_HW3D_E_ALPHA, "the reserved alpha mode is refused");
    /* Video alpha with a destination factor of ZERO: the fields are each
     * legal and the pair is not.  Its own verdict, so one cannot hide
     * behind the other. */
    good(); wb.run[0].alphactrl = 0x00000201U;
    expect(OSMGA_HW3D_E_ALPHACROSS, "video alpha with nothing to blend "
                                    "into is refused");

    /* And every run is judged, not just the first: a batch must not be
     * able to smuggle a bad state in behind a good one. */
    good(); wb.run[1].dwgctl = 0x000c4075U;
    expect(OSMGA_HW3D_E_DWGCTL, "a bad state in a LATER run is still "
                                "refused");
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

/*
 * TEXFILTER and TDUALSTAGE, derived from the client's flags.  Both
 * encoders write these registers, so the derivation is shared and this is
 * where it is pinned to the values version 9 has been shipping.
 */
static void
texRegisters(void)
{
    unsigned long f, t;
    unsigned long base = OSMGA_HW3D_TEXFILTER_ALPHA |
                         OSMGA_HW3D_TEXFILTER_FTHRES1;

    f = osmgaHW3DTexFilter(0UL);
    check(f == base, "no filter request is nearest both ways");

    f = osmgaHW3DTexFilter(OSMGA_HW3D_TEXF_BILIN);
    check(f == (base | 0x20UL), "BILIN sets the MAGNIFICATION field");

    f = osmgaHW3DTexFilter(OSMGA_HW3D_TEXF_BILINMIN);
    check(f == (base | 0x02UL), "BILINMIN sets the MINIFICATION field");

    f = osmgaHW3DTexFilter(OSMGA_HW3D_TEXF_BILIN | OSMGA_HW3D_TEXF_BILINMIN);
    check(f == (base | 0x22UL), "the two fields are independent");

    /* The diagnostic mipmap selector wins the minification field. */
    f = osmgaHW3DTexFilter(OSMGA_HW3D_TEXF_BILINMIN |
                           (OSMGA_HW3D_TEXF_MINMODE_MM8S
                            << OSMGA_HW3D_TEXF_MINMODE_SHIFT));
    check(f == (base | OSMGA_HW3D_TEXF_MINMODE_MM8S),
          "a mipmap mode wins the minification field over BILINMIN");

    t = osmgaHW3DTexDualStage(0UL, 0);
    check(t == OSMGA_HW3D_TDS_COLOR_MUL,
          "an untextured primitive multiplies the colour through");

    t = osmgaHW3DTexDualStage(0UL, 1);
    check(t == OSMGA_HW3D_TDS_ALPHA_ARG2,
          "a texture without its own alpha takes the interpolated one");

    t = osmgaHW3DTexDualStage(OSMGA_HW3D_TEXF_TEXALPHA, 1);
    check(t == 0UL, "a texture with its own alpha takes ARG1, which is zero");

    t = osmgaHW3DTexDualStage(OSMGA_HW3D_TEXF_MODULATE, 1);
    check(t == (OSMGA_HW3D_TDS_COLOR_MUL | OSMGA_HW3D_TDS_ALPHA_ARG2),
          "modulate multiplies the colour and keeps the fragment alpha");

    t = osmgaHW3DTexDualStage(OSMGA_HW3D_TEXF_MODULATE |
                              OSMGA_HW3D_TEXF_TEXALPHA, 1);
    check(t == (OSMGA_HW3D_TDS_COLOR_MUL | OSMGA_HW3D_TDS_ALPHA_MUL),
          "modulate with a texture alpha multiplies both");
}

/*
 * The destination against the window it must stay inside.  Version 9 had
 * this inline in the submit path and the drafted version 10 path skipped
 * it, which is why it is here now: the same declaration reaches the same
 * registers from both contracts.
 *
 * The case worth having is the one the comparison is written to survive --
 * a height whose product with the pitch overflows a 32-bit multiply.
 */
static void
destFits(void)
{
    unsigned long ws = 4UL * 1024UL * 1024UL;
    unsigned long we = 5UL * 1024UL * 1024UL;

    check(osmgaHW3DDestFits(ws, 64UL, 64UL, 1024UL, ws, we) ==
          OSMGA_HW3D_OK, "a destination inside the window fits");
    check(osmgaHW3DDestFits(ws - 1UL, 64UL, 64UL, 1024UL, ws, we) ==
          OSMGA_HW3D_E_DSTORG, "an origin below the window is refused");
    check(osmgaHW3DDestFits(we, 64UL, 64UL, 1024UL, ws, we) ==
          OSMGA_HW3D_E_DSTORG, "an origin at the end is refused");
    check(osmgaHW3DDestFits(ws, 0UL, 64UL, 1024UL, ws, we) ==
          OSMGA_HW3D_E_DSTSIZE, "a width of nothing is refused");
    check(osmgaHW3DDestFits(ws, 64UL, 0UL, 1024UL, ws, we) ==
          OSMGA_HW3D_E_DSTSIZE, "a height of nothing is refused");

    /* The last row must fit: 1 MiB of window at a 1024 pixel pitch is
     * 256 rows, so 256 fits and 257 does not. */
    check(osmgaHW3DDestFits(ws, 64UL, 256UL, 1024UL, ws, we) ==
          OSMGA_HW3D_OK, "exactly filling the window fits");
    check(osmgaHW3DDestFits(ws, 64UL, 257UL, 1024UL, ws, we) ==
          OSMGA_HW3D_E_DSTSIZE, "one row past the window is refused");

    /* And a height that would overflow the multiply the check deliberately
     * does not form. */
    check(osmgaHW3DDestFits(ws, 64UL, 0x400000UL, 1024UL, ws, we) ==
          OSMGA_HW3D_E_DSTSIZE,
          "a height whose product overflows is refused, not wrapped");
    check(osmgaHW3DDestFits(ws, 0x100000UL, 1UL, 1024UL, ws, we) ==
          OSMGA_HW3D_E_DSTSIZE, "a width past the window is refused");
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
    texRegisters();
    destFits();

    if (failures == 0)
        printf("test-warp-batch: layout shared with version 9, every named "
               "defect refused with its own verdict, the state bounded, and "
               "the wrap policy holds (0 failing)\n");
    else
        printf("test-warp-batch: %d failing\n", failures);
    return failures != 0;
}
