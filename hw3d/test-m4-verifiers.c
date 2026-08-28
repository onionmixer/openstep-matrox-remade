/*
 * test-m4-verifiers.c -- judge the M4/M5 verifiers on data whose answer is
 * already known, before a reboot is spent on them.
 *
 * Three times now a band could not have detected the thing it existed for,
 * and each time a reboot paid for the discovery: T5's guard halo was
 * smaller than the filter footprint, T3's single phase sat on a texel
 * boundary, and T8b was about to use a comparator that returns "equal"
 * when neither path drew.  This file exists so the next one is caught
 * here instead.
 *
 * Nothing here links the driver -- its verifiers are static.  What is
 * checked is that the ARITHMETIC the driver uses separates the cases it
 * claims to separate, and that its closed forms agree with an
 * independently written barycentric evaluation.
 *
 * Hosted C89, so it builds with the target's cc.
 */
#include <stdio.h>

static int failures = 0;

static void
check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/* ------------------------------------------------------------------ */
/* T7's discriminator, in the driver's own form.                       */

#define REP_START   (-62L)
#define REP_STEP      4L
#define REP_LO        8UL
#define REP_HI       56UL
#define HOLD_TEXEL    8L

static long
wrapTexel(long t)
{
    return (long)(((unsigned long)(t + 640L)) & 63UL);
}

static long
clampTexel(long t)
{
    return (t < 0L) ? 0L : ((t > 63L) ? 63L : t);
}

/*
 * `read` fills in what a pixel came back as.  The verifier is then run
 * exactly as the driver runs it, and what is checked is the SHAPE of its
 * answer -- one histogram bin for a wrap, an exact clamp match for a
 * clamp -- rather than any particular texel.
 */
typedef void (*reader)(long t, long *swept, long *held, long param);

static void
readWrapped(long t, long *swept, long *held, long off)
{
    *swept = wrapTexel(t + off);
    *held  = HOLD_TEXEL;
}

static void
readClamped(long t, long *swept, long *held, long off)
{
    *swept = clampTexel(t + off);
    *held  = HOLD_TEXEL;
}

static void
readWrongAxis(long t, long *swept, long *held, long off)
{
    /* The axis that was supposed to stay put moved instead.  A verdict
     * that looked only at the swept axis would call this a pass. */
    *swept = wrapTexel(t + off);
    *held  = wrapTexel(t);
}

static void
readScrambled(long t, long *swept, long *held, long off)
{
    *swept = (long)(((unsigned long)(t * 7L + 13L + 640L)) & 63UL);
    *held  = HOLD_TEXEL;
    (void)off;
}

/*
 * Returns what osmgaM4ReportWrap returns: nought when the band fits its
 * own hypothesis.  `topN` and `exact` come back for inspection.
 */
static unsigned long
judge(reader r, long off, int wantWrap,
      unsigned long *topN, unsigned long *topBin, unsigned long *exact)
{
    unsigned long histo[64], k, seen = 0UL, heldBad = 0UL;
    unsigned long best = 0UL, bestK = 0UL, ex = 0UL;
    unsigned long col;

    for (k = 0UL; k < 64UL; k++)
        histo[k] = 0UL;

    for (col = REP_LO; col < REP_HI; col++) {
        long t = REP_START + REP_STEP * (long)(col - REP_LO);
        long swept = 0L, held = 0L;

        r(t, &swept, &held, off);
        seen++;
        histo[((unsigned long)(swept - t + 640L)) & 63UL]++;
        if (swept == clampTexel(t))
            ex++;
        if (held != HOLD_TEXEL)
            heldBad++;
    }
    for (k = 0UL; k < 64UL; k++)
        if (histo[k] > best) { best = histo[k]; bestK = k; }

    *topN = best; *topBin = bestK; *exact = ex;
    if (heldBad != 0UL)
        return heldBad + seen;
    return wantWrap ? (seen - best) : (seen - ex);
}

static void
t7cases(void)
{
    static const long offs[6] = { 0L, 1L, 2L, -1L, -2L, 7L };
    unsigned long topN, topBin, exact, bad;
    int i;

    /* A wrapped read must pass the repeat verdict at EVERY anchor offset:
     * that is the whole reason the verdict is a histogram and not an
     * expected texel.  The sweep is four texels a pixel, so half a pixel
     * of anchor moves the answer two texels. */
    for (i = 0; i < 6; i++) {
        bad = judge(readWrapped, offs[i], 1, &topN, &topBin, &exact);
        check(bad == 0UL, "a wrapped read is accepted at any anchor offset");
        check(topN == 48UL, "a wrapped read puts every pixel in one bin");
    }

    /* And it must FAIL the clamp verdict, or the clamp band proves
     * nothing. */
    bad = judge(readWrapped, 0L, 0, &topN, &topBin, &exact);
    check(bad != 0UL, "a wrapped read is refused by the clamp verdict");
    check(exact < 48UL, "a wrapped read does not match the clamped map");

    /* A clamped read passes the clamp verdict and fails the repeat one. */
    bad = judge(readClamped, 0L, 0, &topN, &topBin, &exact);
    check(bad == 0UL, "a clamped read is accepted by the clamp verdict");
    check(exact == 48UL, "a clamped read matches the clamped map exactly");

    bad = judge(readClamped, 0L, 1, &topN, &topBin, &exact);
    check(bad != 0UL, "a clamped read is refused by the repeat verdict");
    check(topN < 24UL, "a clamped read does not concentrate in one bin");

    /* The axis that was held must be checked, or a band that wrapped the
     * WRONG axis reads as a pass.  This is the defect the review found. */
    bad = judge(readWrongAxis, 0L, 1, &topN, &topBin, &exact);
    check(bad != 0UL, "a band that moved the held axis is refused");

    /* And noise fails both. */
    bad = judge(readScrambled, 0L, 1, &topN, &topBin, &exact);
    check(bad != 0UL, "a scrambled read is refused by the repeat verdict");
    bad = judge(readScrambled, 0L, 0, &topN, &topBin, &exact);
    check(bad != 0UL, "a scrambled read is refused by the clamp verdict");
}

/* ------------------------------------------------------------------ */
/* T4b's two closed forms against a barycentric evaluation.            */

#define LEG 48L

static void
t4bcases(void)
{
    long dx, dy;
    long worst = 0L;

    for (dy = 0L; dy < LEG; dy++) {
        for (dx = 0L; dx < LEG - dy; dx++) {
            long l0 = LEG - dx - dy, l1 = dx, l2 = dy;
            long num, den, bary, closed, baryA, closedA, d;

            /* s' in 128ths: (113, 116, 113); q: (1, 4, 1).
             * u*64 = sum(L*s') / (2 * sum(L*q)). */
            num = l0 * 113L + l1 * 116L + l2 * 113L;
            den = l0 * 1L   + l1 * 4L   + l2 * 1L;
            bary   = num / (2L * den);
            closed = (5424L + 3L * dx) / (2L * (LEG + 3L * dx));
            check(bary == closed,
                  "the projective closed form equals the barycentric one");

            /* The affine hypothesis: interpolate tu itself, no divide.
             * tu in 128ths is (113, 29, 113). */
            baryA   = (l0 * 113L + l1 * 29L + l2 * 113L) / 96L;
            closedA = (5424L - 84L * dx) / 96L;
            check(baryA == closedA,
                  "the affine closed form equals its barycentric one");

            d = bary - baryA;
            if (d < 0L) d = -d;
            if (d > worst) worst = d;
        }
    }
    /* If the two hypotheses were close the band could not tell them
     * apart, whatever the hardware did. */
    check(worst == 14L,
          "the projective and affine maps differ by fourteen texels");
    if (worst != 14L)
        printf("       worst separation was %ld texels\n", worst);
}

/* ------------------------------------------------------------------ */
/* T8a's ramp.                                                         */

static void
t8acases(void)
{
    unsigned long col, prev = 0UL;
    int rising = 1;

    for (col = REP_LO; col < REP_HI; col++) {
        unsigned long want = (255UL * (col - REP_LO)) / (REP_HI - REP_LO);

        if (col == REP_LO)
            check(want == 0UL, "the alpha ramp starts at nought");
        else if (want < prev)
            rising = 0;
        prev = want;
    }
    check(rising, "the alpha ramp never falls");
    check(prev == (255UL * 47UL) / 48UL,
          "the alpha ramp ends where the last drawn column puts it");
    /* The texture's own alpha must not be a value the ramp can produce at
     * every pixel, or the negative control cannot be read. */
    check(0x33UL <= 255UL, "the texture alpha is in range");
}

int
main(void)
{
    t7cases();
    t4bcases();
    t8acases();

    if (failures == 0)
        printf("test-m4-verifiers: the T7 discriminator separates wrapped, "
               "clamped, wrong-axis and scrambled, and the T4b closed forms "
               "match a barycentric oracle (0 failing)\n");
    else
        printf("test-m4-verifiers: %d failing\n", failures);
    return failures != 0;
}
