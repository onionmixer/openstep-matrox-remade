#!/bin/csh -f
set D = /ndrv/openstep-matrox-remade/scratch-seam
setenv OSMGA_MESA_ACCEL 1
foreach sh ( A B C D E )
    foreach n ( 1 2 3 4 )
        /tmp/seam $sh ssolo$n > $D/mx-$sh-ssolo$n.txt
        /tmp/seam $sh hsolo$n > $D/mx-$sh-hsolo$n.txt
    end
end
unsetenv OSMGA_MESA_ACCEL
echo SOLODONE
