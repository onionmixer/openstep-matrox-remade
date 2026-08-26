# The freeze, narrowed by reasoning rather than by more reboots

The machine has frozen three times. We are stopping the freeze-and-reboot
experiments and reasoning from the code.

## What the third experiment established

`prof scissor 120 i1` froze: the instrumentation reduced to ONLY the two
gettimeofday calls around the submit ioctl, with the register-change counting
and the histogram off. So the bookkeeping is irrelevant. What is left of
"inst" is two system calls per submission and nothing else.

## The narrowing

1. **The frame loop enters the kernel in exactly one place.** Userland makes
   three ioctls in the whole back end -- CAPS once at startup, PRESENT, and
   SUBMIT -- and this harness never presents; it ends frames with glFinish.
   The accelerated clear is a batch, so it is SUBMIT too. With `i1` there is
   also gettimeofday.

2. **Every bounded loop in SUBMIT now latches and logs on timeout.** The
   latch is set BEFORE the log, and it disables acceleration for the 3D
   path, the 2D blit and present alike. A machine that latched would fall
   back to software and stay alive.

3. **No log appeared and the machine froze.** So no bounded loop reached its
   limit. (Freeze #1 happened before the latch existed, so the latch cannot
   itself be the cause.)

4. **Therefore the hang is inside a single access that never completes**, not
   inside a loop. The loops advance only after a read returns.

## What can block, and what the driver already knows

The driver's own comment on the FIFO wait says it outright:

    "writing past the free count is safe: the card stalls the bus rather
     than dropping the write"

So a FIFO overflow stalls the bus -- the exact signature of a freeze that
advances no counter and writes no log.

The submit path's own FIFO accounting has margin, though: it reserves 13
slots and issues 11 writes into the drawing-register bands (0x1c00 and
0x2c00). OPMODE, ICLEAR, PRIMADDRESS and PRIMEND are all in the 0x1e00
control band and do not queue.

So the blocking access is either a drawing-register write when the FIFO is
full for a reason the reservation did not predict, or a read the chip retries
forever while it is bus-mastering.

## What scissor actually changes

Not just pixel count. The clip registers are programmed by the CPU before the
DMA starts and are NOT rewritten by the command list -- the list writes
DSTORG and never YTOP, YBOT, YDSTORG or CXBNDRY. So with the 8 by 8 box:

    CXBNDRY  (7 << 16) | 0     instead of (W-1) << 16
    YTOP     0
    YBOT     7 * pitch         instead of (H-1) * pitch

Every trapezoid is still submitted and still edge-walked; almost every span
is clipped away. The engine finishes each list far sooner: the completion
poll's largest index falls from 294 to 30.

## Questions

1. Given the narrowing above, what single access in this path can block
   forever, and under what chip state? Be specific about which register.
2. Is there an MGA behaviour where a small YTOP/YBOT window, or a clip that
   excludes everything, leaves the drawing engine in a state where it neither
   completes nor accepts?
3. The two gettimeofday calls only add about 9 us either side of the ioctl.
   What in the chip cares about that, given that a longer gap should give it
   MORE time, not less?
4. Is there any instrumentation that would survive a hard freeze on this
   system and tell us the phase -- and if not, what is the cheapest
   experiment that does not need one?
