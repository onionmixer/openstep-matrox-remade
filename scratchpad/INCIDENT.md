# Incident: a driver install left the console on VGA 800x600 in four colours

## What was done

1. I edited the display driver source (telemetry counters plus two default-off
   settings) at
   openstep-matrox-remade/OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay_reloc.tproj/OpenStepMGAReplacementDisplay.m
2. I built it with tools/build-matrox-driver.sh -- that succeeded, BUILD_EXIT=0,
   361292 byte relocatable, no warnings.
3. I installed it with tools/nx-install-driver.sh openstep-matrox-remade/OpenStepMGAReplacementDisplay
   The script reported: "빌드 OK: ..._reloc = 83040 bytes",
   "설치 검증 OK: OpenStepMGAReplacementDisplay_reloc = 83040 bytes",
   and that a reboot was needed because the driver was already loaded.
4. The user rebooted. The console came up as VGADisplay0 at 800x600, 4 colours.

## What I found afterwards

- /private/adm/messages for the boot has no OpenStepMGA line at all, only
  "mach: Registering: VGADisplay0".
- kl_util -s says the driver IS loaded.
- The installed relocatable was 83040 bytes and `sum` gave "09670 82" --
  byte-identical to the copy checked into the repository at
  openstep-matrox-remade/OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay.config/OpenStepMGAReplacementDisplay_reloc,
  dated 18 August. So the driver I built was never installed.
- The installed bundle contained only .lastBuildTime, Default.table (433 bytes),
  English.lproj, the 1088-byte executable and that relocatable. A working
  driver bundle on the same machine (SpaceSaver2Keyboard.config) also contains
  Instance0.table. Ours did not.
- The source bundle carries Instance0.table and Display.modes at its ROOT, not
  inside the .config directory; the Makefile's GLOBAL_RESOURCES lists only
  Default.table.
- Running the bundle's own `make` by hand in a copied tree fails: "header file
  'OpenStepMGAHW3D.h' not found" and dozens of consequent errors. That header
  is copied into the tproj by build-matrox-driver.sh, which the install script
  does not do.
- The install script's build line was:
      "$NX" "rm -rf $TMP && cp -r $MOUNTPT/$SRC $TMP && cd $TMP && make 2>&1 | tail -3" \
          || { echo "빌드 실패" >&2; exit 1; }
  and its install line was:
      "$NX" "rm -rf $DEST && cp -r $TMP/$NAME.config /private/Devices/ && chmod -R go-w $DEST && sync"

## My analysis (challenge it)

Two independent defects combined:

A. The build never ran successfully, and the failure was invisible, because a
   pipeline's exit status is the last command's -- `make ... | tail -3` exits
   zero whatever make did, so `||` never fired. The copied tree carries the
   repository's checked-in .config, so the stale 18 August relocatable was
   still sitting there and got installed. The size check compared the stale
   file against itself and passed.

B. `rm -rf $DEST` removed the installed Instance0.table, and nothing put one
   back, because it is not a build product. Without an instance table the
   driver loads but no instance is configured, nothing claims the card, and
   the console falls back to VGA.

I claim B is the direct cause of the black-and-blue screen and A is why the
change I thought I was testing was not on the machine at all. I also claim
every previous install through this script must have installed a stale bundle
in the same way, which I have not proved.

## The fix I applied

- clear $TMP/$NAME.config, $TMP/i386_obj and $TMP/sym from the copy before make
- capture make's own exit status through an echoed MAKE_EXIT, print the last 20
  lines of its log on failure, and do not touch the installed bundle
- copy Instance0.table and Display.modes into the installed bundle explicitly
- refuse the install if Instance0.table is absent afterwards
Then I rebuilt with build-matrox-driver.sh and installed by hand, with the two
tables, and the machine came back at 1024x768 RGB:888/32.

## Questions

1. Is B really sufficient to produce the VGA fallback, or is there a further
   cause I am missing -- for example the stale relocatable being incompatible
   with something else on the machine?
2. Is my claim in A correct that the checked-in .config is what got installed,
   and can you find any way the script could have installed a fresh build
   instead?
3. Does my fix leave any path where a stale or incomplete bundle can still
   reach /private/Devices?
4. Is copying Instance0.table from the source tree the right thing, or does it
   overwrite settings a user changed with the Configure application?
5. What else in this repository's tooling has the same "pipeline hides the
   exit status" shape?
