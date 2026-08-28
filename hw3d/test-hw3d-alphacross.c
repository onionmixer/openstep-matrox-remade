/*
 * test-hw3d-alphacross.c -- host test for the ALPHACTRL combination rules.
 *
 * The astipple table is swept EXHAUSTIVELY: nine source factors by eight
 * destination factors is seventy-two pairs, of which the spec lists four.
 * Testing only that forbidden pairs are refused would pass an
 * implementation that refuses the allowed ones too, and this project has
 * already shipped one check whose test looked only at the case it expected
 * to fail.
 *
 *   cc -O -Wall -o test-hw3d-alphacross test-hw3d-alphacross.c OpenStepMGAHW3D.c
 */
#include <stdio.h>
#include "OpenStepMGAHW3D.h"

static int failed;

static unsigned long
ac(unsigned long src, unsigned long dst, unsigned long mode, unsigned long stip)
{
    return (src & 0xFUL) | ((dst & 0xFUL) << 4)
         | ((mode & 0x3UL) << 8) | ((stip & 0x1UL) << 11);
}

static void
one(const char *what, unsigned long w, int want)
{
    int got = osmgaHW3DAlphaCross(w);
    if (got != want) {
        printf("  FAIL  %-52s %08lx got %d want %d\n", what, w, got, want);
        failed++;
    } else {
        printf("  ok    %-52s %08lx %d\n", what, w, got);
    }
}

int
main(void)
{
    unsigned long src, dst;
    int allowed = 0, refused = 0;

    printf("dst ZERO on its own is legal -- it is how blending is disabled\n");
    one("src ONE, dst ZERO (the opaque word Mesa sends)",
        ac(1UL, 0UL, 1UL, 0UL), 1);
    one("src ZERO, dst ZERO",            ac(0UL, 0UL, 0UL, 0UL), 1);
    one("src SRC_ALPHA, dst OM_SRC_ALPHA (the OVER word)",
        ac(4UL, 5UL, 1UL, 0UL), 1);

    printf("rule 1 is measured, not refused\n");
    one("SRC_ALPHA_SATURATE with dst ZERO", ac(8UL, 0UL, 1UL, 0UL), 1);
    one("SRC_ALPHA_SATURATE with dst ONE",  ac(8UL, 1UL, 1UL, 0UL), 1);

    printf("rule 2 -- video alpha needs a destination\n");
    one("video alpha, dst ZERO",         ac(4UL, 0UL, 2UL, 0UL), 0);
    one("video alpha, dst ONE",          ac(4UL, 1UL, 2UL, 0UL), 1);
    one("alpha channel, dst ZERO",       ac(4UL, 0UL, 1UL, 0UL), 1);
    one("FCOL, dst ZERO",                ac(4UL, 0UL, 0UL, 0UL), 1);

    printf("rule 3 -- video alpha and the stipple cannot combine\n");
    one("video alpha with astipple, otherwise legal pair",
        ac(0UL, 1UL, 2UL, 1UL), 0);
    one("alpha channel with astipple, same pair",
        ac(0UL, 1UL, 1UL, 1UL), 1);

    printf("rule 4 -- every one of the seventy-two stipple pairs\n");
    for (src = 0UL; src <= OSMGA_HW3D_AC_SRC_MAX; src++) {
        for (dst = 0UL; dst <= OSMGA_HW3D_AC_DST_MAX; dst++) {
            int want =
                (src == 0UL && dst == 1UL) || (src == 1UL && dst == 0UL) ||
                (src == 4UL && dst == 5UL) || (src == 5UL && dst == 4UL);
            int got = osmgaHW3DAlphaCross(ac(src, dst, 0UL, 1UL));

            if (got != want) {
                printf("  FAIL  astipple src %lu dst %lu: got %d want %d\n",
                       src, dst, got, want);
                failed++;
            }
            if (want) allowed++; else refused++;
        }
    }
    printf("  ok    %d pairs allowed, %d refused, of %d\n",
           allowed, refused, allowed + refused);
    if (allowed != 4) {
        printf("  FAIL  the spec lists four pairs, the sweep found %d\n", allowed);
        failed++;
    }

    printf("without the stipple the same pairs are all fine\n");
    {
        int bad = 0;
        for (src = 0UL; src <= OSMGA_HW3D_AC_SRC_MAX; src++)
            for (dst = 0UL; dst <= OSMGA_HW3D_AC_DST_MAX; dst++)
                if (!osmgaHW3DAlphaCross(ac(src, dst, 1UL, 0UL))) bad++;
        if (bad) { printf("  FAIL  %d refused with astipple clear\n", bad); failed++; }
        else       printf("  ok    all 72 pairs pass with astipple clear\n");
    }

    printf("the fields these rules do not read\n");
    {
        /* atmode, atref and aten sit above bit 12; alphasel at 24-25.  None
         * of them may change any of the three decisions. */
        unsigned long base = ac(4UL, 0UL, 2UL, 0UL);   /* refused by rule 2 */
        unsigned long good = ac(4UL, 1UL, 2UL, 0UL);   /* allowed */
        unsigned long extra[4];
        int i, bad = 0;

        extra[0] = 0x00001000UL;  /* aten      */
        extra[1] = 0x0000E000UL;  /* atmode    */
        extra[2] = 0x00FF0000UL;  /* atref     */
        extra[3] = 0x03000000UL;  /* alphasel  */
        for (i = 0; i < 4; i++) {
            if (osmgaHW3DAlphaCross(base | extra[i]) != 0) bad++;
            if (osmgaHW3DAlphaCross(good | extra[i]) != 1) bad++;
        }
        if (bad) { printf("  FAIL  %d decisions moved with an unrelated field\n", bad); failed++; }
        else       printf("  ok    aten, atmode, atref and alphasel change nothing\n");
    }

    printf("\n%s (%d failing)\n",
           failed ? "SOME CASES ARE WRONG" : "all cases behave as specified",
           failed);
    return failed ? 1 : 0;
}
