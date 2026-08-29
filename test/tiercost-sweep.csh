#!/bin/csh -f
# M15 Phase 1: the enqueue cost, both arms, alternating so that ordering
# cannot favour one of them.
#
# Four independent runs per arm in AB BA AB BA order.  Per-run samples are
# kept rather than only the summary, because a single median hides a run
# that behaved differently from the other three.
cd /ndrv/openstep-matrox-remade
if (! -d scratch-cost) mkdir scratch-cost
set M = /ndrv/openstep-matrox-remade/scratch-mesh/mesh16.txt
setenv OSMGA_MESA_ACCEL 1
set r = 1
while ( $r <= 4 )
    if ( $r == 1 || $r == 3 ) then
        set first = 0
        set second = 1
    else
        set first = 1
        set second = 0
    endif
    setenv OSMGA_MESA_WARP $first
    /tmp/tiercost $M 25 4 > scratch-cost/cost-$first-r$r.txt
    setenv OSMGA_MESA_WARP $second
    /tmp/tiercost $M 25 4 > scratch-cost/cost-$second-r$r.txt
    @ r++
end
echo COSTDONE
