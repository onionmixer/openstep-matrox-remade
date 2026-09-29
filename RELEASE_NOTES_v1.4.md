# OPENSTEP Matrox G450 display driver 1.4

A rebuild, not a new driver.  The OSMesa contract layer this project shares
with the Radeon 9250 driver changed in the Mesa port (commit `f2ab89f`), and
`libGL_mga.a` compiles that file -- so the library is rebuilt, and the
release moves with it.  Install with `Installer.app`; `INSTALL.md` in the
repository has the order and the recovery route.

| Asset | Package |
| --- | --- |
| `OpenStep-MGA-G450-1.4-i486-Display.pkg.tar.gz` | `OSMGADisplay` 1.4 |
| `OpenStep-MGA-G450-1.4-i486-MesaAccel.pkg.tar.gz` | `OSMGAMesaAccel` 1.4 |
| `OpenStep-Mesa-3.4.2-openstep.1-mga.2-i486-Demos.pkg.tar.gz` | `OpenStepMesa342DemosMGA` `3.4.2-openstep.1+mga.2` |

## What changed

`OSMesaMakeCurrent` now offers a back end a video-memory surface only when
the context's pixel format is OSMESA_RGBA, BGRA or ARGB.  On Intel an
OSMESA_RGB or OSMESA_BGR context has the same shifts as ARGB, so 1.3
accepted them -- and then copied four bytes a pixel back into an array the
caller had allocated at three, writing a third of the array's size past its
end, while the software rasteriser and the engine disagreed about the row
length.  Those contexts now draw in their own buffer, in software, as they
would with no back end.  Every program this workspace ships (SDL2, GLQuake,
the demos) uses ARGB and is unaffected.  The full judgement is
`docs/MESA_F2AB89F_IMPACT.md`.

## Byte for byte against 1.3

- **The driver is 1.3's.**  Of the 13 files the driver package installs, 12
  are identical to the published 1.3 package's, the relocatable and the
  inspector included; the thirteenth is `Default.table`, whose only change
  is `"Version" = "1.4"`.
- **The library differs in one member.**  Of `libGL_mga.a`'s 84 members only
  `osmesa.o` (and the archive's symbol table) changed.
- The demo binaries are relinked against the new library.

## What was and was not verified

There was no G450 in the machine when 1.4 was built.  The packaging
verifiers all passed (every shipped file compared with its source, the BOMs,
the architecture, the symbol gates), no file is claimed by two packages
across this project, the Radeon project and the Mesa port, and with no card
present `teapot_hybrid` wrote a picture byte-identical to `teapot_sw`'s.
The accelerated path itself was **not** run on hardware for this release.
It is the path 1.3 shipped with, unchanged except for the format test above,
which an ARGB context passes exactly as before -- a judgement from the
source, not a measurement.
