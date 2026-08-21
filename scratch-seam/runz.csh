#!/bin/csh -f
set D = /ndrv/openstep-matrox-remade/scratch-seam
foreach sh ( A B C D E )
    setenv OSMGA_MESA_ACCEL 1
    /tmp/seam $sh zplane > $D/hw-$sh-zplane.txt
    setenv OSMGA_MESA_ACCEL 0
    /tmp/seam $sh zplane > $D/sw-$sh-zplane.txt
end
unsetenv OSMGA_MESA_ACCEL
echo ZDONE
