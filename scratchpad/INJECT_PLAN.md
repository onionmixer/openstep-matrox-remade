# Plan: make the recovery path run, on purpose, for the first time

Driver: openstep-matrox-remade/OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay_reloc.tproj/OpenStepMGAReplacementDisplay.m
NeXT Mach 4.2 DriverKit, i386, cc 2.7.2.1, C89.

## Why

The completion poll used to acknowledge the soft trap unconditionally, which
made the recovery poll -- whose condition requires SOFTRAPEN SET -- unable to
ever succeed. That is fixed. But the recovery branch has still never
executed: on this machine the largest completion-poll index ever seen is 2025
against a limit of 100000. A safety net nobody has ever landed in is a claim,
not a net. Until it runs I will not promote the poll delay to a default,
because the delay is exactly what enlarges the window it protects.

## What the branch does today (read, not assumed)

Entered when the completion poll gave up OR the final idle wait failed.

    poll again for the WHOLE condition, undelayed, limit 100000
    if it completes:  ICLEAR, IOLog "completion arrived during recovery"
                      -- and rc3 stays 0, so the client is told the
                      submission FAILED and replays it in software
    if it does not:   stormBlitFailed = YES -- acceleration off permanently

## The injection

A fourth word on the existing OSMGAHW3DTune parameter, default 0, values
0..4: the number of subsequent submissions whose primary completion poll
reports a timeout WITHOUT polling at all. The list has already been handed to
the engine at that point (PRIMADDRESS/PRIMEND are written before the poll),
so this reproduces exactly the case the recovery exists for: the list is
running, the driver stopped looking too early, and completion arrives
unobserved.

    if (injectNow != 0UL) { injectNow--; spins3 = limit3; }
    else { for (spins3 = 0; spins3 < limit3; spins3++) { ... } }

Nothing else changes. With spins3 == limit3 the fixed code does not
acknowledge the trap, does not run the final idle wait, and enters recovery --
which is the whole point.

## What I expect to see

1. IOLog "the poll gave up but completion arrived during recovery", with a
   small "more spins" count -- the engine was about to finish anyway.
2. stormBlitFailed NOT set: the next submission is accepted, hookBatches
   keeps rising.
3. The client counts one refusal (hookDeclined +1) and one software replay.
4. The picture unchanged.

## The wrinkle I want your opinion on

On the recovery-success path the engine HAS drawn the batch -- it completed --
and yet the client is told the submission failed and replays it in software.
Every pixel is therefore drawn twice. For opaque replace-mode drawing that is
idempotent. For BLENDING it is not: the second pass blends over the first.
This repository already records the same shape of hazard from a different
cause ("a refusal of the second half arrived with the first already drawn,
and the software redraw wrote those pixels twice; survivable only because
texturing and blending were never on together").

So my test scene will be opaque, and I will note the blended case rather than
pretend the test covered it.

## Test procedure

    1. inject 1 at delay 0, draw one teapot frame
    2. read /private/adm/messages for the recovery line
    3. read the hook counters: declined +1, replayed +N, batches still rising
    4. draw ten more frames; if acceleration had latched they would all be
       software and the counters would say so
    5. repeat at delay 1, 2, 4
    6. the byte-identity check (tracker off vs on) once more at the end

If recovery FAILS the driver latches acceleration off until the next reboot.
The machine stays usable -- software rendering -- and nothing is destroyed.
That is the cost I am accepting, and it is the same cost the latch exists to
impose.

## Questions

1. Is skipping the poll entirely the right injection, or should it poll a
   few times first so the engine is partway through rather than barely
   started?
2. Is the double-draw on the recovery-success path a bug I should fix in the
   same change, or is telling the client "failed" after a completed list the
   deliberate contract?
3. Should the injection be reachable only under some additional guard, or is
   default-0 with a 0..4 whitelist enough?
4. What should the test assert that I have not listed?
5. Is there any way this injection can leave the ENGINE, rather than the
   driver, in a state a reboot would not clear?
