#!/bin/csh -f
# M12 Stage 1f: the four blend scenes, three arms.
#
# blend-compare.py wants four dumps -- unblended and blended, engine and
# software -- and scores each path against ITS OWN unblended source, so the
# software pair is shared between the two accelerated arms and only the
# engine pair is measured twice.
#
# Explicit switch values, never "unset": the WARP switch is sampled once
# per process, so an inherited environment is an unnamed arm.
cd /ndrv/openstep-matrox-remade
if (! -d scratch-blend) mkdir scratch-blend
foreach s ( blrr blrm blar blam )
    setenv OSMGA_MESA_ACCEL 0
    setenv OSMGA_MESA_WARP 0
    /tmp/tex d ${s}n > scratch-blend/$s-Asw.txt
    /tmp/tex d $s    > scratch-blend/$s-Bsw.txt
    setenv OSMGA_MESA_ACCEL 1
    setenv OSMGA_MESA_WARP 0
    /tmp/tex d ${s}n > scratch-blend/$s-Ahw-trap.txt
    /tmp/tex d $s    > scratch-blend/$s-Bhw-trap.txt
    setenv OSMGA_MESA_ACCEL 1
    setenv OSMGA_MESA_WARP 1
    /tmp/tex d ${s}n > scratch-blend/$s-Ahw-warp.txt
    /tmp/tex d $s    > scratch-blend/$s-Bhw-warp.txt
end
echo BLENDDONE
