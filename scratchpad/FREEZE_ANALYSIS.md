# The machine froze, and I think I know why -- attack it

## What was done, in order

The DMA regrouping had been installed and the machine rebooted. After that
reboot, all of this ran clean:

    5 runs of `prof frame`            13.44 - 13.51 ms
    14 scene baselines                SCENES_MOVED=0
    20-scene hardware byte identity   0 moved
    quick regression                  0 failing
    4 scenes with instrumentation     all reported normally

Then, trying to separate list length from drawing load by toggling the live
`track` setting:

    1. `waits 4 1 1`                       (ok)
    2. `prof frame 120 inst`               (ok, 14.41 ms, reported)
    3. `prof scissor 120 inst`             STARTED
    4. -- my LOCAL 2-minute timeout fired and killed my telnet client.
          The remote process was not killed by that.
    5. `waits 4 1 0`                       (new telnet session)
    6. `prof frame 120 inst`               no output
    7. the machine stopped answering ARP entirely; console stayed up; the
       user confirms it froze and is force-rebooting.

## What I think happened

Step 6 started a second accelerated client while the one from step 3 was
still running. The driver's own note says what that means
(OpenStepMGAReplacementDisplay.m:1972-1982):

    "There is no per-client state anywhere in this device ... two
     accelerated processes share one batch buffer and one video-memory
     surface with nothing arbitrating between them -- each writes its
     triangles where the other writes its own, and either may submit while
     the other is halfway through filling it.  Neither is told."

`stormBusy` serialises the submit ioctl, so the DMA ring is not written by
two threads at once. It does not protect the BATCH BUFFER, which is the
mmapped region the clients fill. So a batch can be validated and then mutated
by the other client while the kernel is encoding it, and the engine can be
handed a trapezoid the validator never saw.

## Why I do not think the regrouping caused it

Everything above ran clean on this same boot with the new encoder, including
the twenty-scene byte identity and the quick suite -- and the identity check
exercises track=0 as its OFF arm, so the untracked path was covered too. The
textured tail, which is where the two promoted registers ride in the
non-packed block, is covered by the textured scenes in those baselines.

The one thing NOT exercised since the change is `prof scissor` -- an 8x8
scissor box, so almost no pixels are filled while every trapezoid is still
submitted. That is a genuinely different timing regime: the engine finishes
each list almost instantly.

## The other candidate I cannot exclude

Before the doorbell there is a wait for the engine to be idle with the DMA
ended, and it has no delay in it and a limit of 100000 reads
(OpenStepMGAReplacementDisplay.m:4961-4964, OSMGA_S1_SPIN_LIMIT at line 930).
At the measured 1.08 us a read that is 108 ms per occurrence. If something
put the engine in a state where that condition never became true, 33
submissions a frame would each pay it and the machine would look frozen
while still running.

## Questions

1. Is the two-client explanation right? Trace what a second client actually
   shares -- the probe, the batch buffer, the surface -- and say whether the
   submit ioctl's stormBusy is enough to make the freeze impossible.
2. Is there a path where a corrupt or concurrently-mutated batch passes the
   validator and then makes the engine walk forever, rather than being
   refused?
3. Could `waits 4 1 0` -- changing osmgaTrackState while a client is mid-run
   -- do this on its own?
4. Is the scissor path different in any way the regrouping touches?
5. What should guard this in future: something in the driver, or only
   discipline in how I run the tests?
