# Plan: make the boot self-test exercise the tracker's skip

Driver: openstep-matrox-remade/OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay_reloc.tproj/OpenStepMGAReplacementDisplay.m
Test: -runHW3DBatchTest, which runs from -enterLinearMode at every boot.

## Why

The tracker skips a trapezoid's colour and alpha blocks when nothing in them
changed since the previous trapezoid of the same list. It is on by default
since the last reboot. But the boot self-test builds a batch of exactly ONE
trapezoid, and the tracker starts empty on every encode, so the first
trapezoid always writes all three blocks. The test therefore exercises the
packing (its list went 60 -> 55 dwords, which is the FXBNDRY block) and never
the skip.

The functional authority for the skip is the twenty-scene byte-identity check
in userland, and it stays there. What is missing is a cheap check at boot, on
the machine, in the kernel's own encoder.

## The change

In -runHW3DBatchTest, after the existing trapezoid is built:

    batch->triCount = 2;
    batch->tri[1] = batch->tri[0];        /* the same trapezoid, twice */

Then encode twice into the same list buffer, with the encoder's snapshot
forced each way, and compare the two lengths:

    osmgaTrackStateNow = 0; totalOff = osmgaHW3DEncode(...);
    osmgaTrackStateNow = 1; totalOn  = osmgaHW3DEncode(...);
    /* three blocks of five dwords, skipped on the second trapezoid */
    if (totalOff - totalOn != 15) -> report a PROBLEM

The submitted list is the tracked one, and the existing pixel comparison
against the MMIO reference is unchanged.

## Why two identical trapezoids are safe to draw

They draw the same pixels twice, and this primitive is idempotent: DWGCTL is
TRAP | ATYPE_I with no depth mode addressed, ALPHACTRL is opaque, and the
colour registers are constant across the shape (dr[1], dr[2], dr[4], dr[5],
dr[7], dr[8] are all zero from the batch clearing, so only the three start
values matter). Opaque replace over the same pixels with the same values
gives the same picture, so the existing comparison against the MMIO band
still holds -- the same argument this repository already makes for a clear
that may have been half done.

The validator sees two trapezoids it has already accepted as one, and
validates each independently.

## Expected numbers

    1 trapezoid, packed              55 dwords   (observed at the last boot)
    2 trapezoids, packed, untracked  90          (+35, seven blocks of five)
    2 trapezoids, packed, tracked    75          (+20, three blocks skipped)

## What this does NOT prove

That a CHANGED block is re-emitted. Both trapezoids are identical, so the
test asserts only that identical state is skipped. The re-emit case is what
modg covers in the userland byte-identity check, where a scene gives every
corner its own colour.

## Questions

1. Is forcing osmgaTrackStateNow around the two encodes acceptable in the
   driver's own test, or should the encoder take the flag as an argument so
   nothing has to reach into its snapshot?
2. Is drawing the same opaque trapezoid twice really idempotent on this
   engine, or is there a mode in the test's DWGCTL that makes the second
   pass differ?
3. Is 15 the right constant to assert, or should the test compute it from
   the block layout so a future change to the encoder does not silently
   turn the assertion into a lie?
4. Anything else cheap that this test could assert while it is being touched?
