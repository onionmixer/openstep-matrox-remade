# The protocol hypothesis is rejected; what survives

## The test

The reorder was installed: full three-bit quiescence proven BEFORE any CPU
register write, OPMODE written only when it would change (never, after the
first submission), entry status recorded. All identity gates passed
(SCENES_MOVED=0, 20-scene identity 0 moved, regression 0, frame 13.50 ms
unchanged). Telemetry confirmed the new order ran: quiescence entered per
submission, largest 1 read, entry status 20000, entry trap 0.

Then the reproducer, run 1 of the planned 5: FROZE between frame 21 and 30.
Stopped at first freeze per the protocol. No 3-61 line in the externally
collected log. Sixth freeze overall.

## What this rejects

CPU register traffic colliding with parser tail work. The submit path now
issues NOTHING but reads (pre-idle byte, quiescence dwords) until the parser
has proved trap-clear + drawing-idle + DMA-ended, and it still froze. The
per-submission OPMODE rewrite is also gone in steady state. Both halves of
the divergence hypothesis are dead.

Cumulative rejections: two clients, post-validation batch mutation, the
present path, FIFO under-reservation in the submit path, the settling read,
state-writes-before-quiescence, per-submission OPMODE.

## What survives

Every path the freeze can hide in now involves READS, or something outside
the driver entirely:

1. **A status read colliding with rapid engine transitions.** In scissor
   mode the drawing engine goes busy and idle per trapezoid with almost no
   drawing in between -- ~1800 transitions a frame, against a handful in any
   other scene. Our pre-idle wait samples ENGSTATUS+2 as a BYTE read
   (osmgaR8), and X.Org carries a warning that exactly this access --
   MGAISBUSY() = INREG8(Status+2) & 1, mga_macros.h:30 -- "reportedly causes
   a freeze" on Mystique revisions 0-1 (mga_storm.c:1062-1064). That is a
   different chip; but it is documented precedent that a STATUS byte read
   can freeze an MGA, and it is the only surviving candidate with any
   precedent at all. The DRM polls status as full 32-bit reads only.

2. gettimeofday's clock/interrupt interaction with the spin loops --
   no mechanism, no precedent, just not excluded.

3. Something outside the driver (bus arbitration, scanout) -- not excluded,
   not actionable.

## The candidate change, if we spend more reboots

Make osmgaStormWaitIdle use the full 32-bit ENGSTATUS read and test the
DWGENGSTS bit, exactly as the DRM's mga_is_idle does and as our own
quiescence and completion polls already do. Value-identical, order-identical;
only the access width changes. Cost: one reboot to install, up to one more
if it still freezes.

## The honest position

Six freezes in, each elimination costs a reboot, and the surviving
candidates are weaker each round. The freeze needs scissor AND
instrumentation -- a test-only combination that no real workload has ever
hit. Stopping here and forbidding the combination in the test tools is a
defensible endpoint; so is one more round on the byte-read candidate, which
is the last one with documented precedent.

## Questions

1. Is the byte-read candidate coherent? Does anything in the references
   suggest WHY a status byte read could wedge a chip, and does the Mystique
   warning plausibly extend to a G450 under rapid transitions?
2. Is there a surviving candidate this list misses?
3. Given the cost structure -- each test is a reboot -- is one more round
   justified, or is documenting and forbidding the combination the right
   engineering call?
