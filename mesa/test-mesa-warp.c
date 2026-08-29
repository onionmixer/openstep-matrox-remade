/*
 * test-mesa-warp.c -- the Mesa-to-WARP vertex conversion, against a table
 * this code did not compute.
 *
 * The expected bit patterns come from python, written into the table
 * below.  Re-deriving them here with the same expressions the builder uses
 * would test that the builder equals itself; the point is that it equals
 * something written independently.
 *
 * The depth constant is the one worth guarding.  The engine multiplies a
 * normalised z by 65536 and saturates -- measured in M4 -- so a Mesa depth
 * code divides by 65536 and not by 65535, which is what the reference DRI
 * does.  Case three carries the largest code there is: if the constant
 * ever becomes the reference's, that row stops matching.
 *
 * Hosted C89.
 */
#include <stdio.h>
#include <string.h>
#include "OpenStepMGAMesaWarp.h"

static int failures = 0;

static void
check(int ok, const char *what, unsigned long got, unsigned long want)
{
    if (!ok) {
        printf("FAIL: %s -- got %08lx wanted %08lx\n", what, got, want);
        failures++;
    }
}

static const struct { long x, y; unsigned long z;
                     double qw, tq; unsigned long r, g, b, a;
                     double s, tc;
                     unsigned long ex, ey, ez, erhw, ediff, etu, etv;
} cases[] = {
    { 0L, 0L, 0UL, 1.0, 1.0, 0UL, 0UL, 0UL, 0UL, 0.0, 0.0,
      0xBF000000UL, 0xBF000000UL, 0x00000000UL, 0x3F800000UL, 0x00000000UL,
      0x00000000UL, 0x00000000UL },
    { 256L, 512L, 8388608UL, 1.0, 1.0, 255UL, 128UL, 64UL, 255UL, 0.25, 0.75,
      0x3F000000UL, 0x3FC00000UL, 0x3F000000UL, 0x3F800000UL, 0xFFFF8040UL,
      0x3E800000UL, 0x3F400000UL },
    { 100L, 200L, 16776960UL, 1.0, 1.0, 1UL, 2UL, 3UL, 4UL, 0.5, 0.5,
      0xBDE00000UL, 0x3E900000UL, 0x3F7FFF00UL, 0x3F800000UL, 0x04010203UL,
      0x3F000000UL, 0x3F000000UL },
    { -2048L, 4096L, 256UL, 0.5, 2.0, 10UL, 20UL, 30UL, 40UL, 0.125, 0.875,
      0xC1080000UL, 0x41780000UL, 0x37800000UL, 0x3F800000UL, 0x280A141EUL,
      0x3D800000UL, 0x3EE00000UL },
    /* The two ends of the accepted window, which the half-pixel MOVED:
     * raw 2097280 and -2097024 are what now convert to +-8192.0 exactly.
     * The old pair (2097152, -2097152) is kept below, in refusals(), where
     * the negative one is now refused -- which is the whole visible
     * consequence of the bias outside the picture itself. */
    { 2097280L, -2097024L, 3160320UL, 4.0, 32.0, 99UL, 8UL, 7UL, 6UL, 1.0, 2.0,
      0x46000000UL, 0xC6000000UL, 0x3E40E400UL, 0x43000000UL, 0x06630807UL,
      0x3D000000UL, 0x3D800000UL },
    { 333L, 777L, 13906176UL, 0.25, 0.5, 11UL, 22UL, 33UL, 44UL, 0.3, 0.7,
      0x3F4D0000UL, 0x40224000UL, 0x3F543100UL, 0x3E000000UL, 0x2C0B1621UL,
      0x3F19999AUL, 0x3FB33333UL }
};

static OSMGAMesaVertex mv;
static OSMGAMesaTex    mt;

static void
fill(int i)
{
    memset(&mv, 0, sizeof mv);
    memset(&mt, 0, sizeof mt);
    mv.x  = cases[i].x;
    mv.y  = cases[i].y;
    mv.z  = cases[i].z;
    mv.qw = cases[i].qw;
    mv.tq = cases[i].tq;
    mv.r  = cases[i].r;
    mv.g  = cases[i].g;
    mv.b  = cases[i].b;
    mv.a  = cases[i].a;
    mv.s  = cases[i].s;
    mv.tc = cases[i].tc;
}

static void
table(void)
{
    OSMGAHW3DVertex out;
    int i, n = (int)(sizeof cases / sizeof cases[0]);

    for (i = 0; i < n; i++) {
        fill(i);
        if (OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &out) != 0) {
            printf("FAIL: case %d was refused\n", i);
            failures++;
            continue;
        }
        check((unsigned long)out.x == cases[i].ex, "x",
              (unsigned long)out.x, cases[i].ex);
        check((unsigned long)out.y == cases[i].ey, "y",
              (unsigned long)out.y, cases[i].ey);
        check((unsigned long)out.z == cases[i].ez, "z",
              (unsigned long)out.z, cases[i].ez);
        check((unsigned long)out.rhw == cases[i].erhw, "rhw",
              (unsigned long)out.rhw, cases[i].erhw);
        check((unsigned long)out.diffuse == cases[i].ediff, "diffuse",
              (unsigned long)out.diffuse, cases[i].ediff);
        check((unsigned long)out.tu0 == cases[i].etu, "tu0",
              (unsigned long)out.tu0, cases[i].etu);
        check((unsigned long)out.tv0 == cases[i].etv, "tv0",
              (unsigned long)out.tv0, cases[i].etv);
        check((unsigned long)out.specular == 0UL, "specular",
              (unsigned long)out.specular, 0UL);
    }
}

/*
 * The whole depth code range, round tripped.  This is the guard on the
 * constant: with 65535 in place of 65536 the largest code comes back one
 * short, and with the vertex's own 1/256 dropped the fraction goes.
 */
static void
depthRange(void)
{
    OSMGAHW3DVertex out;
    unsigned long code;
    int wrong = 0;

    for (code = 0UL; code <= 65535UL; code++) {
        float f;
        unsigned int u;
        unsigned long back;

        fill(0);
        mv.z = code * 256UL;
        if (OSMGAMesaBuildWarpVertex(&mv, (const OSMGAMesaTex *)0,
                                     0.0, &out) != 0) {
            wrong++;
            continue;
        }
        u = (unsigned int)out.z;
        memcpy(&f, &u, sizeof f);
        back = (unsigned long)(f * 65536.0f);
        if (back != code)
            wrong++;
    }
    check(wrong == 0, "every depth code round trips through the engine's "
                      "own scale", (unsigned long)wrong, 0UL);
}

static void
refusals(void)
{
    OSMGAHW3DVertex out;

    fill(0); mv.x = 8193L * 256L;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a coordinate past the bound is refused", 0UL, 0UL);
    fill(0); mv.y = -8193L * 256L;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "the bound is symmetric", 0UL, 0UL);
    /*
     * And the window's own half-pixel move, both sides, stated as tests
     * rather than left to be discovered.  The bound is applied AFTER the
     * bias, so raw -8192*256 -- which used to convert to exactly -8192.0
     * and pass -- now converts to -8192.5 and is refused, while raw
     * 8192*256 + 128 converts to exactly 8192.0 and is accepted where it
     * used to be past the end.  A refusal is a decline and the triangle
     * takes the trapezoid path, so this costs a tier and not a picture.
     */
    fill(0); mv.x = -8192L * 256L;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "the half pixel moved the negative end in", 0UL, 0UL);
    fill(0); mv.x = 8192L * 256L + 128L;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &out) == 0 &&
          (unsigned long)out.x == 0x46000000UL,
          "and moved the positive end out",
          (unsigned long)out.x, 0x46000000UL);
    fill(0); mv.qw = 0.0;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a zero weight is refused", 0UL, 0UL);
    fill(0); mv.qw = -1.0;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a negative weight is refused", 0UL, 0UL);
    fill(0); mv.tq = 0.0;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a zero texture divisor is refused", 0UL, 0UL);
    fill(0); mv.qw = 1.0; mv.tq = 1024.0;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a weight past the converted Q ceiling is refused", 0UL, 0UL);
    fill(0); mv.qw = 0.001; mv.tq = 1.0;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a weight below the converted Q floor is refused", 0UL, 0UL);
    /* z cannot leave [0,1] by construction, but the check has to be there
     * for the day the vertex changes scale. */
    fill(0); mv.z = 65536UL * 256UL * 2UL;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a depth past the buffer's range is refused", 0UL, 0UL);

    /* And a vertex with no texture leaves the coordinates alone rather
     * than dividing by a divisor nobody set. */
    fill(1);
    check(OSMGAMesaBuildWarpVertex(&mv, (const OSMGAMesaTex *)0, 0.0, &out) == 0,
          "an untextured vertex is built", 0UL, 0UL);
    check((unsigned long)out.tu0 == 0UL && (unsigned long)out.tv0 == 0UL,
          "an untextured vertex carries no texture coordinates",
          (unsigned long)out.tu0, 0UL);
}

/*
 * The assembler, judged by the kernel's own validator.
 *
 * That is the strongest check available without hardware: the two halves
 * were written against the same invariant -- runs partition the vertices,
 * contiguous, in order, covering every one -- and if either drifts, a
 * batch the assembler built stops being one the kernel accepts.
 */
static OSMGAHW3DWarpBatch  wb;
static OSMGAHW3DLimits     lim;
static OSMGAMesaWarpBuilder wbuild;

/*
 * The batch state the runs will program.  The assembler does not fill it
 * in -- it assembles primitives, and the state is the caller's -- so the
 * test supplies it, and the validator now bounds it.
 */
static void
stateUp(void)
{
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
accepted(const char *what)
{
    unsigned long badRun = 0UL;
    int v;

    v = osmgaHW3DValidateWarp(&wb, &lim, &badRun);
    if (v != OSMGA_HW3D_OK) {
        printf("FAIL: %s -- the kernel refused an assembled batch, "
               "verdict %d run %lu\n", what, v, badRun);
        failures++;
    }
}

static void
assemble(void)
{
    OSMGAHW3DVertex v;
    unsigned long i;
    int r;

    fill(1);
    if (OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &v) != 0) {
        printf("FAIL: the assembler's own vertex was refused\n");
        failures++;
        return;
    }

    /* One state, many triangles: one run. */
    OSMGAMesaWarpReset(&wbuild, &wb);
    stateUp();
    for (i = 0UL; i < 40UL; i++) {
        r = OSMGAMesaWarpAdd(&wbuild, 0x000c4074UL, 0x00000001UL, &v, &v, &v);
        check(r == 0, "a triangle is taken", (unsigned long)r, 0UL);
    }
    check((unsigned long)wb.runCount == 1UL,
          "forty triangles under one state are one run",
          (unsigned long)wb.runCount, 1UL);
    check((unsigned long)wb.vtxCount == 120UL, "and 120 vertices",
          (unsigned long)wb.vtxCount, 120UL);
    accepted("one run");

    /* Alternating state: a run each, and the runs still partition. */
    OSMGAMesaWarpReset(&wbuild, &wb);
    stateUp();
    for (i = 0UL; i < OSMGA_HW3D_MAX_RUN; i++) {
        /* Two states that are both LEGAL -- TRAP with atype I and TRAP
         * with atype ZI.  Adding one to the word made opcode 5, which the
         * primitive rule now refuses, and the assembler's batches stopped
         * validating: the fixture was wrong, not the assembler. */
        r = OSMGAMesaWarpAdd(&wbuild,
                             ((i & 1UL) != 0UL) ? 0x000c4034UL : 0x000c4074UL,
                             0x00000001UL, &v, &v, &v);
        check(r == 0, "an alternating triangle is taken",
              (unsigned long)r, 0UL);
    }
    check((unsigned long)wb.runCount == OSMGA_HW3D_MAX_RUN,
          "alternating states make a run each",
          (unsigned long)wb.runCount, OSMGA_HW3D_MAX_RUN);
    accepted("alternating runs");

    /* One more state change has nowhere to go, and the batch is unharmed. */
    r = OSMGAMesaWarpAdd(&wbuild, 0x000c4076UL, 0x00000001UL, &v, &v, &v);
    check(r == OSMGA_MESA_WARP_FULL, "a run past the maximum is refused",
          (unsigned long)r, (unsigned long)OSMGA_MESA_WARP_FULL);
    check((unsigned long)wb.runCount == OSMGA_HW3D_MAX_RUN,
          "and the refusal changed nothing",
          (unsigned long)wb.runCount, OSMGA_HW3D_MAX_RUN);
    accepted("after a refused run");

    /* But the SAME state still fits, because it needs no new run. */
    r = OSMGAMesaWarpAdd(&wbuild, 0x000c4034UL, 0x00000001UL,
                         &v, &v, &v);
    check(r == 0, "the open run still takes triangles",
          (unsigned long)r, 0UL);
    accepted("after extending the open run");

    /* Fill to the vertex maximum. */
    OSMGAMesaWarpReset(&wbuild, &wb);
    stateUp();
    for (i = 0UL; i < OSMGA_HW3D_MAX_VTX / 3UL; i++) {
        r = OSMGAMesaWarpAdd(&wbuild, 0x000c4074UL, 0x00000001UL,
                             &v, &v, &v);
        check(r == 0, "a triangle below the vertex maximum is taken",
              (unsigned long)r, 0UL);
    }
    check((unsigned long)wb.vtxCount == OSMGA_HW3D_MAX_VTX,
          "the batch fills to its vertex maximum",
          (unsigned long)wb.vtxCount, OSMGA_HW3D_MAX_VTX);
    accepted("a full batch");
    r = OSMGAMesaWarpAdd(&wbuild, 0x000c4074UL, 0x00000001UL, &v, &v, &v);
    check(r == OSMGA_MESA_WARP_FULL, "a full batch takes no more",
          (unsigned long)r, (unsigned long)OSMGA_MESA_WARP_FULL);
    accepted("after a refused triangle");

    /* An empty batch is not a valid submission, and the assembler does not
     * pretend otherwise -- the caller must not send one. */
    OSMGAMesaWarpReset(&wbuild, &wb);
    stateUp();
    lim.batchBytes = OSMGA_HW3D_BATCH_BYTES;
    check(osmgaHW3DValidateWarp(&wb, &lim, (unsigned long *)0) !=
              OSMGA_HW3D_OK,
          "an empty batch is not a submission", 0UL, 0UL);
}

/*
 * glPolygonOffset, which the builder used not to take at all.
 *
 * The offset arrives in depth CODES and v->z is in 256ths of one, so it is
 * multiplied by 256 before the shared scale.  A shifted vertex outside
 * [0,1] is REFUSED rather than clamped, which is what the trapezoid
 * builder does: clamping would draw something neither path draws.
 *
 * Expected patterns from python, not from re-writing the expression here.
 */
static void
offsets(void)
{
    OSMGAHW3DVertex out;

    fill(0); mv.z = 32768UL * 256UL;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 100.0, &out) == 0 &&
          (unsigned long)out.z == 0x3F006400UL,
          "a positive offset shifts the depth", (unsigned long)out.z,
          0x3F006400UL);

    fill(0); mv.z = 32768UL * 256UL;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, -100.0, &out) == 0 &&
          (unsigned long)out.z == 0x3EFF3800UL,
          "a negative offset shifts the other way", (unsigned long)out.z,
          0x3EFF3800UL);

    fill(0); mv.z = 32768UL * 256UL;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 0.0, &out) == 0 &&
          (unsigned long)out.z == 0x3F000000UL,
          "no offset leaves the depth alone", (unsigned long)out.z,
          0x3F000000UL);

    /* Off the bottom: refused, not clamped to nought. */
    fill(0); mv.z = 0UL;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, -1.0, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "an offset below the buffer is refused", 0UL, 0UL);

    /* Off the top: the largest code plus two is past one. */
    fill(0); mv.z = 65535UL * 256UL;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 2.0, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "an offset above the buffer is refused", 0UL, 0UL);

    /* And exactly at the top is admitted: T4c measured that z = 1
     * saturates to 65535 rather than wrapping. */
    fill(0); mv.z = 65535UL * 256UL;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, 1.0, &out) == 0 &&
          (unsigned long)out.z == 0x3F800000UL,
          "an offset landing exactly on one is admitted",
          (unsigned long)out.z, 0x3F800000UL);
}

int
main(void)
{
    table();
    depthRange();
    refusals();
    offsets();
    assemble();

    if (failures == 0)
        printf("test-mesa-warp: the vertex conversion matches an independent "
               "table, all 65536 depth codes round trip, polygon offset shifts "
               "and refuses, the half pixel is subtracted and moved the "
               "accepted window with it, and every assembled "
               "batch is accepted by the kernel validator (0 failing)\n");
    else
        printf("test-mesa-warp: %d failing\n", failures);
    return failures != 0;
}
