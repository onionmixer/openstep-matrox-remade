# R9 -- the 16/32 MB declaration: build plan

Follows `R8_VRAM_DECLARATION_SCENARIOS.md` and the verdicts appended to it.
Nothing here is written until this plan has been cross-reviewed.

The user asked for exactly two things:

1. a driver-side flag for whether OpenGL is usable at the selected
   resolution, reflected in the UI;
2. VRAM reservation and addressing for OpenGL driven by that resolution.

---

## 1. What the declaration actually buys

**Correction to R8.** R8's matrix said a 640x480 display at 32 MB could hold a
1600x1200 GL buffer. It cannot. `osmgaFillHW3DCaps` publishes
`CAP_STRIDE = displayInfo->rowBytes / 4`, and `OpenStepMGAMesaBuffer.c:846`
refuses any surface whose (32-pixel-rounded) stride exceeds it. **A GL surface
can never be wider than the screen.** The declaration does not buy bigger
buffers at small resolutions; it buys a full-screen buffer at the two large
ones, and texture memory everywhere.

Recomputed in python (`scratchpad/r9matrix.py`), kernel page 8192, measured
aperture 32 MiB, the existing 4 MiB top-of-VRAM margin kept:

| display mode (RGB:888/32) | 16 MB: window / full-screen GL / arena | 32 MB: window / full-screen GL / arena |
|---|---|---|
| 640x480   | 10.203 MiB · yes · 8.445 MiB | 26.203 MiB · yes · 24.445 MiB |
| 800x600   |  9.383 MiB · yes · 6.625 MiB | 25.383 MiB · yes · 22.625 MiB |
| 1024x768  |  8.000 MiB · yes · 3.500 MiB | 24.000 MiB · yes · 19.500 MiB |
| 1280x1024 |  5.750 MiB · **no** · 0      | 21.750 MiB · **yes** · 14.250 MiB |
| 1600x1200 |  3.109 MiB · **no** · 0      | 19.109 MiB · **yes** ·  8.117 MiB |

Only the five `RGB:888/32` modes can accelerate at all; the other fifteen are
1 or 2 bytes per pixel and `CAP_READY` already refuses them.

Two honest sentences follow from the table:

- **The declaration is what makes 1280x1024 and 1600x1200 accelerate.** At
  16 MB neither gets a full-screen colour+depth surface, and neither gets a
  texture arena.
- **The 16 MB column at 1600x1200 is 3.109 MiB, and today it is zero** -- the
  window is never registered because `start (9,322,496) > end (7,340,032)`.
  That fix is independent of the declaration and benefits a 16 MB board too.

The table classifies **colour plus the depth reservation**, which is what the
Mesa back end always reserves (`OpenStepMGAMesaBuffer.c:74-90`), not a generic
"largest buffer". Texture arena is reported separately, as its own column.

---

---

## 2. Cross-review of the first draft (codex terra, 2026-08-26)

Every citation opened, every number recomputed in `scratchpad/r9verify.py`.
Three of the four blockers are accepted and this plan is rewritten around
them; the fourth is answered by removing the risky step rather than defending
it.

| claim | check | verdict |
|---|---|---|
| Publishing 12 MiB then probing 12→28 exposes the published window: stage two wraps into it, and the witnesses it restores sit inside it | python: stage two writes 4,096 words, 1,024 real, 3,072 wrapped, **0 inside its own compare range**, **796 inside the published 9,322,496..12 MiB window**, 6 of them on witnesses | accepted -- **blocker, real** |
| `CAP_READY` requiring a full-screen surface is the wrong bar | python: at 1600x1200 / 16 MB the window is 3,260,416 B and an 800x600 colour+depth pair is 2,885,120 B -- **it fits**, at a stride inside the 1600-pixel cap | accepted -- **my error** |
| `OpenStepMGAManualConfig.c` is not a "both binaries" precedent; it is in the reloc target's `CFILES` only, and the bundle has no `CFILES` at all | read both Makefiles: reloc `CFILES` lists it (`Makefile:20`); bundle has only `CLASSES`/`HFILES` (`Makefile:32-33`) | accepted -- **my error** |
| The panel must always say "would" -- it knows neither BAR size nor proof result | true by construction | accepted |
| The nib table reverses Storm and VRAM-Mmap | `build-inspector-nib.py:154-157`: Storm is placed at `Y_MMAP` (44) and mmap at `Y_STORM` (24) -- the two constants are misnamed | accepted -- **my error**, and a latent trap in the script |
| Range-list ownership is a blocker, not a matter of honesty | `:2288-2296` computes `fbPhysical + off` with no reference to the declared ranges; DriverKit's own rule is not established either way | accepted -- resolved below by declaring the range, not by arguing about it |
| BAR0 write-1s sizing is unsafe while the console is live, and "log only" is not a safe intermediate because it performs the same transaction | the transaction is real, and `COMMAND.MEM` gates legacy VGA decode as well as BAR0 | accepted as a risk -- **and the step is dropped**, see 4A |
| Stage two's detection is weakened because nothing wraps into its own compare range | python: **all 24 witnesses below `from` are written over**, so a wrapping board is caught with certainty; the compare range contributing nothing is not the same as blindness | added -- codex did not state this, and it is what makes the redesign fail closed |

---

## 3. Part 0 -- one arithmetic, compiled into both binaries

The panel cannot ask the driver anything, so the arithmetic exists twice and
two copies drift. `OpenStepMGAManualConfig.c` is the precedent for the
*shape* -- a free-standing C file with a host unit test
(`test/openstep-mga-manual-config-test.c`) -- but **not** for the wiring: it
is compiled only by the reloc target. The new file must be added to the reloc
`CFILES` **and** to a new `CFILES` in the bundle Makefile (a `.c` file does
not belong in `CLASSES`).

`OpenStepMGAWindowMath.{c,h}`, pure C89, no libc, no kernel headers:

```
OSMGAVramDeclaration(value, &bytes, &status)      /* "16" or "32" only */
OSMGAModeFromConfig(displayModeValue, colorSpaceValue, &mode)
        /* the same parsing -selectModeFromConfig: does, legacy BW:4 included */
OSMGAFeatureIsOn(value)                           /* the same "Yes" rule */
OSMGAWindowGeometry(&mode, pageBytes, apertureBytes, ceilingBytes, &geom)
OSMGASurfaceFits(w, h, strideCapPixels, pageBytes, availBytes, &lay)
OSMGAAccelVerdict(&in, &out)                      /* in: everything below */
```

`OSMGAAccelVerdict`'s input carries **predicted and actual as separate
fields**, because they differ exactly when it matters:

- `declaredBytes` -- what the table asks for (both callers have it);
- `apertureBytes`, `windowStart`, `windowEnd`, `hasWindow`,
  `hasCommandWindow` -- what the driver actually established (the driver
  fills these; the inspector leaves them absent);
- `mmapOn`, `mesaOn` -- both switches;
- `pageBytes` -- **explicit, never inherited**. The geometry depends on 8192,
  and the inspector runs in userland where `vm_page_size` is not guaranteed
  to be the kernel's.

`out.text` is the panel's line and the driver's log line from one function.
With the actual fields absent the text is in the conditional -- *"32 MB
declared: would give 19.1 MB offscreen and a full-screen buffer, if the board
has it"* -- and **every** inspector sentence is conditional, including the
16 MB one, because the inspector knows neither the BAR size nor any proof
result.

`OSMGAVramDeclaration` adds the "16 or 32 only" rule on top of
`OSMGAParseManualMemoryMB` rather than replacing it, so the existing 3..63
acceptance and its tests are untouched.

---

## 4. Part 1 -- the flag

### The predicate

`CAP_READY` today is `mmioMapped && linearModeActive && bytesPerPixel == 4`
(`:4225-4227`).

The first draft proposed "a full-screen colour+depth surface fits". **That was
wrong.** At 1600x1200 with a 16 MB declaration the window is 3,260,416 bytes
and an 800x600 colour+depth pair needs 2,885,120 -- it fits, its 800-pixel
stride is well inside the 1600-pixel cap, and Mesa would accelerate it. A
predicate that refuses a small GL window on a big screen refuses work that
demonstrably succeeds.

**New meaning:** *the driver currently offers a usable acceleration surface.*

```
mmioMapped && linearModeActive && bytesPerPixel == 4
  && hasWindow && windowEnd > windowStart
  && OSMGASurfaceFits(320, 240, strideCap, pageBytes, avail)
```

320x240 is the floor the Mesa back end already treats as the small case
(`OpenStepMGAMesaBuffer.c:88`), and it needs 464,896 bytes -- so the bit says
"there is a real surface here", not "your surface will fit". Whether a
*particular* surface fits stays Mesa's judgement, which is where the stride,
pitch, packing and overflow tests live.

Full-screen capability is **reported separately** -- in the panel line and in
the log -- because it is what the declaration actually buys, but it never
gates the bit.

### The helper must be shared for real

`:4204` already promises that submit tests the same predicate. The promise is
currently kept by duplicated inline conditions: `!mmioMapped ||
!linearModeActive || f2->bytesPerPixel != 4` at `:4365`, `!linearModeActive ||
!osmgaMmapRegistered || start >= end` at `:4598`, and window-range tests at
`:4381`, `:4657`, `:4936`. Narrowing the meaning in one place and not the
others is how they start to disagree, so extracting the helper is part of this
change, not a follow-up.

### Known limit, accepted

`OpenStepMGAMesaProbe.c:219-236` caches the probe for the life of a process.
Changing the display mode requires a reboot, so no live process ever sees the
answer change. Recorded so the next person need not rediscover it.

### UI

One status line, recomputed in `setTable:` and in every control action:

```
1600x1200 RGB:888/32 -- 32 MB would give 19.1 MB offscreen, full-screen GL
1600x1200 RGB:888/32 -- 16 MB would give 3.1 MB offscreen, GL up to 800x600
1600x1200 BW:8 -- no OpenGL: needs 32-bit colour
1024x768 RGB:888/32 -- VRAM Mmap is off, so no OpenGL
```

**S5 corrected.** R8 claimed the line changes immediately when the resolution
changes. It does not: `OSMGADisplayInspector.m:91` `setTable:` is the only
place the panel reads the mode, and nothing observes the stock resolution
picker, which lives outside our view. The line is correct when the panel is
opened or the instance reselected, and it names the mode it is talking about
so a stale line is visibly stale rather than silently wrong.

---

## 5. Part 2 -- reservation and addressing

### A. Establish the aperture without writing to config space

The first draft proposed BAR0 write-1s sizing at init. The objection is
accepted: `COMMAND.MEM` gates legacy VGA decode as well as BAR0, the boot
console is live, nothing here establishes quiescence or serialises
mechanism-1 access, and a "log only" stage performs the identical
transaction. H1 S1 proved one controlled probe was survivable; it did not
prove the operation is race-free inside display-driver initialisation.

**So the step is dropped, not defended.** Three read-only tests replace it,
and together they answer the question the sizing was asked to answer -- *is
`fbPhysical + 16..28 MiB` the card's own aperture?*

- **A1 -- neighbour survey (read-only).** Walk every bus/device/function the
  way `osmgaFindMGAFunction` already does (`:1107-1139`, `osmgaPciReadConfigLong`
  only), read every base address register plus bridge memory windows, and
  refuse any widening if **anything other than our own function** claims a
  base inside `[fbPhysical, fbPhysical + declared)`. Handles the cases:
  skip I/O BARs (bit 0 set), consume the upper dword of a 64-bit BAR (type
  bits `2:1 == 2`), and read header type 1's memory base/limit at `0x20`.
  A device based *below* `fbPhysical` cannot reach into our range without
  also overlapping the 16 MiB nobody disputes we own, which the bus allocator
  does not do.
- **A2 -- alignment.** A BAR is naturally aligned to its size. If
  `frameBufferPhysical` is not aligned to the declared size, the BAR cannot
  be that large. Observed here: `0xf8000000`, exactly 32 MiB-aligned. A
  misalignment is treated as a malformed device condition and **fails closed
  for all widening**, not merely for the declaration.
- **A3 -- the proof already answers the rest.** An address nothing decodes
  reads back all ones and drops writes, so absent memory fails. An aliasing
  board is caught by the witnesses -- python confirms **all 24 witnesses
  below a 12 MiB start are written over** by a wrapping stage two, so
  detection does not depend on the compare range contributing anything.

What remains unresolved by A1-A3 is a device that decodes the range while
declaring no BAR for it. That is not a configuration the PCI bus allocator
produces, and it is written down as the residual rather than argued away.

`OSMGA_S1_VRAM_CEILING` becomes `min(declared, surveyed) - 4 MiB`, page
aligned. If any test above fails, the effective aperture is 16 MiB and the
driver behaves exactly as it does today.

**The margin stays a fixed 4 MiB.** Its rationale (`:206-224`) is unknown
possible top-of-VRAM uses, which is not a proportional quantity; a fraction
would be invented policy.

### B. Prove everything before publishing anything

`:3504-3513` widens in one all-or-nothing step from `PROVEN` to `CEILING`,
and the cdev is registered at init (`:2982-3095`) before any of it. Both have
to change, and the reason is measured, not aesthetic:

python, stage two (12→28 MiB) on a real 16 MiB board -- 4,096 word writes,
1,024 real, 3,072 wrapped, and **796 of them land inside a 9,322,496..12 MiB
window that stage one would already have published**. Six coincide with
witnesses, which `osmgaRestoreWitnesses` then writes back -- over whatever a
client had put there. So publishing between stages hands a client memory and
then writes into it.

- **B1.** Run every proof stage first, low to high, stopping at the first
  failure, and **register the cdev once, afterwards**, with the end the last
  successful stage established. This is the same late-registration move
  `docs/R2_LATE_WINDOW_REGISTRATION_PLAN.md` already needs for the empty
  1600x1200 window; the two are one change, not two.
- **B2.** Keep the stages (7→12, then 12→ceiling) so a 16 MB board that
  wrongly declares 32 still ends with the 12 MiB result instead of nothing.
  Staging is about *what is kept*; publication happens once at the end.
- **B3.** A failing stage leaves its signatures (`:9178-9190`, zeroing is
  guarded by `bad == 0`). With publication moved after the proof, the
  contaminated region has never been handed to anyone, and the mode-set clear
  repairs the part of it that is visible.

### C. Declare the range instead of arguing about it

`ranges[0].size` and `mapFrameBufferAtPhysicalAddress:length:` are
`MGA_VRAM_16MB` (`:2938-2947`), while the cdev's pfn path computes
`fbPhysical + off` with no reference to the declared ranges (`:2288-2296`).
Whether DriverKit permits a user mapping outside a device's declared resource
is not established, and inferring it from a variable name is not evidence.

Rather than establish the contract, **remove the question**: set
`ranges[0].size` to the surveyed aperture at init, so every pfn the cdev can
ever return lies inside a range the driver declared. A1 has already shown
nothing else claims that space, so declaring it cannot take it from anyone.
`mapFrameBufferAtPhysicalAddress:length:` stays at 16 MiB -- it covers
scanout, whose largest mode is 7,680,000 bytes, and enlarging it would change
what the display subsystem maps for no gain.

---

## 6. Part 3 -- the panel

Computed in `scratchpad/r9nib.py` from the real constants. Current rows top
out at 81 with `GROW = 95`, i.e. 14 px of headroom.

**Correction:** the first draft labelled y=24 "Storm" and y=44 "VRAM Mmap".
It is the other way round -- `build-inspector-nib.py:154-157` places Storm at
`Y_MMAP` (44) and mmap at `Y_STORM` (24). The two constants are misnamed in
the script. Renaming them is part of this change; leaving them is how the
next row gets put in the wrong place.

| row | y | h | top |
|---|---|---|---|
| caption | 6 | 14 | 20 |
| VRAM Mmap switch (`Y_STORM` today) | 24 | 15 | 39 |
| Storm switch (`Y_MMAP` today) | 44 | 15 | 59 |
| Gray levels matrix | 66 | 15 | 81 |
| **VRAM size matrix** | **88** | **15** | **103** |
| **status line** | **110** | **14** | **124** |

`GROW` becomes **138** to keep the same 14 px. (R8 said 130, from an assumed
uniform row pitch; the real steps are 18/20/22.) A second status line would
need 154.

Two cells, titles `16` and `32`, tags 0 and 1, one row -- the same
`radio-template-BusLogicIntrInspector.xml` graft the grey matrix uses. Key:
the existing `"MGA Memory Size"`, whose parser accepts 3..63 and is currently
clamped to 16 (`:3184-3186`); the clamp becomes "16 or 32, anything else
clamped down".

Whether Configure accepts a box 43 px taller cannot be settled by arithmetic.
It is checked on screen, and the nib installs without a reboot.

---

## 7. Order of work

Review, then build, then install, then ask for a reboot -- and the panel goes
in **after** the driver semantics it describes, never before, or it persists a
`32` the running driver silently clamps to 16 (`:3184-3186`).

| stage | what | verified by |
|---|---|---|
| 0 | `OpenStepMGAWindowMath.{c,h}` + host unit test: 8192 pages, mode parsing, both switches, predicted vs actual, empty interval, stage-two failure | host, no target involvement |
| 1 | neighbour survey + alignment, **reported only**, nothing depends on it | boot 1 reads the log |
| 2 | `CAP_READY` narrowed via the shared helper; all five duplicated gates replaced | boot 1, same boot |
| 3 | late registration: prove all stages, register once | boot 2 at 1024x768, behaviour must be unchanged |
| 4 | ceiling from `min(declared, surveyed)`, `ranges[0].size` follows | boot 3 at 1024x768, still 16 declared |
| 5 | inspector + nib (`GROW`, radio, status line, constant rename) | Configure on screen; no reboot |
| 6 | declare 32, select 1600x1200, run the GL tests | boot 4 |

Stage 1 is now genuinely log-only: a read-only survey performs no
transaction the driver does not already perform at probe time.

Rollback at every stage is the instance table -- `MGA Memory Size` back to
`16` restores today's numbers exactly, because the surveyed aperture only
ever narrows what the declaration asks for.

---

## 8. Residual risks, written down rather than closed

1. A device that decodes `fbPhysical + 16..28 MiB` while declaring no BAR for
   it would pass A1. Not a configuration the bus allocator produces.
2. The proof samples two words per page; it cannot exclude a hole inside a
   page or a dead address line within one.
3. Stage-two failure leaves signatures in 12..28 MiB. Nothing publishes that
   region, and the visible part is cleared at mode set -- but the region is
   left dirty until the next mode set, by design (`:9171-9177`: a region that
   failed may be somebody else's memory, and zeroing it would repeat the
   mistake).
4. `OSMGA_S1_VRAM_PROVEN` (7 MiB) still appears at `:3600`, `:5962`, `:7109`,
   `:7654`, `:8022` and others. Checked: every one of them is inside an
   **opt-in boot self-test** -- `stormTestEnabled`, `dmaRingTestEnabled`,
   `rasterTestEnabled` -- and none is on a client path. The client submit
   validator takes its colour, depth and texture limits from the window
   itself (`:4864-4869`), so widening the window does widen what clients may
   reach, as intended. What is left behind is the self-tests: they will keep
   exercising only the first 7 MiB and no longer describe the real bound.
   Not a correctness problem, and not fixed here; recorded so the next person
   reading those tests is not misled.


---

## 9. Stage 0 -- done (2026-08-26)

`OpenStepMGAReplacementDisplay_reloc.tproj/OpenStepMGAWindowMath.{c,h}`,
`test/openstep-mga-window-math-test.c`, `test/run-window-math-host.sh`.
C89, `-pedantic -Wall -Wextra -Werror`, no libc, no kernel headers. It is
**not yet in any target's build**: nothing calls it, so compiling it into the
kernel driver would only add dead code. It joins the reloc `CFILES` at stage 2
and the bundle `CFILES` at stage 5, with their callers.

Reuses rather than reimplements: `OSMGAParseManualMemoryMB` for the digits of
the declaration, and `OSMGAParseManualDisplayMode` for the
`Height:/Width:/Refresh:` form. Duplicating either is the drift this file
exists to prevent.

Every expected value in the test was computed independently in python and
written out in full, so the test cannot agree with the code by sharing its
arithmetic. Verified by mutation -- a guard row of 255 instead of 256, a
2 MiB margin instead of 4, and a 3200-pixel minimum surface are each caught by
a named assertion.

**Two limits recorded rather than implied:**

1. The module's 32-bit overflow guards are **not** exercised here. The target's
   `unsigned long` is 32 bits, this host's is 64, and no 32-bit libc is
   installed; deleting the pre-rounding guard still passes, because a later
   bound catches the value instead. The absurd-input tests establish only that
   the refusal happens. The guards are held by inspection and by the target
   compile at stage 2.
2. `OSMGAWindowMathTablesAgree` is the drift alarm between this file's identity
   subset and the driver's real tables, and it is tested here against
   hand-written copies. It only becomes load-bearing when the driver calls it
   at init, which is stage 2.

Replication checked line by line against `-selectModeFromConfig:`
(`:3196-3303`): both indices reset to the defaults first, the resolution
matched only on an exact width/height pair, the first `ColorSpace` token in
table order winning, `BW:4` meaning `BW:8` with four greys, and a pair whose
row the CRTC cannot describe exactly falling back to the default pair whole.
