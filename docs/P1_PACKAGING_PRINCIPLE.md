# P1 — What this project ships, and why it is two packages

The principle is the operator's and predates this document; it was recorded
only in a commit message (`16b7b91`, M1-3, 2026-08-20) and in a later
restatement, which is how it came to be re-derived from scratch once.  It is
written down here so that does not happen again.

## The principle

> **The package adds a second library rather than replacing one.**
>
> Mesa already renders in software.  What this project did inside Mesa is an
> ADDITION for the Matrox driver, so it ships as its own package -- not as a
> change to Mesa's.

Three questions it answers, and the answers:

| Question | Answer |
| --- | --- |
| What happens when our driver is not used? | The ported Mesa **library** is untouched, not restored.  A stronger guarantee than a fallback, because it does not depend on our fallback code being correct.  Precisely: the shipped `libGL.a` is the stock build, and the Mesa SOURCE tree does carry twelve dormant hook sites in `src/OSmesa/osmesa.c`, every one inside `#ifdef OPENSTEP_MESA_ACCEL_HOOK`, which the default build never defines.  Measured: 0 hook symbols in `libGL.a`, 31 in `libGL_mga.a`. |
| What does our package do to the routing of OpenGL calls? | **Nothing.**  The port ships STATIC libraries, so there is no dynamic linkage to shadow and no interposition to arrange.  The choice is made when an application is built; binaries built earlier keep the copy already linked into them. |
| Does the ported Mesa still render in software with neither our driver nor our package present? | Yes, unchanged. |

And the consequence the operator drew: **acceleration is something an
application opts into by asking us for a buffer.**  Accelerating an
unmodified application does not work -- with per-primitive fallback part of a
frame would land in video memory and part in the application's own memory
with neither aware of the other.  An application has to be rebuilt to pick up
the library anyway, so one further line costs it nothing.

## What that makes the products

| Package | Owner | Payload | Standalone? |
| --- | --- | --- | --- |
| `OpenStepMesa342*` | the Mesa port project | stock `libGL.a`, `libGLU.a`, headers, demos | already released; this project changes **no library and no header** of it.  See the amendment below for the one thing it does change. |
| `OpenStepMGAReplacementDisplay` | this project | the kernel driver bundle into `/private/Drivers/i386` | yes -- a complete display driver on its own |
| `OpenStepMGAMesaAccel` | this project | `libGL_mga.a` and the opt-in header, into `/LocalDeveloper` | no -- it is the driver's client half |

**The accelerated library belongs to THIS project, not to the Mesa port.**
Absorbing it into the Mesa Libraries package would make it a replacement,
and the guarantee "Mesa is untouched" would be gone.  (Cross-review proposed
exactly that absorption on license-tidiness grounds; it is refused here on
the principle.)

## Amendment, 2026-08-26 -- what "does not touch it" now means

The row above once read "this project does not touch it".  That was true
when it was written and is no longer, because the operator directed the
teapot demo into the Mesa DEMOS package -- and one of its two binaries,
`teapot_hybrid`, is linked against `libGL_mga.a`.  Cross-review was right to
call the old wording inconsistent with the plan, so the wording is amended
rather than the instruction reinterpreted.

What the principle actually protects is the **library**, and that is intact:

- No library in any `OpenStepMesa342*` package changes.  `Libraries/libGL.a`
  and `libGLU.a` stay the stock build, byte for byte.
- No header changes.
- Nothing is replaced, shadowed or interposed at run time.

What changes is the DEMOS product only, and it changes as a **separately
versioned variant**, never in place:

- `build-split-packages.csh` builds the released Demos package exactly as
  before when given no overlay.  That path is untouched and stays
  reproducible.
- Given an overlay produced by this project, it builds a Demos package with
  a different `.info` -- different Version, and a Description that says the
  teapot pair is in it.  Two artefacts with two names; neither overwrites
  the other's identity.

The dependency direction is worth being honest about.  Cross-review pointed
out, correctly, that an overlay does NOT keep the dependency pointing from
this project to Mesa: with an overlay, repo A's OUTPUT depends on repo B's
BUILD.  That is accepted deliberately, and bounded -- it exists only for the
variant artefact, and the released one still builds from repo A alone.

## What the accelerated library actually is, checked

`tools/build-matrox-mesa.csh` copies the stock `libGL.a`, replaces its
`osmesa.o` with one compiled with the hook point enabled, and adds one
`osmgaccel.o` linked from this project's six Mesa-side objects.  So it is
not a supplementary archive to be linked *alongside* Mesa: it is a COMPLETE
alternative `libGL`, which is precisely "a second library beside the stock
one".

The opt-in is real and small, from `test/openstep-mga-mesa-spin.c`: the
application creates and binds an OSMesa context as usual, then asks
`OSMGAMesaBufferOrigin()` whether it got a video-memory buffer, and
participates through `OSMGAMesaBufferPresentMode()`.  One extra header and a
different `-l` choice at build time.

Dependency runs one way: the accelerated package needs the driver (the
probe needs the driver's `/dev/osmgavram`); the driver needs nothing from
it.  So the driver package installs first.

## What follows regardless of the principle

`libGL_mga.a` contains Mesa code.  Ownership does not change that: the
accelerated package must carry Mesa's `COPYRIGHT` and `COPYING`, and must
retain Mesa's upstream statement that it is not a licensed OpenGL
implementation.  The gate is the SDL project's license-inventory model --
"a release gate, not a substitute for the upstream notices", with
byte-for-byte comparison of the copied texts.

## Release-blocking facts found while checking this

1. **`Instance0.table` must not ship as it is.**  Measured: `"Raster Test" =
   "Yes"` (an opt-in diagnostic that drives the drawing engine at every
   boot -- and whose FIFO over-reservations were only just repaired),
   `"MGA Memory Size" = "16"` (this card), `"VRAM Mmap" = "Yes"` (once
   enabled the driver must not be unloaded), `"Mesa Acceleration" = "Yes"`.
   `Default.table` has all of those at `"No"` and an empty `Location`, so it
   carries nothing machine-specific.  A release `Instance0.table` has to be
   written.
2. **`packaging/System.config.Instance0.activate-mga.table` is this
   machine's own file** -- it names `SpaceSaver2Mouse Pro1000
   SoundBlaster16PCI`.  It is a local convenience and must never enter a
   payload.
3. **G400 is not supported.**  `Auto Detect IDs 0x0525102B` is shared by
   G400 and G450, but the code accepts revision `>= 0x80` only and the G400
   path is unimplemented.  The release documentation must say that the
   shared ID is a candidate filter, not a support claim.
4. **The WARP microcode carries a Matrox MIT-style notice** whose terms
   require the notice in copies; `NOTICE` must preserve it.
5. **The inspector nib derives from Configure.app's stock nib**, so it needs
   a provenance record rather than being treated as new material.
6. **OPENSTEP's tar silently drops long-path nib files.**  The shipped
   sibling package uses a one-character staging basename for exactly that
   reason, and any build script here must do the same and then verify the
   nib files survived.
