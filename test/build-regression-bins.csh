#!/bin/csh -f
# The binaries the whole regression net needs, in one place.
#
# /tmp is emptied at every boot, so every reboot costs these builds; a
# multi-line build over the remote shell does not chain reliably, and this
# is the shape the sweeps already use.
#
# Two parallel lists rather than "name:source" pairs: csh's :r and :e split
# a word on its DOT, not on a colon, so a pair list silently hands the whole
# string to both halves and every build fails with the same status.
cd /ndrv/openstep-matrox-remade/test
set I = "-I/usr/local/var/openstep-matrox/OpenStepMesa342/src/Mesa-3.4.2/include -I/ndrv/openstep-matrox-remade/hw3d"
set L = /ndrv/openstep-matrox-remade/build/mesa/libGL_mga.a
set bins = ( cov seam attrib mesh zagree zfunc zoff tex unif )
set srcs = ( openstep-mga-mesa-coverage-test openstep-mga-mesa-seam-test \
             openstep-mga-mesa-attrib-test openstep-mga-mesa-mesh-test \
             openstep-mga-mesa-depth-agree openstep-mga-mesa-depthfunc-test \
             openstep-mga-mesa-polyoffset-test openstep-mga-mesa-texdraw-test \
             openstep-mga-mesa-uniformfill-test )
set i = 1
while ( $i <= $#bins )
    set bin = $bins[$i]
    set src = $srcs[$i]
    cc -m486 -O $I -o /tmp/$bin $src.c $L -lm >& /tmp/bb-$bin.log
    echo "$bin = $status"
    @ i++
end
cc -m486 -O -o /tmp/waits openstep-mga-hw3d-waits.m -lDriver >& /tmp/bb-waits.log
echo "waits = $status"

#
# And the ones that call a TEST HOOK, which the release library does not
# define.  R20's rule is that a hook whose misuse loses pixels must not ship,
# so `-test` builds a second library beside the first and these link against
# THAT one.  Building them against the release library fails at link, which
# is the intended outcome rather than something to work around: it is how a
# test that reaches for a gated symbol announces itself.
#
set LT = /ndrv/openstep-matrox-remade/build/mesa-test/libGL_mga.a
set tbins = ( tnm mircost )
set tsrcs = ( openstep-mga-narrow-mirror-test openstep-mga-mirror-cost )
set i = 1
while ( $i <= $#tbins )
    set bin = $tbins[$i]
    set src = $tsrcs[$i]
    cc -m486 -O $I -o /tmp/$bin $src.c $LT -lm >& /tmp/bb-$bin.log
    echo "$bin = $status"
    @ i++
end
echo BUILDDONE
