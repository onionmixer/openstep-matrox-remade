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
# The row is an argument so the same alternation serves all of them, and
# the count lets the capacity boundaries be reached without a second mesh.
# Block form, not the one-line "if ( ... ) set x = $argv[2]".  csh expands
# every variable on the line BEFORE it decides whether to run it, so the
# one-liner dies with "Subscript out of range" on the run that has no second
# argument -- which is every row but the capacity ones.
set row = "plain"
set cnt = 0
if ( $#argv >= 1 ) then
    set row = $argv[1]
endif
if ( $#argv >= 2 ) then
    set cnt = $argv[2]
endif
set tag = $row
if ( $cnt != 0 ) then
    set tag = "$row$cnt"
endif
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
    /tmp/tiercost $M 25 4 $row $cnt > scratch-cost/cost-$tag-$first-r$r.txt
    setenv OSMGA_MESA_WARP $second
    /tmp/tiercost $M 25 4 $row $cnt > scratch-cost/cost-$tag-$second-r$r.txt
    @ r++
end
echo COSTDONE
