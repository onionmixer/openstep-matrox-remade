/*
 * Does inlining change the value on this compiler?
 *
 * A call passes a double argument through memory at 64 bits.  Inlined, gcc
 * may keep it in an x87 register at 80 and the extra bits survive.  If that
 * happens here, inlining the encoder's leaves is not value-preserving and the
 * picture moves.
 *
 * The probe: a quotient that is not exact in 64 bits, handed to the same body
 * twice -- once through a call, once inlined.  If the two disagree the hazard
 * is real on this compiler.
 */
#include <stdio.h>

static double clampCall(double v)
{ if (v > 255.0) return 255.0; if (v < -255.0) return -255.0; return v; }

static __inline__ double clampInl(double v)
{ if (v > 255.0) return 255.0; if (v < -255.0) return -255.0; return v; }

static unsigned long fixCall(double v)
{ double s = v * 32768.0; return (unsigned long)(long)((s >= 0.0) ? (s+0.5) : (s-0.5)); }

static __inline__ unsigned long fixInl(double v)
{ double s = v * 32768.0; return (unsigned long)(long)((s >= 0.0) ? (s+0.5) : (s-0.5)); }

static void show(const char *what, double a, double b)
{
    unsigned char *pa = (unsigned char *)&a, *pb = (unsigned char *)&b;
    int i, same = 1;
    for (i = 0; i < 8; i++) if (pa[i] != pb[i]) same = 0;
    printf("  %-14s call ", what);
    for (i = 7; i >= 0; i--) printf("%02x", pa[i]);
    printf("  inline ");
    for (i = 7; i >= 0; i--) printf("%02x", pb[i]);
    printf("  %s\n", same ? "same" : "*** DIFFER ***");
}

int
main(void)
{
    int i, bad = 0;
    /* quotients and products that need more than 53 bits to be exact */
    static const double num[] = { 1.0, 2.0, 7.0, 1e6, -3.0, 12345.0, 1e-4 };
    static const double den[] = { 3.0, 7.0, 11.0, 3.0, 7.0, 49.0, 3.0 };

    printf("x87 excess precision across a call boundary, cc -m486 -O\n\n");
    for (i = 0; i < 7; i++) {
        double q = num[i] / den[i];
        double a = clampCall(num[i] / den[i]);
        double b = clampInl(num[i] / den[i]);
        unsigned long fa = fixCall(num[i] / den[i]);
        unsigned long fb = fixInl(num[i] / den[i]);

        show("clamp(a/b)", a, b);
        if (fa != fb) {
            printf("  %-14s call %lu  inline %lu  *** DIFFER ***\n",
                   "fixed(a/b)", fa, fb);
            bad++;
        }
        (void)q;
    }
    printf("\n%s\n", bad ? "HAZARD REAL" : "no difference seen on these");
    return 0;
}
