# R5 — the installer must stop overwriting the machine's configuration

Status: **plan, rewritten after cross-review blocked the first version.**
No code changed.

## The defect

`tools/install-matrox-driver.sh:19-21`:

```sh
rm -rf $DST                      # the whole installed bundle
cp -r $SRC $DST                  # rebuilt from the build tree
cp $INST $DST/Instance0.table    # and the REPOSITORY's instance table on top
```

The repository's `Instance0.table` is the **development instance**.  The
installed one, on a machine that has booted, carries what driverLoader and
Configure discovered about THIS machine -- measured today,
`"Location" = "Dev:0 Func:0 Bus:4"` and `"Version" = "0.5"` -- plus whatever
mode and switches the operator chose.  Every install throws that away.

What followed was measured, not inferred: Configure offered to ADD the
driver; adding it made Configure write a fresh instance from `Default.table`,
where every development switch is `No`; and 3D was silently off
(`Mesa acceleration switch is No`).

**What is NOT established** is Configure's decision predicate.  The empty
`Location` is a plausible cause and is consistent with the behaviour, but it
cannot be asserted: a CONFIGURED PS/2 keyboard instance also has
`"Location" = ""` (`ref/openstep/ps2/PS2Keyboard.config/Instance0.table`),
so an empty location does not universally mean unconfigured.  Configure's
executable is not in the local mirror and its parser was never located.
Settling it would need a controlled experiment varying `Location` alone,
then `Version` alone, relaunching Configure between snapshots.  This plan
does not depend on the answer: destroying machine-specific configured state
is wrong whatever makes Configure notice.

**A correction to my own earlier report**: I said `"MGA Memory Size"` had
been dropped from the installed table.  It had not -- that was a parsing
slip on my side, and the key is present.

## The rule

> **The installer installs the BUNDLE.  It does not install the
> CONFIGURATION.**

`Instance*.table` is the machine's.  Everything else in the bundle is ours
and is replaced every time.

## What the first draft got wrong, and why the rule is now simpler

The first draft added a merge: "add keys the installed table lacks, never
change a value it has".  Cross-review blocked it and the objections check
out:

1. **Absence is not "no choice" -- it is today's migration signal.**  The
   driver reads `Gray Levels`, and only when the key is ABSENT does a legacy
   `ColorSpace: BW:4` mean four greys (`:3275`, `if (!grayKeyPresent)`).  A
   merge that appended `Gray Levels = 256` would turn a legacy four-grey
   configuration into 256 greys -- exactly the migration contract R4
   promised not to break.
2. **The repository table is the DEVELOPMENT instance, not a set of
   defaults.**  Merging its missing keys into an older table would add
   `VRAM Mmap = Yes`, `Mesa Acceleration = Yes` and `Raster Test = Yes`,
   where `Default.table` says `No`.  An install would silently enable
   mappings and boot-time engine diagnostics.
3. **The table is not line-oriented.**  `Gray Levels` occupies three
   physical lines in the repository copy, because its value carries a
   comment.  Appending "the repository's line" would append an unterminated
   `/*` and corrupt the table; appending all three breaks the draft's own
   line-count invariant.

So the merge is dropped entirely:

> **Preserve `Instance*.table` byte for byte.  Do not merge, do not add
> keys, do not remove keys.**

A key a new release adds is handled where it belongs -- **in the driver**,
which already defaults sensibly when a key is absent, and for which absence
can carry meaning.  If a future release genuinely needs a key to appear in
an operator's table, that is an explicit, versioned migration written for
that release, not a side effect of copying files.

"Never remove a key" is also dropped as a stated rule: this project has
removed one of its own before (`Storm Blit Observe`, commit `482d589`).
Byte-for-byte preservation covers it without needing the claim.

## Two more defects found while rewriting

- **The installer mixes two snapshots.**  It copies the built bundle and
  then overlays `Instance0.table` and `Display.modes` from the CURRENT
  repository, so a source edit made after the last build pairs an old
  relocatable with new resources.  The Makefile already installs both into
  the built bundle (`GLOBAL_RESOURCES`, `Makefile:30`), so the overlay is
  redundant as well as wrong.  **Everything comes from the built bundle.**
- **`-f` accepts a zero-byte relocatable.**  A zero-byte bundle is the one
  failure this project has already been bitten by; the check must be on
  size and must cover every required member, not just the relocatable.

## The design

1. Build a **candidate directory** beside the destination, on the same
   filesystem: `$DST.new`.
2. Copy the complete built bundle into it.
3. If the destination exists, copy every `Instance*.table` from it into the
   candidate, byte for byte.  If it does not, use the built bundle's.
4. Remove `.lastBuildTime` -- build residue, which the release manifest
   already excludes from a payload.
5. `chown root`, `chmod go-w`, and only then validate the candidate:
   - the relocatable, the inspector, `Default.table`, `Display.modes`,
     `English.lproj/Localizable.strings` and all three nib files present and
     non-empty;
   - every `Instance*.table` byte-identical to the one it came from;
   - no group- or other-writable file anywhere (driverLoader abandons the
     whole driver configuration otherwise:
     `file ... is writable; aborting`).
6. Only if all of that passes: rename the old bundle to `$DST.prev`, rename
   the candidate into place.
7. On any failure before the rename, remove the candidate and leave the
   installed bundle untouched.

`$DST.prev` is kept **beside the bundle, not in /tmp**: /tmp is emptied at
boot, and the boot that would expose a broken install is exactly the one
that would erase the backup.  It is removed by the NEXT successful install,
so at most one generation is kept.

The two renames still leave a small power-loss window -- replacing a
non-empty directory is not one atomic operation -- but the live bundle is
never the target of a recursive copy, and the complete previous bundle
remains on disk.

**Close Configure.app before installing.**  It writes `Instance0.table`, and
an edit saved between the snapshot and the rename would be lost.

## What this does not fix

Configure can still rewrite an instance from `Default.table` if the operator
adds the driver for some other reason.  This plan removes the reason we were
creating.  If it happens anyway the switches are restored with
`tools/set-config-key.sh`, and the fact should be recorded rather than
quietly corrected.

## Test plan

No reboot for 1-6.

1. **Preservation.**  Install over the current bundle and confirm
   `Instance0.table` is byte-for-byte unchanged (`cmp`), while the
   relocatable has the new build's size and mtime.
2. **Legacy `BW:4`.**  Set the installed table to `ColorSpace: BW:4` with no
   `Gray Levels`, install, and confirm both facts survive -- this is the
   case the dropped merge would have broken.
3. **An older table.**  Remove `VRAM Mmap`, `Mesa Acceleration` and
   `Raster Test` from the installed table, install, and confirm they are
   still absent afterwards -- not silently added as `Yes`.
4. **First install.**  Move the bundle aside entirely, install, and confirm
   the built bundle's instance table is used and the bundle is complete.
5. **A second instance.**  Put an `Instance1.table` in the bundle, install,
   and confirm it survives.
6. **Refusal.**  Truncate the built relocatable to zero bytes, install, and
   confirm the installed bundle is untouched and the exit status is
   non-zero.
7. Then one reboot: the log must say `Mesa acceleration switch is Yes` and
   print an `S4a: VRAM window` line.  Switches surviving an install is the
   whole point.
