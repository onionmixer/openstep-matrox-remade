# The package silently drops the inspector nib, and the bundle name is why

## What happened

The driver package builds and the verifier catches it: the three nib files
are absent from the archive.  The nib DIRECTORY is there; its contents are
not.  No error is reported by the package tool -- the old tar just stops
writing entries whose path exceeds 100 characters.

## The arithmetic, which I got wrong the first time

I measured the path inside the bundle and concluded a one-character staging
basename left 13 characters of headroom.  That measured the wrong string:
the archive path includes the INSTALL location, not just the bundle.

    ./private/Drivers/i386/OpenStepMGAReplacementDisplay.config/English.lproj/DisplayInspector.nib/data.dependency
    = 110 characters, against a 100-character limit

The sibling driver package that shipped clears it at 91:

    ./private/Drivers/i386/SpaceSaver2Mouse.config/English.lproj/SS2MouseInspector.nib/data.nib

The whole difference is the bundle name: `SpaceSaver2Mouse.config` is 23
characters, `OpenStepMGAReplacementDisplay.config` is 36.  The sibling
passed with 9 to spare; we are 10 over.

The staging basename cannot fix this.  It is not part of the archive path
here -- `package` records paths relative to the stage, so the prefix is the
INSTALL path and nothing shorter is available.

## What cannot move

- `private/Drivers/i386/` is where OPENSTEP looks for driver bundles.
- `English.lproj/` is the localisation convention.
- `DisplayInspector.nib` is named in the inspector's own code; Configure
  loads it by name.
- The bundle basename is the DRIVER NAME -- `Driver Name`, `Class Names` and
  `Server Name` in both tables, and the class name compiled into the
  relocatable.  Renaming it is not a packaging change; it is renaming the
  driver.

## The options, as I see them

1. **Rename the driver.**  `OpenStepMGA.config` would be 18 characters and
   clear the limit by 8.  It touches the tables, the class name, the
   Makefile, every document, and any machine already running the driver --
   including the development machine's System.config.  Correct, and by far
   the most invasive thing anyone has proposed since the freeze.

2. **Ship the nib some other way.**  A post_install that unpacks the nib
   from a short-pathed archive carried elsewhere in the payload, e.g.
   `usr/local/lib/OpenStepMGA-nib.tar`.  The BOM then does not describe the
   nib, which is a real cost: the Installer cannot remove what it does not
   know it installed, and `DeleteWarning` becomes a partial truth.

3. **Ship without the inspector nib.**  The driver works; Configure shows
   the standard display panel instead of ours.  The three switches the
   inspector exposes are all editable in the table.  Honest, and a loss.

4. **Find out whether this tar really is the limit.**  `package` may use a
   tar with a longer limit than the one the sibling's note assumes, and the
   drop may have another cause.  Cheap to test, and it decides whether any
   of the above is needed.

## What I have not done

I have not chosen.  Option 1 changes the driver's identity after a code
freeze; option 2 trades BOM completeness for path length; option 3 gives up
a feature that works.  This needs the operator's call, and 4 first.

## Questions

1. Is 4 worth doing first, and what is the cheapest form of it?
2. Is there a fifth option -- something about how `package` records paths,
   or a tar it can be told to use?
3. If it comes to 1, is the driver's name actually load-bearing anywhere
   beyond the tables and the class, e.g. in a machine's System.config that
   would silently stop finding it?
