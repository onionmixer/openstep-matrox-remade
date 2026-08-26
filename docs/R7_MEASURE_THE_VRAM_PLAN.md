# R7 — more offscreen memory for OpenGL, by measuring the card

Status: **plan, fourth draft.**  No code changed.  The three previous drafts
were each blocked by cross-review; what follows records what was wrong with
them, because two of the mistakes were the same mistake in opposite
directions.

## The goal

Not "know the card size".  **More offscreen VRAM for OpenGL at 1600x1200, if
the board is really 32 MB.**

## What blocks it, computed

At 1600x1200x32:

```
visible 1600*4*1200                      = 7,680,000
guard   256 rows                         = 1,638,400
window start, page aligned               = 9,322,496   (8.89 MiB)
registration bound OSMGA_S1_VRAM_PROVEN  = 7,340,032   (7.00 MiB)
start >= bound  ->  the cdev is NEVER REGISTERED       (:2995-3002)
```

It is the only one of the twenty published modes in that position.  And the
ceiling that decides how large any window may grow is the constant
`OSMGA_S1_VRAM_CEILING = 12 MiB` (`:224`) -- wrong by 16 MiB if the board is
32 MB.

| 1600x1200x32 | window | largest GL buffer |
| --- | --- | --- |
| today | **none** | none |
| registration fixed, 16 MB board | 3.11 MiB | 800x600 |
| registration fixed, **32 MB board** | **19.11 MiB** | **1600x1200, 8.12 MiB arena** |

The other nineteen modes each gain exactly **16.00 MiB** of window if the
measurement says 32.  Computed for all twenty.

## What is known about the board

| | evidence |
| --- | --- |
| aperture **32 MiB** | H1 S1 sized BAR0 by config write-1s -> mask -> restore, restore verified, command register unchanged (`docs/TEST_STATUS.md`) |
| board a **32 MB candidate** | R2 maps subsystem `0d43` to G45FMDVP32DSF, 32 MB DDR |
| populated VRAM | **never measured** -- the S3 that would have is recorded as not run |
| `MGA Memory Size = 16` | an override someone typed; R2.1 says it is not physical evidence |

## Writing above fitted memory is the reference driver's ordinary path

`scratch/xf86-video-mga-2.0.0/src/mga_driver.c`, `MGACountRam`, `probe_size`
32768 KiB for G400/G450 (`:298`): it writes `0xAA` at every 2 MiB boundary
top-down over the whole 32 MiB, then reads back and takes the highest that
retained it.  Inside BAR0 throughout.

**Aliasing is therefore not what the hardware does** -- the same value goes
everywhere, so a 16 MiB board that aliased its top half down would read
`0xAA` at 32 MiB and be reported as 32.  An earlier draft of this plan
asserted the opposite; it is withdrawn.  Separately checked: the only state
that routine needs is CRTCEXT3 bit 7, "enable linear frame buffer"
(`:1197`), which normal mode programming sets anyway (`mga_dacG.c:1404`);
and **it does not restore its test bytes** (`:1255`), which ours must.

## The design

### 1.  Register with an EMPTY window rather than not registering

`d_mmap` reads the bounds fresh every call and already refuses an empty
window (`:2284-2288`), and `osmgaMmapWindowEnd` is already mutable because
M1-4F1 raises it (`:3504`).  Only "does the cdev exist" is fixed at init.

So when `VRAM Mmap = Yes`, register whatever the conservative bound says: if
it leaves room, publish `start..7 MiB` as today; if not, publish
`start..start`, which refuses every client request, and let the proof open
it.  Registration stays exactly where it is, so the unproven DriverKit
question about registering later does not arise.

**Four things must move with it, and three were found by cross-review:**

- **The M1-4F1 guard.**  It requires `osmgaMmapWindowEnd == aligned(7 MiB)`.
  An empty 1600x1200 window has `end == start == 8.89 MiB`, so it would
  never prove and never open -- inert at exactly the mode this is for.  The
  guard becomes "registered, and the current end is below the ceiling".
- **`CAP_MMAP`.**  It is set on `osmgaMmapRegistered` alone (`:4221`) while
  the shared ABI means "the VRAM window is published".  During the empty
  interval a client would be told MMAP is available with a zero-length
  window; traced, `CAP_REQUIRED` would pass, the probe would answer
  `HARDWARE`, and the surface allocator would then refuse on
  `height > avail / (stride*4)` with `avail == 0`
  (`OpenStepMGAMesaBuffer.c:842`).  Nothing crashes and nothing unproven is
  handed out, but the caller is told the wrong thing -- and **the Mesa probe
  caches its verdict for the life of the process** (`probeDone`,
  `OpenStepMGAMesaProbe.c:59`), so the wrong answer would stick.  So
  `CAP_MMAP` becomes conditional on the window being at least a page.
- **`osmgaProbeAlias`.**  Registration maps an uncached page at the window
  start (`:3122`) for the batch settle-word.  On an empty window that maps
  unproven VRAM before any measurement.  It is set up only once a non-empty
  range exists.
- **`runMmapWindowTest`** assumes a non-empty range and would report false
  failures at `start == end`.

### 2.  Measure once, on a fixed grid, in one transaction

`osmgaProbeVramExtent` (`:9270`) already saves every sampled word before
writing any, writes an address-encoding signature, reads back, restores and
verifies the restore.  It runs at the mode set, screen blanked.

- **A fixed grid at 18, 20, 22, 24, 26, 28, 30, 32 MiB** -- X.Org's own
  step, above the conservative 16 MiB baseline.  Eight boundaries.
  A grid phased from the visible image was the second draft's mistake and
  python found it: stepping 2 MiB from `firstMb` **misses the 32 MiB
  boundary in nine of the twenty modes**, so a 32 MB board would have been
  reported as 16 in nine of them.
- **Sample at `G - 512 KiB` and `G - 1 MiB`**, not at the first and last
  word of the MiB.  Two separate reasons, both found by computing rather
  than reading:

  1. **Witness alignment.**  Witnesses sit every 512 KiB.  Under a wrap, a
     sample at a 512 KiB multiple lands on another 512 KiB multiple, so it
     is either witnessed or inside the sampled set.  The probe's present
     offsets are the first and last WORD of a MiB, and the last-word one
     would alias to `(G-16) MiB - 4` -- four bytes below a witness, and
     invisible.
  2. **The BAR's end.**  An intermediate draft of this plan sampled at `G`
     and `G + 512 KiB`, which for `G = 32 MiB` puts both samples **outside
     the 32 MiB BAR** (33,554,432 is already one byte past the last
     addressable byte).  X.Org samples the LAST byte of each region
     (`(i*1024)-1`) precisely because the test is "does the board have at
     least this much", which is answered from inside the region, not from
     its upper edge.  Corrected offsets keep the highest sample at
     31.5 MiB.

  Computed over the whole grid: sixteen samples, lowest 17.0 MiB, highest
  31.5 MiB, all inside the BAR, all multiples of 512 KiB.

  And computed for board sizes 4, 8, 16, 20, 24 and 32 MiB: **every sample
  is either in real memory, or wraps onto a witness, or wraps onto another
  sample where the distinct-signature test sees it.  Zero invisible in every
  case.**  That is a stronger statement than "witnesses catch a 16 MiB
  wrap", and it holds because every sample and every witness is a multiple
  of 512 KiB, so any wrap factor that is itself a multiple of 512 KiB
  preserves the alignment.
- **One transaction, sixteen slots** (`OSMGA_VRAM_PROBE_MAX` is 32), so
  every candidate is live at once and the distinct-signature test covers the
  whole set.  Stepping would have let a later sample alias one already
  restored, and both would pass.
- **Witnesses across 0 .. 17 MiB at 512 KiB** -- 34 of them, so
  `OSMGA_VRAM_WITNESS_MAX` rises from 32 (`:8981`).
- **Classification, not a byte count**: the answer is 16 or 32.  Anything
  incomplete, any disturbed witness, any failed restore, keeps 16.

### 3.  Fix the restore hole -- a defect in what ships today

```c
if (osmgaMapUncachedBlock(...) != IO_R_SUCCESS) { held[i] = 0; continue; }   /* readback */
/* "Put back everything that was taken, including where the readback failed" */
for (...) { if (!held[i]) continue;      /* and it skips exactly those */
```

The comment states the intent and the code does the opposite.  `held` splits
into saved / written / readable; **everything written is restored**, and a
failed restore is logged and fails the measurement.

`osmgaRestoreWitnesses` is `void` and never verifies that the restore took
(`:9010`).  It must read back and report.

### 4.  The measurement is authoritative; the key does not cap it

`MGA Memory Size` is **removed from the limit calculation entirely** and
logged only.  Every shipped table says `16` (`Default.table:43`,
`Instance0.table:13`, `pkg/Instance0.release.table:51`), so either direction
of override defeats the feature: winning upward keeps the old bug, and the
third draft's "downward-only cap" would pin every normal boot to 16 MiB.
That was the same mistake twice, once in each direction.

### 5.  Every 16 MiB constant, not just the ceiling

| line | what | becomes |
| --- | --- | --- |
| `:2939` `ranges[0].size` | declared resource | the measured BAR, 32 MiB |
| `:2947` `mapFrameBufferAtPhysicalAddress:length:` | ordinary FB mapping | **stays 16 MiB** -- every visible mode fits under it and the probe uses its own uncached mappings.  This is an assumption and is tested, not asserted |
| `:2999` `end > MGA_VRAM_16MB` | registration sanity | measured |
| `:3184`, `:3186` | the manual-memory clamp | deleted; see 4 |
| `:3606` `byteEnd > MGA_VRAM_16MB` | S1 self-test bound, behind the proven bound already | measured |
| `:5733` `-displayMemorySize` | reported capacity | measured |
| `:9084` `to > MGA_VRAM_16MB` | **the proof's own bound** | measured -- without this the proof can never reach past 16 MiB and the whole change is inert |

### 6.  The proof on failure -- and one thing cross-review asked for that is refused

`osmgaProveVramTo` writes every page of its range and deliberately leaves
the signatures when it fails.  Cross-review asked for the whole range to be
saved and restored transactionally.  **Refused, with reasoning.**

Computed: a 7..28 MiB proof writes 5,376 words, so saving them costs 21 KiB
of kernel memory.  That is affordable -- but it protects nothing.  The range
is memory above the window that nothing else uses; if it is real, leaving
signatures is harmless and success zeroes it anyway.  If it aliases, the
damage lands **below** the range, where the range's own saved words cannot
reach.  Only witnesses can see or repair that, and the real gap is that
**witness restore is unverified** -- which is fixed in 3.

What IS adopted: the proof must fail closed on any restore or map failure,
and must not widen on a partial result.

## Order of operations

1. init: register, empty window if the conservative bound leaves no room;
   no probe alias yet; ceiling still 12 MiB.
2. first mode set, blanked: measure -> 16 or 32.
3. ceiling := measured - 4 MiB.
4. prove from the current end up to the ceiling; the window opens to what is
   proven; the probe alias and `CAP_MMAP` follow.

## What must not change, and the one thing that must

On a 16 MB result every existing decision must read as today: same
registration, same 7 MiB bound, same widening to 12 MiB.  New measurement
lines will exist, so "identical logs" is the wrong test -- identical
DECISIONS is the right one.

**The deliberate exception**: at 1600x1200x32 the cdev is absent today and
will be registered (empty, then opened) after this.  That is the point of
the change and must be called out rather than hidden inside "unchanged".

## Test plan

Offline (done, python):

1. The fixed grid samples the 32 MiB boundary in all twenty modes, fits the
   probe array, and its highest byte 33,554,428 is inside the 32 MiB BAR.
2. 18 MiB is above the largest visible image (7.32 MiB), so the grid never
   reaches down into one.
3. Every grid sample is inside the BAR, is a multiple of 512 KiB, and for
   board sizes 4, 8, 16, 20, 24 and 32 MiB has zero invisible aliases.
4. The 32 MiB ceiling is >= the 12 MiB one for every mode.

On the machine, one boot each:

5. 1024x768x32: read the measurement.  **Either answer is a result.**  Zero
   disturbed witnesses; if not, the board aliases, which contradicts the
   X.Org reasoning -- stop and report rather than work around it.
6. If 16: every pre-existing decision line unchanged, `caps` window
   unchanged, teapot output unchanged.
7. If 32: ceiling 28 MiB, `caps` reports the larger window, and the teapot's
   output is **byte-identical** -- more offscreen memory must not move a
   pixel.
8. 1600x1200x32, the point of the exercise.  Judge it by the **acceleration
   split and the caps window**, not by whether `teapot_hybrid` runs -- that
   binary is built to succeed in software too.
9. 1600x1200 BW:8 and 640x480 RGB:555/16 -- the two modes whose `firstMb`
   parity broke the second draft's grid.

## Honest limits

- The board may be 16 MB.  Then 1600x1200x32 gets 3.11 MiB and an 800x600 GL
  buffer, and the other nineteen modes gain nothing.  Still more than today,
  and a measurement either way.
- "Blanked" is not proof the aperture is exclusive: the cdev is published
  before the first mode set, a client could hold a mapping, and this driver
  has never controlled the second head.  The measurement being once-only and
  early narrows it.  The claim is "nothing of OURS is scanning out".
