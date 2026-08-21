#!/bin/csh -f
set D = /ndrv/openstep-matrox-remade/scratch-mesh
setenv OSMGA_MESA_ACCEL 1
foreach m ( mesh mesh16 mesh12 )
    foreach md ( hw sw mix )
        /tmp/mesh $D/$m.txt $md > $D/$m-$md.txt
    end
end
unsetenv OSMGA_MESA_ACCEL
echo MESHDONE
