#!/bin/csh -f
set D = /ndrv/openstep-matrox-remade/scratch-seam
setenv OSMGA_MESA_ACCEL 1
foreach sh ( A B C D E )
    /tmp/seam $sh mixz > $D/mx-$sh-mixz.txt
end
unsetenv OSMGA_MESA_ACCEL
echo MIXZDONE
