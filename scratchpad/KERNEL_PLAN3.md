# Kernel plan v3: write only the blocks that changed, and keep what already won

Driver: openstep-matrox-remade/OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay_reloc.tproj/OpenStepMGAReplacementDisplay.m
Encoder: osmgaHW3DEncode, around .m:7860-7975. NeXT Mach 4.2 DriverKit, i386,
cc 2.7.2.1, C89.

## What the last reboot established, on the machine

Both switches from plan v2 worked and are measured:

    pack 0 delay 0   20.50 ms/frame   48.8 fps
    pack 1 delay 0   19.27            51.9
    pack 1 delay 2   18.07            55.3
    pack 1 delay 4   17.80            56.2      scene baselines 0, regression 0

- FXBNDRY riding in the execute block removed exactly 5 dwords per untextured
  trapezoid: 9142 per frame against 9140 predicted. Time per dword barely
  moved (171 -> 176 ns), so the engine ingests the list dword by dword and
  cutting the list cuts the wait.
- Delaying the completion poll made the wait SHORTER, not merely quieter:
  86% fewer reads and 45 us less per submission. The poll was taking bus
  bandwidth from the DMA it was waiting on. 1 us is reproducibly worse than
  0; 2 and 4 us are wins.
- The wait telemetry: four of the five waits are satisfied on their first
  read, every time (mean 1.00, max 1). All of it is the completion poll.

## What is now measured about the list itself

Counting, in userland, how much of each trapezoid the previous one already
left in the engine (109803 trapezoids of a real frame; the first trapezoid of
each batch counts as all-changed, which is the only safe convention):

    12.8 of 25 register values differ
    blocks 768621 -> 575734, i.e. 74.9% of the list would still go out

    block                changes on
    dwgctl + AR0..AR2      99.8% of trapezoids
    AR4..AR6 + SGN         99.9%
    DR4/DR6/DR7/DR8        62.1%
    DR10/DR11/DR12/DR14    61.8%
    DR15/DR0/DR2/DR3       99.9%
    alpha (4 registers)     1.8%
    FXBNDRY + execute     100.0%

So the whole 25% is in three blocks: the alpha block, which is essentially
constant, and the two colour blocks, which repeat on four trapezoids in ten.

## The change

(a) Change-tracking encoder, behind a setting, default OFF for the first
    boot. The encoder keeps the previous trapezoid's values for the three
    skippable blocks and emits a block only if one of its registers differs.
    State is reset at the START of every list -- the engine's contents before
    a list are the previous submission's, another client's blit may have run
    in between, and nothing here may assume otherwise. The other four blocks
    are emitted unconditionally as today.

    Expected: 35 -> 26.2 dwords a trapezoid, and if the wait keeps tracking
    dwords, about 2.4 ms off a 17.8 ms frame.

(b) Make the two settings that already won the defaults: pack on, poll delay
    4 us. They are proven on this machine with the baselines and the
    regression suite; leaving them off by default means every boot starts
    slower than the machine has been measured to run.

(c) Widen the delay whitelist to 0, 1, 2, 4, 8, 16 so the knee can be found
    without another reboot. The limit is already divided by the delay so the
    wait's worst case in wall time does not grow.

Not in this change: the barrier read, the recovery poll, the latch, the
validator, and the four blocks that change every trapezoid.

## Gates

Before install: the no-hardware checks under openstep-matrox-remade/tools/.
After reboot, with (a) off: scene baselines 14 unchanged, regression zero,
frame at the new defaults within noise of 17.8 ms.
Then (a) on: baselines and regression again BEFORE believing any timing, then
the frame, then the delay sweep.

## Questions

1. Is intra-list state tracking safe on this engine -- do the DR/ALPHA
   registers persist across primitives within one DMA list, given that
   YDSTLEN+EXEC starts drawing between them? If any of them is consumed or
   reset by the execute, skipping is wrong and the picture will show it, but
   I would rather know before the reboot.
2. Is resetting the tracking at the start of each list sufficient, or is
   there a path inside a list that can change engine state behind the
   encoder's back?
3. The colour blocks repeat on 38% of trapezoids. Given the two trapezoids of
   one triangle share the plane but have different anchors, is that number
   plausible, or does it suggest my grouping is wrong?
4. Making the delay a default: the recovery poll stays undelayed, and the
   limit is scaled. Is there any failure mode where a 4 us delay turns a
   recoverable timeout into a latch?
5. What would you keep out of this reboot?
