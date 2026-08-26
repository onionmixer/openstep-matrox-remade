# R8 — the 16/32 MB declaration: scenarios, before any code

Status: **scenario document for review.**  No code, no plan-to-build yet.
The operator asked for the consequences to be worked through in detail first.

## What the control drives, in two parts

1. **Whether OpenGL can work at the chosen resolution** -- a flag the driver
   computes and the capability bits report, and which the inspector shows
   BEFORE the reboot.
2. **How much VRAM is reserved for OpenGL and where it lives** -- the
   offscreen window's start, its ceiling, and the addressing the Mesa
   back end does inside it.

The second is where the memory comes from.  The first is what makes the
control usable: a radio button whose effect is invisible until a reboot is a
control nobody can reason about, and this driver's own inspector already
says as much about its switches.

## The matrix, computed

Only `RGB:888/32` can accelerate at all -- `CAP_READY` requires four bytes
per pixel -- so fifteen of the twenty modes answer the same way whatever the
declaration.  For the five that can:

| display mode | 16 MB declared | 32 MB declared |
| --- | --- | --- |
| 640x480 | 10.2 MiB -> GL up to 1280x1024 | 26.2 MiB -> **1600x1200** |
| 800x600 | 9.4 MiB -> 1280x1024 | 25.4 MiB -> **1600x1200** |
| 1024x768 | 8.0 MiB -> 1280x1024 | 24.0 MiB -> **1600x1200** |
| 1280x1024 | 5.8 MiB -> 1024x768 | 21.8 MiB -> **1600x1200** |
| **1600x1200** | **3.1 MiB -> 800x600** | **19.1 MiB -> 1600x1200** |

Two things to read off it.  At 32 MB every 32-bit mode can run a
full-screen-sized GL buffer.  And the 1600x1200 row at 16 MB says 3.1 MiB
where today it says nothing at all -- because that row assumes the
empty-window registration, which is a separate fix this work needs anyway.

## Scenario 1 -- the operator opens Configure today, board unknown

Selects `1600x1200 RGB:888/32`, leaves memory at 16 MB.

The panel should say, in its own words:

```
Video memory:  (o) 16 MB   ( ) 32 MB
OpenGL at 1600x1200, 32-bit:  3.1 MB offscreen, buffers up to 800x600
```

Reboot.  The driver registers the window empty, programs the mode, proves
7 -> 12 MiB, opens the window to 12 MiB, and 1600x1200 has a 3.1 MiB
window where it had none.  `caps` reports `MMAP yes / READY yes` and a
3,260,416-byte window.

**What is new here is not the declaration.**  It is the empty-window
registration.  Even a 16 MB board gains a usable window at 1600x1200.

## Scenario 2 -- the operator selects 32 MB and the board really is 32 MB

Panel:

```
Video memory:  ( ) 16 MB   (o) 32 MB
OpenGL at 1600x1200, 32-bit:  19.1 MB offscreen, buffers up to 1600x1200
```

Reboot.  The ceiling becomes 28 MiB, `osmgaProveVramTo` runs from 7 MiB to
28 MiB and every page answers with its own signature, no witness moves, the
window opens to 28 MiB.  `teapot_hybrid` at 1600x1200 draws on the card.

## Scenario 3 -- the operator selects 32 MB and the board is really 16 MB

The panel says the same thing as scenario 2, because the panel is showing
what the declaration WOULD give.  It cannot know better; nothing has
measured the board.

At boot the proof runs 7 -> 28 MiB.  Computed for that exact case: of the
5,376 words it writes, 2,304 are real memory, **14 wrap onto a witness** and
**1,280 wrap into the proof's own range where the readback sees the wrong
signature**.  The proof fails, the ceiling stays where the last successful
proof left it, and the window is what a 16 MB board would have had.

So a wrong declaration **cannot hand out memory that is not there**.  What
it costs is written down honestly: 1,778 of those writes land below 7 MiB
without individually being witnessed, which is the visible framebuffer and
the guard rows.  The framebuffer clear at the end of the mode set wipes the
visible part, so the cost is a flash during boot, not a lasting artefact.

**The operator must be told what happened.**  The log says so, and the panel
cannot -- which is the honest limit of a declaration, and the reason the
panel's line should read "would give", not "gives".

## Scenario 4 -- a mode that cannot accelerate at all

Selects `1600x1200 BW:8`.

```
Video memory:  (o) 16 MB   ( ) 32 MB
OpenGL at 1600x1200, greyscale:  not available -- needs 32-bit colour
```

The declaration is still honoured for the offscreen window; it just does not
buy OpenGL, because `CAP_READY` needs four bytes per pixel.  Saying so in
the panel answers a question this project has already been asked once.

## Scenario 5 -- the operator changes the mode but not the memory

The line under the radio changes as soon as the mode selection changes,
because it is computed from both.  That is the whole point of part 1: the
consequence is visible at the moment of choosing.

## What has to be true for the panel to be able to say any of this

The inspector is a Configure.app plug-in.  It does not talk to the driver;
it reads and writes the configuration table.  So it computes the line from
`Display Mode` and `MGA Memory Size` -- the same two strings the driver
reads.

**That is a duplicated calculation, and duplicated calculations disagree.**
The window start, the ceiling and the buffer-fit arithmetic must live in one
free-standing C file compiled into both the reloc and the inspector, the way
`OpenStepMGAManualConfig.c` already is for the memory-size parser.  If the
panel and the driver ever disagree, the panel is lying, and a lying panel is
worse than no panel.

## What the driver side has to do

Part 1, the flag:

- `CAP_READY` today is `mmioMapped && linearModeActive && bytesPerPixel == 4`.
  It does not ask whether the window is large enough to hold anything.  At
  1600x1200 with a 3.1 MiB window it would answer "ready" for a GL buffer
  that cannot fit.  It should mean "this mode can accelerate", which is the
  flag part 1 asks for.
- `CAP_MMAP` must mean "the window is published AND usable", not "the cdev
  exists" -- otherwise the empty-window interval advertises a zero-length
  window as available, and the Mesa probe caches that answer for the life of
  the process.

Part 2, the memory:

- the ceiling stops being the constant 12 MiB and becomes
  `declared - 4 MiB`;
- the proof's own `to > MGA_VRAM_16MB` bound must move with it, or the
  ceiling can rise and nothing can ever prove it;
- registration publishes an empty window rather than refusing, so
  1600x1200x32 has something to open;
- the `osmgaProbeAlias` page is not mapped until a non-empty window exists;
- the extent probe's restore hole is fixed -- a word that was written but
  whose readback mapping failed is currently never restored, though the
  comment beside it says it is.

## Open questions this document does not answer

1. **Should the driver write the proven result back anywhere the panel can
   read it?**  It would turn scenario 3's panel from "would give" into
   "gives", but a driver writing its own configuration table is a new
   behaviour with its own hazards, and Configure would fight it.  Proposed
   answer: no.  The log is the record, and the panel says "would".
2. **What does the panel show when `Display Mode` is absent or unparsable?**
   The driver falls back to 1024x768 RGB:888/32; the panel must show the
   same fallback, not a blank.
3. ~~**Does the inspector's nib have room?**~~  Computed, not assumed.
   The present layout inside `MODE_VIEW` is caption at y=6, the two switches
   at 24 and 44, the grey label and matrix at 66, so the highest control
   tops out at 81 with `GROW = 95` -- **14 px of headroom**, not the 11 I
   had said.

   Two more rows -- a label plus the 16/32 radio at y=85, and the OpenGL
   status line at y=104 -- top out at 118.  So `GROW` must rise from 95 to
   **130** to keep the same 12 px of headroom; 120 leaves 2 px and 110 does
   not fit at all.

   That is a 35 px taller box.  Whether Configure's own panel accommodates
   a box that tall is not something arithmetic can answer -- it is a look at
   the screen, and it is cheap, because the nib can be built and installed
   without a reboot.

---

## Cross-review verdicts (codex terra, 2026-08-26)

Every citation was opened; every number was recomputed in python.

| codex claim | how it was checked | verdict |
|---|---|---|
| The driver's physical trust boundary is 16 MiB (`ranges[0].size`, `mapFrameBufferAtPhysicalAddress:length:`) | read `OpenStepMGAReplacementDisplay.m` 2938-2947 | true |
| Therefore raising the ceiling opens an unproved physical range | the proof's own comment (9052-9058) justifies safety by "under sixteen megabytes so inside BAR0", and `MGA_VRAM_16MB` is a hardcoded constant, never a measurement | concern real, blocker refuted -- see below |
| A failed proof deliberately leaves signatures behind | read 9178-9190: `if (bad == 0UL)` guards the zeroing | true |
| At 1600x1200 the clear repairs only 0..7.32 MiB, so wrapped writes survive above it | python: of 3,072 wrapped words, **1,861 land under the visible 7,680,000 and are cleared**; **1,197 land at or above it and survive** | true, but the surviving set is the high one, not the low one |
| A failed 7->28 proof cannot leave "what a 16 MB board would have had"; widening advances only on complete success, so an initially empty window stays empty | read 3504-3513: the widen is one all-or-nothing step from `PROVEN` to `CEILING` | **true -- the most important catch in the review** |
| 5,376 is the page count, not the word count; 10,756 stores | python with `PAGE_SIZE = 8192`: `(28-7) MiB / 8192 = 2,688` pages x 2 words = **5,376 words** | false -- codex assumed 4096 (5,376 pages is exactly the 4096 answer) |
| The 2,304 / 14 / 1,280 / 1,778 partition describes one endpoint per page, not all writes | python enumerates **both** endpoints of every page: 5,376 addresses total, partition reproduced exactly | false |
| The 16 MB / 1600x1200 window is 3,264,512 bytes, not 3,260,416 | python: `start = align_up(7,680,000 + 1,638,400, 8192) = 9,322,496`; `12 MiB - start = 3,260,416`. 3,264,512 is the PAGE=4096 answer | false -- same root error, third time |
| `CAP_READY` should mean "at least one valid acceleration surface", not "OpenGL can accelerate" | `OpenStepMGAMesaBuffer.c` 833-870: the allocator judges the *caller's* row length rounded to 32 px, plus stride, pitch and overflow tests the driver never sees | true -- adopt the narrow predicate |
| A tightened predicate will not make live clients re-probe (result cached for process lifetime) | `OpenStepMGAMesaProbe.c` 219-236: `probeDone`/`probeCached`, re-run only when the pid changes | true, and nearly harmless -- a mode change requires a reboot, so no live process ever sees the answer change |
| The matrix is a colour-plus-depth-reservation classification, not a generic "largest GL buffer" | `OpenStepMGAMesaBuffer.c` 74-90: depth is reserved whether or not it is requested; textures have a separate test | true -- label it |
| Scenario 5 has no UI mechanism | `OSMGADisplayInspector.m` 91-100: `setTable:` is the only place the panel reads the mode; nothing observes the stock resolution picker | true -- S5 must be reworded |
| The shared file must also carry both enable switches | `VRAM Mmap` gates registration (2982), `Mesa Acceleration` gates the library (2969-2975) | true |

### Why the aperture objection is a requirement, not a blocker

The aperture has already been measured on this machine, non-destructively:
`docs/TEST_STATUS.md` H1 S1 -- BAR0 base `0xf8000000`, size `0x02000000`
(**32 MiB**, prefetchable), by write-1s then read mask then restore, with the
command register untouched and all three BARs verified restored. Base
`0xf8000000` is exactly 32 MiB-aligned, which a 32 MiB BAR must be.

So the method is proven safe here, and the fix is to stop hardcoding it:

- **A1.** Size BAR0 at init and keep the result. Clamp the ceiling, every
  proof, and the registration invariant to `min(declaration, BAR0 size)`.
  Sizing does not disturb the picture -- scanout reads VRAM internally and
  never goes through the host aperture -- but no `IOLog` may run inside the
  window, because legacy VGA text decode rides the same command bit.
- **A2.** Free disproof, worth having anyway: if `fbPhysical` is not aligned
  to the declared size, the BAR cannot be that large. Refuse the declaration.
- **A3.** `end > MGA_VRAM_16MB` at registration (2999) is bypassed by the
  widening path, which assigns `CEILING` directly. Both must test the
  measured aperture instead of the constant.

### What the empty-window catch forces

One all-or-nothing 7->28 MiB proof is the wrong shape once the window may
start above `PROVEN`. At 1600x1200 the window starts at 9,322,496, so a
board that fails at 28 MiB would get **nothing**, where today's 16 MB
assumption would have given it 3,260,416 bytes.

- **B1.** Prove in stages: 7->12 MiB first, publish that, then attempt
  12->ceiling as a separate step. A 16 MB board that wrongly declares 32
  then keeps the 12 MiB result instead of losing the window entirely.
- **B2.** A stage that fails leaves its signatures. Stage two's failure
  contaminates 12..28 MiB, which no successful stage has handed out -- but
  stage one's range is below it and already published, so the stages must
  run low-to-high and stop at the first failure. Never widen past a failure.

### Scenarios to add

S6 32 declared, BAR aperture only 16 MiB; S7 16 declared on a smaller board;
S8 true 32 MB but ring/alias setup fails; S9 `VRAM Mmap` off, or
`Mesa Acceleration` off, with 32 declared; S10 proof failure cleanup,
including the reserved top of VRAM; S11 wrong-16-on-true-32 (safe, limited);
S12 mode changed in the stock picker while Configure is open.
