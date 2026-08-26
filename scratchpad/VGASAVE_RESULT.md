# Stage 1 result: the snapshot, and what it says about stage 2

## What the boot logged

    OpenStepMGA V1: saved the console's card -- misc e3 seq1 01 crtc0 5f
      crtc9 40 crtc17 e3 ext0 00 ext3 00 dac[0] 00 dac[19] 00 mask ff

## How I read it

    MISC 0xe3   bit0=1 colour I/O at 3Dx (matches the driver's 0x1fd4 base)
                bit1=1 RAM enable
                clock select = 0  -> 25.175 MHz, the VGA clock
                sync polarity negative/negative
    CRTC[0x00] = 0x5f -> horizontal total 100 characters -> 800 pixel clocks.
                800 clocks at 25.175 MHz, negative/negative, is 640x480 @ 60.
    CRTC[0x09] = 0x40 -> max scan line 0, so character height ONE, double
                scan off, line compare bit 9 set.
    CRTC[0x17] = 0xe3 -> standard linear addressing, sync enabled.
    CRTCEXT[0] = 0x00, CRTCEXT[3] = 0x00 -> no extended addressing, MGA
                extended mode off: the card is in plain VGA.
    pixel read mask 0xff.

Every value is the textbook one for a 640x480 VGA mode. The snapshot is sane.

## Two things this settles

1. **It corroborates the diagnosis exactly.** The console runs on clock
   select 0, the VGA clock. -programLinearMode ORs MGA_CLKSEL_MGA (0x0c) into
   MISC, which is clock select 3, and nothing in the driver ever puts it
   back. CRTCEXT3 is 0 here and non-zero after we program. So the card the
   console gets back at shutdown is timing something else entirely.

2. **The console is a GRAPHICS mode, not text.** Max scan line zero means a
   character height of one; a text mode needs fourteen or sixteen. So there
   is no character generator in play, and therefore **no font planes to save
   or restore** -- which was the part of stage 2 I expected to be worst.
   What our linear framebuffer destroys is the console's pixel content, not
   its font; new messages written after a restore will land and be seen.

## The other change in this boot

The boot self-test now exercises the tracker's skip, and reported it:

    M1-2a: two identical trapezoids encode to 90 dwords untracked and 75
           tracked -- the tracker took out the 15 the second one repeats
           (packing on)
    M1-2a: PASS -- the batch drew exactly what MMIO drew, pixel for pixel

90 and 75 are exactly what was predicted before it was built.

## What stage 2 would be, on this evidence

Restore, behind a default-off switch: MISC, SEQ 1..4 (0 stays the reset
register), CRTC 0..24 with the write protect in CR11 bit 7 cleared first and
put back after, GR 0..8, ATTR 0..20 with the palette address source handled,
CRTCEXT 0..5, the indexed DAC 0..0x4f, the palette and the pixel mask. No
fonts. Order matters: X.Org protects the sequencer around the whole restore
(vgaHWProtect), which this would copy.

## Questions

1. Does anything in these values contradict "the snapshot is the console's
   state"?
2. Is my reading that a max scan line of zero rules out text mode -- and
   therefore rules out needing the font planes -- sound?
3. For stage 2, what is the correct ORDER to write these back, and what
   must be protected while it happens?
4. Is there any state the console depends on that this snapshot still does
   not contain?
