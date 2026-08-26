# R2 — registering the offscreen window late, so 1600x1200x32 can accelerate

Status: **plan only.** No code is written. The code is frozen; this exists so
the decision can be made on evidence rather than re-derived later.

## The defect, measured

At `1600x1200 RGB:888/32` the driver never publishes `/dev/osmgavram`, so
hardware 3D is unavailable in that mode — every OpenGL program falls back to
software regardless of its own buffer size.

The arithmetic, at this machine's 8192-byte page:

```
visible   1600 * 4 * 1200          =  7,680,000
guard     256 rows * 1600 * 4      =  1,638,400
start     page_align(sum)          =  9,322,496   (8.89 MiB)
end       OSMGA_S1_VRAM_PROVEN     =  7,340,032   (7 MiB)
start >= end  ->  "S4a: no usable offscreen window for this mode"
```

Measured on the machine rather than only computed -- the boot of
2026-08-26 00:53:50 at that mode logged:

```
OpenStepMGA S4a: no usable offscreen window for this mode
                 (start=9322496 end=7340032), device NOT registered
```

which is the arithmetic above to the byte.

`OpenStepMGAReplacementDisplay.m:2967-2987`. The widening to
`OSMGA_S1_VRAM_CEILING` (12 MiB) at `:3428` cannot rescue it: it is guarded by
`osmgaMmapRegistered`, which is still zero.

**This is not a hardware shortage** -- but say it no more strongly than the
evidence allows, which cross-review had to point out twice. The driver maps
and caps a 16 MiB aperture; it does not establish that 16 MiB of distinct
VRAM is populated without the probe. And it does not "prove 7..12 MiB at
every other mode": it ATTEMPTS the proof only where a window was registered
and the end is still 7 MiB, and widens only if the attempt passes. What is
measured is narrower and still sufficient: at 1024x768 BW:8 and at
1600x1200 BW:8 the attempt passed and the window widened to 12 MiB
(`M1-4F1: offscreen window widened to 1048576..12582912` and
`...2334720..12582912`, 2026-08-26). So the memory 1600x1200x32 is refused
for is memory this same driver has repeatedly proven and used. It is an
artefact of proving it AFTER deciding whether to publish the window.

## What it would buy, exactly

Registering against the ceiling instead gives a 3,260,416-byte window:

| OpenGL buffer | needs | at 1600x1200x32 |
| --- | --- | --- |
| 1024x768 | 4.50 MiB | does not fit |
| 800x600 | 2.76 MiB | fits, 0.35 MiB texture arena |
| 640x480 | 1.76 MiB | fits, 1.35 MiB arena |
| 512x384 | 1.12 MiB | fits, 1.98 MiB arena |

So: a 1600x1200 desktop could run an accelerated GL window up to 800x600.
That is the whole prize. It is not nothing, and it is not large.

## The obvious fix is wrong, and here is the evidence

The first idea was to call `osmgaProveVramTo(7 MiB, 12 MiB)` from
`-initFromDeviceDescription:`, just before the `start >= end` test, and
register against whatever it proves.

**Refused.** `osmgaProveVramTo` does not confine itself to the region it is
opening. Before touching 7..12 MiB it SAMPLES everything below -- one witness word
every 512 KiB, up to a 32-witness cap, starting at offset 0 (`:9016`) --
and offset 0 is the top left of the visible framebuffer. It restores them afterwards (`:9097`), but
at init the console is still live in the aperture, and the file states the
rule itself (`:8883`):

> **WHEN.** After the mode is programmed and while the screen is still
> BLANKED, which is the only moment when this driver owns the aperture and
> nothing is being scanned out of it. Before that the console may still be
> live in it.

Cross-review raised this independently and it was confirmed by reading the
witness loop. Calling the proof from init would break the driver's own
stated invariant to gain 3 MiB.

Two other shortcuts were considered and both fail:

- **Shrink the 256 guard rows.** Cannot help: with zero guard rows the start
  is still 7,680,000, which is above the 7,340,032 bound. The guard is also a
  fault-containment gap between scanout and client-addressable memory, and
  shrinking it for large modes makes the largest, most bandwidth-hungry mode
  the least protected.
- **Raise `OSMGA_S1_VRAM_PROVEN`.** That constant is the bound proven by a
  working 1600x1200x32 scanout. Raising it without proof is the exact mistake
  the probe exists to prevent.

## The design that does work: decide late, not early

Move the *decision*, not the proof.

1. **Init records intent only.** Where the block now computes `start`/`end`
   and either registers or refuses, it instead stores `start`, the requested
   flag, and — when `start < 7 MiB` — registers exactly as today. Behaviour
   for every mode that works today is unchanged, which matters: those modes
   have a published device before the WindowServer ever runs.
2. **Only the failing case is deferred.** When `start >= 7 MiB`, init records
   "requested, pending" and refuses nothing yet.
3. **`-programLinearMode`, while blanked**, after the existing
   `osmgaProbeVramExtent` call and in the same place the M1-4F1 widening
   already lives (`:3399-3437`): if a registration is pending, run
   `osmgaProveVramTo(start, 12 MiB)` and, if it passes, publish the device
   with `end = 12 MiB`. If it fails, log the refusal and leave the pending
   flag cleared, permanently.
4. **Once only.** `-programLinearMode` DOES run more than once: every
   `-enterLinearMode` calls it with no once-only guard, and
   `-revertToVGAMode` clears `linearModeActive` so a later enter programs
   again. So "once only" is a REQUIREMENT ON THE IMPLEMENTATION, not a
   property it inherits: the pending flag must be cleared before the
   attempt, on every path, success or failure. The existing M1-4F1 widening
   cannot fire on a deferred window in any case, because its guard requires
   `osmgaMmapWindowEnd == aligned(7 MiB)` (`:3428`) and a deferred
   registration publishes 12 MiB.

Note the proof range differs from M1-4F1's: it starts at `start` (8.89 MiB),
not at 7 MiB, because the region below `start` is scanout and must not be
opened to clients. The witnesses still cover everything below it, which is
what makes the proof meaningful.

## What has to be established before writing any of it

1. **Can the cdevsw entry be added from `-programLinearMode`?** The call is
   `+[IODevice addToCdevswFromDescription:open:close:read:write:ioctl:stop:
   reset:select:mmap:getc:putc:]` at `:3079`, supplying all eleven vectors
   with `mmap` in slot 8. It reads `"Character Major"` from the device
   description, uses -1 for automatic allocation, and needs an empty slot.
   Nothing in it or in this repository's notes says "init only" -- and
   nothing says it is safe to mutate the global cdevsw table from the
   WindowServer-driven `-enterLinearMode` context either. No precedent for
   late registration was found in this repository or in `ref/`.

   What would settle it, in order of preference: an OPENSTEP DriverKit rule
   about the call's context; the locking assumptions of `IOAddToCdevswAt`;
   an existing driver that does it late; and only last, a
   recovery-backed experiment on the machine.

   **This is the load-bearing unknown.** If the answer is no, the design
   falls and documenting the limitation is the whole answer.
2. **Does anything read the capability before the first mode set?** A client
   that probes between init and `enterLinearMode` would see `CAP_MMAP` clear
   and then set. The library re-probes per context, so this is probably
   harmless, but "probably" is not the standard here.
3. **The "must not unload" commitment moves later.** Today the S4a log warns
   at init. With a deferred registration the warning has to move with it.

## Is it worth doing?

Arguments for: it removes a silent, confusing behaviour — the operator's own
first reaction was that 1600x1200 "breaks OpenGL", and it does, for a reason
that has nothing to do with the hardware's capacity.

Arguments against: the gain is a 800x600 accelerated window on a 1600x1200
desktop; the change touches the driver's device-publication lifecycle, which
is the one part of it that a mistake makes unbootable; and the code is
frozen with three packages built and verified.

**Recommendation: document, do not implement, unless the operator wants
1600x1200 with 3D specifically.** The documentation half is already done —
`release-packaging/PORT-NOTES.md` and `INSTALL.md` now carry the per-mode
table and say plainly that 1600x1200x32 has no acceleration and that
1024x768x32 is the mode with the most room.

## If it is implemented, the test plan

1. Build, install, boot at **1024x768x32** — the mode that works today. The
   S4a log must be byte-identical to today's: registration at init, window
   1048576..7340031, then M1-4F1 widening to 12 MiB. Any difference here is
   a regression in the path that was not supposed to change.
2. Boot at **1600x1200x32**. Expect a new log line: pending at init, then the
   proof and the registration from `programLinearMode`, window
   9322496..12582912.
3. `openstep-mga-caps-client` must report `MMAP yes READY yes` and a vram
   length of 3,260,416.
4. `teapot_hybrid` at 640x480 must report the surface on the card, and its
   output must match the software rendering to the same tolerance the
   1024x768 case does (429 bytes in 921740).
5. Boot at **1600x1200 RGB:555/16**, where the window registers today: must
   be unchanged.

## Cross-review, 2026-08-26

The plan was reviewed before any code was written, which is the point of
having written it. The verdict agreed with the recommendation -- document,
do not implement, unless 1600x1200 with 3D is specifically wanted -- on the
grounds that the gain is bounded while the new dependency is global
device-publication timing, the one area where a mistake changes boot
behaviour rather than merely losing acceleration.

Three things in the draft were wrong and are corrected above. All three were
mine, all three were overstatements of what the evidence supports:

| claim as drafted | what is actually true |
| --- | --- |
| "the card has 16 MiB" | the driver maps and caps a 16 MiB aperture; it does not establish 16 MiB of distinct populated VRAM without the probe |
| "the driver already proves 7..12 MiB at every other mode" | it ATTEMPTS the proof only where a window was registered and the end is still 7 MiB, and widens only if it passes |
| "witnesses cover everything below" | they SAMPLE everything below, one word every 512 KiB, capped at 32 |

Two things the review established that the draft had merely hedged:

- The registration call is
  `+[IODevice addToCdevswFromDescription:...]` at `:3079`, not the
  `IOAddToCdevswTable` the draft guessed at. Its documented behaviour needs
  an empty cdevsw slot and says nothing about call context, so the
  load-bearing question stays open -- which is exactly why the
  recommendation is what it is.
- `-programLinearMode` genuinely runs more than once, so "once only" is a
  requirement the implementation must meet rather than a property it
  inherits. The draft asserted it as a fact.

One thing the review confirmed: proving `[start, 12 MiB)` rather than
`[7 MiB, 12 MiB)` is right, because the latter would write probe signatures
into 7.0-7.32 MiB of live scanout at this mode.
