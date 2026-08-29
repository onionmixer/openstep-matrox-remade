# Payload manifest

Every file either package installs, where it comes from, and where it goes.
Anything not listed here is not shipped; the exclusions at the end are the
ones that would otherwise ride along by accident.

## 1. `OSMGADisplay.pkg` — the driver

`DefaultLocation /`, not relocatable, `NeedsAuthorization YES`,
`DisableSparseInstall YES`.  Installs; does NOT activate.

| Destination | Source | Bytes | Note |
| --- | --- | --- | --- |
| `/private/Drivers/i386/OSMGADisplay.config/OSMGADisplay_reloc` | built on target | 369712 | the kernel driver, Mach-O preloaded i386 |
| `.../OSMGADisplay` | built on target | 15364 | the Configure inspector, the bundle's executable |
| `.../Default.table` | `OSMGADisplay/Default.table` | 2660 | all switches already `No`; ships verbatim |
| `.../Instance0.table` | **`pkg/Instance0.release.table`** | 704 | NOT the development instance -- see below |
| `.../Display.modes` | `OSMGADisplay/Display.modes` | — | five geometries x four formats, 60 Hz, no monitor identifiers.  The byte count is deliberately not pinned here: the verifier compares the packaged copy with the source byte for byte and asserts the list is the complete 5x4 product, which is a stronger check than a number that has to be edited by hand |
| `.../English.lproj/Localizable.strings` | built on target | 166 | |
| `.../English.lproj/DisplayInspector.nib/{data.classes,data.dependency,data.nib}` | built on target | 1382/43/3361 | all three files; see the nib hazard below |
| `/usr/local/Documentation/OpenStep-MGA-G450/*` | `release-packaging/`, `LICENSE`, `NOTICE` | — | licence, notice, install/recovery guide |

The release `Instance0.table` differs from the development one in exactly
three values, verified by diff -- `Raster Test`, `VRAM Mmap` and
`Mesa Acceleration` all `Yes` -> `No`; the other twenty keys are identical.
The reasons are in the file's own header.

## 2. `OSMGAMesaAccel.pkg` — the driver's client half

`DefaultLocation /LocalDeveloper`, relocatable, i386-only.  Requires the
driver package; the driver does not require this one.

| Destination | Source | Note |
| --- | --- | --- |
| `Libraries/libGL_mga.a` | `build/mesa/libGL_mga.a` | a COMPLETE alternative libGL: stock `libGL.a` with `osmesa.o` replaced and `osmgaccel.o` added.  It sits BESIDE the stock library, never over it. |
| `Headers/OpenStepMGAMesaHook.h` | `mesa/OpenStepMGAMesaHook.h` | the opt-in surface an application includes |
| `Headers/OpenStepMGAMesaBuffer.h` | `mesa/OpenStepMGAMesaBuffer.h` | buffer/present API |
| `Headers/OpenStepMGAHW3D.h` | `hw3d/OpenStepMGAHW3D.h` | **required**: `OpenStepMGAMesaHook.h` includes it for `OSMGAHW3DTri`.  Found by building the demo against a private prefix -- with only the first two headers the compile fails |
| `Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/COPYRIGHT` | Mesa port's `upstream/.../docs/COPYRIGHT` | byte-for-byte, release gate |
| `Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/COPYING` | Mesa port's `upstream/.../docs/COPYING` | byte-for-byte, release gate.  The LGPL text `COPYRIGHT` cites; **no LGPL component is in the archive** -- it ships because a licence that names another should not arrive without it |
| `Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/README.Mesa` | Mesa port's `upstream/.../docs/README` | byte-for-byte, release gate.  Mesa's disclaimer that it is not a licensed OpenGL implementation is HERE, not in `COPYRIGHT` |
| `Documentation/OpenStep-MGA-Accel/PORT-NOTES.md` | written for the release | what was added, which Mesa licence each archive member is actually under, and that no library is replaced |
| `Documentation/OpenStep-MGA-Accel/LICENSE` | `LICENSE` | BSD 2-Clause; a package that ships the library ships the terms it is under |
| `Documentation/OpenStep-MGA-Accel/NOTICE` | `NOTICE` | the WARP MIT notice, the nib provenance and SGI's teapot grant |
| `Tools/OpenStepMGAAccel-Intel` | `packaging/openstep/installer-architecture-marker.c` | tiny i386 Mach-O so the BOM is i386-only |

### 2a. Why Mesa's notices are under THIS package's directory

They were not, at first.  They went to
`Documentation/OpenStep-Mesa-3.4.2/`, which reads naturally and is where a
reader looks -- and which the Mesa port's Headers package already owns.

Measured on the machine, comparing BOMs: this package claims 11 files, the
three Mesa packages claim 53, and **exactly two were claimed by both** --
`Documentation/OpenStep-Mesa-3.4.2/COPYRIGHT` and `.../COPYING`.  Nothing
else collided: `libGL_mga.a` is distinct from `libGL.a` and `libGLU.a`, the
three headers are distinct from `Headers/GL/*`, and the architecture marker
is distinct from Mesa's three.

Whether this Installer reference-counts a shared path when a package is
removed is not established by anything in this repository, in `ref/`, or in
`Installer.app` itself -- and cross-review could not establish it either.
That is the reason to move rather than a reason to relax: a package that
might, on removal, take another package's licence texts with it is not one
to ship on an assumption.  The obligation is this package's own in any case,
because it is independently installable, so it cannot be discharged by a
file another package happened to put there.

They now go to `Documentation/OpenStep-MGA-Accel/Mesa-3.4.2/`, which names
the upstream version in the path.  `PORT-NOTES.md` points at them so
discoverability is not lost.

### 2b. The two demos — in the MESA DEMOS package, as pairs

There are two demos -- the offline teapot, which writes a file, and the
window demo, which spins one on the screen and reports its frame rate.  Each
is built from ONE source into TWO binaries, and both go into the Mesa port's
Demos package rather than this project's, because in each pair the stock form
carries no Matrox code at all.

Each ships its **source and its build script** as well as its binaries, so
nothing about how the shipped binaries were made is missing from the package.
It is not self-contained, and the READMEs say so: rebuilding also needs the
Mesa 3.4.2 SOURCE tree, because the teapot geometry is cut out of `tea.c` at
build time rather than copied into this repository, and it needs the Mesa and
Matrox libraries and headers installed at the prefix.

**It ships as a VARIANT, `OpenStepMesa342DemosMGA.pkg`, never in place.**
The Mesa port is released; its own Demos package has to keep building from
its own repository alone.  So this project builds an OVERLAY tree
(`pkg/build-demos-overlay.sh`) and the Mesa builder takes it or does not:

- `build-split-packages.csh` with `MESA_DEMO_OVERLAY` unset behaves exactly
  as it did before the variable existed, and produces
  `OpenStepMesa342Demos.pkg` version `3.4.2-openstep.1`.  Verified: run
  without the overlay after the change, that is the package that appears.
- With `MESA_DEMO_OVERLAY` set to the overlay tree, it copies the tree into
  the Demos payload and packages under `OpenStepMesa342DemosMGA.info` --
  version `3.4.2-openstep.1+mga.1`, its own name, its own description.
  `package` names a `.pkg` after the `.info` FILE, so the two artefacts
  cannot collide.  Install one or the other, not both.

Be honest about the direction: with an overlay, repo A's OUTPUT depends on
repo B's BUILD.  That is accepted for the variant only, and the released
artefact still builds from repo A alone.

| Destination (Demos payload, variant only) | Source | Note |
| --- | --- | --- |
| `Examples/Mesa342/Teapot/openstep-mga-mesa-teapot.c` | `test/openstep-mga-mesa-teapot.c` | one source, both forms; `-DOSMGA_TEAPOT_PLAIN` selects the stock one |
| `Examples/Mesa342/Teapot/build-teapot.csh` | `examples/build-teapot.csh` | `-sw`, `-hybrid`, or both |
| `Examples/Mesa342/Teapot/README_teapot.md` | `examples/README_teapot.md` | |
| `Examples/Mesa342/Teapot/teapot_sw` | built on target | stock Mesa only.  **No Matrox code.** |
| `Examples/Mesa342/Teapot/teapot_hybrid` | built on target | links `libGL_mga.a`; runs with or without the driver |
| `Examples/Mesa342/Teapot/NOTICE` | `NOTICE` | carries SGI's grant for the geometry compiled into both binaries -- the terms require it in supporting documentation |
| `Examples/Mesa342/Teapot/COPYRIGHT` | Mesa port's `upstream/.../docs/COPYRIGHT` | byte-for-byte; both binaries statically contain Mesa |
| `Examples/Mesa342/GLWindow/openstep-mga-glwin.m` | `test/openstep-mga-glwin.m` | one source, both forms; `-DOSMGA_GLWIN_PLAIN` selects the stock one |
| `Examples/Mesa342/GLWindow/build-glwin.csh` | `examples/build-glwin.csh` | `-sw`, `-hybrid`, or both |
| `Examples/Mesa342/GLWindow/README_glwin.md` | `examples/README_glwin.md` | how to run them, and how to read the title bar |
| `Examples/Mesa342/GLWindow/glwin_sw` | built on target | stock Mesa, AppKit delivery.  **No Matrox code.** |
| `Examples/Mesa342/GLWindow/glwin_hybrid` | built on target | links `libGL_mga.a`; **needs the driver**, see below |
| `Examples/Mesa342/GLWindow/NOTICE` | `NOTICE` | same SGI grant; the same geometry is compiled into these two as well |
| `Examples/Mesa342/GLWindow/COPYRIGHT` | Mesa port's `upstream/.../docs/COPYRIGHT` | byte-for-byte |

**The window pair is not symmetric, and the manifest should say so.**
`teapot_hybrid` runs with or without the driver.  `glwin_hybrid` does not:
its picture lives in video memory and reaches the screen by a kernel blit, so
the driver is needed for its DELIVERY and not only for its drawing.  Without
it the window opens, says "no accelerated surface" in its title, and shows
nothing.  `glwin_sw` is the member of that pair which runs anywhere.

Measured on a G450 at 800x600: `glwin_hybrid` 47.6 fps, `glwin_sw` 12.8 fps.
The gap is the delivery path, not the drawing -- the stock build's whole draw
phase is the SHORTER of the two, 8.58 ms a frame against 13.33.  What costs
it its frame rate is 63.07 ms a frame waiting for the window server.

That draw phase is not rasterisation on its own: it contains the evaluators,
the transform and the lighting, which are Mesa's software in both builds.
Being the same work on both sides, it cancels out of the 4.75 ms between
them, but nothing here measures a rasteriser by itself and the figures should
not be quoted as if it did.

**Why two binaries rather than one.**  Not because one would fail:
`teapot_hybrid` runs perfectly well with no driver present -- the library is
statically linked into it and the probe answers "no device", verified on the
target to exit 0 and write a file byte-identical to `teapot_sw`'s.  The pair
exists so the Mesa Demos package keeps a demo that is purely Mesa's, and so
that running both separates a Mesa problem from a driver problem in one
step.

`teapot_hybrid` does mean the Demos VARIANT contains a binary built from
this project's library.  That is a build-time input, not a change to Mesa:
the Mesa LIBRARIES the package ships are still the stock ones, which is what
the principle protects.  Asserted at build time rather than assumed --
`build-demos-overlay.sh` refuses to finish unless `teapot_sw` carries zero
`OSMGAMesaHook` symbols and `teapot_hybrid` carries them.  Measured in the
built payload: 0 and 31.  The same assertion is made for `glwin_sw` and
`glwin_hybrid`.

The overlay also BUILDS both demos with the very scripts it ships beside
them, from the staged prefix rather than from the tree.  A build script that
had drifted from its source, or a header the package forgot to install, fails
the packaging instead of reaching a user.

**Verified on the target, all four paths:** stock build, hybrid on real
hardware (16106 triangles, 100% on the card), hybrid forced to software, and
hybrid with acceleration unavailable.  The three software paths are byte
identical to each other; the hardware one differs in 429 bytes of 921740
(0.05%), all at triangle edges.

**The geometry is not in the payload, and the reason is not the one first
written here.**  `tea.c` is GPL only down to line 529; from line 531 to the
end it carries `Copyright (c) Mark J. Kilgard, 1994` under SGI's 1993 grant,
which permits copying and distribution for any purpose without fee provided
the copyright and permission notices travel with it.  The cut is lines
**581-730** -- `patchdata`, `cpdata`, `teapot()` -- entirely inside that
block.  So the excerpt IS redistributable, and both prebuilt binaries, which
embody it, are distributable with the SGI notice in their supporting
documentation.  `teapot-geometry.h` is nevertheless not committed and not
shipped, as a matter of keeping copied upstream material out of the
repository.  Both binaries are shipped so that RUNNING needs no Mesa source
tree; rebuilding does, and `build-teapot.csh` says so and checks the cut
before compiling.

`post_install` reruns `ranlib` -- OPENSTEP's archive index records the
pre-install pathname, so a relocated `.a` is unusable without it.

The driver package has hooks of its own, and for a different reason.  A note
here used to say it needed no `post_install` because it ships executables
rather than archives; that was true of `ranlib` and missed what the pair is
actually for.  Its `pre_install` copies the machine's instance tables aside
before the payload is extracted and its `post_install` puts them back, so an
upgrade does not cost the operator the resolution, the switches and the
`Location` the system worked out for the card.  Installing through
Installer.app used to do exactly that.

## Exclusions, and why each would otherwise ship

| Not shipped | Why |
| --- | --- |
| `.lastBuildTime` | build residue; the shipped sibling package strips it explicitly |
| `packaging/System.config.Instance0.activate-mga.table` | **this machine's own file** -- it names `SpaceSaver2Mouse Pro1000 SoundBlaster16PCI`.  Installing it would wreck an unrelated system, and site values belong in `site.conf`, never in shipped files |
| `OSMGADisplay/Instance0.table` | the DEVELOPMENT instance, with three diagnostics on |
| `teapot-geometry.h` itself | cut out of the Mesa tree at build time so the repository carries no copied upstream source.  **Not** a licence bar: the cut region is SGI's 1993 permissive grant, not GPL, and the binaries built from it ARE shipped, with the SGI notice |
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

## How the release is actually built, in order

Seven steps, and the first six run on the target.  Nothing here installs
anything; the whole sequence can be run on a working machine without
changing what it boots.

```sh
# 1-2. this project's two packages
sh  /ndrv/openstep-matrox-remade/pkg/build-driver-pkg.sh  /ndrv/openstep-matrox-remade /tmp/pkgout
sh  /ndrv/openstep-matrox-remade/pkg/build-accel-pkg.sh   /ndrv/openstep-matrox-remade /tmp/pkgout /ndrv/opennstep-mesa342

# 3. both demo pairs, staged as an overlay tree
sh  /ndrv/openstep-matrox-remade/pkg/build-demos-overlay.sh

# 4. the Mesa Demos VARIANT.  The wrapper sets the two variables the Mesa
#    builder reads and refuses when either target is missing -- both of its
#    defaults are wrong here, and MESA_STAGE_PARENT defaulting to /tmp once
#    produced a silent exit 2 with no output at all.
csh -f /ndrv/opennstep-mesa342/build/stage-openstep-mesa342.csh /ndrv
csh -f /ndrv/openstep-matrox-remade/pkg/build-demos-mga-pkg.csh

# 5. verify all three, without installing anything
sh  /ndrv/openstep-matrox-remade/pkg/verify-driver-pkg.sh    /tmp/pkgout
sh  /ndrv/openstep-matrox-remade/pkg/verify-accel-pkg.sh     /tmp/pkgout /ndrv/openstep-matrox-remade /ndrv/opennstep-mesa342
sh  /ndrv/openstep-matrox-remade/pkg/verify-demos-mga-pkg.sh

# 5b. and the gate that spans them: no file claimed by two packages
sh  /ndrv/openstep-matrox-remade/pkg/check-bom-overlap.sh

# 6. collect them into the repository, one tar per package
sh  /ndrv/openstep-matrox-remade/pkg/collect-release-pkgs.sh
```

```sh
# 7. on the HOST: compress, name, checksum
bash openstep-matrox-remade/pkg/make-release-assets.sh 1.0
```

**Editing a shipped file invalidates the built packages.**  `INSTALL.md`,
`PORT-NOTES.md`, `LICENSE` and `NOTICE` are payload files, so changing one
means steps 1-2 run again; `README_teapot.md`, `build-teapot.csh` and the
teapot source are payload too, so changing one means steps 3-4 run again.

All three verifiers now `cmp` what they unpack against its source copy
rather than only checking that it is present.  This is not hypothetical: a
package built before an edit passes every other check in them and ships
stale text, and it happened twice in one session -- `PORT-NOTES.md` gained
the per-mode acceleration table, and `README_teapot.md` gained the two
mode-related causes of "NO -- software only", which are the two a reader is
most likely to need.

Two things about step 6 that are not obvious.  The three packages are built
in three different places because three different owners build them, so
collecting is a step rather than a directory listing.  And each package
crosses as ONE plain tar: the export refuses to create a mode-444 file and
then write into it, which is exactly what `package` leaves the payload, the
`.info` and the install scripts as -- a file-by-file copy carries the 644
files and silently fails on the rest.  Inside a tar the modes are data, and
the executable bit on `pre_install` survives, which step 7 then checks
rather than assumes.

Step 4 wipes the Mesa builder's whole `dist` directory on every run, so a
run without the overlay leaves no variant behind at all.  The variant's
verifier says so in one line instead of reporting thirty consequences of the
same absence.

## The install rehearsal, and exactly what it costs

Before installing anything on a working machine, measure what installing
would change. `pkg/diff-against-installed.sh` does it read-only: it unpacks
the package and compares it file by file against the bundle that is running.

Measured on the development machine, 2026-08-26, with the package built from
that machine's own build tree:

```
the driver bundle
  same      OSMGADisplay_reloc
  same      OSMGADisplay
  same      Default.table
  PRESERVED Instance0.table   (the payload's is put back by post_install)
  same      Display.modes
  same      English.lproj/Localizable.strings
  same      English.lproj/DisplayInspector.nib/{data.classes,data.dependency,data.nib}
files the installed bundle has and the package does not
  LEFT ALONE (not in the package)  ./.lastBuildTime
the switches
  CHANGES   Raster Test         Yes -> No
  CHANGES   VRAM Mmap           Yes -> No
  CHANGES   Mesa Acceleration   Yes -> No
  same      Storm 2D Test, DMA Ring Test, WARP Test
  CHANGES   Display Mode        1600x1200 BW:8 -> 1024x768 RGB:888/32
documentation
  ADDED     /usr/local/Documentation/OpenStep-MGA-G450/{LICENSE,NOTICE,INSTALL.md}
```

**Not one executable, table or nib file differs.** The whole effect of the
install is one file -- `Instance0.table` -- plus three documents in a
directory that did not exist. That is the design working: the package
carries the RELEASE instance, and the machine is running the DEVELOPMENT
one.

So the rehearsal costs the operator's development settings and nothing else,
and the cost is undone by one copy:

```sh
# before
cp /private/Drivers/i386/OSMGADisplay.config/Instance0.table \
   /private/Drivers/i386/OSMGADisplay.config/Instance0.table.dev

# install with /NextAdmin/Installer.app, then confirm the release instance
# actually landed
grep '^"Raster Test"' \
   /private/Drivers/i386/OSMGADisplay.config/Instance0.table

# after, to go on developing
cp /private/Drivers/i386/OSMGADisplay.config/Instance0.table.dev \
   /private/Drivers/i386/OSMGADisplay.config/Instance0.table
```

No reboot is needed either way: nothing the install writes takes effect
until the next one, and restoring before rebooting means the machine comes
back exactly as it is now.

**Run the diff again on a machine that is NOT the build machine.** There,
the executables will differ and this output would be the wrong shape to
expect -- which is the point of measuring rather than assuming.
