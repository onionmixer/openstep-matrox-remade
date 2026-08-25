# Results of the third reboot, and what I conclude from them

## What shipped in this driver

1. The completion poll now acknowledges the soft trap ONLY when it saw the
   completion. It used to clear it unconditionally, which meant the recovery
   poll -- whose condition requires SOFTRAPEN set -- could never succeed.
2. A change-tracking encoder, off by default: the two colour blocks and the
   alpha block are written only when something in them changed since the
   previous trapezoid of the SAME list. Tracker is a local of
   osmgaHW3DEncode, empty at every call.
3. FXBNDRY riding in the execute block for untextured trapezoids: default ON
   this time (it was measured last reboot).
4. Poll delay: still default 0, live-settable to 1, 2 or 4 us.

## Measured, on the machine, after the reboot

Baselines are the fourteen scene comparisons; regression is the quick suite.

    configuration             frame ms      fps  us/submit dwords/sub   vs base
    pack 0 track 0 delay 0       20.50     48.8      376.6     2199.7     +0.0%
    pack 1 track 0 delay 0       19.97     50.1      340.8     1927.2     -2.6%
    pack 1 track 1 delay 0       17.92     55.8      277.3     1449.4    -12.6%
    pack 1 track 1 delay 1       18.18     55.0      285.4     1449.4    -11.3%
    pack 1 track 1 delay 2       17.07     58.6      249.9     1449.4    -16.7%
    pack 1 track 1 delay 4       16.86     59.3      244.2     1449.4    -17.8%

    baselines with track on: SCENES_MOVED=0
    regression with track on: PROBLEM 0, FAIL 0
    frame with everything off after the reboot: 19.97/19.97/19.95 -- stable

The userland prediction made BEFORE the encoder was written said the list
would fall to 74.9% of its size. It fell to 75.2%.

    ns per dword: 177 (track off) -> 192 (track on)
    kernel ms a frame: 12.63 at the session's start -> 8.19 now

## What I conclude

- The engine's ingest of the DMA list is the frame's largest single cost, and
  it is proportional to the list, so removing register writes removes time.
- The colour and alpha interpolator state survives YDSTLEN|EXEC. X.Org's
  MGA driver depends on the same thing (mga_storm.c:419-431 writes them once,
  549-565 writes only TMR6/TMR7/FXBNDRY/EXEC per primitive).
- The completion poll competes with the DMA it waits on; 2 and 4 us between
  reads make the wait shorter in wall time. 1 us is reproducibly worse than
  0, in four separate runs across two reboots.
- ns per dword rose from 177 to 192, which I read as the fixed per-submission
  cost (74 us measured earlier) becoming a larger share of a shorter list,
  not as the engine slowing down.

## What I have NOT established

- Why 1 us is worse than 0. I have a story (fewer reads but the bus is still
  busy, and IODelay's own granularity at 1 us) and no evidence for it.
- Whether the recovery path now works. The fix is in, but no submission has
  ever timed out on this machine (largest poll index 2025 against a limit of
  100000), so the corrected path has not executed.
- Whether the colour blocks repeat 38% of the time for the reason I think.
  Flat-shaded triangles and fixed-point rounding of similar lit colours are
  both plausible; I have not separated them.

## Questions

1. Does anything in these numbers contradict the conclusions I drew?
2. The tracker is now proven on one scene family (the teapot and the fourteen
   baselines). What scene would break it if my safety argument were wrong,
   and is it in the regression suite?
3. Should the poll delay be promoted to a default now that the trap
   acknowledgement is fixed, or does the untested recovery path argue for
   waiting? What would you do to exercise the recovery path deliberately?
4. Is there a next lever of comparable size that I am not seeing?
