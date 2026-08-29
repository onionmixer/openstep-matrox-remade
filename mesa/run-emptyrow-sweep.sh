#!/bin/sh
# Does holding the empty rows to the same range cost anything, and does it
# buy the wider slope budget its safety?  Budget crossed with the check.
cd /ndrv/openstep-matrox-remade/mesa || exit 1
for b in 8388608 16777216 67108864 536870912; do
    for e in off on; do
        if [ "$e" = on ]; then E=-DOSMGA_HW3D_TEX_CHECK_EMPTY_ROWS; else E=-UNOTHING; fi
        echo "########## budget $b  emptyrow $e"
        cc -O -m486 -DOSMGA_HW3D_TEX_SLOPE_ROOM=${b}L $E \
           -I../hw3d -o /tmp/tce test-mesa-texcoord.c OpenStepMGAMesaTriangle.c \
           ../hw3d/OpenStepMGAHW3D.c || { echo "  (cc failed)"; continue; }
        /tmp/tce > /tmp/tce.out || echo "  (run failed)"
        grep qbud /tmp/tce.out
    done
done
