# Where this driver's primary-DMA protocol departs from the only other
# driver that runs one, and what that says about the freeze

No machine was touched for any of this. It is a source comparison between
our submit path and the two reference drivers, made because the freeze is
probabilistic and n=1 experiments on hardware cannot attribute it.

## The established facts, restated once

- scissor + instrumentation freezes the machine, 5 of 5, at frame 1..40 of
  120. Scissor alone: 160 frames clean. Instrumentation alone on four other
  scenes: clean. So it is the pair, and it is probabilistic.
- The freeze leaves nothing: no 3-61 give-up line (the handler now records
  to RAM before anything else), no syslog tail (external collector was
  running), and the give-up latch would have left the machine alive in
  software -- it did not survive. So no bounded wait expired: the machine
  stops inside a single access, or in something no counter guards.
- The one unbounded access with a live switch (the settling read) is
  excluded: freeze with it off, at frame 31..40.

## What scissor changes about TIMING, precisely

The engine ingests a list at about 29 MB/s and the drawing takes as long as
the pixels take. With the 8x8 box almost no pixels survive the clip, so the
drawing FIFO is empty when the parser reaches the SOFTRAP at the end of the
list: trap, engine-idle and ENDPRDMASTS assert nearly together, and -- this
is the point -- they assert while the parser's TAIL WORK may still be in
flight. The DRM documents that tail work exists: it pads every flush because
"the card actually (partially?) reads the first of these commands" past
PRIMEND, citing page 4-16 of the G400 manual (mga_dma.c:132-135).

With real drawing (every other scene), the queued primitives take hundreds
of microseconds to drain after the parser finishes fetching; by the time the
three status bits assert, the parser has been quiet for ages. The completion
this driver observes in scissor mode is a HOT completion; everywhere else it
is a cold one. That is the asymmetry that matches "scissor freezes, nothing
else does".

The instrumentation adds ~10-25 us of userland between submissions -- two
gettimeofday calls and a pass over the batch. It cannot touch the kernel.
What it can do is move WHERE in the post-completion window the next
submission's register traffic lands. That matches "the pair, probabilistic"
and matches instrumentation raising the frequency without being able to
cause anything by itself.

## The protocol comparison

Three drivers exist for this engine. X.Org's DDX never runs primary DMA (it
uses the pseudo-DMA window). The Matrox DRM is the only other driver that
does what we do -- PRIMADDRESS/PRIMEND lists -- so it is the reference that
matters.

Where we agree with the DRM:
- Before starting/extending a list, both require the full three-bit
  condition (trap clear, engine idle, ENDPRDMASTS): our quiescence test
  `(ENGSTATUS & DONE_MASK) == ENDPRDMASTS` (.m:5028) is bit-for-bit the
  DRM's `mga_is_idle` (mga_drv.h:688-691) and its pre-flush wait
  (mga_dma.c:117-123).
- Rewriting PRIMADDRESS is allowed under exactly that guard: the DRM's
  wrap_end does it (mga_dma.c:216, guarded at mga_drv.h:254-260). So our
  per-submission PRIMADDRESS rewrite is DEFENSIBLE -- it is a wrap every
  submission, under the DRM's own wrap guard.
- Both pad past the trap so the tail fetch has somewhere harmless to read.

Where we depart, and nobody else does what we do:

**D1. OPMODE is rewritten on every submission, while streaming primary
DMA.** osmgaStormInitState reads OPMODE and writes it back with the
DMA_BLIT bits (.m:1524-1532) on every submission. The DRM -- the only other
primary-DMA driver -- never writes OPMODE at ALL: the symbol appears once in
its register list and nowhere in its code. X.Org writes it only in paths
where primary DMA is not running (init/state at mga_storm.c:1013-1140, and
around its pseudo-DMA window at 2237-2300). We are alone in touching the
DMA-mode register between DMA lists, thousands of times a second.

**D2. The twelve state writes happen BEFORE the full quiescence check.**
Our order is: pre-idle (drawing-busy byte only) -> FIFO admit -> InitState's
twelve writes including OPMODE -> ICLEAR -> full quiescence -> doorbell.
So the CPU's register traffic lands guarded only by "drawing engine idle",
not by "parser fully quiet". The DRM cannot make this mistake structurally:
its state writes travel IN the DMA stream, so they cannot race the parser.

The mechanism this suggests: on a hot completion, the parser's tail work is
still in flight when the next submission's CPU writes arrive -- InitState's
OPMODE rewrite above all -- and some collision of CPU register traffic with
parser tail state makes the card hold the bus in retry forever. The card
holding off CPU writes by PCI retry is not speculation: X.Org's WAITFIFO is
bypassed exactly when PCI retries are enabled (mga_macros.h:32), and this
driver's own FIFO comment records that the card "stalls the bus rather than
dropping the write". A permanent bus stall advances no counter, writes no
log, and stops the machine -- the signature of all five freezes.

What this hypothesis does NOT explain cleanly: the instrumentation makes the
gap LONGER, and a longer gap should land the traffic AFTER the tail work
more often, not inside it. It still changes the phase, and the phase is the
only thing it can change -- but the direction is against intuition and the
honest statement is "it moves the phase", not "it narrows the margin".

## The proposed change (not made yet)

Two reorderings in the submit path, no new writes, no removed writes:

1. Verify full quiescence FIRST: move the three-bit check before InitState
   and the FIFO admit, so no CPU register traffic reaches the engine until
   the parser is provably quiet. One semantic change rides along: today a
   stale un-acknowledged trap would be cleared by our ICLEAR and then waited
   out; after the reorder it would hit the give-up latch instead. A stale
   trap should be impossible (completion requires it set and acks it), and
   if it happens I want it latched and logged, not silently absorbed.

2. Write OPMODE only when it needs to change: InitState already reads it;
   skip the write when the value it would write equals what it read. After
   the first submission this makes us match the DRM -- OPMODE untouched at
   runtime -- while keeping the guarantee that it is right.

Both are order/skip changes; every register still receives the same value
before every EXEC, so the 20-scene byte identity, the scene baselines and
the quick suite apply unchanged.

## The test protocol, with the reboot budget computed

Before the fix, 5 of 5 scissor+instrumentation runs froze within 120
frames; the 95% one-sided lower bound on that freeze probability is 0.549.
After the fix, run the same reproducer up to five times, stopping at the
first freeze:

    clean runs    confidence the fix changed the behaviour
        3                    90.8%
        4                    95.9%
        5                    98.1%

A clean run costs nothing; only a freeze costs a reboot. So the whole test
spends AT MOST ONE reboot, and five clean runs give 98% confidence. This is
the distribution-aware design the probabilistic behaviour demands, at the
price of one reboot instead of ten.

## Questions

1. Attack D1 and D2. Is there any reading of the sources under which the
   per-submission OPMODE rewrite, or state writes before full quiescence,
   are known SAFE -- something I missed that makes the DRM's abstinence a
   coincidence rather than a rule?
2. Attack the hot-completion mechanism. Is there evidence in either
   reference against the parser having tail work after ENDPRDMASTS asserts?
   The DRM pad comment is my only positive evidence.
3. The instrumentation-direction problem in the mechanism section: is there
   a better account of WHY extra userland time raises the frequency?
4. Is the reorder safe? What breaks if quiescence is checked before the
   FIFO admit and the state writes -- is there a path where the trap is
   legitimately still set at entry to a submission?
5. Is the test protocol statistically honest, and is stopping at the first
   freeze the right rule?
