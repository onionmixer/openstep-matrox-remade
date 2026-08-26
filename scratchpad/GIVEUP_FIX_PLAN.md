# Two small kernel changes: make the next freeze legible, and stop reserving
# exactly as many FIFO slots as we write

Not a fix for the freeze. The cause is still unknown and this does not chase
it. These are the two defects the reasoning turned up, and the first is mine.

## 1. The give-up handler touches the chip before it records anything

I added a latch and a log to the four submit-path waits that used to fail in
silence. The handler begins:

    static void
    osmgaSubmitGaveUp(int which, const char *what, vm_address_t base)
    {
        unsigned long st = osmgaR32(base, MGA_ENGSTATUS);   <-- FIRST

        if (which >= 0 && which <= 4)
            osmgaWaitGaveUp[which]++;
        IOLog(...);
    }

A wait gives up because the chip did not reach a state. If the reason is that
the chip has stopped answering, then the first thing this does is read the
chip that has stopped answering -- and if that read does not return, the
counter is never incremented and the line is never written.

So "no 3-61 line after the freeze" does NOT establish "no wait timed out",
which is what I had been concluding from it. The handler has to record before
it touches anything.

The fix: increment the counter, then log, and do not read ENGSTATUS at all.
Which wait gave up is the information that matters; the two admission waits
never had a status value to hand anyway, and the two that do (quiescence,
completion) already have one in a local.

## 2. Thirteen writes against thirteen reserved slots

The submit path reserves 13 FIFO entries and then writes:

    osmgaStormInitState   12 registers   (PITCH YDSTORG MACCESS PLNWT FCOL
                                          BCOL OPMODE CXBNDRY YTOP YBOT
                                          SRCORG DSTORG)
    ICLEAR                 1

That is exactly 13 -- no margin. I had argued the count was really 11,
because OPMODE and ICLEAR are in the 0x1e00 control band rather than the
0x1c00/0x2c00 drawing bands that the DRM's command lists encode. The
references do not support leaning on that: X.Org reserves twelve slots and
then writes a state batch that INCLUDES OPMODE, CXBNDRY, YTOP and YBOT
(mga_storm.c:990-1013). Being addressable outside the command-list bands is
not the same as being unable to stall.

And the consequence of getting it wrong is the exact shape of this freeze.
The driver's own comment on the FIFO wait says so:

    "writing past the free count is safe: the card stalls the bus rather
     than dropping the write"

A stalled bus write advances no counter, writes no log, and stops the
machine.

The fix: reserve OSMGA_S1_FIFO_MAX (16) rather than 13. It is the clamp
already in the code, described there as "the DDX's own largest ask", so it
cannot spin out for being unsatisfiable, and it gives three slots of margin
over everything the CPU writes before the next check.

## What is deliberately NOT in this

**The clip is not restored.** The 3D submit programs CXBNDRY, YTOP and YBOT
and never puts them back, so the next user of the engine inherits the last
batch's scissor -- an 8 by 8 box after a scissored frame. X.Org resets its
clip to 0xffff0000 / 0 / 0x007fffff after narrowing (mga_storm.c:1205); this
driver does not. It is a real defect and it is written down, but the window
server never reaches the blit path (IO_DISPLAY_CAN_BLIT is deliberately not
advertised, and the string is absent from WindowServer and mach_kernel), the
present path programs its own clip, and adding three more writes to a path
under suspicion is not what to do while the cause is unknown.

## Questions

1. Is removing the ENGSTATUS read from the give-up handler right, or is there
   a way to keep the status that cannot block?
2. Is 16 slots correct, and is there anywhere else in the 3D path that writes
   more registers than it reserved?
3. Does either change alter behaviour on a healthy machine? I believe not:
   the first only runs on a path that never runs, and the second asks for
   more of something that is free.
4. What would make the next freeze leave evidence, given that IOLog needs
   syslogd and syslogd needs the machine?
