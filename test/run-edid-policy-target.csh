#!/bin/csh -f
#
# Build and run the D0 pure-C EDID parser policy on OPENSTEP i386.
#
# This runner has no DriverKit, LKS, PCI, DDC, display, or MGA dependency.
# It uses only the exact temporary directory named below and removes it on
# either success or failure.
#
# Usage:
#   csh -f run-edid-policy-target.csh /ndrv/openstep-matrox-remade

if ($#argv != 1) then
    echo "usage: $0 <openstep-matrox-remade-source-root>"
    exit 2
endif

set sourceRoot = "$argv[1]"
set edidRoot = "$sourceRoot/edid"
set testSource = "$sourceRoot/test/openstep-mga-edid-policy-test.c"
set probeSource = "$sourceRoot/test/openstep-mga-edid-loader-probe.c"
set tempRoot = /tmp/OSMGAD0
set testBinary = "$tempRoot/test"
set probeBinary = "$tempRoot/probe"
set plainProbeBinary = "$tempRoot/plain"

if (! -f "$edidRoot/OpenStepMGAEDID.c" || \
    ! -f "$edidRoot/OpenStepMGAEDID.h" || ! -f "$testSource" || \
    ! -f "$probeSource") then
    echo "OPENSTEP_MGA_D0_TARGET_INPUT=missing"
    exit 2
endif

sync
sync
/bin/rm -rf "$tempRoot"
mkdir "$tempRoot"
if ($status != 0) then
    echo "OPENSTEP_MGA_D0_TARGET_TEMP=fail"
    exit 1
endif

# P2 target-native clients use the target compiler's default user-program
# convention (`cc -O -Wall`).  D0 first follows that known-good convention;
# prior `-arch i386` and `-m486` variants both reached the same loader error.
cc -O -Wall "$probeSource" -o "$plainProbeBinary"
if ($status != 0) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_D0_TARGET_PLAIN_PROBE_BUILD=fail"
    exit 1
endif
"$plainProbeBinary"
set plainStatus = $status
if ($plainStatus == 0) then
    echo "OPENSTEP_MGA_D0_TARGET_PLAIN_LOADER=pass"
else
    echo "OPENSTEP_MGA_D0_TARGET_PLAIN_LOADER=fail"
endif

cc -O -Wall -DOSMGA_TARGET_NETNAME_BOOTSTRAP -I"$edidRoot" \
    "$edidRoot/OpenStepMGAEDID.c" "$probeSource" \
    -o "$probeBinary"
if ($status != 0) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_D0_TARGET_PROBE_BUILD=fail"
    exit 1
endif
/bin/nm -u "$probeBinary"
echo "OPENSTEP_MGA_D0_TARGET_PROBE_IMPORTS_END"
"$probeBinary"
if ($status != 0) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_D0_TARGET_LOADER=fail"
    exit 1
endif
echo "OPENSTEP_MGA_D0_TARGET_LOADER=pass"

cc -O -Wall -DOSMGA_TARGET_NETNAME_BOOTSTRAP -I"$edidRoot" \
    "$edidRoot/OpenStepMGAEDID.c" "$testSource" \
    -o "$testBinary"
if ($status != 0) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_D0_TARGET_BUILD=fail"
    exit 1
endif
echo "OPENSTEP_MGA_D0_TARGET_BUILD=pass"

/bin/nm -u "$testBinary" | egrep '(_?memset|_?memcpy|_?strcmp)'
set forbiddenImports = $status
if ($forbiddenImports == 0) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_D0_TARGET_IMPORT_GUARD=fail"
    exit 1
endif
if ($forbiddenImports != 1) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_D0_TARGET_IMPORT_GUARD=error"
    exit 1
endif
echo "OPENSTEP_MGA_D0_TARGET_IMPORT_GUARD=pass"

"$testBinary"
set testStatus = $status
/bin/rm -rf "$tempRoot"
if ($testStatus != 0) then
    echo "OPENSTEP_MGA_D0_TARGET_TEST=fail"
    exit 1
endif

echo "OPENSTEP_MGA_D0_TARGET_TEST=pass"
exit 0
