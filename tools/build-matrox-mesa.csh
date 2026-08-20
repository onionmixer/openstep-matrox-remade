#!/bin/csh -f
# Build an accelerated libGL: the Mesa port's own sources, compiled with the
# hook point enabled, plus this project's back end.
#
# The stock library is not touched.  What comes out here is a second one, and
# the difference between them is entirely the three objects added below and
# one macro -- so a machine without this package has the Mesa it always had,
# rather than one restored to look like it.
#
# Run on the target:  sh /ndrv/openstep-matrox-remade/tools/build-matrox-mesa.csh

set mesa_src  = /tmp/OpenStepMesa342/src/Mesa-3.4.2
set mga_src   = /ndrv/openstep-matrox-remade
set port_src  = /ndrv/opennstep-mesa342/upstream/Mesa-3.4.2
set out       = /tmp/OpenStepMesaMGA
set accel     = -DOPENSTEP_MESA_ACCEL_HOOK

if (! -r $mesa_src/Make-config) then
    echo "build-matrox-mesa: stage and build the Mesa port first"
    exit 2
endif
if (! -r $mesa_src/lib/libGL.a) then
    echo "build-matrox-mesa: the stock libGL.a is not built yet"
    exit 2
endif

rm -rf $out
mkdir $out
cp $mesa_src/lib/libGL.a $out/libGL_mga.a

# The one Mesa object that changes: rebuilt with the hook point enabled.
# From the port's own tree, not the staged copy: the staging is done once and
# would otherwise hold whatever osmesa.c looked like then, so a change to the
# hook point would be compiled out of a stale file and nothing would say so.
cc -m486 -O -c $accel -I$mesa_src/src -I$mesa_src/include \
   -o $out/osmesa.o $port_src/src/OSmesa/osmesa.c
if ($status != 0) exit 1

foreach f (OpenStepMGAMesaHook OpenStepMGAMesaProbe OpenStepMGAMesaTriangle OpenStepMGAMesaBuffer)
    cc -m486 -O -c $accel -I$mesa_src/src -I$mesa_src/include -I$mga_src/hw3d \
       -o $out/$f.o $mga_src/mesa/$f.c
    if ($status != 0) exit 1
end

# One object, not three.  This ar truncates member names at fifteen
# characters, and all three of ours begin with the same fifteen -- archived
# separately they collide into a single ambiguous member and two of them
# quietly vanish.  Linking them together first sidesteps the whole question.
ld -r -o $out/osmgaccel.o $out/OpenStepMGAMesaHook.o \
    $out/OpenStepMGAMesaProbe.o $out/OpenStepMGAMesaTriangle.o \
    $out/OpenStepMGAMesaBuffer.o
if ($status != 0) exit 1

ar r $out/libGL_mga.a $out/osmesa.o $out/osmgaccel.o
ranlib $out/libGL_mga.a
if ($status != 0) exit 1

# Every symbol the back end must supply has to be present, or the library
# links and simply never accelerates.
foreach sym (OpenStepMesaAccelUpdateState OpenStepMesaAccelBuffer OpenStepMesaAccelReleaseBuffer OSMGAMesaProbeRun OSMGAMesaBuildTriangle OSMGAMesaProbeSubmit)
    nm $out/osmgaccel.o | grep "T _$sym" > /dev/null
    if ($status != 0) then
        echo "build-matrox-mesa: $sym is missing from the back end"
        exit 1
    endif
end

# Keep the result somewhere a reboot does not erase.  Staging lives in /tmp,
# which is cleared at every boot, and rebuilding Mesa from source to run one
# test costs about a quarter of an hour -- on a machine whose reboots are
# already the scarcest thing in this project.
set keep = $mga_src/build/mesa
if (! -d $mga_src/build) mkdir $mga_src/build
if (! -d $keep) mkdir $keep
cp $out/libGL_mga.a $keep/
cp $mesa_src/lib/libGL.a $keep/
cp -r $mesa_src/include $keep/
echo "build-matrox-mesa: kept a copy in $keep"

echo "build-matrox-mesa: PASS $out/libGL_mga.a"
