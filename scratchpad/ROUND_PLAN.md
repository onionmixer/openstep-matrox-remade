# Taking the floor call out of osmgaRound without bounding its inputs

## Why it was deferred

When osmgaFix lost its floor call (worth 0.37 ms), osmgaRound kept its own,
because its argument range is not established anywhere and out of range the
two forms fail DIFFERENTLY: (long)floor(t) yields the FPU's indefinite
integer 0x80000000 either way, while trunc-and-fixup turns a large negative
into LONG_MAX via i386 wraparound. Bounding the nine call sites' inputs --
texture plane coefficients, which a sliver can make arbitrarily large --
looked like real work.

## The turn: match the out-of-range behaviour instead of excluding it

The replacement does not need the inputs bounded if it produces the same
value for EVERY double:

    static __inline__ long
    osmgaRound(double v)
    {
        volatile double vv = v;         /* the call rounded v to 64 bits   */
        volatile double t = vv + 0.5;   /* ...and the sum, at floor's door */
        long i;

        if (!(t >= -2147483648.0 && t < 2147483648.0))
            return (long)0x80000000L;   /* what fistpl yields out of range,
                                           for NaN and both infinities too;
                                           measured on this FPU (see
                                           osmgaFix's history) */
        i = (long)t;                    /* toward nought */
        return (t < 0.0 && (double)i != t) ? i - 1L : i;
    }

Three points of care:

1. **The second volatile store.** The old code computed vv + 0.5 in an x87
   register and passed it to floor as a 64-bit double -- the call rounded
   it. Inlined, the sum would stay at 80 bits into the comparison and the
   cast. Forcing it through a 64-bit store makes the new form's input
   bit-identical to what floor received. Same trick, same measured cost
   (about half a nanosecond), as the argument barriers.

2. **The lower bound is >= -2147483648.0, not > -2147483649.0.** In the gap
   (-2^31-1, -2^31), floor lands on -2^31-1, whose cast is the indefinite
   integer -- but the fixup's i-1 wraps to LONG_MAX. So that gap must fall
   in the out-of-range branch. The condition's shape (a single AND of two
   comparisons, negated) also sends NaN there, since both comparisons are
   false on NaN.

3. **The out-of-range value is not invented.** The osmgaFix bound-tightening
   recorded the measurement: a double-to-long conversion out of range on
   this FPU yields the indefinite integer, 0x80000000. NaN and the
   infinities go the same way through fistpl.

Python, 500000 random doubles across 80 binades plus 21 boundary and special
values (both signs of zero, half-integers at both limits, the exact limits,
the one-ULP neighbours, NaN, both infinities): 0 disagreements against
(long)floor(t) modelled with the indefinite-integer rule.

## What it is worth

floor was 6.6% of user time in the last profile; osmgaFix's removal took the
larger share. The nine osmgaRound sites run only for TEXTURED trapezoids
(texture matrix and anchors), so the teapot scene pays them on every
trapezoid. Expected saving: small, 0.1-0.2 ms; the real motive is finishing
the transform so no hot-path floor remains.

## Gates

The usual three: scene baselines, 20-scene byte identity, quick regression
-- plus the two builder harnesses (tr, tc) which exercise the texture path
against recorded values. No reboot: userland only.

## Questions

1. Is the indefinite-integer assumption safe as stated -- one measured case
   (large positive in osmgaFix's history) generalising to negatives, NaN and
   infinities? gcc 2.7.2.1 emits fistpl with the truncation control word for
   (long); is there any input where it yields something other than
   0x80000000 out of range?
2. Is the double-negated range test (`!(t >= LO && t < HI)`) the right way
   to catch NaN with this compiler, or can its optimiser rewrite it into
   something NaN-unsafe?
3. Anything else that makes inlined trunc-and-fixup differ from the called
   floor here?
