# OSMGAMesaAccel — what this package adds, and what it does not

This package ships one static library, `libGL_mga.a`, and the three headers
an application needs to opt into it.  It is the client half of the
`OSMGADisplay` driver: the driver draws the screen on its
own and needs nothing from here, while this library needs the driver.

## It adds a second library; it replaces nothing

Mesa 3.4.2 for OPENSTEP already renders in software, and that is not changed
by installing this package.

- `Libraries/libGL.a` and `Libraries/libGLU.a` from the Mesa port's
  Libraries package are **not touched, not moved and not shadowed.**  This
  package installs `libGL_mga.a` beside them, under a different name.
- Nothing is interposed at run time.  Both are STATIC archives, so the
  choice is made once, at the moment an application is linked.  A binary
  built before this package was installed keeps the copy already inside it
  and behaves exactly as it did.
- Removing this package removes the extra archive and its headers.  Nothing
  needs restoring, because nothing was displaced.

## The Mesa source does carry hook points, and they are dormant

Being exact about this, because "Mesa is untouched" is true of the library
and not of the source tree.

The Mesa port's `src/OSmesa/osmesa.c` contains twelve conditional sites, and
every one of them is inside `#ifdef OPENSTEP_MESA_ACCEL_HOOK`.  The Mesa
port's own build never defines that macro, so its shipped `libGL.a` contains
none of them.  Measured with `nm` on the target: the stock archive has **0**
hook symbols, `libGL_mga.a` has **31**.

`libGL_mga.a` is built by taking the stock `libGL.a`, replacing its
`osmesa.o` with one compiled with `OPENSTEP_MESA_ACCEL_HOOK` defined, and
adding a single further member, `osmgaccel.o`, which holds this project's
Matrox back end.  So it is a COMPLETE alternative `libGL` — 83 objects where
the stock archive has 82 — and not a supplement to be linked alongside it.
Link one or the other, never both.

## What an application has to do

Acceleration is opted into, not inherited.  An unmodified program cannot be
accelerated by relinking alone: with per-primitive fallback, part of a frame
would land in video memory and part in the program's own buffer, with
neither aware of the other.  Since a program must be rebuilt to pick up a
static library at all, the extra step costs it nothing.

```
cc -m486 -I/LocalDeveloper/Headers myprog.c \
    /LocalDeveloper/Libraries/libGL_mga.a -lm -o myprog
```

Create and bind an `OSMesa` context exactly as before, then ask whether the
buffer you were given lives on the card:

```c
#include "OpenStepMGAMesaBuffer.h"

if (OSMGAMesaBufferOrigin() != 0) {
    /* the surface is the engine's; present it rather than copying it */
    OSMGAMesaBufferPresentMode(1);
}
```

`OSMGAMesaBufferOrigin()` answers 0 when there is no card, no driver, or the
driver's `VRAM Mmap` and `Mesa Acceleration` switches are off.  An
application that checks it therefore runs unchanged on a machine with none
of this installed — which is what makes a single binary able to do both.
The counters in `OpenStepMGAMesaHook.h` (`OSMGAMesaHookDrawn()`,
`OSMGAMesaHookSoftware()`, `OSMGAMesaHookDeclined()` and the rest) report
afterwards how much of a frame the card actually took.

## Which display modes can accelerate, and which cannot

Acceleration needs a block of video memory that is NOT being scanned out, and
how much is left over depends entirely on the display mode. Two rules decide
it, and both are the driver's, not the library's:

1. **32-bit colour only.** The driver reports the 3D path as ready only at
   `RGB:888/32`. At 16bpp, 8bpp and the greyscale modes it reports "not
   ready" and this library renders in software — deliberately, and it tells
   you which capability was missing rather than failing quietly.
2. **The offscreen window has to fit.** The driver places it above the
   visible image plus a 256-row guard, and up to a 12 MiB ceiling.

Measured on a 16 MiB G450, at 32-bit colour:

| Display mode | Offscreen window | Largest accelerated buffer |
| --- | --- | --- |
| 640x480 | 10.20 MiB | 1280x1024 |
| 800x600 | 9.38 MiB | 1280x1024 |
| 1024x768 | 8.00 MiB | 1280x1024 |
| 1280x1024 | 5.75 MiB | 1024x768 |
| **1600x1200** | **none** | **none — no acceleration in this mode** |

**1600x1200 at 32-bit colour leaves no window at all.** The visible image is
7.32 MiB and the guard rows another 1.56 MiB, which is already past the
7 MiB bound the driver registers the window against. The driver says so in
the system log, in as many words:

```
OpenStepMGA S4a: no usable offscreen window for this mode
                 (start=9322496 end=7340032), device NOT registered
``` Every OpenGL program
falls back to software in that mode, whatever size its own buffer is. If you
want hardware 3D, 1024x768 at 32-bit colour is the mode with the most room
left over; 1280x1024 works but leaves little space for textures.

A buffer bigger than 1600x1200 never fits in any mode: colour 7.32 MiB plus
the reserved depth 3.66 MiB is 10.99 MiB, and the largest window this driver
offers is 9.78 MiB.

You do not have to guess. Ask the driver:

```
cc -O -o caps openstep-mga-caps-client.c && ./caps
```

which prints the four capability bits and, when one is missing, says which:

```
ENABLED yes  MMAP yes  CMD yes  READY NO
VERDICT: software (missing 00000008)
```

## Requirements

1. `OSMGADisplay` installed **and active** — the display in
   use must be the Matrox driver, not the VGA fallback.
2. In `Configure.app`, on the display's Instance, `VRAM Mmap` = `Yes` and
   `Mesa Acceleration` = `Yes`.  Both ship as `No`; turning `VRAM Mmap` on
   means the driver must not afterwards be unloaded.
3. A G450.  The `0x0525102B` device ID is shared with the G400, and the
   G400 path is not implemented — the ID is a candidate filter, not a
   support claim.
4. i386.  The archive is i386 machine code and `pre_install` refuses
   anything else.

`ranlib` runs in `post_install` because OPENSTEP's archive index records the
pathname the archive had when `ranlib` last ran; a relocated `.a` is
unusable until the index is rebuilt where it now lives.

## Licensing, and where the sources are

`libGL_mga.a` contains Mesa code, so Mesa's own terms travel with it.
`Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/COPYRIGHT`, `.../COPYING` and
`.../README.Mesa` are copied byte for byte from the Mesa port's upstream
tree.

Which of those terms actually applies was checked member by member rather
than assumed, because Mesa 3.4.2 is not under one licence. `COPYRIGHT`
records that the core library moved OFF the GNU LGPL at Mesa 3.1 and onto
the XFree86 (MIT-style) grant, while some components — GLU, the 3Dfx, SVGA,
DOS and GGI drivers — stayed LGPL.

`libGL_mga.a` holds 84 members: `__.SYMDEF`, which is the archive's own
symbol table and no one's source, and 83 object files. Every one of the 83
maps to `src/*`, `src/X86/*` or `src/OSmesa/*`, except this project's own
`osmgaccel.o`. All of the Mesa ones
fall under the Main Mesa Copyright: *Copyright (C) 1999-2001 Brian Paul*,
permission to deal in the software without restriction, provided the
copyright and permission notice are included in all copies. **No LGPL
component is in this archive** — GLU is `libGLU.a`, which this package does
not ship and does not touch. So the obligation is notice retention, and it
is discharged by the copied `COPYRIGHT`.

`COPYING` is shipped anyway, because `COPYRIGHT` names it for the components
that are still LGPL, and a licence document that cites another should not
arrive without it.

Mesa's own disclaimer — that it is not a licensed OpenGL implementation, and
that OpenGL is a trademark of Silicon Graphics — is in `README.Mesa`
(upstream `docs/README`), not in `COPYRIGHT`. That is why the file is
shipped as well.

The sources, for anyone who would rather rebuild than relink: the Mesa side
is the Mesa port's own tree, where the hook sites are committed under the
macro named above; this project's side is the driver repository, whose
headers are in this package.

This project's own code is BSD 2-Clause: see `LICENSE`.  `NOTICE` carries
the notices that must travel with it — the Matrox WARP microcode's MIT
notice, the provenance of the inspector nib, and SGI's grant covering the
teapot geometry compiled into the demo binaries.
