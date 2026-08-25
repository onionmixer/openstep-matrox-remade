# Plan: split the kernel's 12.3 ms a frame into named phases (instrumentation only)

Repo /mnt/USERS/onion/DATA_ORIGN/Workspace/NeXT_DRIVER, driver source
openstep-matrox-remade/OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay_reloc.tproj/OpenStepMGAReplacementDisplay.m
OPENSTEP 4.2, NeXT Mach 4.2, DriverKit, i386, cc 2.7.2.1, C89.

## What is known, measured on the machine

Frame 20.2 ms (640x480 teapot, 987 triangles, 1843 trapezoids, present mode):
  user 7.5 ms   sys 12.3 ms   idle ~0
Linear in trapezoid count (least squares, grid 2..8): sys = 2.8 ms + 5.52 us x traps.
Scissoring the picture to an 8x8 box with the SAME 1843 trapezoids going out
drops sys by only 0.8 ms, and turning the depth test off drops it by 0.7 ms.
So the engine writing pixels is under a millisecond; the rest is the cost of
DESCRIBING each trapezoid to the hardware, and it lives in the kernel.

Per frame the client sends 32 submissions; each carries ~58 trapezoids and the
encoder emits about 40 dwords per trapezoid, so the DMA list is roughly 292 KB
a frame.

## What the submit path does today (runHW3DSubmit, .m:3920-)

  1. osmgaHW3DEncode builds the DMA list                       (.m:4225)
  2. osmgaStormWaitIdle + osmgaStormWaitFifo(13), state blocks  (.m:4228-4230)
  3. ICLEAR, then a poll for ENDPRDMASTS quiescence             (.m:4281-4287)
  4. `for (i3 = 0; i3 < total3; i3++) sum3 += list3[i3];`       (.m:4288-4292)
     -- a full read-back of the list before the doorbell
  5. PRIMADDRESS + PRIMEND written, then a poll for DMA done    (.m:4294-4304)
  6. osmgaStormWaitIdle, then the optional settle read          (.m:4305-)

The ring is IOMallocLow memory (.m:2419), so the read-back at step 4 is a
cached read of about 292 KB a frame, not an uncached one.

## What I propose to add, and nothing else

Instrumentation. No register write, no ordering, no limit and no barrier
changes -- in particular step 4 stays exactly as it is; measuring it must not
be the same act as changing it.

Six accumulators, all cumulative since load, plus a submission count:
  T1 encode          around step 1
  T2 pre-submit wait around step 2
  T3 quiescence poll around step 3
  T4 barrier read    around step 4
  T5 doorbell + DMA-done poll around step 5
  T6 final idle wait around step 6
Each measured two ways, because I do not know this kernel's clock:
  (a) IOGetTimestamp (driverkit/generalFuncs.h:88, ns_time_t) deltas, and
  (b) exact iteration counts for the three polling loops, which are exact
      whatever the clock does.
The same block records the clock's own granularity once per submission: the
smallest nonzero delta between two back-to-back IOGetTimestamp calls, and the
cost of the pair itself, so the phase numbers can be corrected or discarded.

Out through a new getIntValues parameter beside the existing ones
(OSMGA_HW3D_STATUS_PARAM at .m:3605 is the pattern), read by a small userland
probe before and after a known number of frames. The submit block's ABI is
not touched.

## Why this needs a reboot, and what I will do about that

It is a kernel driver: build, install with tools/nx-install-driver.sh, reboot.
I want the one reboot to answer as much as possible, so the same change also
adds a settable switch (setIntValues) that does nothing yet -- it only selects
which of two identical code paths runs -- so that a later measurement can be
turned on and off live without another reboot. If you think a do-nothing
switch is dead weight, say so and I will drop it.

## Gates

Before install: the no-hardware checks under openstep-matrox-remade/tools/
that cover the command path and the mesa back end. After reboot: the frame
measurement must be unchanged within noise (instrumentation that costs
measurable time has already failed), the scene baselines unchanged, the
regression suite at zero.

## Questions

1. Is IOGetTimestamp safe and meaningful in this context (DriverKit display
   driver, ioctl thread, interrupts as they are), and what resolution does it
   actually have on NeXT Mach 4.2? If it is the 10 ms tick, the ns numbers are
   worthless for 400 us phases and only the iteration counts survive -- is
   there a better clock available to a driver here?
2. Can adding these calls change the engine handshake -- for instance by
   delaying the doorbell write after the barrier, or by touching a register
   the engine is sensitive to?
3. Is the parameter path the right way out, or would you extend the submit
   block instead?
4. Which phase is this split most likely to mis-attribute?
5. What would make an iteration count misleading here (a loop that usually
   exits on its first read but occasionally runs to the limit would have a
   mean that describes nothing) -- should I record maxima as well?
