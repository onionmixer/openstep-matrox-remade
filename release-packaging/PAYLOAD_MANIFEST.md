# Payload manifest

Every file either package installs, where it comes from, and where it goes.
Anything not listed here is not shipped; the exclusions at the end are the
ones that would otherwise ride along by accident.

## 1. `OpenStepMGAReplacementDisplay.pkg` — the driver

`DefaultLocation /`, not relocatable, `NeedsAuthorization YES`,
`DisableSparseInstall YES`.  Installs; does NOT activate.

| Destination | Source | Bytes | Note |
| --- | --- | --- | --- |
| `/private/Drivers/i386/OpenStepMGAReplacementDisplay.config/OpenStepMGAReplacementDisplay_reloc` | built on target | 369712 | the kernel driver, Mach-O preloaded i386 |
| `.../OpenStepMGAReplacementDisplay` | built on target | 15364 | the Configure inspector, the bundle's executable |
| `.../Default.table` | `OpenStepMGAReplacementDisplay/Default.table` | 2660 | all switches already `No`; ships verbatim |
| `.../Instance0.table` | **`pkg/Instance0.release.table`** | 704 | NOT the development instance -- see below |
| `.../Display.modes` | `OpenStepMGAReplacementDisplay/Display.modes` | 1435 | five geometries x five formats, 60 Hz, no monitor identifiers |
| `.../English.lproj/Localizable.strings` | built on target | 166 | |
| `.../English.lproj/DisplayInspector.nib/{data.classes,data.dependency,data.nib}` | built on target | 1382/43/3361 | all three files; see the nib hazard below |
| `/usr/local/Documentation/OpenStep-MGA-G450/*` | `release-packaging/`, `LICENSE`, `NOTICE` | — | licence, notice, install/recovery guide |

The release `Instance0.table` differs from the development one in exactly
three values, verified by diff -- `Raster Test`, `VRAM Mmap` and
`Mesa Acceleration` all `Yes` -> `No`; the other twenty keys are identical.
The reasons are in the file's own header.

## 2. `OpenStepMGAMesaAccel.pkg` — the driver's client half

`DefaultLocation /LocalDeveloper`, relocatable, i386-only.  Requires the
driver package; the driver does not require this one.

| Destination | Source | Note |
| --- | --- | --- |
| `Libraries/libGL_mga.a` | `build/mesa/libGL_mga.a` | a COMPLETE alternative libGL: stock `libGL.a` with `osmesa.o` replaced and `osmgaccel.o` added.  It sits BESIDE the stock library, never over it. |
| `Headers/OpenStepMGAMesaHook.h` | `mesa/OpenStepMGAMesaHook.h` | the opt-in surface an application includes |
| `Headers/OpenStepMGAMesaBuffer.h` | `mesa/OpenStepMGAMesaBuffer.h` | buffer/present API |
| `Headers/OpenStepMGAHW3D.h` | `hw3d/OpenStepMGAHW3D.h` | **required**: `OpenStepMGAMesaHook.h` includes it for `OSMGAHW3DTri`.  Found by building the demo against a private prefix -- with only the first two headers the compile fails |
| `Documentation/OpenStep-Mesa-3.4.2/COPYRIGHT` | Mesa port's `upstream/.../docs/COPYRIGHT` | byte-for-byte, release gate |
| `Documentation/OpenStep-Mesa-3.4.2/COPYING` | Mesa port's `upstream/.../docs/COPYING` | byte-for-byte, release gate |
| `Documentation/OpenStep-MGA-Accel/PORT-NOTES.md` | written for the release | what was added, and that Mesa is not modified in place |
| `Tools/OpenStepMGAAccel-Intel` | `packaging/openstep/installer-architecture-marker.c` | tiny i386 Mach-O so the BOM is i386-only |

### 2b. The teapot demo — in the MESA DEMOS package, twice

The demo is built from ONE source into TWO binaries, and it goes into the
Mesa port's Demos package rather than this project's, because the stock form
carries no Matrox code at all:

| Destination (Mesa Demos payload) | Source | Note |
| --- | --- | --- |
| `Examples/Mesa342/Teapot/openstep-mga-mesa-teapot.c` | `test/openstep-mga-mesa-teapot.c` | one source, both forms; `-DOSMGA_TEAPOT_PLAIN` selects the stock one |
| `Examples/Mesa342/Teapot/build-teapot.csh` | `examples/build-teapot.csh` | `-sw`, `-hybrid`, or both |
| `Examples/Mesa342/Teapot/README_teapot.md` | `examples/README_teapot.md` | |
| `Examples/Mesa342/Teapot/teapot_sw` | built on target | stock Mesa only.  **No Matrox code.** |
| `Examples/Mesa342/Teapot/teapot_hybrid` | built on target | links `libGL_mga.a`; runs with or without the driver |

**Why two binaries rather than one.**  Not because one would fail:
`teapot_hybrid` runs perfectly well with no driver present -- the library is
statically linked into it and the probe answers "no device", verified on the
target to exit 0 and write a file byte-identical to `teapot_sw`'s.  The pair
exists so the Mesa Demos package keeps a demo that is purely Mesa's, and so
that running both separates a Mesa problem from a driver problem in one
step.

`teapot_hybrid` does mean the Mesa Demos package contains a binary built
from this project's library.  That is a build-time input, not a change to
Mesa: the Mesa LIBRARIES the package ships are still the stock ones, which
is what the principle protects.

**Verified on the target, all four paths:** stock build, hybrid on real
hardware (16106 triangles, 100% on the card), hybrid forced to software, and
hybrid with acceleration unavailable.  The three software paths are byte
identical to each other; the hardware one differs in 429 bytes of 921740
(0.05%), all at triangle edges.

**The geometry is not in the payload.**  `teapot-geometry.h` is cut out of a
Mesa source tree at build time, because `tea.c` is GPL as a whole while the
teapot inside it is Kilgard's under GLUT's terms.  Both prebuilt binaries
are shipped so that RUNNING needs no such tree; rebuilding does, and
`build-teapot.csh` says so and checks the cut before compiling.

`post_install` reruns `ranlib` -- OPENSTEP's archive index records the
pre-install pathname, so a relocated `.a` is unusable without it.  (The
driver package needs no `post_install`: it contains executables, not
archives.)

## Exclusions, and why each would otherwise ship

| Not shipped | Why |
| --- | --- |
| `.lastBuildTime` | build residue; the shipped sibling package strips it explicitly |
| `packaging/System.config.Instance0.activate-mga.table` | **this machine's own file** -- it names `SpaceSaver2Mouse Pro1000 SoundBlaster16PCI`.  Installing it would wreck an unrelated system, and site values belong in `site.conf`, never in shipped files |
| `OpenStepMGAReplacementDisplay/Instance0.table` | the DEVELOPMENT instance, with three diagnostics on |
| `teapot-geometry.h` and anything built from it | cut out of the Mesa tree at build time precisely so it is never committed or redistributed |
| everything under `refs/` | analysis-only third-party material; `refs/SOURCES.md` forbids redistribution |
| `build/mesa/libGL.a` | that is stock Mesa; shipping it here would be replacing the Mesa package's job |
| test binaries, `scratchpad/`, `.glwin-*.c` | development only |

## Two hazards the build script must handle

1. **Long paths lose nib files.**  OPENSTEP's tar silently drops them, which
   is why the shipped sibling stages under a one-character basename.  Both
   scripts here do the same and then VERIFY the three nib files are present
   in the unpacked archive.
2. **The architecture marker is not optional.**  Verifying an i386-only BOM
   proves nothing if no marker was staged; the R1 script compiles one and
   the verifier asserts i386 present and m68k absent.
