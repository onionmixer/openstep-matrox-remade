# Kernel plan v2: one reboot, three experiments

Driver: openstep-matrox-remade/OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay_reloc.tproj/OpenStepMGAReplacementDisplay.m
NeXT Mach 4.2 DriverKit, i386, cc 2.7.2.1, C89.

## What is now MEASURED, from userland, no reboot spent

The driver fills verdict/triangle/dwords/spins on every submission (.m:1594-1607).
Accumulating those inside the Mesa hook's submit helper, with the ioctl timed by
gettimeofday (clock granularity measured at 4 us), over 60-frame runs:

    grid2:  151.4 us/submission,  582.7 dwords,  14.0 traps,  139.4 poll index
    grid4:  374.9 us/submission, 2199.7 dwords,  54.5 traps,  345.7 poll index

    us/submission        = 74.1 + 5.52 x traps
    poll index/submission = 68.0 + 5.10 x traps
    dwords/submission     = 23.1 + 39.95 x traps
    => one ENGSTATUS read costs 1.08 us
    => the engine takes 5.52 us to swallow one trapezoid
    => 138 ns per list dword, i.e. 28.9 MB/s of register traffic

Per frame (1828 traps, 33.5 submissions): 2.48 ms fixed + 10.09 ms = 12.58 ms,
against 12.6-13.3 ms of measured system time. The model closes.

Scissoring the picture to 8x8 with the SAME trapezoid count moves the poll index
from 346 to 285 and the time from 375 to 331 us. So the wall is not pixel fill;
it is the rate at which the engine ingests the register stream.

## What I want the reboot to answer

H1. Does the completion poll steal PCI bandwidth from the DMA it is waiting for?
    It reads ENGSTATUS every ~1.08 us, five times per trapezoid, on the same bus
    the engine is using to fetch its list.
H2. Is the cost proportional to LIST DWORDS or to TRAPEZOID COUNT? Today the two
    move together (40 dwords per trapezoid, always). Cutting dwords without
    cutting trapezoids separates them.
H3. Where does the fixed 74 us per submission go? Five distinct waits exist and
    only one is currently counted.

## The change

(a) Telemetry. Per wait -- pre-idle (.m:1099 helper), FIFO admission (.m:1128
    helper), state programming (.m:4278), pre-DMA quiescence poll (.m:4282),
    primary completion poll (.m:4298), final engine idle (.m:4305), and the
    recovery completion poll (.m:4350/4373) -- record count, summed read count,
    minimum, maximum, timeout count, and a five-bucket histogram (1, 2-4, 5-16,
    17-256, >256). Outcomes counted separately: refused before claim, prewait
    failure, quiescence timeout, normal completion, recovery completion,
    permanent latch. Elapsed time from clock_value(System) (kernserv/
    clock_timer.h:29, documented callable from any IPL), with
    clock_attributes(System)->resolution recorded once so the numbers can be
    believed or discarded. ns_time_t is 64 bit and getIntValues carries
    unsigned, so every total goes out as two 32-bit words, behind a version
    word, read under the existing lock so a reader cannot see a torn total.
    Out through a new getIntValues parameter, following the exact-size
    convention already used at .m:3570 and .m:3605. The submit block ABI is
    not touched.

(b) Switch 1, default off: poll backoff. When set to N > 0 the primary
    completion poll does IODelay(N) between reads. Default 0 reproduces today's
    loop exactly. The spin limit is scaled so the worst-case wall time of the
    wait does not grow beyond what it is today (today: 100000 reads x 1.08 us
    ~= 108 ms).

(c) Switch 2, default off: pack FXBNDRY. Today the trapezoid's FXBNDRY goes out
    in a block of its own with three DMAPAD slots (.m:7801-7810 area), five
    dwords to carry one register. With the switch on it rides in the preceding
    alpha block, which keeps it before the YDSTLEN|EXEC that must follow it.
    That is 5 dwords off 40 -- 12.5% -- with no register value changed and no
    register reordered relative to the execute.

Nothing else changes. The barrier read at .m:4288-4292 stays exactly as it is;
measuring it must not be the same act as changing it.

## Gates

Before install: the no-hardware checks under openstep-matrox-remade/tools/ that
cover the command path and the mesa back end.
After reboot, with both switches off: the fourteen scene baselines unchanged,
the regression suite at zero, and the frame measurement within noise of today's
20.2 ms -- telemetry that costs measurable time has already failed.
Only then are the switches turned on, one at a time, live.

## Questions

1. Is IODelay inside the completion poll safe here, and is ENDPRDMASTS a level
   the engine holds, or a transient a slower poll could miss? If it can be
   missed, H1 has to be tested another way.
2. Switch 2: is there an ordering rule I am missing that puts FXBNDRY in its own
   block on purpose? The comment above it explains why it left the YDSTLEN
   block, not why it may not share the alpha one.
3. Does clock_value(System) cost enough to distort a 5 us phase, and is there a
   cheaper monotonic source in this kernel?
4. What in this change could hang the machine rather than merely mismeasure?
5. If you could only keep ONE of (a), (b), (c) for this reboot, which, and why?
