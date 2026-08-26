# The reboot-needing fixes, batched into one reboot

Four kernel changes, each small and already analysed in earlier
cross-reviews; bundling them spends ONE reboot instead of four. None touches
the hot 3D submit protocol's order (the freeze investigation's ground).

## K1: the three diagnostics that write more FIFO slots than they reserved

The driver's own comment records the consequence: "writing past the free
count ... the card stalls the bus rather than dropping the write". All three
are in `Raster Test` opt-in diagnostics, off by default.

  a) D3-3b compare bands (current line 7362): reserves 16, writes 17
     (DSTORG, ZORG, twelve DRs, DWGCTL, FXBNDRY, then YDSTLEN+EXEC).
     FIFO max is 16, so asking 17 would clamp and spin forever.
     Fix: one more `osmgaStormWaitFifo(base, 1U)` before the EXEC write.

  b) D3-4b/D3-4c (7739, 7939): reserve 12, then write DSTORG plus
     osmgaTextureSetup's twenty.
     Fix: make osmgaTextureSetup SELF-GATING -- it returns int now, waits
     16 before its first sixteen writes and 4 before its last four, and
     both callers check the return (goto fifo, as their other waits do).
     Self-gating fixes every caller at once and an extra wait at a caller
     that over-reserved is harmless: the wait is a floor check, not a
     ledger.

## K2 -- deliberately NOT in this bundle

The 3D submit path programs a narrow clip and never restores it (3-64).
The only inheritor is doDisplayBlit, which the window server never calls
(IO_DISPLAY_CAN_BLIT deliberately not advertised, string absent from
WindowServer and mach_kernel) and which only the probe test client reaches.
Restoring would add writes to the submit path that froze six times for
unknown reasons; that trade stays refused until the freeze is understood.

## K3: the byte status read becomes the dword everyone else uses

osmgaStormWaitIdle reads ENGSTATUS+2 as a BYTE and tests bit 0 -- the
MGAISBUSY() shape X.Org marks with a Mystique freeze warning and disables
in one place because "this will hang if the PLLs aren't on".  The DRM polls
status as full 32-bit reads only.  The helper now reads the dword and tests
MGA_STATUS_DWGENGSTS -- the same bit through a different access width, so
it is value-identical; all 43 callers go through the helper.
Explicitly NOT claimed as a freeze fix; deferred cleanup taken because a
reboot is happening anyway.

## K4: a recovered submission is a success, not a failure

When the completion poll gives up but the recovery poll then sees the FULL
three-bit completion (trap fired, engine idle, DMA ended), today the code
acknowledges the trap, logs "software may have the surface back" -- and
still returns failure.  Userland sees a failure whose verdict is OK, which
means "the engine may have drawn some of it", and revokes acceleration
permanently.  For a submission that provably COMPLETED.  And because the
failure path never marks the surface soiled, the mirror can hand back stale
data: reporting it wrong makes the picture wrong, not just the speed.

Fix: a `done3` flag set by both the normal ack and the recovery-saved ack;
the final-idle wait, the settling read and `rc3 = 1` move behind it, shared.
The recovery-latched arm is untouched.  osmgaWaitStat/telemetry unchanged.

The condition the recovery poll accepts is bit-for-bit the condition the
normal poll accepts; the only difference is HOW LONG it took to become
true.  Success cannot depend on which loop observed it.

## Gates after the reboot

Boot diagnostics (M1-2a pixel identity), the three identity gates, tnr,
waits telemetry (gave-up 0, entry trap 0), frame time.  The recovery path
is exercisable without a natural timeout: the injection knob forces the
completion poll to report a timeout it did not suffer, so `waits <...> 1`
plus one submission proves K4 end to end (recovery saved -> submission
reported as SUCCESS now; before, it revoked).

## Questions

1. K1a: is one extra wait(1) before EXEC right, or does splitting 16+1 at
   that boundary change anything the test measures?
2. K1b: does self-gating osmgaTextureSetup break any caller's accounting?
3. K4: is there ANY reading under which a recovery-observed completion
   should still be reported as failure -- something the late observation
   loses that the prompt one has?
4. K3: any caller of osmgaStormWaitIdle that depends on the byte read's
   side effects (a narrower bus cycle, the specific address)?
