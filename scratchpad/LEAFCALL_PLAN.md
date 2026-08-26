# The encoder spends a third of its time entering and leaving functions

## What the profile says now

600 frames, wall 17.25 ms = user 8.67 + sys 8.15. The old profile in the
notes predates the value-preserving transforms and no longer describes this
code; this is a fresh one.

    25.0%  OSMGAMesaBuildTriangleTex     the builder itself
    14.2%  osmgaFixed                    double -> 15.15 fixed
    10.9%  de_casteljau_surf             Mesa's evaluator, the teapot's
     6.6%  osmgaFix                      double -> the engine's fixed
     6.2%  osmgaMesaTriangle
     5.4%  osmgaClampSlope
     5.2%  osmgaMesaCountDeltas          <- instrumentation, mine
     4.3%  osmgaCeilDiv
     4.3%  osmgaTrapezoid
     3.9%  floor                         <- libm, from osmgaRound
     2.5%  eval2_obj_norm                Mesa's
     1.9%  osmgaEdgeRegs
     1.6%  osmgaClampDepthSlope
     1.4%  osmgaStartFixed

The leaves add to 34.3% of user time, 2.97 ms a frame. They are two to four
lines each. Almost all of that is the call, not the work.

## Measured, on the machine

A static leaf taking one double, called three million times, against the same
body marked `__inline__`, `cc -m486 -O`:

    static      37.1 ns a call
    __inline__   2.2 ns
    macro       16.0 ns   (it evaluates its argument three times)

So this compiler DOES honour `__inline__` at -O -- worth checking rather than
assuming, since gcc 2.7 does not inline ordinary statics at this level -- and
a call costs about 35 ns.

## What I mean to do

1. **Take the instrumentation out of the hot path.** `osmgaMesaCountDeltas`
   runs on every submission (OpenStepMGAMesaHook.c:709) and exists only so a
   test can report which registers change. It should run only when a test
   asks. Worth 0.45 ms a frame and it costs nothing to give up.

2. **Inline the leaves**: osmgaFixed, osmgaFix, osmgaClampSlope,
   osmgaClampDepthSlope, osmgaCeilDiv, osmgaFloorDiv, osmgaStartFixed,
   osmgaRound, osmgaAbsD, osmgaFirstDrawn. `__inline__`, not macros -- a
   macro of these would evaluate its argument more than once, which the
   measurement above shows is slower and which would be wrong for any
   argument with an effect.

3. **Stop calling floor.** `osmgaRound(v)` is `(long)floor(v + 0.5)`, and
   floor is a library call. For any v whose result fits a long the same value
   comes out of a truncating cast with a fixup:

       double t = v + 0.5;
       long   i = (long)t;                    /* toward zero */
       return (t < 0.0 && (double)i != t) ? i - 1L : i;

   floor rounds toward minus infinity; a cast truncates toward zero; the two
   differ only for a negative non-integer, which is what the fixup covers.

Estimated together: 2.1 to 3.5 ms a frame, 12 to 20% of the frame.

## What could make it wrong, and how it gets caught

Inlining on x87 can CHANGE RESULTS. Passing a double argument stores it to
memory at 64 bits; inlined, gcc may keep the value in an x87 register at 80
bits and the extra precision can survive into the result. That is a real
hazard for the ones taking a computed expression -- osmgaClampSlope is called
on a quotient, osmgaFixed on a product.

Every change here is gated on the picture not moving: 14 scene baselines,
the 20-scene hardware-against-hardware byte identity, the quick suite, and
the hook counters. If a scene moves, the fix is to force the argument through
a 64-bit store rather than to abandon the change.

## Questions

1. Is `__inline__` on these ten safe as to value on this compiler, or does
   one of them need its argument forced to 64 bits first? Which ones take a
   computed expression rather than a variable?
2. Is the floor replacement exactly equal to `(long)floor(v+0.5)` over the
   range these arguments take? What is that range?
3. `osmgaFixed` is the single biggest leaf at 14.2%. Is inlining the whole of
   what is available there, or is its body doing something avoidable?
4. Is there a leaf on that list where inlining would make things worse --
   called from enough places that the code growth costs more than the call?
5. What have I missed that is bigger than this?
