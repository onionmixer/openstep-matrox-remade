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
      0x00000000UL, 0x00000000UL, 0x00000000UL, 0x3F800000UL, 0x00000000UL,
      0x00000000UL, 0x00000000UL },
    { 256L, 512L, 8388608UL, 1.0, 1.0, 255UL, 128UL, 64UL, 255UL, 0.25, 0.75,
      0x3F800000UL, 0x40000000UL, 0x3F000000UL, 0x3F800000UL, 0xFFFF8040UL,
      0x3E800000UL, 0x3F400000UL },
    { 100L, 200L, 16776960UL, 1.0, 1.0, 1UL, 2UL, 3UL, 4UL, 0.5, 0.5,
      0x3EC80000UL, 0x3F480000UL, 0x3F7FFF00UL, 0x3F800000UL, 0x04010203UL,
      0x3F000000UL, 0x3F000000UL },
    { -2048L, 4096L, 256UL, 0.5, 2.0, 10UL, 20UL, 30UL, 40UL, 0.125, 0.875,
      0xC1000000UL, 0x41800000UL, 0x37800000UL, 0x3F800000UL, 0x280A141EUL,
      0x3D800000UL, 0x3EE00000UL },
    { 2097152L, -2097152L, 3160320UL, 4.0, 32.0, 99UL, 8UL, 7UL, 6UL, 1.0, 2.0,
      0x46000000UL, 0xC6000000UL, 0x3E40E400UL, 0x43000000UL, 0x06630807UL,
      0x3D000000UL, 0x3D800000UL },
    { 333L, 777L, 13906176UL, 0.25, 0.5, 11UL, 22UL, 33UL, 44UL, 0.3, 0.7,
      0x3FA68000UL, 0x40424000UL, 0x3F543100UL, 0x3E000000UL, 0x2C0B1621UL,
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
        if (OSMGAMesaBuildWarpVertex(&mv, &mt, &out) != 0) {
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
                                     &out) != 0) {
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
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a coordinate past the bound is refused", 0UL, 0UL);
    fill(0); mv.y = -8193L * 256L;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "the bound is symmetric", 0UL, 0UL);
    fill(0); mv.qw = 0.0;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a zero weight is refused", 0UL, 0UL);
    fill(0); mv.qw = -1.0;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a negative weight is refused", 0UL, 0UL);
    fill(0); mv.tq = 0.0;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a zero texture divisor is refused", 0UL, 0UL);
    fill(0); mv.qw = 1.0; mv.tq = 1024.0;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a weight past the converted Q ceiling is refused", 0UL, 0UL);
    fill(0); mv.qw = 0.001; mv.tq = 1.0;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a weight below the converted Q floor is refused", 0UL, 0UL);
    /* z cannot leave [0,1] by construction, but the check has to be there
     * for the day the vertex changes scale. */
    fill(0); mv.z = 65536UL * 256UL * 2UL;
    check(OSMGAMesaBuildWarpVertex(&mv, &mt, &out) ==
          OSMGA_MESA_TRI_UNSUPPORTED,
          "a depth past the buffer's range is refused", 0UL, 0UL);

    /* And a vertex with no texture leaves the coordinates alone rather
     * than dividing by a divisor nobody set. */
    fill(1);
    check(OSMGAMesaBuildWarpVertex(&mv, (const OSMGAMesaTex *)0, &out) == 0,
          "an untextured vertex is built", 0UL, 0UL);
    check((unsigned long)out.tu0 == 0UL && (unsigned long)out.tv0 == 0UL,
          "an untextured vertex carries no texture coordinates",
          (unsigned long)out.tu0, 0UL);
}

int
main(void)
{
    table();
    depthRange();
    refusals();

    if (failures == 0)
        printf("test-mesa-warp: the vertex conversion matches an independent "
               "table and all 65536 depth codes round trip (0 failing)\n");
    else
        printf("test-mesa-warp: %d failing\n", failures);
    return failures != 0;
}
