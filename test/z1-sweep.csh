#!/bin/csh -f
# M13 Z1: two sweeps x three arms x two passes.
#
# Both sweeps and all three arms come from the SAME library in the same
# run, because the question is a difference between arms and a baseline
# recorded at another time is a second variable.
#
# One process per (sweep, arm, pass): the WARP switch is sampled once per
# process, and the t order is shuffled INSIDE the process, so the ordering
# guard costs nothing here.
cd /ndrv/openstep-matrox-remade
if (! -d scratch-z1) mkdir scratch-z1
foreach sw ( a b )
    foreach p ( 1 2 )
        setenv OSMGA_MESA_ACCEL 0
        setenv OSMGA_MESA_WARP 0
        /tmp/z1 $sw $p > scratch-z1/z1-$sw-soft-$p.txt
        setenv OSMGA_MESA_ACCEL 1
        setenv OSMGA_MESA_WARP 0
        /tmp/z1 $sw $p > scratch-z1/z1-$sw-trap-$p.txt
        setenv OSMGA_MESA_ACCEL 1
        setenv OSMGA_MESA_WARP 1
        /tmp/z1 $sw $p > scratch-z1/z1-$sw-warp-$p.txt
    end
end
echo Z1DONE
