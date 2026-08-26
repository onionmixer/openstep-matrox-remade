# The freeze is reproducible, and the safety net caught nothing

## What is now established

Two hard freezes, both on exactly `prof scissor 120 inst`, one client only.

    scissor  20              ok    12.27 ms/frame
    scissor  20  (again)     ok
    scissor 120              ok    11.98 ms/frame
    scissor 120 inst         FREEZE   (twice, on two different boots)
    frame   120 inst         ok
    nolight/grid8/small 120 inst   ok

So it is neither scissor alone nor the instrumentation alone. It is the pair,
and it is deterministic rather than a rare event.

The second freeze happened with ONE accelerated process running. That
disposes of the two-client theory for good; it was already refuted by the
kernel's snapshot copy of the batch and by the timeline.

## The safety net caught nothing

Between the two freezes I added, to the four submit-path waits that failed
silently, a latch and a log line naming the wait and ENGSTATUS. After the
freeze and reboot there is NO 3-61 line in /usr/adm/messages.

Two readings, and I cannot tell them apart from here:

  (a) no wait reached its limit, so those loops are not the mechanism;
  (b) one did, but IOLog never reached the disk -- it writes the kernel
      message buffer and syslogd copies it to the file, and syslogd cannot
      run if the kernel is spinning inside the driver.

I did not think hard enough about (b) before relying on the log.

The driver carrying that net has now booted cleanly three times, with the
boot diagnostic M1-2a passing its pixel-for-pixel comparison of the DMA path
against MMIO (900 pixels, 0 differing). So the net itself is not breaking
anything.

## What the three parts of "inst" are

    1. two gettimeofday calls around every submission -- a system call,
       measured at 4.58 us each on this machine
    2. a pass over the batch counting which registers changed since the
       previous trapezoid -- plain userland array work
    3. the change-pattern histogram -- plain userland array work

Two of the three cannot reach the kernel at all. Only the first does, and
what it does is not compute anything: it adds about 9 us of system call
either side of the submit ioctl, which changes the PHASE of the submission
stream and nothing else.

Scissor is the other half of the pair, and what scissor changes is also
timing: with an 8 by 8 box almost no pixels are filled, so the engine
finishes each list nearly at once. The completion poll's mean index goes from
30.9 to 22.1 and its largest from 294 to 30.

So both halves of the reproducing pair are timing, and neither is geometry.

## What I have prepared but not run

The instrumentation is now three separately settable bits rather than one
switch, so the component can be found in three short runs instead of by
guessing. It is userland only; no reboot needed.

## Questions

1. Given that two of the three parts cannot enter the kernel, is the timing
   part the only candidate, or is there a way userland bookkeeping could
   freeze this machine?
2. What does a submission stream with 9 us more slack either side actually
   change for this hardware -- what races with what?
3. Is there a way to get evidence out of a hard freeze on this system that
   does not depend on syslogd running? The driver keeps counters in its own
   memory; is there anything that survives a reboot?
4. Should the next step be the three-way split, or is there something safer
   that would learn as much?
