# Packaging the G450 driver: the premises, and the work that follows

Code is frozen.  This is the preparation plan for the OPENSTEP Installer
package the project set out to produce.  Nothing here is a code change.

## The premises, checked rather than assumed

**The tool.**  `/NextAdmin/Installer.app/package`, run ON the target.  Both
sibling precedents use it and this project already used it once
(`packaging/openstep/build-recovery-staging-package.csh`, the R1
fail-closed staging package, verified on 2026-08-18).

**The shape of a released package here.**  The one product in this
repository that reached a public release, `openstep-spacesaver2ps2`, has:
`pkg/<NAME>.info`, `pkg/build-pkg.sh` (run on target), the built
`<NAME>.pkg.tar` committed, and `LICENSE` + `NOTICE` at project root.  Its
`.info` is `DefaultLocation /`, `Relocatable NO`, `NeedsAuthorization YES`,
`DisableSparseInstall YES`, and it stages `/private/Drivers/i386/<X>.config`
plus a `/usr/local/bin` tool.  It strips `.lastBuildTime` from the bundle.

**The most developed precedent** is `openstep-sdl20/release-packaging/`: a
split-package contract, a payload manifest, an upstream-provenance record,
and a LICENSE INVENTORY that is explicitly "a release gate, not a substitute
for the upstream notices", with byte-for-byte comparison of copied license
texts as the gate.

**What the bundle actually is**, from the installed copy on the machine:

    OpenStepMGAReplacementDisplay.config/
        OpenStepMGAReplacementDisplay          (the inspector, 15 KB)
        OpenStepMGAReplacementDisplay_reloc    (the driver, ~370 KB)
        Default.table  Instance0.table  Display.modes
        English.lproj/{Localizable.strings, DisplayInspector.nib/}
        .lastBuildTime                          <- must not ship

All three tables matter: omitting `Instance0.table` is exactly what booted
this machine into 800x600 four-colour VGA once (the Makefile's
GLOBAL_RESOURCES line), and `install-matrox-driver.sh` has verified their
presence ever since.

## The four premises that are NOT yet satisfied

**P1.  There is no LICENSE and no NOTICE in this project.**  The released
sibling has both.  Nothing can be published without them.

**P2.  This is a DISPLAY driver, so a bad install has no screen to
apologise on.**  The sibling keyboard/mouse package could say "edit
System.config and reboot" because a wrong keyboard still leaves a screen.
Here the recovery path is `config=Default` at the boot prompt, and the
project already owns the apparatus for it (the R1 staging package, the VGA
console restore at shutdown, `revertToVGAMode`).  The package's activation
policy has to be decided deliberately, not inherited.

**P3.  `packaging/System.config.Instance0.activate-mga.table` is this
machine's own file.**  It names `SpaceSaver2Mouse Pro1000 SoundBlaster16PCI`
-- one specific machine's drivers.  Installing it anywhere would wreck an
unrelated system, and it violates the repository's own rule that site values
live in `site.conf` and never in shipped files.  It is a local convenience
and must be excluded from any payload.

**P4.  The accelerated `libGL_mga.a` is a Mesa derivative.**  It is
`osmesa.o` built from the Mesa port's own source plus six objects of ours,
archived together.  Shipping it ships Mesa binary code and inherits Mesa's
notice requirements -- and `opennstep-mesa342` is a separate sibling project
with its own COPYING/COPYRIGHT/NOTICE and its own split packages.  Whether
the accelerated library belongs in THIS release, or is a variant of the Mesa
project's Libraries package, is a scoping decision to make before any
payload is designed.

## The proposed shape

**Two packages, not one**, following the split-package contract's logic:

  1. `OpenStepMGAReplacementDisplay.pkg` -- the driver bundle into
     `/private/Drivers/i386`, at `DefaultLocation /`, non-relocatable,
     `NeedsAuthorization YES`.  Installs; does NOT activate.  Activation
     stays a documented operator step (edit `Active Drivers`, reboot, and
     `config=Default` if the screen does not come back), because an
     installer that edits System.config can leave a machine with no display
     and no way to say why.

  2. `OpenStepMGAAcceleratedGL.pkg` -- the accelerated `libGL_mga.a` and its
     headers at a relocatable `/LocalDeveloper`, deferred until P4 is
     decided, and shaped as the SDL/Mesa Libraries packages are (i386 BOM
     marker, `pre_install` architecture rejection, `post_install` ranlib).

## The work, in order

  W1  LICENSE and NOTICE for this project; decide the licence, record the
      third-party boundaries already written in `refs/SOURCES.md` (the
      MatroxMGA binary is analysis-only and nothing derived from it ships;
      the teapot geometry is cut from the Mesa tree at build time and never
      committed, and must not enter a payload either).
  W2  A LICENSE_INVENTORY for this project in the SDL project's format,
      with the byte-for-byte gate.
  W3  Decide P4 (scope of the GL library) and record the decision.
  W4  PAYLOAD_MANIFEST: every file, its source, its destination, its mode.
      Explicitly excludes `.lastBuildTime` and the activate table.
  W5  `pkg/OpenStepMGAReplacementDisplay.info` and `pkg/build-pkg.sh`,
      modelled on the sibling's, with the driver's three tables and the
      lproj, and a `pre_install` that refuses non-i386.
  W6  A verify script in the R1 style: unpack the archive, assert the
      payload tree, the i386-only BOM, the relocatable's Mach-O type, the
      absence of the excluded files.
  W7  An INSTALL/RECOVERY document: activation steps, what a wrong install
      looks like, and `config=Default` recovery -- written before anyone
      installs, not after.
  W8  Operator-approved install rehearsal on the machine, then rollback.
      This is the only step that needs the target and a reboot.

W1-W6 need no reboot and no machine.  W7 needs the machine only for
transcript accuracy.  W8 is the gate.

## Questions

1. Is two packages right, or should the GL library wait entirely for the
   Mesa project to absorb it?
2. Is "install but do not activate" the right policy for a display driver,
   given the sibling package also declined to edit System.config -- or does
   the recovery apparatus this project owns make a guided activation
   defensible?
3. What licence should this project carry, given what `refs/SOURCES.md`
   already constrains?
4. Is anything in the bundle machine-specific the way the activate table is
   -- do `Default.table`, `Instance0.table` or `Display.modes` carry values
   that only suit this G450 or this monitor?
5. Is there a prerequisite I have missed?
