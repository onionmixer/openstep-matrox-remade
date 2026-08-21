#!/bin/csh -f
# csh will not take foreach on one line any more than while, so this lives in
# a file -- the same trap that ate a run earlier in this work.
set d = /ndrv/openstep-matrox-remade/scratch-seam
foreach sh ( A B C )
    foreach md ( solo1 solo2 both rev plane )
        unsetenv OSMGA_MESA_ACCEL
        /tmp/seam $sh $md > $d/hw-$sh-$md.txt
        setenv OSMGA_MESA_ACCEL 0
        /tmp/seam $sh $md > $d/sw-$sh-$md.txt
        unsetenv OSMGA_MESA_ACCEL
    end
end
echo SEAMDONE
