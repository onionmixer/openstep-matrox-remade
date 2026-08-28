/*
 * test-hw3d-secrange.c -- host test for the secondary DMA range check.
 *
 * Every bound on both sides, the same rule the batch validator's suite
 * states: checking only that good input passes misses every off-by-one.
 *
 * Build 32-bit -- unsigned long is four bytes on the target and eight on a
 * modern host, and this code is about what fits in thirty-two bits:
 *
 *   cc -O -Wall -o test-hw3d-secrange test-hw3d-secrange.c OpenStepMGAHW3D.c
 */
#include <stdio.h>
#include "OpenStepMGAHW3D.h"

static int failed;
static unsigned long ringPhys = 0x00300000UL;   /* plausible IOMallocLow base */

static void
expect(const char *what, unsigned long s, unsigned long e, int want)
{
    int got = osmgaHW3DSecRange(ringPhys, s, e);
    if (got != want) {
        printf("  FAIL  %-52s wanted %d got %d\n", what, want, got);
        failed++;
    } else {
        printf("  ok    %-52s %d\n", what, got);
    }
}

int
main(void)
{
    unsigned long base = ringPhys + OSMGA_HW3D_SEC_OFF;
    unsigned long P    = OSMGA_HW3D_SEC_PACKET;
    unsigned long N    = OSMGA_HW3D_SEC_BYTES;

    printf("secondary region: +%lu, %lu bytes, %lu packets of %lu\n",
           OSMGA_HW3D_SEC_OFF, N, N / P, P);
    printf("list region:      %lu bytes, encoder worst case %lu\n",
           OSMGA_HW3D_LIST_BYTES, OSMGA_HW3D_ENC_DWORDS * 4UL);

    printf("the smallest and largest legal ranges\n");
    expect("one packet at the base",            base,        base + P,      1);
    expect("the whole region",                  base,        base + N,      1);
    expect("the last packet",                   base + N - P, base + N,     1);

    printf("length\n");
    expect("zero length",                       base,        base,          0);
    expect("one packet less a dword",           base,        base + P - 4UL, 0);
    expect("one packet plus a dword",           base,        base + P + 4UL, 0);
    expect("two packets",                       base,        base + 2UL * P, 1);
    expect("the whole region plus a packet",    base,        base + N + P,  0);

    printf("position\n");
    expect("one dword below the base",          base - 4UL,  base + P,      0);
    expect("one packet below the base",         base - P,    base,          0);
    expect("last packet, one dword too far",    base + N - P + 4UL,
                                                base + N + 4UL,             0);
    expect("starting past the end",             base + N,    base + N + P,  0);

    printf("alignment\n");
    expect("start one byte off",                base + 1UL,  base + 1UL + P, 0);
    expect("start two bytes off",               base + 2UL,  base + 2UL + P, 0);

    printf("the region base itself must not wrap\n");
    {
        unsigned long high = 0xFFFFFFFFUL - OSMGA_HW3D_SEC_OFF + 1UL;
        int got = osmgaHW3DSecRange(high, 0UL, P);
        if (got != 0) { printf("  FAIL  a ring base that wraps\n"); failed++; }
        else            printf("  ok    a ring base that wraps                           0\n");
    }

    printf("\n%s (%d failing)\n",
           failed ? "SOME CASES ARE WRONG" : "all cases behave as specified",
           failed);
    return failed ? 1 : 0;
}
