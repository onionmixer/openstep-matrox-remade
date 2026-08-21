#!/bin/csh -f
set D = /ndrv/openstep-matrox-remade/scratch-seam
setenv OSMGA_MESA_ACCEL 1
foreach sh ( A B C D E )
    foreach md ( allsoft mix mixrev mixz )
        /tmp/seam $sh $md > $D/mx-$sh-$md.txt
    end
end
foreach md ( solo1 solo2 both over overrev allsoft )
    /tmp/seam F $md > $D/mx-F-$md.txt
end
unsetenv OSMGA_MESA_ACCEL
echo MIXDONE
