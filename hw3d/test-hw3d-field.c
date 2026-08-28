/*
 * test-hw3d-field.c -- host test for the checked field encoding.
 *
 * It checks the WORD, not only the verdict.  A test that asks "was this
 * refused" proves the range half and nothing about the encoding half, and
 * the encoding half is the one that reaches the register.
 *
 * The contract under test (see the header): an unsigned batch field carries
 * a SIGN-EXTENDED signed value, so 0x00ffffff means +16,777,215 and does
 * not fit a signed 24-bit field, while 0xffffffff means -1 and does.
 *
 *   cc -O -Wall -o test-hw3d-field test-hw3d-field.c OpenStepMGAHW3D.c
 */
#include <stdio.h>
#include "OpenStepMGAHW3D.h"

static int failed;

static void
word(const char *what, long v, unsigned bits, int wantOk, unsigned long wantW)
{
    unsigned long got = 0xDEADBEEFUL;
    int ok = osmgaHW3DField(v, bits, &got);

    if (ok != wantOk || (wantOk && got != wantW)) {
        printf("  FAIL  %-46s ok=%d want %d, word %08lx want %08lx\n",
               what, ok, wantOk, got, wantW);
        failed++;
    } else if (wantOk) {
        printf("  ok    %-46s %08lx\n", what, got);
    } else {
        printf("  ok    %-46s refused\n", what);
    }
}

int
main(void)
{
    printf("22-bit fields (AR0 AR2 AR4 AR5 AR6): value <21:0>, reserved <31:22>\n");
    word("zero",                        0L,        22U, 1, 0x00000000UL);
    word("one",                         1L,        22U, 1, 0x00000001UL);
    word("minus one -- the whole point", -1L,      22U, 1, 0x003fffffUL);
    word("the largest that fits",        2097151L, 22U, 1, 0x001fffffUL);
    word("one past the largest",         2097152L, 22U, 0, 0UL);
    word("the most negative that fits", -2097152L, 22U, 1, 0x00200000UL);
    word("one past the most negative",  -2097153L, 22U, 0, 0UL);

    printf("24-bit fields (AR1, the nine DRs, the alphas): 9.15 signed\n");
    word("minus one",                   -1L,       24U, 1, 0x00ffffffUL);
    word("colour 255 as 9.15",     255L * 32768L,  24U, 1, 0x007f8000UL);
    word("the largest that fits",        8388607L, 24U, 1, 0x007fffffUL);
    word("one past the largest",         8388608L, 24U, 0, 0UL);
    word("the most negative that fits", -8388608L, 24U, 1, 0x00800000UL);
    word("one past the most negative",  -8388609L, 24U, 0, 0UL);

    printf("the carrier means a sign-extended value, not a raw encoding\n");
    /* 0xffffffff read back as long is -1: fits, and encodes to 0x00ffffff.
     * 0x00ffffff read back as long is +16777215: does NOT fit. */
    word("0xffffffff as a carrier (-1)",
         (long)(unsigned long)0xffffffffUL, 24U, 1, 0x00ffffffUL);
    word("0x00ffffff as a carrier (+16777215)",
         (long)(unsigned long)0x00ffffffUL, 24U, 0, 0UL);

    printf("no reserved bit survives, for any accepted value\n");
    {
        long probe[7];
        unsigned i;
        int bad = 0;

        probe[0] = -1L; probe[1] = -2097152L; probe[2] = 2097151L;
        probe[3] = -12345L; probe[4] = 0L; probe[5] = 7L; probe[6] = -8388608L;
        for (i = 0U; i < 7U; i++) {
            unsigned long w = 0UL;
            if (osmgaHW3DField(probe[i], 22U, &w) && (w >> 22) != 0UL) bad++;
            if (osmgaHW3DField(probe[i], 24U, &w) && (w >> 24) != 0UL) bad++;
        }
        if (bad) { printf("  FAIL  %d accepted words kept a reserved bit\n", bad); failed++; }
        else       printf("  ok    every accepted word has its reserved bits clear\n");
    }

    printf("widths the helper must refuse outright\n");
    word("zero bits",                    0L,        0U,  0, 0UL);
    word("thirty-two bits",              0L,        32U, 0, 0UL);

    printf("\n%s (%d failing)\n",
           failed ? "SOME CASES ARE WRONG" : "all cases behave as specified",
           failed);
    return failed ? 1 : 0;
}
