#!/bin/csh -f
# Build an accelerated libGL: the Mesa port's own sources, compiled with the
# hook point enabled, plus this project's back end.
#
# The stock library is not touched.  What comes out here is a second one, and
# the difference between them is entirely the three objects added below and
# one macro -- so a machine without this package has the Mesa it always had,
# rather than one restored to look like it.
#
# Run on the target:  csh -f /ndrv/openstep-matrox-remade/tools/build-matrox-mesa.csh
#
# csh, not sh: this file is csh syntax and always has been, and the line here
# said sh for long enough that it was worth fixing rather than working around.

#
# Where the staged Mesa is, and where this build's own output goes.  Both are
# PARENTS with the leaf appended here, so that the removal below can only ever
# be pointed at a directory of the expected name.  Unset means /tmp, which is
# what it has always been and what a one-off build wants; a tree that should
# survive a restart wants somewhere else.
#
# They must not nest.  If the output were inside the staged tree the clearing
# of the output would take the stock library this reads with it.
#
#
# The names are short because they have to be: this csh refuses a variable
# name longer than 18 characters with "Variable syntax." and nothing else.
# Measured on the machine -- 18 works, 19 does not.
#
#
# The default is the local disk, not /tmp.  /tmp is emptied at boot, so a
# staged tree there had to be rebuilt after every restart -- about a
# quarter of an hour each time, for a tree that never changes.  Set the
# variable to /tmp explicitly for a genuinely one-off build.
#
if (! $?MESA_STAGE_PARENT) setenv MESA_STAGE_PARENT /usr/local/mesastage
if (! $?MGA_OUT_PARENT)      setenv MGA_OUT_PARENT /usr/local/mesastage
#
# switch rather than a comparison: csh expands an unquoted /* in an if into
# every entry of the root directory, which is a syntax error and not a test.
# A switch pattern is matched, not globbed, and it answers no for an empty
# value as well -- checked on the machine.
#
switch ("$MESA_STAGE_PARENT")
case /*:
    breaksw
default:
    echo "build-matrox-mesa: MESA_STAGE_PARENT must be absolute"
    exit 2
endsw
switch ("$MGA_OUT_PARENT")
case /*:
    breaksw
default:
    echo "build-matrox-mesa: MGA_OUT_PARENT must be absolute"
    exit 2
endsw
set mesa_src  = "$MESA_STAGE_PARENT/OpenStepMesa342/src/Mesa-3.4.2"
# The mount point is not always /ndrv: a wedged mount falls back to /ndrv2
# (tools/nx-mount.sh), and a script that hardcodes the primary cannot build
# from the fallback.  Same convention as the sh tools.
if (! $?MOUNTPT) setenv MOUNTPT /ndrv
set mga_src   = $MOUNTPT/openstep-matrox-remade
set port_src  = $MOUNTPT/opennstep-mesa342/upstream/Mesa-3.4.2
set out       = "$MGA_OUT_PARENT/OpenStepMesaMGA"   # -test appends a suffix, below

# Enforced, not only asked for in the comment above: the two must be separate
# places, or clearing one destroys what the other is read from.
if ("$out" == "$MESA_STAGE_PARENT/OpenStepMesa342") then
    echo "build-matrox-mesa: the output would be the staged tree itself"
    exit 2
endif
set accel     = -DOPENSTEP_MESA_ACCEL_HOOK

#
# Two first-class outputs, not one path written twice.
#
# The fault injectors exist to drive the refusal, narrowing and revocation
# paths on purpose, and they must never reach a release.  A flag that wrote
# the SAME archive would make the shippable artefact depend on which build
# ran last, and a rule like that gets forgotten -- twice in one afternoon, in
# this project's own history.  So the test flavour has its own directory, the
# packaging step accepts only the release path, and it refuses an archive in
# which the injectors' symbols appear.
#
#   csh -f build-matrox-mesa.csh          -> build/mesa      (shippable)
#   csh -f build-matrox-mesa.csh -test    -> build/mesa-test (injectors in)
#
# $#argv, not "$1": with no arguments at all csh does not give an empty
# string for $1, it stops with "Subscript out of range" -- and every existing
# caller of this script passes nothing.
set testhooks = ""
set outleaf   = mesa
if ($#argv > 0) then
    if ("$argv[1]" == "-test") then
        # Both switches.  TESTHOOKS gives the library its arms; SUBMIT_DRY
        # gives arm B the ioctl it sends, which is gated in
        # OpenStepMGAHW3D.h and in the driver.  A test library without the
        # second would still build -- arm B would simply not be compiled --
        # and that silence is exactly what this pair avoids.
        set testhooks = "-DOSMGA_MESA_TESTHOOKS -DOSMGA_HW3D_SUBMIT_DRY"
        set outleaf   = mesa-test
        set out       = "${out}-test"
    else
        echo "build-matrox-mesa: unknown argument '$argv[1]' (only -test)"
        exit 2
    endif
endif

if (! -r $mesa_src/Make-config) then
    echo "build-matrox-mesa: stage and build the Mesa port first"
    exit 2
endif
# The stock library, from the staging tree if it has been built this boot,
# and otherwise from the copy kept across reboots.  Staging itself is only a
# copy and costs seconds; building Mesa from source costs a quarter of an
# hour, and /tmp is emptied at every boot.
set stock = $mesa_src/lib/libGL.a
if (! -r $stock) set stock = $mga_src/build/mesa/libGL.a
if (! -r $stock) then
    echo "build-matrox-mesa: no stock libGL.a, in staging or kept"
    exit 2
endif

rm -rf "$out"
mkdir $out
cp $stock $out/libGL_mga.a

# The one Mesa object that changes: rebuilt with the hook point enabled.
# From the port's own tree, not the staged copy: the staging is done once and
# would otherwise hold whatever osmesa.c looked like then, so a change to the
# hook point would be compiled out of a stale file and nothing would say so.
cc -m486 -O -c $accel -I$mesa_src/src -I$mesa_src/include \
   -o $out/osmesa.o $port_src/src/OSmesa/osmesa.c
if ($status != 0) exit 1

foreach f (OpenStepMGAMesaHook OpenStepMGAMesaProbe OpenStepMGAMesaTriangle OpenStepMGAMesaBuffer OpenStepMGAMesaTexArena OpenStepMGAMesaTexture)
    cc -m486 -O -c $accel $testhooks -I$mesa_src/src -I$mesa_src/include -I$mga_src/hw3d \
       -o $out/$f.o $mga_src/mesa/$f.c
    if ($status != 0) exit 1
end

# One object, not three.  This ar truncates member names at fifteen
# characters, and all three of ours begin with the same fifteen -- archived
# separately they collide into a single ambiguous member and two of them
# quietly vanish.  Linking them together first sidesteps the whole question.
ld -r -o $out/osmgaccel.o $out/OpenStepMGAMesaHook.o \
    $out/OpenStepMGAMesaProbe.o $out/OpenStepMGAMesaTriangle.o \
    $out/OpenStepMGAMesaBuffer.o \
    $out/OpenStepMGAMesaTexArena.o $out/OpenStepMGAMesaTexture.o
if ($status != 0) exit 1

ar r $out/libGL_mga.a $out/osmesa.o $out/osmgaccel.o
ranlib $out/libGL_mga.a
if ($status != 0) exit 1

# Every symbol the back end must supply has to be present, or the library
# links and simply never accelerates.
foreach sym (OpenStepMesaAccelUpdateState OpenStepMesaAccelBuffer OpenStepMesaAccelDepthBuffer OpenStepMesaAccelReleaseBuffer OSMGAMesaProbeRun OSMGAMesaBuildTriangle OSMGAMesaProbeSubmit OSMGAMesaHookInjectRefusal)
    nm $out/osmgaccel.o | grep "T _$sym" > /dev/null
    if ($status != 0) then
        echo "build-matrox-mesa: $sym is missing from the back end"
        exit 1
    endif
end

# The named injector, checked in whichever direction this flavour calls for.
#
# Only the NAMED one.  OSMGAMesaHookInjectRefusal is a documented feature of
# the shipped teapot demo -- five paragraphs of examples/README_teapot.md
# turn on it -- and it has been in the library since the batching commit, so
# it belongs in the release and is asserted PRESENT above with the rest of
# the back end.  What must never ship is the mid-batch trapezoid spoiler,
# which exists only so a harness can reach the revoke-during-flush path.
#
# Absence alone would be a weak test -- a build that failed early and left a
# stale object would also show nothing -- so the test flavour asserts these
# are PRESENT under the same two names.  One check, both directions, and no
# way for a silent build failure to read as a clean release.
# The measurement arms joined this list on 2026-08-28.  They are exactly the
# same kind of thing as the spoiler: machinery that makes the library draw the
# wrong thing on purpose so a harness can time it.  Arm B additionally sends
# an ioctl that a shipped driver no longer answers.
foreach sym (OSMGAMesaHookInjectNamed OSMGAMesaHookInjectedNamed \
             OSMGAMesaHookMeasureArm OSMGAMesaHookDryStatus \
             OSMGAMesaHookDryCount OSMGAMesaProbeSubmitDry)
    nm $out/osmgaccel.o | grep "T _$sym" > /dev/null
    set injseen = $status
    if ("$outleaf" == "mesa") then
        if ($injseen == 0) then
            echo "build-matrox-mesa: $sym is in the RELEASE library"
            exit 1
        endif
    else
        if ($injseen != 0) then
            echo "build-matrox-mesa: $sym is missing from the TEST library"
            exit 1
        endif
    endif
end

# Keep the result somewhere a reboot does not erase.  Staging lives in /tmp,
# which is cleared at every boot, and rebuilding Mesa from source to run one
# test costs about a quarter of an hour -- on a machine whose reboots are
# already the scarcest thing in this project.
set keep = $mga_src/build/$outleaf
if (! -d $mga_src/build) mkdir $mga_src/build
if (! -d $keep) mkdir $keep
cp $out/libGL_mga.a $keep/
cp $stock $keep/
cp -r $mesa_src/include $keep/
# Copying an archive across NFS leaves its table of contents older than its
# members, and the linker refuses it outright rather than searching anyway.
ranlib $keep/libGL_mga.a
ranlib $keep/libGL.a
echo "build-matrox-mesa: kept a copy in $keep"

echo "build-matrox-mesa: PASS $out/libGL_mga.a ($outleaf flavour)"
