#!/bin/csh -f
# csh will not take while/end separated by semicolons on one line, so the
# sweep lives in a file.  Modes alternate inside each shape and the whole
# thing repeats, because one pair of processes is thin evidence.
cd /ndrv/openstep-matrox-remade
set p = 1
while ( $p <= 3 )
    set i = 1
    while ( $i <= 7 )
        unsetenv OSMGA_MESA_ACCEL
        /tmp/cov $i > scratch-cov/hw-$i-$p.txt
        setenv OSMGA_MESA_ACCEL 0
        /tmp/cov $i > scratch-cov/sw-$i-$p.txt
        unsetenv OSMGA_MESA_ACCEL
        @ i++
    end
    @ p++
end
echo SWEEPDONE
