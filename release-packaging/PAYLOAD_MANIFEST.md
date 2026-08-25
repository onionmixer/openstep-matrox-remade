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
| `Documentation/OpenStep-Mesa-3.4.2/COPYRIGHT` | Mesa port's `upstream/.../docs/COPYRIGHT` | byte-for-byte, release gate |
| `Documentation/OpenStep-Mesa-3.4.2/COPYING` | Mesa port's `upstream/.../docs/COPYING` | byte-for-byte, release gate |
| `Documentation/OpenStep-MGA-Accel/PORT-NOTES.md` | written for the release | what was added, and that Mesa is not modified in place |
| `Tools/OpenStepMGAAccel-Intel` | `packaging/openstep/installer-architecture-marker.c` | tiny i386 Mach-O so the BOM is i386-only |

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
