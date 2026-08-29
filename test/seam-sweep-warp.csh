#!/bin/csh -f
# The seam sweep, three arms.
#
# scratch-seam/run.csh runs software against the accelerated path.  With a
# second accelerated tier there are three, and the trapezoid arm is
# re-measured from the SAME library rather than trusted from the record --
# otherwise a WARP result is compared against a baseline that has also
# moved.
#
# Explicit values, never "unset": the WARP switch is sampled once per
# process and cached, so an inherited environment is an unnamed arm.
#
#   csh -f test/seam-sweep-warp.csh
#
# Leaves scratch-seam-trap/ and scratch-seam-warp/, each in the layout
# seam-compare.py reads, with the shared software arm copied into both.
cd /ndrv/openstep-matrox-remade
foreach d ( scratch-seam-trap scratch-seam-warp )
    if (! -d $d) mkdir $d
end
foreach sh ( A B C D E )
    foreach md ( solo1 solo2 solo3 solo4 both rev plane zplane )
        setenv OSMGA_MESA_ACCEL 0
        setenv OSMGA_MESA_WARP 0
        /tmp/seam $sh $md > scratch-seam-trap/sw-$sh-$md.txt
        cp scratch-seam-trap/sw-$sh-$md.txt scratch-seam-warp/sw-$sh-$md.txt
        setenv OSMGA_MESA_ACCEL 1
        setenv OSMGA_MESA_WARP 0
        /tmp/seam $sh $md > scratch-seam-trap/hw-$sh-$md.txt
        setenv OSMGA_MESA_ACCEL 1
        setenv OSMGA_MESA_WARP 1
        /tmp/seam $sh $md > scratch-seam-warp/hw-$sh-$md.txt
    end
end
echo SEAMDONE
