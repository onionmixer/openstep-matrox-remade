/*
 * What (long) of a double actually yields on this FPU and compiler, for the
 * values the osmgaRound replacement must match: out of range both ways,
 * NaN, both infinities, and the exact boundaries.  And whether the guard
 * !(t >= LO && t < HI) really catches NaN at -O.
 */
#include <stdio.h>

static double mk(unsigned long hi, unsigned long lo)
{
    union { double d; unsigned long w[2]; } u;
    u.w[0] = lo; u.w[1] = hi;       /* little endian: w[1] is the high word */
    return u.d;
}

static long viaFloor(double t)
{
    extern double floor(double);
    return (long)floor(t);
}
static long viaTrunc(double t)
{
    return (long)t;
}
static int guardOut(double t)      /* 1 = the out-of-range branch taken */
{
    if (!(t >= -2147483648.0 && t < 2147483648.0))
        return 1;
    return 0;
}

int main(void)
{
    static struct { const char *what; double v; } c[14];
    int i;

    c[0].what = "2^31 exactly";       c[0].v = 2147483648.0;
    c[1].what = "2^31 - 0.25";        c[1].v = 2147483647.75;
    c[2].what = "-2^31 exactly";      c[2].v = -2147483648.0;
    c[3].what = "-2^31 - 0.5";        c[3].v = -2147483648.5;
    c[4].what = "-2^31 - 1";          c[4].v = -2147483649.0;
    c[5].what = "1e18";               c[5].v = 1e18;
    c[6].what = "-1e18";              c[6].v = -1e18;
    c[7].what = "+inf";               c[7].v = mk(0x7ff00000UL, 0UL);
    c[8].what = "-inf";               c[8].v = mk(0xfff00000UL, 0UL);
    c[9].what = "quiet NaN";          c[9].v = mk(0x7ff80000UL, 0UL);
    c[10].what = "signalling NaN";    c[10].v = mk(0x7ff00000UL, 1UL);
    c[11].what = "-NaN";              c[11].v = mk(0xfff80000UL, 0UL);
    c[12].what = "-0.5";              c[12].v = -0.5;
    c[13].what = "2147483646.5";      c[13].v = 2147483646.5;

    printf("%-18s %11s %11s  guard\n", "value", "(long)floor", "(long)t");
    for (i = 0; i < 14; i++)
        printf("%-18s  0x%08lx  0x%08lx  %s\n", c[i].what,
               (unsigned long)viaFloor(c[i].v),
               (unsigned long)viaTrunc(c[i].v),
               guardOut(c[i].v) ? "OUT" : "in");
    return 0;
}
