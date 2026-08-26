# Plan: give the console its card back — stage 1, SAVE ONLY

Driver: openstep-matrox-remade/OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay_reloc.tproj/OpenStepMGAReplacementDisplay.m

## The symptom and what it is

On reboot the console should come back at a low resolution and show shutdown
progress. With this driver it stays black. Boot-time low resolution is fine,
because at that moment the card is still in the state the BIOS left.

-revertToVGAMode sets a flag and calls super. It restores no hardware. The
DriverKit header says the subclass must: "back into a state where it can be
used as a standard VGA device ... implemented by subclasses in a device
specific way" (driverkit/IOFrameBufferDisplay.h:56-60). Both stock OPENSTEP
display drivers in the mirror do exactly that -- TsengLabsET4000 rewrites the
general, sequencer, CRTC, graphics and attribute registers from a stored VGA
mode and re-enables the timing sequencer; CirrusLogicGD542X writes the MISC
output register. X.Org's MGA driver saves VGA mode registers, the palette and
(on the primary card) the text fonts on entry and restores them on exit.

Meanwhile -programLinearMode rewrites SEQ, GR, ATTR, MISC, CRTC, CRTCEXT, the
PLL and the DAC palette, and sets MGA_CLKSEL_MGA in MISC -- the clock source.
Nothing in this source ever clears that bit: it is defined once and set once.
So the console writes text into a card that is timing a 1024x768 linear
scanout from somewhere else. Black is the correct outcome of what we do.

## Why this is being staged

This is the change the project's own testing rule warns about: reprogramming
display, clock and mode registers on a live machine, where a mistake is a
hard hang rather than a wrong picture. And the framework calls
-revertToVGAMode BEFORE -enterLinearMode (measured: this boot shows
enterLinear 1, revertVGA 1), so a restore that misbehaves does not merely
break shutdown -- it breaks the way into the desktop.

So stage 1 saves and proves the save, and changes no behaviour at all.

## Stage 1 -- what goes in

1. A one-shot save, taken at the TOP of -programLinearMode before it writes
   anything, guarded so it happens exactly once per load:

       MISC (read 0x1fcc)
       SEQ    0..4     (index 0x1fc4, data 0x1fc5)
       CRTC   0..24    (index 0x1fd4, data 0x1fd5)
       GR     0..8     (index 0x1fce, data 0x1fcf)
       ATTR   0..20    (index 0x1fc0, with the INSTS1 read to reset the
                        flip-flop first)
       CRTCEXT 0..8    (index 0x1fde, data 0x1fdf)
       DAC palette 256 x 3 and the pixel read mask

   It is taken after the framework's first -revertToVGAMode has run, which is
   the state the console actually had.

2. A log line with enough of it to judge from the machine: MISC, SEQ[1],
   CRTC[0], CRTC[9], CRTC[23], CRTCEXT[0], CRTCEXT[3], and a count of
   non-zero palette entries.

3. NO restore. -revertToVGAMode is untouched in stage 1.

## Stage 2 -- only after stage 1's values are seen and judged

- restore behind a settable flag, default off, so it can be turned on live
  and tested by logging out rather than by rebooting into it;
- the CRTC write protect (CR11 bit 7) cleared before writing CRTC 0..7 and
  restored after;
- the font/text planes, which our linear framebuffer has overwritten -- X.Org
  saves them with VGA_SR_FONTS precisely because entering graphics mode
  destroys them, so registers alone may restore sync and still show nothing
  readable;
- only then, making it the default.

## What stage 1 cannot break

It reads registers and writes none. The only new failure mode is reading a
register that is not readable at that moment; each read is a byte from a
mapped MMIO aperture the driver already reads elsewhere in the same function.

## Questions

1. Is the top of -programLinearMode the right moment, given the framework
   calls -revertToVGAMode first? Is the state there still the console's, or
   has super's revert already changed it?
2. The attribute controller: reading it needs INSTS1 read to reset the
   address/data flip-flop, and the index register must be restored with bit 5
   (palette address source) handled. Is my sequence right, and can reading it
   disturb the display that is currently on screen?
3. Is reading the DAC palette through 0x3c00/0x3c01 safe while the card is in
   text mode, and does reading it disturb anything?
4. CRTCEXT: how many registers does the G450 actually have there, and is
   reading an index past the end harmful?
5. Anything in this save that could itself hang or corrupt the display.
