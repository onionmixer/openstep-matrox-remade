#!/bin/csh -f
#
# Build the accelerated libGL without doing the whole thing again every time.
#
# The Mesa port stages a copy of itself and builds that copy.  With the copy
# under /tmp, which OPENSTEP empties at boot, every restart cost a staging, a
# Mesa build and an accelerated build -- and the accelerated build is the only
# one of the three that usually needed doing.  This puts the copy somewhere
# that survives and stages again only when it has to.
#
# When it HAS to is not a matter of taste.  "make openstep" delegates to the
# submakes and never runs "make dep", so a changed Mesa source or header can
# leave an object behind that nothing will rebuild.  Only the accelerated back
# end is safe to build on its own, and that build clears its own output and
# recompiles all of it every time.  So: anything newer than the staged tree
# means stage again.
#
#   csh -f .../nx-mesa-build.csh          stage only if the source moved
#   csh -f .../nx-mesa-build.csh -force   stage regardless
#
set src    = /ndrv
set parent = /usr/local/var/openstep-matrox
set staged = "$parent/OpenStepMesa342"
set mark   = "$staged/.stage-complete"
# Kept beside the tree rather than inside it, so that staging -- which
# removes the tree entire -- does not take the record with it.  A missing
# tree is caught by the mark above, so the two together cannot lie.
set stamp  = "$parent/.port-stamp"
#
# What the digest covers: the directories staging actually copies, and not the
# whole checkout.  Listing the checkout put .git into the digest -- two thirds
# of its lines -- so committing anything at all looked like the source had
# moved, and forced a staging that changed nothing.
#
set ported = "$src/opennstep-mesa342/upstream $src/opennstep-mesa342/build $src/opennstep-mesa342/packaging $src/opennstep-mesa342/docs $src/opennstep-mesa342/examples $src/opennstep-mesa342/test"
set force  = 0

# Nested, not "&&": csh substitutes before it evaluates, so a one-line test
# mentioning $argv[1] reaches for it even when there are no arguments.
if ($#argv > 0) then
    if ("$argv[1]" == "-force") set force = 1
endif

setenv MESA_STAGE_PARENT "$parent"
setenv MGA_OUT_PARENT "$parent"

if (! -d "$parent") then
    echo "nx-mesa-build: $parent does not exist; make it first"
    exit 2
endif

set restage = 0
if ($force) then
    set restage = 1
    echo "nx-mesa-build: staging again because -force was given"
else if (! -r "$mark") then
    set restage = 1
    echo "nx-mesa-build: staging because there is no complete tree at $staged"
else
    #
    # Has the port source moved since the copy was made?
    #
    # NOT by comparing modification times.  This was found the hard way: the
    # two clocks were four years apart, so every source file looked newer than
    # everything staged and a time comparison meant "stage every time" -- the
    # very thing this exists to stop.  The clocks have since been put right,
    # so that particular reading would work today.
    #
    # The digest stays anyway.  It asks a question about the files themselves
    # and so does not depend on two machines agreeing about the time, which
    # they did not last week and might not next week.  cp does not carry a
    # file's time across either, so the staged tree's times say when it was
    # staged rather than what it holds.
    #
    # A digest of the listing instead: both readings are of the same files
    # over the same mount, so nothing depends on either clock.  Sorted,
    # because this ls returns a directory in whatever order it likes and two
    # unsorted readings of an unchanged tree already disagreed.
    #
    set now = `ls -lR $ported | sort | sum`
    set was = ""
    if (-r "$stamp") set was = "`cat $stamp`"
    if ("$now" != "$was") then
        set restage = 1
        echo "nx-mesa-build: staging because the port source has changed"
    endif
endif

if ($restage) then
    csh -f $src/opennstep-mesa342/build/stage-openstep-mesa342.csh $src || exit 1
    csh -f $src/opennstep-mesa342/build/build-openstep-mesa342.csh || exit 1
    #
    # Written the way it will be READ.  Piping sum straight into the file
    # keeps its own spacing, while the comparison above goes through a
    # backquote, which splits on whitespace and rejoins with one space -- so
    # the two never matched and every run staged again.
    #
    set now = `ls -lR $ported | sort | sum`
    echo "$now" > "$stamp"
else
    echo "nx-mesa-build: the staged Mesa is current; not building it again"
endif

csh -f $src/openstep-matrox-remade/tools/build-matrox-mesa.csh || exit 1
echo "nx-mesa-build: PASS"
