# The freeze happens in ONE frame, and my last two conclusions were wrong

## Two corrections first

1. **"Timing alone is enough" was never tested.** The harness printed
   "timing ON, counting off, histogram off" while the library translated the
   mask value 1 into all three -- a legacy special case I had written into
   the setter myself. Both runs I reported as timing-only were the full
   instrumentation. The setter no longer translates anything.

2. **The freeze is not a 120-frame endurance thing.** With the log collector
   running and the output captured raw, the last line received was
   `frame 1`. The warm-up frame completed and its counters printed; the very
   next frame hung the machine. Every earlier run of this reproducer was
   dying in the first second and I had been reading it as "it ran for a
   while and then died".

## The run, exactly

    prof scissor 120 i1        (= all three parts of the instrumentation)

    warm frame                 completed, counters printed, deltas non-zero
    frame 1                    printed
    <machine gone>

`scissor 20` and `scissor 120` without instrumentation complete normally.
`frame 120 inst` and three other scenes with instrumentation complete
normally. So it is scissor plus instrumentation, and it takes about one
frame -- roughly 30 submissions.

## What the evidence channels say now

The give-up handler no longer reads the chip before recording: it increments
a RAM counter, then logs, and touches nothing. The latch is set by the caller
before that. So a wait that timed out would leave the machine refusing
acceleration and alive, and the next frame line would have printed.

The external collector (nxlogd, fsyncing each line of /usr/adm/messages to
the host) captured up to 19:42:59 and shows NO 3-61 line.

That is stronger than last time but still not conclusive: the chain is kernel
message buffer -> syslogd -> file -> nxlogd -> host, and a line emitted at
the instant the kernel wedges never reaches syslogd. What IS new is the
liveness argument: a timed-out wait now sets the latch and returns, the
submission fails, Mesa falls back to software, and the frame loop continues.
It did not continue.

## What I think this leaves

Either no bounded wait timed out and the hang is a bus access that never
returns, or a wait timed out and IOLog itself did not return.

## Questions

1. Can IOLog block on this system when called from an ioctl context with the
   window server running? If the kernel console is the display this driver
   owns, is there a path where logging from the driver re-enters it?
2. Given the freeze is one frame and about thirty submissions, is there
   anything that changes between the first frame and the second -- any
   state that is set up once and only bites on the second use?
3. The warm frame ran with instrumentation and survived. What is different
   about it? It draws the same scene.
4. What is the cheapest experiment now, given each costs a reboot and the
   reproduction is one frame rather than 120?
