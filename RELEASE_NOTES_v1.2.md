# OPENSTEP Matrox G450 display driver 1.2

The driver, the Mesa 3.4.2 acceleration and the demos, for OPENSTEP 4.2 on
Intel. Install with `Installer.app`; `INSTALL.md` in the repository has the
order and the recovery route.

**This release is about SDL2.** With
[SDL2 openstep.2](https://github.com/onionmixer/openstep-sdl2/releases) an
SDL2 program can now draw on the card *and* put the frame on the screen
without it ever entering system memory. A spinning teapot at 800x600 went
from 0.54 frames a second to 43.9.

## The display driver itself is unchanged from 1.1

Byte for byte. If you are running 1.1 and do not use SDL2 or OpenGL, there
is nothing here for you. What changed is the acceleration package and the
demos, and the version moved with them so that a machine reports the release
it was installed from.

## What is new

### The SDL2 teapot

`OpenStepMesa342DemosMGA.pkg` gains `Examples/Mesa342/SDLTeapot` — the same
Utah teapot as the demo beside it, through SDL2, with a build script and a
README that explains what the numbers mean.

**Source only, no prebuilt binary.** It is the one demo that needs a second
product at the same prefix, and shipping a binary would put a statically
linked copy of one SDL2 release inside a Matrox package. Install
`OpenStepSDL2Libraries` and `OpenStepSDL2Headers` (openstep.2 or later) and
build it:

```
csh -f build-sdl-teapot.csh /LocalDeveloper /path/to/Mesa-3.4.2
OSMGA_MESA_WARP=1 OSMGA_SDLTEAPOT_PRESENT=3 ./sdlteapot_hybrid
```

Earlier SDL2 releases build but do not accelerate: openstep.2 is where SDL2's
GL backend stopped handing this driver's surface back at every bind.

### `OpenStepMGAMesaProbe.h` now ships

`OpenStepMGAMesaAccel.pkg` installs it beside the other three headers. A demo
that reports whether the card actually drew has to be able to ask **why** when
it did not, and that verdict is the probe's.

It also had a bug that only appeared once installed: it included
`"../hw3d/OpenStepMGAHW3D.h"`, a path that exists in this repository and
nowhere else. It uses the bare name now, as its siblings always did.

## A correction to the 1.1 notes

The 1.1 notes said:

> **The offscreen mirror can be narrowed** to the rectangle a frame actually
> drew ... Opt-in with `OSMGA_MESA_NARROW=1`.

**There is no such environment variable.** Narrowing is
`OSMGAMesaHookNarrowMirror()`, a function, and a search of the whole
repository finds no caller outside a test. The measured 41x is real and the
code is real, but **no application can reach it**, and setting
`OSMGA_MESA_NARROW=1` does nothing at all. Two runs a millisecond apart
either side of that variable are what showed it.

It is left as it is in this release rather than wired up in a hurry. What
made the SDL2 case fast was not narrowing the read-back but removing it.

## What made SDL2 fast, for anyone doing the same thing elsewhere

An accelerated frame lives in video memory. Delivering it the ordinary way
means copying it back into system memory first — 746 ns a pixel here — and
that copy runs at the close of every *rendering batch*, not every frame. One
teapot at 320x240 is 32 batches a frame, so the copies alone were 99.9% of
the frame time.

`OSMGAMesaBufferPresentMode(1)` stands that copy down and
`OSMGAMesaBufferPresentRect()` asks the kernel to blit video memory to the
visible screen. Both have been in this package since 1.0; what is new is that
SDL2 knows how to use them when an application hands them over.

Measured, same library and same demo source:

| build | delivery | wall | fps | read-backs |
| --- | --- | --- | --- | --- |
| stock Mesa | AppKit | 16.06 ms | 62.26 | none |
| `libGL_mga.a` | AppKit | 1847.43 ms | 0.54 | 32 a frame |
| `libGL_mga.a` | VRAM stamp | 22.77 ms | 43.91 | **none** |

First row 320x240, last row 800x600; the middle row is 320x240 because at
800x600 that path is about eleven seconds a frame.

## Packages

| package | changed since 1.1 |
| --- | --- |
| `OSMGADisplay.pkg` | version string only |
| `OSMGAMesaAccel.pkg` | ships `OpenStepMGAMesaProbe.h`, and its relative include is fixed |
| `OpenStepMesa342DemosMGA.pkg` | adds the SDL2 teapot |

Install order and the recovery route are in `INSTALL.md`. The Demos package
keeps the Mesa port's own version, because it is that package with a
directory added rather than one of ours.
