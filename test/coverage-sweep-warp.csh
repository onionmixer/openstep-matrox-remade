#!/bin/csh -f
# The coverage sweep, three arms instead of two.
#
# coverage-sweep.csh runs software against the accelerated path and leaves
# the two in scratch-cov/.  That was every path there was.  With a second
# accelerated tier there are three, and the middle one is not optional:
# comparing a freshly built WARP arm against a baseline recorded months ago
# compares two things that both moved.  So the trapezoid arm is re-measured
# from the SAME library in the same sweep, and it is the one that has to
# reproduce the recorded result.
#
# Nine shapes, not the original sweep's seven.  Shapes 8 and 9 are shape 1
# translated by half a pixel and by 37/128 of one, and coverage-compare.py
# has known about them for as long as they have existed while
# coverage-sweep.csh still stopped at seven -- so the two shapes built to
# ask what a sub-pixel translation does to the fill rule had never been
# swept.  For a tier whose whole first defect WAS a half-pixel translation,
# they are the two that matter most.
#
# Explicit values, never "unset".  OSMGA_MESA_ACCEL treats unset and 1
# identically (Probe.c:80-86 -- only 0/n/f force software), but the WARP
# switch is sampled once per process and cached, so a stale environment is
# a stale arm, and saying which arm this is beats inheriting it.
#
#   csh -f test/coverage-sweep-warp.csh
#
# Leaves three directories, each in the layout coverage-compare.py reads:
#
#   scratch-cov-sw    sw-N-P.txt only   (software, the shared control)
#   scratch-cov-trap  hw-N-P.txt        (trapezoid tier)
#   scratch-cov-warp  hw-N-P.txt        (WARP tier)
#
# The software files are copied into the other two afterwards, because
# coverage-compare.py wants both arms in one directory and running the
# software pass three times over would only measure the same thing twice.
cd /ndrv/openstep-matrox-remade
foreach d ( scratch-cov-sw scratch-cov-trap scratch-cov-warp )
    if (! -d $d) mkdir $d
end
set p = 1
while ( $p <= 3 )
    set i = 1
    while ( $i <= 9 )
        setenv OSMGA_MESA_ACCEL 0
        setenv OSMGA_MESA_WARP 0
        /tmp/cov $i > scratch-cov-sw/sw-$i-$p.txt
        setenv OSMGA_MESA_ACCEL 1
        setenv OSMGA_MESA_WARP 0
        /tmp/cov $i > scratch-cov-trap/hw-$i-$p.txt
        setenv OSMGA_MESA_ACCEL 1
        setenv OSMGA_MESA_WARP 1
        /tmp/cov $i > scratch-cov-warp/hw-$i-$p.txt
        @ i++
    end
    @ p++
end
set p = 1
while ( $p <= 3 )
    set i = 1
    while ( $i <= 9 )
        cp scratch-cov-sw/sw-$i-$p.txt scratch-cov-trap/sw-$i-$p.txt
        cp scratch-cov-sw/sw-$i-$p.txt scratch-cov-warp/sw-$i-$p.txt
        @ i++
    end
    @ p++
end
echo SWEEPDONE
