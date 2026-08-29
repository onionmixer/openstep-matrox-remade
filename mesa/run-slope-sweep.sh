#!/bin/sh
# What a wider slope budget would refuse, measured rather than argued.
# Builds the harness once per budget and reports the per-band tallies.
cd /ndrv/openstep-matrox-remade/mesa || exit 1
for b in 8388608 16777216 67108864 268435456 536870912; do
    echo "########## OSMGA_HW3D_TEX_SLOPE_ROOM = $b"
    cc -O -m486 -DOSMGA_HW3D_TESTSITE -DOSMGA_HW3D_TEX_SLOPE_ROOM=${b}L \
       -I../hw3d -o /tmp/tcs test-mesa-texcoord.c OpenStepMGAMesaTriangle.c \
       ../hw3d/OpenStepMGAHW3D.c || { echo "  (cc failed)"; continue; }
    /tmp/tcs > /tmp/tcs.out || echo "  (run failed)"
    grep affine /tmp/tcs.out
    grep qbud /tmp/tcs.out
done
