# R10 -- stages 1 and 2: the neighbour survey, and the flag

Implements `R9_VRAM_DECLARATION_BUILD_PLAN.md` stages 1 and 2. Both land in
one boot: stage 1 only reports, stage 2 only narrows a capability bit.

Nothing here writes to PCI configuration space. R9's first draft proposed
BAR0 write-1s sizing and that was withdrawn under cross-review: `COMMAND.MEM`
gates legacy VGA decode as well as BAR0, the boot console is live at driver
init, and nothing establishes quiescence or serialises mechanism-1 access.

---

## 1. Stage 1 -- the neighbour survey

### The question

`osmgaProveVramTo`'s safety argument is "every offset this admits is
`frameBufferPhysical` plus something under sixteen megabytes, and that is
inside BAR0" (`:9052-9058`). `MGA_VRAM_16MB` is a constant nobody measured.
Before the ceiling may rise above it, one thing has to be true:

> nothing other than this card decodes `[fbPhysical, fbPhysical + declared)`.

### A neighbour's extent is bounded without sizing it

The survey cannot ask a neighbour how big its window is without writing to
its BAR. It does not have to. **A PCI BAR is naturally aligned to its size**,
so a base address `B` belongs to a region whose size is a power of two that
divides `B`. Therefore

```
size <= lowest_set_bit(B)
```

and the neighbour occupies at most `[B, B + lowbit(B))`. That is a rigorous
upper bound obtained from a read.

Worked from this machine's own values: BAR1 at `0xe8200000` has
`lowbit = 0x00200000`, so it is at most 2 MiB -- and `TEST_STATUS.md` H1 S1
measured it at exactly 16 KiB, well inside the bound. BAR2 at `0xe8800000`
gives `lowbit = 0x00800000` = 8 MiB, and it measured exactly 8 MiB, so the
bound is tight where it matters.

This closes the gap R9 left open by argument -- "a device based below us
would have to overlap the 16 MiB nobody disputes we own" -- with a test
instead.

### What is walked

The same `bus 0..7 x device 0..31` walk `osmgaFindMGAFunction` already does
(`PCI_MAX_BUS` 8, `PCI_MAX_DEVICE` 32, `:46-47`), reads only, using the
existing `osmgaPciReadConfigLong`.

For each present function, skipping our own `(bus, device, function)`:

| header type | base registers | expansion ROM | extra |
|---|---|---|---|
| `0x00` device | `0x10`-`0x24` (six dwords) | `0x30` | -- |
| `0x01` bridge | `0x10`, `0x14` | `0x38` | memory window at `0x20`, prefetchable window at `0x24` |
| anything else | -- | -- | **refuse all widening**: an unknown layout is not a layout we may reason about |

Each base register:

- bit 0 set -- an I/O BAR, not memory: skip;
- type bits `2:1 == 2` -- 64-bit: read the upper dword; if it is non-zero the
  region is above 4 GiB and cannot touch a 32-bit range, so skip it, and skip
  the following dword either way;
- base zero -- unassigned by the firmware: skip;
- otherwise the claimed interval is `[base, base + lowbit(base))`.

The expansion ROM register is a base with an enable bit at 0; the address is
`v & 0xFFFFF800`, and it counts **only when the enable bit is set**, because a
disabled ROM decodes nothing.

Bridge windows are read as claimed ranges: memory base/limit at `0x20`
(`base = (v & 0xFFF0) << 16`, `limit = ((v >> 16) & 0xFFF0) << 16 | 0xFFFFF`),
prefetchable the same at `0x24`. A window whose base exceeds its limit is
disabled and ignored.

### The bridge we sit behind is not a neighbour

A bridge whose window **contains** `fbPhysical` is our own parent: its window
covering our aperture is what it is for, and treating it as a collision would
refuse every machine. So a bridge window containing `fbPhysical` is not a
collision -- but it is evidence of a different kind:

> **If our parent bridge's window ends before `fbPhysical + declared`, the
> aperture cannot be that large.** The bridge does not forward what it does
> not claim.

That is a second free disproof, alongside R9's alignment one, and it is
recorded as a refusal with its own log line.

### The verdict

Stage 1 computes and logs, and **nothing consumes it**:

```
OpenStepMGA R10: survey: aperture at 0xf8000000, 32 MB declared
OpenStepMGA R10: survey: 1:0:0 base 0xe8200000 <= 2048 KiB, clear
OpenStepMGA R10: survey: parent bridge 0:1:0 window 0xf0000000..0xffffffff, clear
OpenStepMGA R10: survey: VERDICT clear to 32 MB
```

or, when something is in the way, the specific reason. Stage 4 is what makes
the ceiling follow it.

### Cost and risk

Reads only, at init, on the same path that already enumerates the bus to find
the card. The walk is `8 x 32 x 8` config reads in the worst case; python:
2,048 slot probes plus at most 8 dwords each where a function answers, and a
mechanism-1 read is two port accesses. This is the same order as the
enumeration already performed and happens once.

**The one hazard being accepted:** reading a configuration register of a
device this driver does not own. A read has no side effect on any
specification-conforming device, and the driver already reads every function's
vendor/device and header type on this exact walk. What is new is reading
`0x10`-`0x38` of foreign functions, which is the same class of access.

---

## 2. Stage 2 -- the flag

### Three predicates, not one

R9 said the five duplicated gates should share one helper. Looking at them,
they are not the same predicate and collapsing them into one would change
behaviour that is currently correct:

| site | what it tests today |
|---|---|
| `:4225-4227` `osmgaFillHW3DCaps` | `mmioMapped && linearModeActive && bpp == 4` |
| `:4364-4366` probe-fill parameter | that, **plus** `osmgaMmapRegistered && !stormBlitFailed && stormBlitReady` |
| `:4598-4600` present | `linearModeActive && osmgaMmapRegistered && start < end` |
| `:4381`, `:4657`, `:4936` | range tests against the window |

So the extraction is **layered**, and only the top layer changes meaning:

```
osmgaLinear32()    -> mmioMapped && linearModeActive && bpp == 4
osmgaWindowLive()  -> osmgaMmapRegistered && windowEnd > windowStart
osmgaAccelReady()  -> osmgaLinear32() && osmgaWindowLive()
                      && OSMGASurfaceFits(320, 240, displayWidth,
                                          PAGE_SIZE, windowLen, 0)
```

The 2D and present gates take the **first two** and keep exactly today's
behaviour. Only `CAP_READY` takes `osmgaAccelReady()`.

That is deliberate. A present blits a surface that already exists; a window
too small to hold one has nothing to present, but proving that is not
necessary to make this change, and a gate that starts refusing work it used
to accept is a regression however good the reason. The minimum-surface test
goes exactly one place: the bit that answers "can OpenGL be accelerated here".

### Why the bit moves at all

`CAP_READY` today says "linear mode, four bytes a pixel". It says yes at
1600x1200x32 **while no window is registered at all** -- python: start
9,322,496 is past the 7,340,032 bound, so `S4a` refuses the device and the
Mesa back end is told the hardware is ready when there is nowhere to put a
batch. `CAP_MMAP` is what stops it, but `CAP_READY` is lying, and the panel
this work adds would repeat the lie.

The 320x240 floor is Mesa's own small case and costs 464,896 bytes.

### What must not change

`CAP_REQUIRED` keeps all four bits (`OpenStepMGAHW3D.h:797-806`). The
capability parameter is a shipped contract; narrowing one bit's meaning can
only refuse acceleration, never enable it, so an old library sees at worst a
conservative answer. `OpenStepMGAMesaProbe.c:219-236` caches the probe per
process and a mode change requires a reboot, so no live client observes the
answer change.

### The drift alarm and the log line

Stage 2 also wires the two things stage 0 built but nothing yet calls:

- `OSMGAWindowMathTablesAgree` once at init, against the driver's own
  `osmgaRes`/`osmgaFmt`, logging loudly on a mismatch. Two tables that
  disagree is the failure the shared file exists to prevent, so it is
  detected rather than assumed.
- `OSMGAAccelVerdict` with `haveActual = 1` at the end of `enterLinearMode`,
  producing the same sentence the panel will produce at stage 5 -- from the
  same function, which is the whole point.

`OpenStepMGAWindowMath.c` joins the reloc `CFILES` here, with its callers.

---

## 3. Verification

| claim | how |
|---|---|
| the survey finds this machine's three BARs and clears 32 MB | boot 1 log against `TEST_STATUS.md` H1 S1: `0xf8000000`, `0xe8200000`, `0xe8800000` |
| `lowbit` bounds are not violated by the measured sizes | python, against the H1 S1 record, before the boot |
| the tables agree | boot 1 log; a mismatch is a build error in practice |
| `CAP_READY` is unchanged at 1024x768x32 | boot 1: the Mesa demos must behave exactly as today |
| `CAP_READY` becomes 0 at 1600x1200x32 | boot 1 log -- and this is a **correction**, not a regression: there is no window there today |
| no 2D or present path changed | the three-layer split; the existing regression scripts |

Rollback is the instance table, as before.

---

## 4. Open questions for cross-review

1. **Is the `lowbit` bound sound in practice?** It is exact for a BAR the
   firmware placed at its natural alignment. Is there a real case where a
   BAR's base is *more* aligned than its size, making the bound loose but
   safe, or *less*, making it wrong?
2. **Cardbus and unknown header types refuse everything.** Too strict? The
   alternative is to ignore them, which is the unsafe direction.
3. **Reading foreign configuration space** at driver init: is the "reads are
   side-effect free on a conforming device" argument good enough, given this
   driver already reads vendor/device/header type of every function?
4. **Is the three-layer split right**, or should the present path also take
   the minimum-surface test and accept the behaviour change?


---

## 5. Addendum -- the bound is sound, and it is not the rule we need

Written after computing it, before the cross-review returned.

### The bound is proven, not assumed

`scratchpad/r10lowbit.py` enumerates every legal `(size, base)` pair for
sizes 16 B..2 GiB with `base = k * size`, `k >= 1`, under 4 GiB: **1,506
pairs, 0 violations**, and the bound is exactly tight in 767 of them. The
reason is structural rather than empirical -- a memory BAR hardwires the bits
below its size to zero, so the base is always a multiple of the size, so the
size always divides the base and `size <= lowbit(base)` cannot fail.

Against this machine's measured BARs (`TEST_STATUS.md` H1 S1):

| BAR | base | measured size | `lowbit(base)` | bound |
|---|---|---|---|---|
| 0 framebuffer | `0xf8000000` | 32 MiB | 128 MiB | holds, loose 4x |
| 1 MMIO | `0xe8200000` | 16 KiB | 2 MiB | holds, loose 128x |
| 2 ILOAD | `0xe8800000` | 8 MiB | 8 MiB | holds, **tight** |

### But it is loose in the direction that costs the feature

python: a neighbour based at `0xf0000000` has `lowbit = 256 MiB`, so its
bound reaches `0x100000000` and **collides** with `0xf8000000..0xfa000000` --
refusing a 32 MB declaration on account of a device that may be 4 KiB.
`0xf0000000` is an ordinary address for a neighbour to have. Refusing is the
safe direction, but a rule that refuses on most machines is a rule nobody
gets to use.

### A stronger rule, from a premise already established

Two enabled memory decoders must not overlap. This driver's proof reaches
`OSMGA_S1_VRAM_CEILING` (12 MiB) successfully at every boot, so
`[fbPhysical, fbPhysical + 12 MiB)` is **ours by measurement**, not by
assumption. For any neighbour based at `B < fbPhysical` with size `S`:

```
B + S <= fbPhysical        (or it would overlap what we have proven is ours)
```

so a neighbour below us **can never reach into our range at all**, whatever
its size. The lowbit bound is not needed for that case, and the `0xf0000000`
false positive disappears.

What remains is much simpler than section 1's table:

- a neighbour base inside `[fbPhysical, fbPhysical + declared)` -- **refuse**;
- a neighbour base below `fbPhysical` -- clear, by the argument above;
- a neighbour base at or above `fbPhysical + declared` -- clear;
- a **bridge window** overlapping the range and *not* containing
  `fbPhysical` -- **refuse**: a bridge forwards addresses to another bus, and
  its base and limit are read directly, so no bound is inferred for it.

`lowbit` stays, but only as a reported diagnostic in the survey log, and as
the tie-breaker if the "below us" premise is ever weakened.

### Consequences for section 1

- The prefetchable window matters: `TEST_STATUS.md` records BAR0 as
  prefetchable, so a parent bridge forwards it through the **prefetchable**
  window at `0x24`, not the memory window at `0x20`. The parent test must
  look at whichever window contains `fbPhysical`, and the "parent window ends
  before `fbPhysical + declared`" disproof must be taken from that same
  window.
- A device with memory decode disabled in its command register cannot collide
  at all. Not used as a reason to allow anything -- treating every function as
  enabled is the conservative reading and costs nothing.


---

## 6. Cross-review verdicts (codex terra, 2026-08-26)

| claim | check | verdict |
|---|---|---|
| present's gate is `linearModeActive && osmgaMmapRegistered && start < end` -- it has **no** `mmioMapped` and no bpp test, so `osmgaLinear32()` would change it | read `:4598-4601` | **accepted -- my error.** "byte-for-byte equivalent" was false as written |
| the driver's `osmgaRes` fields are `int`, not `unsigned long`; the drift alarm needs explicit conversion arrays, never a cast | read `:940-953` -- `int width; int height;` | **accepted -- my error** |
| the verification row "the survey finds this machine's three BARs" cannot test the survey, because those are the MGA's own and the survey skips itself | `P0_TARGET_INVENTORY.md:36-45`: we are `04:00.0`, BARs `0xf8000000`/`0xe8200000`/`0xe8800000` | **accepted -- my error** |
| the upstream bridge is `03:0d.0` (HiNT HB4 `3388:0021`), not the `00:01.0` in my example | `P0_TARGET_INVENTORY.md:45` | **accepted** -- the example was invented; the real one is now used |
| "a bridge whose window contains `fbPhysical`" does not establish parenthood; use primary/secondary/subordinate bus numbers | structural, and we sit on bus 4 behind `03:0d.0` | **accepted** |
| a type-1 bridge's prefetchable window can be 64-bit, needing `0x28`/`0x2c` | structural | **accepted** |
| reserved BAR type `3` and a 64-bit pair at the last slot must be refused, not bounded | structural | **accepted** |
| `PCI_MAX_BUS == 8` is a discovery bound, not proof every neighbour was seen | read `:46-47` | **accepted** -- see the hierarchy walk below |
| `size <= lowbit(base)` is unsound for expansion-ROM BARs | could not be settled: no PCI specification is available offline, and the two readings (ROM size bits hardwired to zero like a BAR, versus a 2 KiB granularity guarantee only) disagree | **not relied upon** -- section 5's rule does not use lowbit for any decision, so the question does not gate anything. Recorded as unresolved rather than argued |
| mechanism-1 is a two-port latch: `outl(0xCF8)` then `inl(0xCFC)` is not atomic, and a concurrent foreign config **writer** could land its data on the BDF we selected | read `:1078-1091` -- no serialisation | **accepted as a hazard, rejected as a blocker**: the shipped driver already performs exactly this pair, twice per function, on every boot, in `osmgaFindMGAFunction` and again for its own BARs. The survey does not introduce the hazard; it lengthens the window. Mitigated below rather than dismissed |
| the 320x240 floor is 464,896 bytes and `CAP_READY` narrowing is otherwise sound | python agrees exactly | accepted |

### The three corrections that change the design

**C1 -- re-layer the predicates.** present takes only the window layer:

```
osmgaWindowLive()  -> osmgaMmapRegistered && windowEnd > windowStart
osmgaLinear32()    -> mmioMapped && linearModeActive && bpp == 4
osmgaAccelReady()  -> osmgaLinear32() && osmgaWindowLive() && minSurfaceFits
```

- present (`:4598`) becomes `linearModeActive && osmgaWindowLive()` -- which
  is exactly today's expression, with the second and third terms named;
- probe-fill (`:4363-4366`) becomes `osmgaWindowLive() && !stormBlitFailed &&
  stormBlitReady && osmgaLinear32()` -- also exactly today's;
- **only** `CAP_READY` takes `osmgaAccelReady()`.

Verified by a truth table, not by assertion: `test/` gains a host test that
enumerates all 2^5 combinations of the five inputs and checks the extracted
expressions against the originals transcribed from the source. Booting the
demos demonstrates one positive case and is not a substitute.

**C2 -- walk the discovered hierarchy, not a fixed bus count and not 256
buses.** Start at bus 0; every type-1 function encountered contributes its
secondary and subordinate bus numbers to the set of buses still to walk. That
is complete in the sense codex asked for -- a neighbour on bus 9 is reached --
while probing far fewer slots than `256 x 32`, which matters because every
slot probe is another unserialised `CF8`/`CFC` pair. The two objections
(completeness, and exposure) have one answer.

**C3 -- narrow the config-access window.** Each `CF8`/`CFC` pair is bracketed
by an interrupt disable **if DriverKit 4.2 provides one** -- this machine is
uniprocessor, so that makes the pair atomic against the only other agent that
could interleave. No DriverKit headers are available on this host, so whether
the primitive exists is settled at the target build, not asserted here; if it
does not exist, the survey ships with the same exposure the enumeration
already has and that fact is logged in the plan rather than hidden. Either
way the helper is shared, so `osmgaFindMGAFunction` and the driver's own BAR
reads get the same protection.

### Two further corrections to the verification table

- **The drift alarm must fail closed.** A log line proves nothing. If
  `OSMGAWindowMathTablesAgree` returns 0, `osmgaAccelReady()` returns 0 --
  disagreeing tables mean the shared arithmetic is describing a different
  driver, and the honest answer is to refuse acceleration rather than to
  accelerate on numbers nobody can vouch for.
- **The survey must log every function it walks, including our own,** marked
  `self`, so that the boot log demonstrates BAR extraction working on real
  foreign functions rather than only reporting a verdict. The row that
  claimed our own three BARs as evidence is removed.


---

## 7. Stage 2 -- done (2026-08-26)

### What changed, and what deliberately did not

Reading the source again while implementing turned up **more composite gates
than section 2 listed** -- `:4767` (HW3D submit) and `:4833` as well as the
three already named. That changes the judgement about how much to rewire.

**Only `CAP_READY` moved.** Every 2D, present and submit gate is left exactly
as it was, byte for byte. The reasoning is asymmetric and worth stating:
narrowing a *capability* can only make a caller fall back to software, while
narrowing a *gate* would refuse work that runs today. Section 2's layered
extraction was the right instinct and codex found one of its layers already
wrong (`osmgaLinear32()` includes `mmioMapped`, present's gate does not); the
safe version of the same goal is to move one bit and touch nothing else.

So R9's "extract the shared helper, it is part of this change not a
follow-up" is **not** what shipped. It buys less than it risks, and the
divergence it was meant to prevent cannot occur in the dangerous direction:
a narrowed `CAP_READY` is more conservative than every gate, never less.

### The predicate is a pure function, and it is enumerated

`OSMGAAccelReadyBits(const OSMGAReadyIn *)` lives in the shared file, not in
the driver, so a host test can enumerate it. `test_ready_bits` runs **240
combinations** -- four booleans x five window shapes x three pixel sizes x two
stride caps -- against a reference expression written out independently.

Mutation-tested, and the mutation testing earned its keep: an early version of
the table expressed windows as *lengths*, so `windowEnd < windowStart` was
unreachable and deleting the emptiness check still passed. The table now
carries explicit `(start, end)` pairs including an **inverted** one, because
that is the case where `end - start` wraps to nearly four gigabytes and every
size test downstream would succeed.

| mutant | caught by |
|---|---|
| drop the `tablesAgree` term | `ready-truth-table agree=0` |
| allow an empty/inverted window | `ready-truth-table start=9322496 end=9314304` |
| accept any bytes-per-pixel | `ready-truth-table bpp=1` |
| drop the registration term | `ready-truth-table reg=0` |
| `<=` written as `<` | **not caught -- equivalent**: at `end == start` the length is 0 and the surface test refuses anyway |

### Fail-closed drift alarm

`osmgaTablesAgree` is computed once in `initFromDeviceDescription` and is a
term of the predicate: if this file's identity subset stops matching
`osmgaRes`/`osmgaFmt`, acceleration is refused rather than logged about.
The driver's fields are `int` and the helper takes `unsigned long`, so the
arrays are converted element by element into locals -- never cast, which would
make "agree" a statement about storage layout instead of about the numbers.

### The verdict line

`enterLinearMode` ends by logging the sentence the panel will later show, from
the same function, with `haveActual = 1`. The configuration values it needs
are **copied** at init into fixed buffers rather than kept as pointers into
`IOConfigTable`'s storage, whose lifetime is not documented anywhere this
driver can check; every other use in the file reads and derives immediately,
and a dangling read at mode set would be a kernel fault for the sake of a log
line.

### How it was verified without an OPENSTEP toolchain

The `.m` cannot be compiled on this host. Three checks stand in:

1. the shared file and its 240-case truth table build and pass under
   `-std=c89 -pedantic -Wall -Wextra -Werror`;
2. brace and paren balance compared against `HEAD` -- both unchanged (the
   `-1` paren delta is pre-existing, inside a comment);
3. **`scratchpad/blockcheck.c` compiles the three inserted C bodies verbatim
   against the real headers** with the driver's state faked, under the same
   `-Werror` settings. It prints `tables agree: 1`, `CAP_READY: set`, and
   `1600x1200 RGB:888/32 -- 16.0 MB gives 3.1 MB offscreen, GL up to 800x600`.

That catches a mistyped field or a wrong type, which is what an untested edit
to a kernel driver usually is. It does not replace the target build, which
happens with stage 1 in the same boot.


---

## 8. Stage 1 -- done (2026-08-26)

### The walk was separated so it could be tested at all

The first cut put the whole walk in the driver, where it cannot be exercised
without a boot -- and a boot is the most expensive test this project has. It
was pulled out into `OpenStepMGAPciSurvey.{c,h}`, which takes a **read
callback** and emits **structured events**; the driver supplies
`osmgaPciReadConfigLong` and turns events into `IOLog` lines. There is
deliberately **no write callback**: the design this replaces sized BAR0 by
writing ones to it, and the absence of any way to write is the plainest
statement that it no longer does.

`test/openstep-mga-pci-survey-test.c` then drives it over a synthetic bus
modelled on the real one (`P0_TARGET_INVENTORY.md`: MGA at 04:00.0 behind
bridge 03:0d.0, BAR0 raw `0xf8000008` prefetchable, BAR1 `0xe8200000`, BAR2
`0xe8800000`), bent into shapes the real machine cannot produce on demand.
Every register value -- especially the bridge window format, 1 MiB
granularity with an inclusive limit and both halves packed into one dword --
was encoded in python first.

### The rule that shipped

Section 5's, not section 1's. A neighbour is judged by **base position
alone**: inside the range is a collision, below cannot reach in, above is not
in it. `lowbit` survives only as a diagnostic in the log, because bounding
extents with it would have refused a 32 MB aperture on account of an ordinary
neighbour at `0xf0000000`.

Two corrections found while running the adapter, not by reasoning about it:

- **a base register left at zero is not a claim.** Most functions have
  several; counting them buried the claims that matter under a dozen that say
  nothing.
- **a bridge window based at zero is not a window.** An unconfigured bridge
  was reading as a one-megabyte window at physical zero.

### Mutation results

Ten mutants; **eight caught**, two proven equivalent by analysis rather than
waved away.

| mutant | caught by |
|---|---|
| never follow a bridge to its secondary bus | `neighbour-two-bridges-deep-is-found` |
| mask the header type with `0xFF` instead of `0x7F` | `function-1-found-with-multifunction-bit` |
| judge our own base registers (normal path) | `machine-is-clear-for-32mb` |
| judge our own base registers (64-bit path) | `self-64-bit-bar-not-judged` |
| ignore the expansion ROM's enable bit | `disabled-rom-ignored` |
| count an I/O BAR as memory | `io-bar-ignored` |
| ignore a 64-bit BAR's non-zero upper half | `64-bit-bar-above-4gib-ignored` |
| never read the prefetchable window | `parent-window-stops-short` |
| a 64-bit BAR consumes one dword instead of two | **equivalent**: if the upper half is zero the second dword reads back as an unassigned base and is ignored, and if it is non-zero the other return is taken |
| ROM base masked `0xFFFFFFF0` instead of `0xFFFFF800` | **equivalent**: the two differ only in bits 10:4, and both range boundaries are 2 KiB-aligned, so no verdict can turn on it |

Three of the first suite's assertions were themselves wrong and had to be
fixed before they meant anything: every offender sat on bus 0, so a walk that
never followed a bridge still reported "clear" and looked like success; and
two 64-bit cases had a zero upper half, so misreading the pair changed
nothing. The mutation run is what exposed both.

### Verified without an OPENSTEP toolchain

`scratchpad/adaptercheck.c` extracts the driver's adapter **and its call site
verbatim** from the `.m` and compiles them against the real headers under
`-std=c89 -pedantic -Wall -Wextra -Werror`, with `IOLog` and the port read
faked at this machine's recorded values. It prints:

```
OpenStepMGA R10: survey 0:30.0 bridge sec=4 sub=4 (ours)
OpenStepMGA R10: survey 0:30.0 window f8000000..fc000000 (ours)
OpenStepMGA R10: survey 4:00.0 is us (102b:0525), not judged
OpenStepMGA R10: survey 4:00.0 base f8000000 <= 131072 KiB (ours)
OpenStepMGA R10: survey 4:00.0 base e8200000 <= 2048 KiB (ours)
OpenStepMGA R10: survey 4:00.0 base e8800000 <= 8192 KiB (ours)
OpenStepMGA R10: survey of f8000000..fa000000 over 1 claims: clear
OpenStepMGA R10: base alignment allows up to 131072 KiB
```

That is what boot 1 should look like if the machine really is clear for a
32 MB aperture. It is a prediction, not a result: the real bus has functions
this fake does not.

### Still outstanding for stage 1

The **mechanism-1 interleaving hazard** is unmitigated. The walk follows the
discovered hierarchy rather than sweeping 256 buses, which keeps the number
of unserialised `CF8`/`CFC` pairs close to what `osmgaFindMGAFunction`
already issues at every boot -- but no interrupt guard is in place, because
no DriverKit headers exist on this host to confirm a primitive for it. That
is settled at the target build, and until then the exposure is the same one
the shipped driver already has.
