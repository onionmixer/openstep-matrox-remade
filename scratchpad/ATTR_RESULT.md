# The canonical test answered: graphics. Now the restore may run.

## What the boot logged

    V1: saved the console's card -- misc e3 seq1 01 crtc0 5f crtc9 40
        crtc17 e3 ext0 00 ext3 00 attr10 01 (graphics) gr6 05
        dac[19] 00 panctl 00 mask ff

ATTR[0x10] = 0x01, bit 0 set: the console was in a VGA GRAPHICS mode. That is
the register X.Org's font save returns on -- "if in graphics mode, don't save
anything" -- so the snapshot needs no character generator planes and the
restore is eligible.

GR[6] = 0x05 agrees independently: bit 0 set is graphics mode, and bits 2-3 =
01 is the A0000-AFFFF 64K window.

So my earlier conclusion was right and my route to it was not. I read it off
the maximum scan line, which is suggestive; the test is this bit, and it now
says the same thing on its own evidence.

## Where that leaves the plan

Stage 2 shipped default-off in this same boot. The three conditions it
requires -- snapshot taken, switch on, console was graphics -- are now two,
and the remaining one is a live setting.

## How I mean to test it

The restore runs in -revertToVGAMode, which happens at shutdown. So the test
IS the reboot: set the switch, reboot, and watch whether the console comes
back instead of staying black.

The failure modes, and why I think this is the right test to run:

- restore leaves the card wrong -> the screen is black, which is exactly
  today's symptom, and the next boot's -programLinearMode rewrites every
  register it touched;
- restore hangs on an MMIO access -> the machine stops during shutdown and
  needs a power cycle. Bounded loops cannot prevent that and neither can any
  other test I could run first;
- the switch does not persist across a boot, so the next boot comes up with
  it off again whatever happens.

## The driver otherwise

Scene baselines unmoved, frame 16.59 and 16.76 ms.

## Questions

1. Does anything about ATTR[0x10] = 0x01 and GR[6] = 0x05 fail to establish
   "graphics, no fonts needed"?
2. Is rebooting with the switch on the right test, or is there a cheaper one
   that exercises the same path?
3. Anything in the shipped restore that the graphics answer changes -- a step
   that is only correct for text, or only for graphics?
