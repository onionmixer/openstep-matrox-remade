# The regrouping landed; the prediction did not

## What happened

The DMA block regrouping is in and the picture is unchanged: 20-scene
hardware-against-hardware byte identity 0 moved, 14 scene baselines 0 moved,
quick suite 0 failing.

    dwords a trapezoid   26.55 -> 20.85     predicted 26.17 -> 20.46
    dwords a frame       47757 -> 37493     -21.5%, predicted -21.8%
    ioctl a frame         7.99 -> 7.30 ms   -0.69 ms, PREDICTED -1.09 ms
    frame (median of 5)  14.24 -> 13.48 ms  70.2 -> 74.2 fps

The list shrank exactly as designed. The time did not follow.

## Two candidate causes

### A. The new encoder costs more CPU in the kernel

The emission loop is now table driven: it fills a 24-value array, compares up
to 24 values, builds four register/value pairs per emitted group, and copies
24 values to the previous-state array. The old one compared 12 and called
osmgaDmaBlock with literals.

A three-factor least squares over the four scenes (submissions, dwords,
trapezoids) fitted per-trapezoid cost 0.26 us before and 0.88 us after --
which at 1798 trapezoids a frame would be +1.1 ms, more than the whole gain.

I do not believe it, for two reasons. The same fit puts the per-dword cost at
94.6 ns before and 70.4 ns after, and that number is the engine's ingest rate
-- it cannot change because the kernel's source changed. Four points and
three parameters, with dwords and trapezoids strongly correlated across these
scenes, is not enough to separate them.

### B. The wait was never all list ingest

The completion poll's own counter separates the wait from the rest of the
ioctl. Per frame, poll iterations went 1170 -> 1050 while dwords went 47757
-> 37493: the list fell 21.5% and the wait fell 10.2%.

Taking the poll iteration at 4, 4.5 or 5 us, the non-poll remainder of the
ioctl goes 3.31 -> 3.10, 2.72 -> 2.58, or 2.14 -> 2.05 ms. It DECREASES on
every assumption and never rises. If the new encoder cost a millisecond of
kernel CPU that remainder would have to grow, and it does not.

Solving the two submission-level points for a poll model of
wait = fixed + b * dwords:

    poll 4.0 us/iteration ->  46.7 ns a dword, 74 us a submission not the list
    poll 4.5 us/iteration ->  52.5 ns a dword, 83 us
    poll 5.0 us/iteration ->  58.3 ns a dword, 92 us

So the engine ingests at roughly half the rate the earlier fit implied, and
about 80 us a submission is spent waiting on something that is not list
length -- the drawing itself, the DMA setup, the trap.

The earlier two-factor fit could not see this because across the four scenes
dwords-per-submission and submissions moved together; it folded part of the
per-submission cost into the per-dword coefficient and so over-predicted what
shortening the list would buy.

## What I think this means

The measurement is right, the model was wrong, and the model was wrong in a
way that four scenes could not reveal. 0.69 ms is the real number and the
change is worth keeping.

It also revalues the remaining lever: if a submission carries about 80 us of
cost that is not list length, then cutting submissions from 33.3 to 11 is
worth around 1.8 ms -- more than the 1.92 ms the old fixed term suggested,
not less, and now the bigger of the two remaining levers rather than the
smaller.

## The one real defect

The boot diagnostic M1-2a asserts that a repeated trapezoid saves exactly
three blocks. It now saves six, which is the point of the change, so the
assertion fired and the rest of that test -- including its pixel-for-pixel
comparison of the DMA path against MMIO -- did not run this boot. The
assertion is stale, not the code. M1-2c's refusal is unrelated: it is in the
four previous boots' logs as well.

## Questions

1. Is B right, and is A really excluded by the non-poll remainder argument?
2. Is there a third cause I have not considered?
3. Is the poll iteration cost really about 4-5 us, and does the 4 us IODelay
   quantise the wait enough to matter at these numbers?
4. Does the revalued submission cost change what should be done next?
