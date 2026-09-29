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

## The device node is made by the package

1.3 never made `/dev/osmgavram`: the driver took the first free character
major (1 on the development machine, the same major as the world-writable
`/dev/pp0`), and the node had to be made by hand.  From 1.4 the bundle's
tables name `"Character Major" = "37"`, the install scripts carry that key
into an upgraded machine's instance table too, and `post_install` makes the
node -- `c 37 0`, mode 666 -- on every install, fresh or not.  Every user can
therefore use the card once `VRAM Mmap` and `Mesa Acceleration` are on, and
every user who can open the node can also crash the machine through a defect
in the kernel's mmap; `INSTALL.md` says both, and how to restrict it.
Checked on the target against a scratch prefix (node made `crw-rw-rw- 37, 0`)
and against a 1.3 instance table (the key appended, every other line
unchanged).

## Byte for byte against 1.3

- **The driver binary is 1.3's.**  The relocatable and the inspector are
  identical to the published 1.3 package's; `Default.table` changes in
  `"Version" = "1.4"` and the new `"Character Major"`, and the install scripts
  and the identity-key transform change as above.
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
source, not a measurement.  The same holds for the fixed major: the driver
registering at 37 was not booted on a G450.  It is the mechanism the Radeon
9250 driver uses at 38 and was booted there, and if another driver holds 37
the registration is refused and OpenGL draws in software -- the display is
not affected.
