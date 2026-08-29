#!/bin/csh -f
# M13 Z2: six modes, WARP; and the axis modes on the other two arms as flat
# controls.  One process per (mode, arm) -- the WARP switch is sampled once
# per process and the t order is shuffled inside it.
cd /ndrv/openstep-matrox-remade
if (! -d scratch-z2) mkdir scratch-z2
setenv OSMGA_MESA_ACCEL 1
setenv OSMGA_MESA_WARP 1
foreach m ( x y d xn xb xw )
    /tmp/z2 $m 1 > scratch-z2/z2-$m-warp-1.txt
end
setenv OSMGA_MESA_WARP 0
foreach m ( x y d )
    /tmp/z2 $m 1 > scratch-z2/z2-$m-trap-1.txt
end
setenv OSMGA_MESA_ACCEL 0
foreach m ( x y )
    /tmp/z2 $m 1 > scratch-z2/z2-$m-soft-1.txt
end
echo Z2DONE
