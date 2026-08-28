/*
 * test-f32-validate.c -- the kernel's FPU-free float judgements, against
 * the host's actual floating point.
 *
 * The kernel cannot use the FPU, so every question about a WARP vertex is
 * asked of the bit pattern.  This host has an FPU, so it can ask the same
 * questions the ordinary way and insist the two agree -- which is the only
 * way to know that "the bit patterns order the same way the values do" is
 * true rather than merely plausible.
 *
 * Hosted C89.
 */
#include <stdio.h>
#include <string.h>
#include "OpenStepMGAHW3D.h"

static int failures = 0;

static void
check(int ok, const char *what, unsigned long p)
{
    if (!ok) {
        printf("FAIL: %s (pattern %08lx)\n", what, p);
        failures++;
    }
}

static unsigned long
bits(float f)
{
    unsigned long p = 0UL;
    unsigned int u;

    memcpy(&u, &f, sizeof u);
    p = (unsigned long)u;
    return p;
}

static float
value(unsigned long p)
{
    unsigned int u = (unsigned int)p;
    float f;

    memcpy(&f, &u, sizeof f);
    return f;
}

/* A NaN and the infinities, built without <math.h> so the target's
 * headers are not part of the question. */
#define P_QNAN   0x7FC00000UL
#define P_SNAN   0x7F800001UL
#define P_NEGNAN 0xFFC00000UL
#define P_INF    0x7F800000UL
#define P_NEGINF 0xFF800000UL
#define P_ZERO   0x00000000UL
#define P_NEGZERO 0x80000000UL
#define P_DENORM 0x00000001UL
#define P_NEGDEN 0x80000001UL

static void
finiteCases(void)
{
    static const unsigned long bad[5] = { P_QNAN, P_SNAN, P_NEGNAN,
                                          P_INF, P_NEGINF };
    static const unsigned long good[6] = { P_ZERO, P_NEGZERO, P_DENORM,
                                           0x3F800000UL, 0xBF800000UL,
                                           0x7F7FFFFFUL };
    int i;

    for (i = 0; i < 5; i++)
        check(!osmgaHW3DF32Finite(bad[i]), "NaN and Inf are not finite",
              bad[i]);
    for (i = 0; i < 6; i++)
        check(osmgaHW3DF32Finite(good[i]) != 0, "a number is finite",
              good[i]);
}

static void
posNormalCases(void)
{
    static const unsigned long bad[9] = { P_ZERO, P_NEGZERO, P_DENORM,
                                          P_NEGDEN, P_QNAN, P_INF,
                                          P_NEGINF, 0xBF800000UL,
                                          0xC0000000UL };
    static const unsigned long good[4] = { 0x00800000UL,  /* smallest normal */
                                           0x3F800000UL,  /* 1.0 */
                                           0x40800000UL,  /* 4.0 */
                                           0x7F7FFFFFUL };
    int i;

    for (i = 0; i < 9; i++)
        check(!osmgaHW3DF32PosNormal(bad[i]),
              "zero, denormal, negative, NaN and Inf are not positive normals",
              bad[i]);
    for (i = 0; i < 4; i++)
        check(osmgaHW3DF32PosNormal(good[i]) != 0,
              "a positive normal is one", good[i]);
}

/*
 * The sweep that matters: walk the patterns and compare the bit-pattern
 * answer against the host's own float comparison, for every judgement.
 * Stepping by a large prime covers the space in every exponent without
 * enumerating four billion values.
 */
static void
sweep(void)
{
    unsigned long p;
    unsigned long step = 1048573UL;      /* prime, so the walk does not
                                          * settle into one exponent */
    unsigned long n = 0UL;

    for (p = 0UL; p < 0xFFFFFFFFUL - step; p += step) {
        float f = value(p);
        int finite = osmgaHW3DF32Finite(p);
        int hostFinite = (f == f && f - f == 0.0f) ? 1 : 0;

        n++;
        check(finite == hostFinite,
              "finiteness agrees with the host's own arithmetic", p);
        if (!finite)
            continue;

        check(osmgaHW3DF32InUnit(p) == ((f >= 0.0f && f <= 1.0f) ? 1 : 0),
              "the unit range agrees with the host", p);
        check(osmgaHW3DF32PosNormal(p) ==
                  ((f > 0.0f && f >= value(0x00800000UL)) ? 1 : 0),
              "positive-normal agrees with the host", p);
        check(osmgaHW3DF32AbsAtMost(p, OSMGA_HW3D_F32_COORD) ==
                  (((f < 0.0f ? -f : f) <= 8192.0f) ? 1 : 0),
              "the coordinate bound agrees with the host", p);
        check(osmgaHW3DF32Between(p, OSMGA_HW3D_F32_RHW_MIN,
                                  OSMGA_HW3D_F32_RHW_MAX) ==
                  ((f >= 0.125f && f <= 128.0f) ? 1 : 0),
              "the rhw band agrees with the host", p);
    }
    printf("  swept %lu patterns across every exponent\n", n);
}

/*
 * The sweep above is thin -- one pattern in a million -- and an off-by-one
 * in a comparison does not live in the middle of a range.  It lives at the
 * edge.  So every named bound is walked densely on both sides, in both
 * signs, which is where "<=" against "<" would show.
 */
static void
edges(void)
{
    static const unsigned long around[6] = {
        0x3F800000UL,   /* 1.0, the unit bound          */
        0x46000000UL,   /* 8192.0, the coordinate bound */
        0x3E000000UL,   /* 0.125, the rhw floor         */
        0x43000000UL,   /* 128.0, the rhw ceiling       */
        0x00800000UL,   /* the smallest normal          */
        0x00000000UL    /* zero                         */
    };
    unsigned long n = 0UL;
    int i;
    long d;

    for (i = 0; i < 6; i++) {
        for (d = -64L; d <= 64L; d++) {
            unsigned long p = around[i] + (unsigned long)d;
            float f;

            if (d < 0L && around[i] < (unsigned long)(-d))
                continue;
            f = value(p);
            n++;
            if (!osmgaHW3DF32Finite(p))
                continue;
            check(osmgaHW3DF32InUnit(p) ==
                      ((f >= 0.0f && f <= 1.0f) ? 1 : 0),
                  "the unit bound holds at its own edge", p);
            check(osmgaHW3DF32AbsAtMost(p, OSMGA_HW3D_F32_COORD) ==
                      (((f < 0.0f ? -f : f) <= 8192.0f) ? 1 : 0),
                  "the coordinate bound holds at its own edge", p);
            check(osmgaHW3DF32Between(p, OSMGA_HW3D_F32_RHW_MIN,
                                      OSMGA_HW3D_F32_RHW_MAX) ==
                      ((f >= 0.125f && f <= 128.0f) ? 1 : 0),
                  "the rhw band holds at its own edges", p);
            check(osmgaHW3DF32PosNormal(p) ==
                      ((f > 0.0f && f >= value(0x00800000UL)) ? 1 : 0),
                  "positive-normal holds at the denormal boundary", p);

            /* and the same patterns with the sign bit on */
            p |= 0x80000000UL;
            f = value(p);
            n++;
            if (!osmgaHW3DF32Finite(p))
                continue;
            check(osmgaHW3DF32InUnit(p) ==
                      ((f >= 0.0f && f <= 1.0f) ? 1 : 0),
                  "the unit bound refuses negatives at the edge", p);
            check(osmgaHW3DF32AbsAtMost(p, OSMGA_HW3D_F32_COORD) ==
                      (((f < 0.0f ? -f : f) <= 8192.0f) ? 1 : 0),
                  "the coordinate bound is symmetric at the edge", p);
            check(osmgaHW3DF32Between(p, OSMGA_HW3D_F32_RHW_MIN,
                                      OSMGA_HW3D_F32_RHW_MAX) == 0,
                  "the rhw band refuses every negative", p);
            check(osmgaHW3DF32PosNormal(p) == 0,
                  "positive-normal refuses every negative", p);
        }
    }
    printf("  walked %lu patterns either side of every named bound\n", n);
}

/* The named bounds must be the floats the header says they are, or the
 * comments are the only thing keeping them right. */
static void
boundCases(void)
{
    check(bits(1.0f)    == OSMGA_HW3D_F32_ONE,     "1.0f",     bits(1.0f));
    check(bits(8192.0f) == OSMGA_HW3D_F32_COORD,   "8192.0f",  bits(8192.0f));
    check(bits(0.125f)  == OSMGA_HW3D_F32_RHW_MIN, "0.125f",   bits(0.125f));
    check(bits(128.0f)  == OSMGA_HW3D_F32_RHW_MAX, "128.0f",   bits(128.0f));
    /* And the rhw band really is the Q range converted. */
    check(bits((float)OSMGA_HW3D_Q_MIN / 65536.0f) ==
              OSMGA_HW3D_F32_RHW_MIN,
          "the rhw floor is Q_MIN / 65536", OSMGA_HW3D_F32_RHW_MIN);
    check(bits((float)OSMGA_HW3D_Q_MAX / 65536.0f) ==
              OSMGA_HW3D_F32_RHW_MAX,
          "the rhw ceiling is Q_MAX / 65536", OSMGA_HW3D_F32_RHW_MAX);
}

int
main(void)
{
    finiteCases();
    posNormalCases();
    boundCases();
    sweep();
    edges();

    if (failures == 0)
        printf("test-f32-validate: the integer judgements agree with real "
               "floating point everywhere they were asked (0 failing)\n");
    else
        printf("test-f32-validate: %d failing\n", failures);
    return failures != 0;
}
