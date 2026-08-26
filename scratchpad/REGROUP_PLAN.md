# The DMA blocks are grouped by what the registers MEAN, not by when they change

## Where the frame goes now

14.24 ms = user 6.5 + sys 7.65. The system half is not a mystery any more:
the submit ioctl, timed from userland, is 7.99 ms of it. Fitted by least
squares over four scenes (teapot, nolight, grid8, small), each a separate run:

    us = 85.1 * submissions + 0.1049 * dwords

    scene     measured     model    error
    teapot      958383    940855    -1.8%
    nolight     419107    408795    -2.5%
    grid8      1309806   1318478    +0.7%
    small       271983    302622   +11.3%

So of the teapot's 7.84 ms, 2.83 is per-submission and 5.01 is the engine
ingesting the list. Merging brackets -- the lever priced earlier -- can only
reach the first term, 1.92 ms at best. The list is the bigger half.

## What the list is made of

A DMA block is one packed index dword and four value dwords: five dwords for
four registers, whatever the registers are. The tracker writes a block only
if something in it changed. Measured: 26.55 dwords a trapezoid.

The blocks are grouped the way a person would group them -- the edge
registers together, red and green together, alpha together:

    dwgctl,ar0,ar1,ar2      100.0%   <- dwgctl changes 1.9%, ar1 99.8%
    ar4,ar5,ar6,sgn         100.0%
    Rstart,Rdx,Rdy,Gstart    60.9%
    Gdx,Gdy,Bstart,Bdx       60.8%
    Bdy,Zstart,Zdx,Zdy       99.9%   <- Bdy changes 34.6%, Zstart 99.9%
    alpha x4                  1.9%
    (tail: pad,pad,fxbndry,YDSTLEN+EXEC)

Two registers that change on almost every trapezoid are dragging six that
change on a third of them.

## What they actually do

Marginal rates cannot answer this: a block goes out if ANY member changed, so
the cost of a grouping is the JOINT pattern. I recorded the whole 25-bit
change pattern per trapezoid and kept the commonest 64 -- 99% of the traffic
in every scene -- so any candidate grouping can be priced exactly rather than
argued from averages.

Annealing against the WORST of the four scenes, not the best, and using the
two dead slots the tail block already has:

    tail's two free slots: sgn, Zstart
    Gstart,Gdx,Rstart,Bstart     61 /  2 / 53 / 59 %
    ar6,ar4,ar5,ar1             100 /100 /100 /100 %
    Rdy,ady,dwgctl,adx           35 /  2 / 32 / 47 %
    Zdy,ar0,ar2,Zdx              77 / 77 / 80 / 89 %
    Gdy,Bdx,Rdx,Bdy              35 /  2 / 33 / 49 %
    alphactrl,a0                  2 /  2 /  1 /  4 %

    scene      now     new    saved
    teapot   26.17   20.46    21.8%
    nolight  20.26   14.22    29.8%
    grid8    25.26   19.93    21.1%
    small    26.14   22.45    14.1%

Every scene improves. Through the fitted model the teapot's ioctl goes 7.84
-> 6.75 ms, so the frame goes 14.24 -> 13.15, 70.2 -> 76.1 fps.

## Why this cannot change the picture

Not "the tests will catch it" -- it cannot happen.

Before each EXEC the engine must hold, for every register, that trapezoid's
intended value. The tracker writes a register whenever any member of its
block changed. A register whose own value changed is therefore ALWAYS
written, under any grouping, because it is a member of its own block. A
register whose value did not change may or may not be written, and if it is
not, the engine still holds the previous trapezoid's value -- which is the
same value.

So the engine's state at every EXEC is identical under any grouping. The
grouping decides only how many blocks carry it.

That argument rests on one thing: that these registers persist across
YDSTLEN|EXEC. The tracker already rests on it and has since it was measured.

## Ordering

Grouping unrelated registers together is what Matrox's own DRM driver does:
mga_state.c emits DSTORG, MACCESS, PLNWT and DWGCTL in one block and
ALPHACTRL, FOGCOL, WFLAG and ZORG in the next -- four unrelated families, and
DWGCTL in the last slot rather than the first. Slot order is application
order (the G400 reset block writes DWGCTL then LEN+EXEC in one block), so the
only constraint is that the execute comes last.

## The change

In osmgaHW3DEncode: which registers ride in which block, and two of them into
the tail block's dead slots. Nothing about what is computed, what is
compared, or when a block is skipped. The userland delta counter's blockOf[]
table has to move with it or it stops describing the kernel.

The textured tail is three blocks rather than one and has more dead slots
than the untextured one; the two promoted registers go in the FXBNDRY block
there.

This is the kernel driver, so it needs a reboot to test.

## Questions

1. Is there an MGA ordering constraint I have missed -- specifically, must
   DWGCTL be written before the AR registers, since it selects the operation
   that gives them meaning?
2. Is the persistence argument sound for every one of these 24 registers, or
   is there one the engine does NOT latch across EXEC?
3. The annealing minimises the worst of four scenes. Is that the right
   objective, and are four scenes enough to keep it from fitting the teapot?
4. Is there a register in this set whose value is written but never read
   because the opcode in use ignores it -- which would make it free to drop
   rather than merely cheap to regroup?
5. What is bigger than this that I have not looked at?
