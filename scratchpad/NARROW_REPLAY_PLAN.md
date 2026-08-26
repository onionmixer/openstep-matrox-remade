# One refused sliver should not send thirty triangles to software

## Today

A batch carries up to 180 trapezoids from up to 180 source triangles. The
kernel validates the WHOLE batch before drawing anything; one bad trapezoid
-- a TRICROSS sliver, say -- refuses the lot, and the hook replays every
accumulated source triangle through Mesa's rasteriser. In the teapot scene a
refusal costs ~30 software triangles for one bad sliver. Refusals are rare
(TRICROSS x4 over 120 scissored frames) so this is not a performance lever;
it is proportionality: the picture a refusal produces is 30 software
triangles' worth different from the hardware's, when one triangle deserves
it.

## What the kernel already tells us

The refusal block carries res.verdict and res.triangle. The validator sets
*badTri = i at the top of every per-triangle iteration (OpenStepMGAHW3D.c:
461-465), so for verdicts returned inside that loop the index is exact:

    per-triangle: DWGCTL ALPHA TRIROW TRICOL TRISLOPE EDGEDIV TRISGN
                  TRICROSS TEXCOORD TRIEMPTY
    batch-level:  MAGIC VERSION COUNT DSTORG ZORG TEXORG TEXSIZE DSTSIZE
                  DSTPITCH        (badTri defaults to 0; meaningless)

And the batch drew NOTHING -- validation precedes encoding -- so every
trapezoid before the bad one is still undrawn and still valid.

## The change (userland hook only, no reboot)

Record, with each pendSrc entry, where its trapezoids start in the batch
(firstTrap = pendTraps before the builder appended; sources are contiguous).

In the refusal arm, when the verdict is per-triangle AND res.triangle <
triCount AND retries remain:

    1. map res.triangle -> source s (linear scan of firstTrap, nsrc <= 180)
    2. submit tri[0 .. firstTrap(s)-1] as its own batch  (hardware, in order)
    3. software-replay source s alone, with its saved per-triangle state
    4. slide tri[firstTrap(s+1) ..] down to tri[0], fix triCount, and loop
       -- a further refusal in the remainder narrows again

Bounded at 8 narrowing rounds; past that, or for any batch-level verdict, or
res.triangle out of range, the whole remainder replays in software exactly as
today. The injection test corrupts MAGIC -- batch-level -- so its
force-the-full-replay semantics are untouched.

## Order is preserved, and that is the design's spine

prefix (hw) -> s (sw) -> remainder (hw, recursively). Blending and
equal-depth results depend on draw order; this keeps the source order
exactly. The prefix must end at s's FIRST trapezoid -- not at the refused
one, which may be s's second -- or half of s would be drawn twice.

## What changes on screen, honestly

On a frame with a refusal, the 29 good triangles are now drawn by the
ENGINE instead of by Mesa. Hardware and software rasterisation differ by
design (that difference is why refusals replay in software at all), so a
refusal frame's pixels change -- toward consistency with every other frame.
The gated scenes contain no refusals (declined 0 across all three suites),
so the identity gates are unaffected; this is verified by running them.

Counters: hookDrawn counts sources that reached hardware, hookReplayed the
software ones, per chunk; hookRefusedRun still increments once per refusal
and resets on a taken chunk, so the revoke-at-8 backstop still catches a
driver refusing everything.

## Test

- The injection knob cannot exercise this (MAGIC is batch-level, and that is
  correct). Natural TRICROSS happens in the scissored teapot -- but that
  reproducer is forbidden. Instead: the existing hw3d unit tests (tv) build
  batches with known-bad triangles; add a HOOK-level test that feeds a
  many-triangle scene containing one deliberate sliver through GL with the
  batch limit high, and asserts drawn/replayed counts split 29/1 and the
  image matches the same scene drawn with limit 1 (where the sliver alone is
  refused and replayed). That comparison pins both order and split.
- The three identity gates, plus tr/tc.

## Questions

1. Is the contiguity assumption sound -- can pendSrc entries' trapezoids
   ever interleave, or a source append be partially rolled back?
2. Is memmove-sliding the mapped batch safe while the probe may revoke
   mid-loop, and what happens if OSMGAMesaProbeBatch returns 0 between
   chunks?
3. Is resetting hookRefusedRun on a taken chunk right, or does that weaken
   the revoke backstop in a way that matters?
4. Is the 29-hw/1-sw output change acceptable under the project's own
   rules, given refusal frames were never byte-gated?
5. Is the proposed test the right one, and is there a way to trigger a
   per-triangle refusal deterministically WITHOUT the forbidden reproducer?
