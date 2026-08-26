# Stage 2: the restore, default off, and safe whatever ATTR[0x10] turns out to say

## Why it can be written before the value is read

The restore refuses to run unless three things hold: a snapshot was taken,
the switch is on, and ATTR[0x10] bit 0 says the console was in a GRAPHICS
mode. If that bit says text, the snapshot has no font planes and no restore
built on it could be right -- so it logs that once and does nothing, which is
exactly today's behaviour. So the code is safe to install before the value is
known, and the same reboot that carries it also prints the value.

## Why testing it is bounded

-revertToVGAMode is followed by -enterLinearMode on the way back in, and
-programLinearMode rewrites every register the restore touches. So a restore
that leaves the card wrong is overwritten by the next entry: logging out and
back in is a complete test, and the failure mode is the black screen we
already have rather than a machine that cannot return.

## The switch

A parameter of its own, OSMGAHW3DVgaRestore, one word, 0 or 1, default 0 --
not a fourth word on the tuning parameter, because this one ends in
reprogramming the display and should not be reachable by a client tuning
batching.

## The order, which is X.Org's for this card

  0. no snapshot, or switch off, or ATTR[0x10] says text -> return.
  1. claim the engine; if it cannot be claimed, return without touching it.
  2. protect: SEQ[0] = 0x01 (reset asserted), SEQ[1] |= 0x20 (display off).
  3. do NOT touch the PLL. The saved MISC selects clock 0, the VGA clock, so
     restoring MISC is what takes scanout off the MGA PLL. Programming the
     PLL back would be a second, unnecessary way to get that wrong.
  4. indexed DAC: a named subset, not a blind 0..0x4f sweep -- the same
     registers -programLinearMode writes, plus PAN_CTL at 0xa2.
  5. CRTCEXT 0..5.
  6. MISC.
  7. SEQ 1..4 while reset is still asserted.
  8. clear CRTC[0x11] bit 7, then CRTC 0..24 (which puts the saved 0x11 back,
     protection bit included).
  9. GR 0..8.
 10. ATTR 0..20 with the palette address source clear.
 11. palette: mask 0xff, write address 0, 768 bytes, then the saved mask.
 12. re-latch CRTCEXT0.
 13. unprotect: restore saved SEQ[1], SEQ[0] = 0x03, then attribute index
     0x20 to put video back on.
 14. release the engine.

Every step is a bounded number of byte writes. No loops that can spin.

## What it still does not restore

The console's pixels. Our linear framebuffer overwrote them and
-programLinearMode clears the visible framebuffer besides. Messages written
after the restore will land and be seen; whatever was on screen before will
not come back. For a shutdown screen that is the whole requirement.

The hardware cursor registers (direct RAMDAC 0x0c..0x0f) are not saved. The
console does not use a hardware cursor.

## Questions

1. Is the order above right for this card, and is there a step whose position
   matters that I have put in the wrong place?
2. Which indexed DAC registers should the named subset contain? I intend to
   take exactly the set -programLinearMode writes, so that what we changed is
   what we put back, plus PAN_CTL.
3. Is claiming the engine the right thing to do here, or does taking the
   claim risk blocking shutdown if the engine is wedged?
4. Anything in this sequence that can hang rather than merely look wrong?
