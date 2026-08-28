/*
 * test-warp-texdim.c -- WARP's texture dimension encoding is not the
 * trapezoid path's, and the difference is invisible to reading.
 *
 * The trapezoid path builds TEXWIDTH as ((dim-1)<<18) | ((8-log2)<<9) |
 * log2 and that is hardware-verified -- for a CPU-fed coordinate matrix.
 * Mesa builds a different word for WARP under a comment reading "warp
 * texture registers" (mgatex.c:395).  At 8x8 the two differ in BOTH
 * variable fields, rfw 63 against 5 and tw 14 against 3, so a copy would
 * have fed WARP nonsense and cost a reboot to discover.
 *
 * This test recomputes the reference formula independently and compares.
 * It exists because the check was once done by hand, and a check done by
 * hand once is a check that stops happening.
 */
#include <stdio.h>
#include "OpenStepMGAHW3D.h"

/* mgavb/mgatex's construction, written out again rather than shared, so
 * that a change to the driver's copy has something to disagree with. */
static unsigned long
reference(unsigned long dim, unsigned long log2dim)
{
    unsigned long ofs = 11UL;              /* G400/G450; G200 uses 28 */
    unsigned long twmask = dim - 1UL;
    unsigned long rfw    = (10UL - log2dim - 8UL) & 63UL;
    unsigned long tw     = ((log2dim + ofs) | 0x40UL) & 63UL;

    return (twmask << 18) | (rfw << 9) | tw;
}

/* The trapezoid path's, which must NOT equal the above. */
static unsigned long
trapezoid(unsigned long dim, unsigned long log2dim)
{
    return ((dim - 1UL) << 18) | (((8UL - log2dim) & 63UL) << 9) | log2dim;
}

int
main(void)
{
    static const unsigned long dims[]  = { 8UL, 16UL, 32UL, 64UL,
                                           128UL, 256UL, 512UL, 1024UL,
                                           2048UL };
    static const unsigned long log2s[] = { 3UL, 4UL, 5UL, 6UL,
                                           7UL, 8UL, 9UL, 10UL, 11UL };
    unsigned long i;
    int bad = 0, same = 0;

    for (i = 0UL; i < sizeof(dims) / sizeof(dims[0]); i++) {
        unsigned long got  = osmgaHW3DWarpTexDim(dims[i], log2s[i]);
        unsigned long want = reference(dims[i], log2s[i]);
        unsigned long trap = trapezoid(dims[i], log2s[i]);

        if (got != want) {
            printf("FAIL %4lu: got %08lx wanted %08lx\n",
                   dims[i], got, want);
            bad++;
        }
        /* The whole reason this function exists: if the two encodings ever
         * coincide the test has stopped being able to catch a copy. */
        if (got == trap) {
            printf("FAIL %4lu: WARP and trapezoid encodings are equal "
                   "(%08lx) -- this test can no longer catch a copy\n",
                   dims[i], got);
            same++;
        }
    }

    if (bad == 0 && same == 0)
        printf("warp-texdim: %lu sizes, all match the reference and all "
               "differ from the trapezoid encoding\n",
               (unsigned long)(sizeof(dims) / sizeof(dims[0])));
    else
        printf("warp-texdim: %d wrong, %d indistinguishable\n", bad, same);
    return (bad == 0 && same == 0) ? 0 : 1;
}
