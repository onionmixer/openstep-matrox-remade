# The submit path can burn a third of a second in the kernel and say nothing

## What the machine did

It froze hard during a 3D test and left NOTHING in /usr/adm/messages -- the
log jumps straight from the last good line to the next boot. A hard freeze
with no trace means the kernel never got to write one, which points at a loop
inside a driver rather than a panic.

## The loops

The 3D submit path has five waits. Three of them are bare spins on an MMIO
read with NO delay and a limit of OSMGA_S1_SPIN_LIMIT = 100000
(OpenStepMGAReplacementDisplay.m:930):

    wait 0  pre-idle      osmgaStormWaitIdle          .m:1468-1478
    wait 1  fifo admit    osmgaStormWaitFifo          .m:1486-
    wait 2  quiescence    inline before the doorbell  .m:4961-4964

The driver's own measurement puts a poll read at 1.08 us, so each of those is
108 ms of kernel time when it runs to the limit, and three of them is 324 ms
for one submission. At 33 submissions a frame that is 10.7 seconds a frame.
The console stops, the network stops being serviced, and nothing is logged,
because none of the three logs anything on timeout.

The other two waits are already disciplined: the completion poll has a delay,
a recovery poll, a log line and a permanent latch of acceleration
(.m:5104-5135), and that path has been exercised with injected timeouts.

## What they actually take

Read off the driver's own telemetry after 3993 real submissions at full load
(the teapot, 120 frames):

    wait          entered     reads    mean   largest
    pre-idle         3993      3993    1.00         1
    fifo admit       3993      3993    1.00         1
    quiescence       3993      3993    1.00         1
    completion       3993    123213   30.86       294
    final idle       3993      3993    1.00         1

The three undelayed waits have never taken more than ONE read. The limit is
a hundred thousand times the observed maximum, and every iteration past the
first is a bus transaction that buys nothing.

For scale, the longest legitimate engine-busy period seen anywhere is the
completion poll's 294 reads at a 4 us delay, about 1.2 ms.

## What I mean to do

1. **Bound the three waits in the 3D submit path only.**
   osmgaStormWaitIdle has 43 callers -- the whole 2D blit path, mode setting,
   every boot diagnostic. Shortening the shared limit would put the desktop
   at risk to fix a 3D problem. So: add explicit-limit variants, have the
   existing functions call them with OSMGA_S1_SPIN_LIMIT so no other caller
   changes at all, and use the bounded form only at .m:4901, .m:4903 and the
   inline loop at .m:4961.

   The bound: 8000 reads, about 8.6 ms. That is 8000 times the observed
   maximum and seven times the longest legitimate engine-busy period, and it
   takes the worst case for one submission from 324 ms to 26 ms.

2. **Say so.** Each of the three logs on timeout, naming which wait and what
   ENGSTATUS held. Today they are silent, which is why the freeze left
   nothing to read.

3. **Latch on the quiescence timeout.** That one is immediately before
   handing the engine a DMA list, and the reason the completion path latches
   -- "the engine may still be writing" -- applies exactly. The two admission
   waits do NOT latch: a busy engine is a recoverable condition the code
   already handles by refusing the submission, and a permanent latch on a
   transient would cost acceleration until the next reboot.

## What this does and does not do

It does not fix whatever made the engine stop answering, and it does not
prove that is what happened -- the freeze left no evidence and I cannot say
what caused it. What it does is make the next occurrence survivable and
legible: a log line naming the wait and the status, acceleration off, a
machine still running.

It is deliberately not a reproduction attempt. Reproducing first would freeze
the machine again and leave nothing again.

## Questions

1. Is 8000 right, or is there a legitimate case where one of these three
   waits should take longer than 8.6 ms? What is the engine doing at each of
   the three points, and what is the longest it may honestly take?
2. Is not latching on the admission waits the right call, or does a busy
   engine before a DMA submission carry the same danger as a busy one after?
3. Is there a fourth silent spin in this path I have not found?
4. Does IOLog from this context carry any risk of its own -- it is called
   from the same place, and a log per submission would be its own denial of
   service. Is one line then a latch enough to bound that?
5. Is there anything in the freeze evidence that argues AGAINST these three
   being the mechanism?
