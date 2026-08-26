# OPENSTEP Matrox G450 display driver — 1.0

First public release of `OpenStepMGAReplacementDisplay`, a new DriverKit
display driver for the Matrox G450 on OPENSTEP 4.2 Intel, together with the
Mesa-side hardware 3D acceleration built on top of it.

The driver is not a patched `MatroxMGA`.  It is a new
`IOFrameBufferDisplay` subclass written from public hardware documentation
and public MGA implementations; where behaviour could only be settled by
looking at the original binary — the G450 pixel PLL search and the 15bpp
RAMDAC palette indexing — that was done by disassembly and cross-review, and
the findings are recorded rather than the code copied.

## Packages

Three, and you do not need all of them.

| Package | Where it installs | What it is |
| --- | --- | --- |
| `OpenStepMGAReplacementDisplay.pkg` | `/private/Drivers/i386`, not relocatable | the display driver.  Complete on its own |
| `OpenStepMGAMesaAccel.pkg` | relocatable, normally `/LocalDeveloper` | `libGL_mga.a` and the opt-in headers |
| `OpenStepMesa342DemosMGA.pkg` | relocatable, same prefix | the Mesa port's demos plus two demo pairs of ours |

Install order is driver first: the acceleration package needs the driver's
device, and the driver needs nothing from it.  `INSTALL.md` in the driver
package covers activation and, more importantly, how to get the screen back
if activation goes wrong.  Read that part before activating.

`OpenStepMesa342DemosMGA` is a VARIANT of the Mesa port's own
`OpenStepMesa342Demos`.  Install one or the other, not both.

It adds two directories.  `Examples/Mesa342/Teapot` draws the teapot offline
and writes a TIFF; `Examples/Mesa342/GLWindow` spins one in an 800x600 window
and puts the frame rate on the title bar.  Each is one source built into two
binaries -- a `_sw` form linked against stock Mesa with no Matrox code in it,
and a `_hybrid` form linked against `libGL_mga.a` -- and each ships its
source, its build script and its own guide, so nothing about how the binaries
were made is missing from the package.

Measured at 800x600 on a G450: `glwin_hybrid` 47.6 frames a second against
`glwin_sw`'s 12.8.  The margin is the delivery path rather than the drawing;
`README_glwin.md` says so and shows the split.  Note that `glwin_hybrid`,
unlike `teapot_hybrid`, needs the driver -- its picture reaches the screen by
a kernel blit -- and without it the window opens, says so, and stays empty.

## The display driver

- **Five resolutions** — 640x480, 800x600, 1024x768, 1280x1024, 1600x1200,
  VESA DMT timings, 60 Hz.
- **Four pixel formats** — RGB:888/32, RGB:555/16, RGB:256/8 (PseudoColor,
  with the WindowServer colormap applied through `setTransferTable:count:`)
  and BW:8 greyscale.
- **Greyscale takes 256, 16, 4 or 2 levels**, set by `Gray Levels` rather
  than by the mode name, because all four are the same 8bpp scanout with the
  same `rowBytes` and the same `IODisplayInfo` -- only the DAC ramp differs.
  Four levels is the picture the stock VGA driver gives at `BW:2`.  A true
  2bpp linear framebuffer is not something the G450 can scan out, which was
  established from the original binary rather than assumed; the stock
  driver reaches two bits through legacy planar VGA at 0xA0000, a path this
  driver does not use.
- **20 combinations** selectable in `Configure.app`, or by the `Display Mode`
  string in the config table at boot.
- A **Configure.app inspector** for the driver's own switches and the grey
  level.

The package installs the driver but does **not** activate it.  Nothing
changes until you add it in `Configure.app`.

Its three development diagnostics — `Raster Test`, `VRAM Mmap` and
`Mesa Acceleration` — all ship `No`.  The instance table in the package is a
release table written for that purpose; the development one is not shipped.

## Hardware 3D, and what "opt-in" means here

`libGL_mga.a` is a complete alternative `libGL`: the stock archive with
`osmesa.o` recompiled against the hook points and one further member,
`osmgaccel.o`, carrying the Matrox back end.  It sits BESIDE the stock Mesa
libraries and never over them.  Both are static archives, so nothing is
interposed at run time and every binary already built keeps behaving exactly
as it does today.

Acceleration is something a program asks for, not something it inherits.  A
program creates its `OSMesa` context as usual and then asks
`OSMGAMesaBufferOrigin()` whether the buffer it got lives on the card.  An
unmodified program cannot be accelerated by relinking alone: with
per-primitive fallback, part of a frame would land in video memory and part
in the program's own buffer with neither aware of the other.  Since a static
library has to be linked in anyway, the extra line costs nothing.

Where the card cannot take a primitive, Mesa draws it, in the same frame.

### Measured

Teapot scene, 16106 triangles, on the machine: **13.64 ms per frame, 73.3
frames per second**, from 16.76 ms and 59.7 fps at the start of the
optimisation work — 18.6% off the frame with no pixel changed, verified
against scene baselines rather than by eye.

Output correctness is checked against software Mesa on every scene: the
hardware and software renderings of the teapot differ in 429 bytes of
921740, 0.05%, all of them at triangle edges.

## Licensing

This project's own code is BSD 2-Clause.  Three third-party notices travel
with the packages, and each was checked rather than assumed:

- The **Matrox WARP microcode** is MIT, (c) 1999 Matrox Graphics Inc.  It is
  compiled into the driver, and its terms require the notice in copies.
- **Mesa 3.4.2** is not under one licence.  Every one of the 84 members of
  `libGL_mga.a` was mapped to its source directory; all the Mesa ones fall
  under the MIT-style Main Mesa Copyright, (c) 1999-2001 Brian Paul.  No LGPL
  component is in the archive — the LGPL parts of Mesa are GLU and the 3Dfx,
  SVGA, DOS and GGI drivers, none of which this project ships or touches.
- The **Utah teapot geometry** compiled into all four demo binaries is
  (c) 1993 Silicon Graphics, Inc., parts (c) Mark J. Kilgard 1994, under
  SGI's permissive grant.  It is cut from `tea.c` lines 581-730, which is
  inside that file's second copyright block; the GPL half of `tea.c` is
  never touched.

`LICENSE_INVENTORY.md` is the gate: it names every third-party text, where
the payload copy has to be, and how it is compared.  All comparisons are
byte-for-byte.

## Known limits

- **Hardware 3D depends on the display mode, and two modes have none.**
  Acceleration needs 32-bit colour: at 16bpp, 8bpp and both greyscale modes
  the driver reports the 3D path as not ready and everything renders in
  software. And **at 1600x1200 in 32-bit colour there is no acceleration
  either** -- the visible image plus the guard rows already fill the memory
  the offscreen window needs, so the window is never published and the log
  says `S4a: no usable offscreen window for this mode (start=9322496
  end=7340032), device NOT registered`. Nothing turns it on; 1024x768 at
  32-bit colour is the mode with the most room left over. The per-mode table
  is in `PORT-NOTES.md`, and `openstep-mga-caps-client` asks the driver
  directly. This is an artefact of proving the memory after deciding whether
  to publish the window rather than a shortage on the card;
  `docs/R2_LATE_WINDOW_REGISTRATION_PLAN.md` says what changing it would
  cost and why it was not changed for 1.0.
- **G400 is not supported.**  `0x0525102B` is shared between G400 and G450,
  and `Configure.app` may therefore offer the driver on a G400.  The code
  accepts revision `>= 0x80` and the G400 path is unimplemented; the shared
  ID is a candidate filter, not a support claim.
- **`VRAM Mmap` carries a commitment.**  Once it is on and a program has
  mapped the window, the driver must not be unloaded — client mappings
  outlive it.  Rebooting is fine; `kl_util` unloading it is not.
- **One reproducible hard freeze is unexplained.**  A specific combination
  of 3D diagnostic paths can hang the machine with nothing in
  `/usr/adm/messages`.  Seven hypotheses were tested and rejected across six
  freezes; the cause is not known.  The affected combination is refused by
  the test harness rather than left available, and none of the paths
  involved are reachable with the switches as they ship.  It is recorded as
  `3-62` in `docs/REMAINING_WORK.md`.
- **Two smaller defects are known and not fixed**, both behind switches that
  ship off: a FIFO under-reservation on a diagnostic path (`3-63`) and a 3D
  submission that does not restore the clip (`3-64`).
- Refresh is 60 Hz throughout.  No EDID-driven mode selection.

## Verification in this release

Every package has a verifier that runs on the target without installing
anything, and all three pass:

- **Driver** — 20 checks: payload completeness including all three
  `DisplayInspector.nib` files, no build residue, the release instance table
  (not the development one), Mach-O types, and the licence texts.
- **Acceleration** — the archive is proven to be the accelerated one by
  extracting `osmesa.o` and `osmgaccel.o` and reading them: 31 hook symbols
  against the stock archive's 0, both members i386.  The Installer
  architecture marker is checked separately, because the Installer does not
  look inside static archives.
- **Demos variant** — the stock demos are still all present; both pairs are
  present, i386, and owned by the BOM; each `_sw` binary is proven to carry no
  Matrox symbols while each `_hybrid` carries them; and each binary is asked
  for a string only its own build produces, so a renamed one cannot pass.

One packaging hazard is worth naming because it is silent: OPENSTEP's
`installer_tar` drops any path over 100 characters without failing.  The
driver's nib paths are 110, and three files vanished from a package that
reported success.  The payload is rebuilt with `installer_bigtar`, and the
verifier counts the nib files in the unpacked archive rather than trusting
the build.
