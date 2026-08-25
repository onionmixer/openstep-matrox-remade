# The recovery path ran, seven times, and never latched

## What was injected

A test-only parameter spends a countdown of submissions that skip the primary
completion poll entirely and report a timeout. The list is already running at
that point, so this is the case the recovery exists for: still going, and the
driver stopped looking.

## What happened

    inject 1, delay 0            (the injection lands on the frame's CLEAR)
      driver log: "the poll gave up but completion arrived during recovery
                   (status 80820025, 2076 more spins); the engine is
                   quiescent and software may have the surface back"
      recovery counters: 1 saved, 0 latched
      hook: declined 1, software 0, replayed 0, drawn 20447 (full)
      fills 21 -> 20, mirrors 693 -> 694

    inject 2, delay 0            (the second lands on the first TRIANGLE batch)
      submits 2, drawn 0, batches 1, software 992, declined 994, replayed 0
      recovery counters: 3 saved, 0 latched

    inject 2 at delay 2, and again at delay 4: identical shape,
      recovery counters 5 saved then 7 saved, 0 latched throughout

    "acceleration DISABLED permanently" in the log: 0 occurrences

    a fresh process afterwards: drawn 20447, declined 0, frame 16.86 ms
    scene baselines afterwards: SCENES_MOVED=0

## What I read from it

1. The recovery poll now sees completion. Before the trap-acknowledgement fix
   it could not: completion requires SOFTRAPEN SET and the poll acknowledged
   it unconditionally. Seven recoveries, no latch.

2. The two refusal policies are different on purpose and both behaved as
   their comments say:
   - a refused CLEAR falls back to Mesa's clear and does NOT revoke, because
     "a clear that may already be half done can be finished by writing the
     same colour over it";
   - a refused TRIANGLE batch whose verdict was OK revokes acceleration for
     the process and does NOT replay, because "some of it may be on the
     screen, and drawing it again would double it".
   So the double-draw I was worried about cannot happen: nothing is replayed
   on this path.

3. The cost of a recovered submission is the process losing acceleration.
   The picture stays right -- 992 triangles went to Mesa's rasteriser and the
   baselines are unmoved -- but the process finishes in software.

4. The delay does not change any of this. The same injection at 0, 2 and 4 us
   produced the same outcome.

## The open question I did not settle by measurement

The recovery poll checks the WHOLE completion condition -- trap fired, engine
idle, DMA pointer at the end -- and when it passes, the list demonstrably
finished. Cross-review argues the driver should then report SUCCESS (rc3 = 1)
rather than failure, so the process keeps acceleration for a submission that
actually completed.

Against that: the normal success path also does a settle read of video memory
before declaring done, which the recovery path does not, and the difference
has never been measured. Reporting success without it would be a new claim.

I have not changed it. What I have is the evidence that the branch works.

## Questions

1. Does anything here contradict what I read from it?
2. Is rc3 = 1 on recovery success right, and if so does it need the settle
   read to go with it? What would you measure to decide?
3. The injection lands on whichever submission comes next, and the frame's
   first is always the clear. Is spending a countdown the right control, or
   should it be able to name the kind of submission it wants?
4. Anything about this test that makes it prove less than I think?
