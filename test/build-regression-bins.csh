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
echo BUILDDONE
