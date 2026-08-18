#!/bin/csh -f
# Build/run the offline P-recovery table admission on OPENSTEP i386.

if ($#argv != 1) then
    echo "usage: $0 <openstep-matrox-remade-source-root>"
    exit 2
endif

set sourceRoot = "$argv[1]"
set profileRoot = "$sourceRoot/profile"
set testSource = "$sourceRoot/test/openstep-mga-g450-recovery-config-test.c"
set tempRoot = /tmp/OSMGAG450RecoveryConfig
set testBinary = "$tempRoot/test"

if (! -f "$profileRoot/OpenStepMGAG450RecoveryConfig.c" || \
    ! -f "$profileRoot/OpenStepMGAG450RecoveryConfig.h" || ! -f "$testSource") then
    echo "OPENSTEP_MGA_G450_RECOVERY_CONFIG_TARGET_INPUT=missing"
    exit 2
endif

sync
sync
/bin/rm -rf "$tempRoot"
mkdir "$tempRoot"
if ($status != 0) then
    echo "OPENSTEP_MGA_G450_RECOVERY_CONFIG_TARGET_TEMP=fail"
    exit 1
endif

cc -Wall -I"$profileRoot" "$profileRoot/OpenStepMGAG450RecoveryConfig.c" \
    "$testSource" -o "$testBinary"
if ($status != 0) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_G450_RECOVERY_CONFIG_TARGET_BUILD=fail"
    exit 1
endif
echo "OPENSTEP_MGA_G450_RECOVERY_CONFIG_TARGET_BUILD=pass"

/bin/sh -c "$testBinary"
set testStatus = $status
/bin/rm -rf "$tempRoot"
if ($testStatus != 0) then
    echo "OPENSTEP_MGA_G450_RECOVERY_CONFIG_TARGET_TEST=fail"
    exit 1
endif
echo "OPENSTEP_MGA_G450_RECOVERY_CONFIG_TARGET_TEST=pass"
exit 0
