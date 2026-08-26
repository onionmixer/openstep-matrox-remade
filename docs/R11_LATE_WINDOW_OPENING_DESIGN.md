# R11 -- stage 3: prove first, open the window once

Implements `R9_VRAM_DECLARATION_BUILD_PLAN.md` stage 3, and supersedes the
sketch in `R2_LATE_WINDOW_REGISTRATION_PLAN.md`. Written before any code.

The 1600x1200x32 boot of 2026-08-26 11:17 confirmed the problem on hardware:

```
S4a: no usable offscreen window for this mode (start=9322496 end=7340032),
     device NOT registered
R10: 1600x1200 RGB:888/32 -- no OpenGL: no offscreen window was registered
```

**Correction to the first draft**, which said the same boot also proved the
memory good. It did not, and could not: the widening is short-circuited by
`osmgaMmapRegistered`, so a boot that refuses to register never calls the
proof at all. Checked on the machine -- `grep M1-4F1` returns 02:29, 08:21 and
11:11 and **nothing at 11:17**. The three proof lines come from boots whose
window WAS registered, all reading `7340032..12582912, 640 pages, 0 wrong`.

What that record supports is narrower and still sufficient: the 7 -> 12 MiB
region proves good on every boot that has ever asked. Whether it does so at
1600x1200 is not yet known, because that mode has never asked -- which is the
defect this stage removes.

---

## 1. Why the window is empty when the memory is not

Two things happen in the wrong order.

- **Registration** is in `initFromDeviceDescription` (`:3176-3340`) and takes
  its end from `OSMGA_S1_VRAM_PROVEN` (7 MiB) -- the bound justified
  statically, by the fact that a 1600x1200x32 scanout works.
- **The proof** is in `programLinearMode` (`:3504-3513`), because it can only
  run at the one moment the driver owns the aperture and nothing is scanned
  out of it. It widens the window to `OSMGA_S1_VRAM_CEILING` (12 MiB).

At 1600x1200 the visible image plus its guard rows already ends at 9,322,496,
which is past 7 MiB. So registration refuses, and the widening that would have
made the window real never runs, because it only widens a window that exists.

## 2. What changes

**Not the place registration happens.** Moving `addToCdevswFromDescription:`
out of `initFromDeviceDescription` would be the tidy change and is rejected:
whether a character device may be published from `enterLinearMode` is not
established, and the cost of being wrong is a machine whose display driver
loads but whose device never appears -- diagnosable only by another reboot.

**What changes is when the window becomes non-empty.** The device is
registered at init as it is today, but with an EMPTY interval; the interval is
opened once, at the end of the proof, to whatever the proof established.

```
init:       register if start < CEILING (and the interval would be >= a page)
            windowStart = windowEnd = start          -- nothing is mappable
mode set:   probe, then prove PROVEN -> CEILING
            windowEnd = proof passed ? CEILING : PROVEN
            (and only if that leaves windowEnd > windowStart)
```

The registration test changes from `start >= PROVEN` to `start >= CEILING`.
That is the whole of the fix for 1600x1200.

## 3. What each mode gets, computed in python

| mode | window start | today: registered? | today's window | new, proof passes | new, proof fails |
|---|---|---|---|---|---|
| 1024x768 BW:8 | 1,048,576 | yes | 11,534,336 | 11,534,336 | 6,291,456 |
| 1024x768 RGB:888/32 | 4,194,304 | yes | 8,388,608 | 8,388,608 | 3,145,728 |
| 1280x1024 RGB:888/32 | 6,553,600 | yes | 6,029,312 | 6,029,312 | 786,432 |
| **1600x1200 RGB:888/32** | **9,322,496** | **no** | **0** | **3,260,416** | **0** |

**Correction to the first draft**, which said every working mode gets the same
window "whether the proof passes or fails". The two columns plainly differ.
The defensible claim is the one that matters: for each outcome, a mode that
works today gets **exactly what today's code gives it in that same outcome** --
today a failed widening also leaves the conservative `PROVEN` window. The only
row that changes is the one that is broken.

## 4. It also closes a hazard that exists today

`osmgaProveVramTo` writes a witness every 512 KiB **below** the region it is
proving and restores them afterwards (`:9101-9115`). Proving 7 -> 12 MiB
therefore writes at 0, 0.5, ... 6.5 MiB -- fourteen of them -- and at
1024x768 BW:8 the window published at init is [1 MiB, 7 MiB), so **twelve of
those witnesses land inside a window a client may already have mapped**, and
are then written back over whatever the client put there.

(python, not estimated: 12 inside the BW:8 window, 6 inside 1024x768 RGB:888/32
at [4,194,304, 7,340,032), and 1 inside 1280x1024 at [6,553,600, 7,340,032).
The first draft of this document said thirteen; it counted the witness at
offset zero, which is below every window.)

Nothing has been harmed by it, because at boot no client exists yet. But it is
real, cross-review found it, and the ordering here removes it rather than
arguing about it: while the proof runs there is no mappable interval at all.

This is also what makes stage 4 possible. A second proof stage (12 -> 28 MiB
on a 32 MB declaration) wraps onto 0..12 MiB on a 16 MB board -- python, in
R8: **796 of its writes land inside what stage one would have published**. As
long as the window is opened once, after every stage, that cannot happen.

## 5. Consequences that must be handled

- **`CAP_MMAP` must exclude an empty interval.** Today it is exactly
  `osmgaMmapRegistered` (`:4222`), and registration implied a non-empty
  window. It no longer does, and `CAP_MMAP` is part of `CAP_REQUIRED`
  (`OpenStepMGAHW3D.h:797`), so leaving it would tell a client the window is
  there while every mmap of it fails. `CAP_READY` already tests the interval
  (`OSMGAAccelReadyBits`), so only `CAP_MMAP` needs it.
- **If `enterLinearMode` never runs, the window stays empty**, where today a
  mode whose start is below 7 MiB would have had one. That path means the
  mode programming failed and the display is already lost; recovery is the
  documented config-edit reboot. Stated rather than hidden.
- **`osmgaProbeAlias`** is mapped at `start` during registration and does not
  move; `start` is known at init in both designs.
- **`enterLinearMode` may run more than once** (`statEnterLinear`). Opening
  the window must be idempotent and must not re-open a window that a later
  mode set found unusable -- the existing widening guard has the same
  requirement and is the model.
- **The `end > MGA_VRAM_16MB` invariant** at registration (`:3193`) is now
  tested against `CEILING` rather than the computed end; it keeps its meaning
  (nothing above the mapped aperture is ever offered) and stage 4 replaces the
  constant with the surveyed aperture.

## 6. How it is verified

| claim | how |
|---|---|
| the window opens at 1600x1200 | boot 3: `S4a` registers, and the window becomes 9,322,496..12,582,912 after `M1-4F1` |
| nothing is mappable while the proof runs | the log order: registration says "empty", the proof line, then the open line |
| 1024x768 is unchanged | boot 4 at 1024x768: same window bytes as boot 2 recorded (11,534,336 at BW:8) |
| `CAP_READY` becomes 1 at 1600x1200 | the R10 verdict line changes from "no offscreen window was registered" to "16.0 MB gives 3.1 MB offscreen, GL up to 800x600" |
| a GL client actually works there | the Mesa demos at 1600x1200, which have never run in that mode |
| no 2D/present regression | the existing regression scripts |

Rollback is the instance table: `VRAM Mmap = No` disables the device
entirely, and any earlier resolution restores the previous behaviour exactly.

## 7. Open questions for cross-review

1. **Is "registered with an empty interval" sound?** `osmgaDevOpen` refuses
   while `osmgaMmapRegistered` is zero; it is now 1 with nothing mappable, so
   an `open` succeeds and every `mmap` fails until the proof completes. At
   boot nothing opens it. Is there a caller that would cache that failure?
2. **Should `osmgaDevOpen` refuse an empty window** instead, making "open
   succeeded" mean "there is something here"? That is a stronger contract but
   a new refusal on a path that has none today.
3. **Is registering-then-opening better than deferring registration** to
   `enterLinearMode` after all? The argument above is risk, not elegance.
4. **The failure path**: proof fails at 1600x1200, so the window stays empty
   and the device stays registered. Should it instead be unregistered? Nothing
   in this driver has ever removed a cdev entry, and the S4a comment warns
   that mappings outlive the driver.

---

## 8. Cross-review verdicts (codex terra, 2026-08-26)

| claim | check | verdict |
|---|---|---|
| The log provenance is wrong: a boot that refuses to register cannot have produced the `M1-4F1` proof line, because the widening is short-circuited by `osmgaMmapRegistered` | ran `grep M1-4F1` on the machine: **02:29, 08:21, 11:11 -- nothing at 11:17** | **accepted -- my error.** §1 rewritten; the evidence I cited came from a different boot in a different mode |
| "Nothing is mappable" is false: the mmap handler tries the **command window first** and it does not depend on the VRAM interval, so a client could map the 24 KiB batch area while the VRAM window is empty | read `:2346-2350` -- `if (osmgaMmapCmdPhysical != 0UL && off >= OSMGA_CMD_MMAP_BASE)` comes before every VRAM test, and the ring is allocated at init | **accepted -- blocker, my claim was false** |
| "Every working mode gets the same window whether the proof passes or fails" is false | my own table shows 11,534,336 against 6,291,456 | **accepted -- my error.** The true claim is "the same as today's outcome in the corresponding case" |
| Endpoints cannot encode "the proof already failed": at 1600x1200 a failure leaves `end == start`, indistinguishable from "not attempted", and the existing guard keys on `windowEnd == PROVEN` | read the widening guard | **accepted -- blocker.** An explicit latch is required |
| `osmgaProbeVramExtent` is omitted from the design; it also writes and restores, runs before the proof, and on a repeated `enterLinearMode` writes inside a live window | read it: writes at MiB boundaries from `visibleEnd/MiB + 2` up to 15 MiB. python: **10** of its 28 writes land inside the 1024x768 BW:8 window, 4 of 22 inside 1024x768 RGB:888/32 | **accepted -- a real gap** |
| The witness count is 12, not 13 | python, and I had already corrected it before this review arrived | agreed |
| Mesa caches the capability failure, not the mmap failure -- `probeDone` prevents a second probe in that process | `OpenStepMGAMesaProbe.c:186-190` records `UNAVAILABLE` and closes the fd when a required bit is absent | accepted |
| The command ring is allocated even when the proof fails, and is never freed | `:1832` says so deliberately | accepted -- documented, not changed |
| Submit has no early live-window check where present does | `:4858` versus the submit path | **deferred**: the later destination test already refuses, so this is wasted work rather than a hazard, and stage 3 does not touch gates |
| "Late registration is riskier" is unsupported by the source | true -- it is risk-aversion, not evidence | ⚖️ **accepted as stated, and made moot**: with the open-gate below, early registration is sound on its own terms, so the question no longer has to be settled to proceed |
| Do not unregister after a proof failure | agrees with §7.4 | accepted |

### The design after the review

```
state:  UNOPENED -> OPEN | FAILED          (one shot, never revisited)

init          register if start < CEILING and CEILING - start >= a page
              window = [start, start]      empty
              state  = UNOPENED

open()        REFUSED unless state == OPEN     <-- the blocker fix
              so no fd exists, so neither the VRAM window NOR the command
              batch can be mapped while anything is being proved

mode set      only when state == UNOPENED:
                osmgaProbeVramExtent()         (writes, restores)
                proved = osmgaProveVramTo(PROVEN, CEILING)
                end = proved ? CEILING : PROVEN
                if (end > start) { window = [start, end]; state = OPEN; }
                else             { state = FAILED; }
```

Three things that follow, and are the point:

- **`open` refusing an empty window makes "nothing is mappable" true** rather
  than merely intended. It also closes the command-batch exposure codex found,
  which the first draft did not even know about.
- **The probe and the proof become first-entry-only.** A second
  `enterLinearMode` must not write into a window a client now holds -- which
  is the same hazard as the witnesses, by a second route.
- **`FAILED` is terminal and distinct from `UNOPENED`**, so a mode whose proof
  failed is never retried and never confused with one that has not run.

A failed VRAM proof does **not** fail mode programming -- `programLinearMode`
returns `NO` only for a PLL failure, before the proof -- so the display comes
up either way and the only thing lost is acceleration. That is today's
behaviour and it is kept.

### The rule stage 4 inherits

Codex's formulation, adopted verbatim because it is better than mine:
**every proof that may write below the region it is testing must finish before
the first successful `open`.** Opening once after the last stage satisfies it;
opening after each stage does not.


---

## 9. Stage 3 -- built and installed (2026-08-26)

### What shipped

- `OSMGAWindowState` (`UNOPENED | OPEN | FAILED`) and two pure decisions in
  the shared file: `OSMGAWindowOpenDecision` and `OSMGAWindowMayRegister`.
- Registration is judged against the **ceiling**, not the conservative bound,
  and publishes an **empty** interval.
- `osmgaDevOpen` refuses unless the state is `OPEN` -- the fix for the
  command-batch exposure, since with no descriptor there is nothing to map at
  all.
- `CAP_MMAP` requires a live interval rather than mere registration.
- `osmgaProbeVramExtent` no longer runs while a window is open.
- The widening block became an **open-once** block: prove, decide, set the
  state, and never revisit it.

### Verified before the build

`scratchpad/stage3check.c` extracts the registration condition and the
open-once block **verbatim** from the `.m` and compiles them against the real
headers under `-std=c89 -pedantic -Wall -Wextra -Werror`, with the proof
faked both ways:

| case | window | state |
|---|---:|---|
| 1024x768 BW:8, proof passes | 11,534,336 | OPEN |
| 1024x768 BW:8, proof fails | 6,291,456 | OPEN |
| 1600x1200 RGB:888/32, proof passes | **3,260,416** | OPEN |
| 1600x1200 RGB:888/32, proof fails | 0 | FAILED |

Every number matches the python table in section 3, and the first two match
what the machine actually logged on 2026-08-26 11:11 (`11264 KiB`) -- so the
modes that work today are unchanged in both outcomes.

Mutation-tested, six mutants, all caught. Two needed new cases first: both
real bounds are page multiples, so deleting the rounding changed nothing
until a ragged bound was tested; and every window was either empty or many
pages, so deleting the minimum-size test changed nothing until a sub-page one
was tested.

### Build

`BUILD_EXIT=0`, 410,992 bytes, installed with `Instance0.table unchanged`.
No new warnings -- the three that appear are pre-existing (`osmgaDacSkip`
used before its static definition, and a gcc 2.7.2.1 `duplicate static` on an
unrelated declaration).

### What the next boot should show, at 1600x1200 RGB:888/32

```
S4a: VRAM window ... registered   (empty, at start 9322496)
M1-4F1: 7340032..12582912, 640 pages, 0 wrong
M1-4F1: offscreen window OPENED 9322496..12582912 (3184 KiB), proof passed
R10: 1600x1200 RGB:888/32 -- 16.0 MB gives 3.1 MB offscreen, GL up to 800x600
```

The third line has never appeared in this mode, and the fourth replaces
"no OpenGL: no offscreen window was registered".

**A prediction, not a result.** The proof has never actually run at
1600x1200 -- that is the correction section 1 records -- so whether
7 -> 12 MiB proves good in this mode is what the boot decides.
