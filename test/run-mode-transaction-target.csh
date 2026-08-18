#!/bin/csh -f
#
# Build and run the R6 one-mode transaction policy as an ordinary OPENSTEP
# i386 process.  No driver bundle is built, installed, loaded, or contacted.
#
# Usage: csh -f run-mode-transaction-target.csh /ndrv/openstep-matrox-remade

if ($#argv != 1) then
    echo "usage: $0 <openstep-matrox-remade-source-root>"
    exit 2
endif

set sourceRoot = "$argv[1]"
set profileRoot = "$sourceRoot/profile"
set protocolRoot = "$sourceRoot/protocol"
set edidRoot = "$sourceRoot/edid"
set testSource = "$sourceRoot/test/openstep-mga-mode-transaction-test.c"
set tempRoot = /tmp/OSMGAModeTransaction
set testBinary = "$tempRoot/test"

if (! -f "$profileRoot/OpenStepMGAProfile.c" || \
    ! -f "$profileRoot/OpenStepMGATimingReview.c" || \
    ! -f "$profileRoot/OpenStepMGAModeReview.c" || \
    ! -f "$profileRoot/OpenStepMGAMappingReview.c" || \
    ! -f "$profileRoot/OpenStepMGARecoveryMatrix.c" || \
    ! -f "$profileRoot/OpenStepMGAG450CRTCPlan.c" || \
    ! -f "$profileRoot/OpenStepMGAG450PrimaryCRTCImage.c" || \
    ! -f "$profileRoot/OpenStepMGAG450PrimaryCRTCReadback.c" || \
    ! -f "$profileRoot/OpenStepMGAG450PLL.c" || \
    ! -f "$profileRoot/OpenStepMGAG450PLLEncoding.c" || \
    ! -f "$profileRoot/OpenStepMGAG450RangePlan.c" || \
    ! -f "$protocolRoot/OpenStepMGABoundedPoll.c" || \
    ! -f "$protocolRoot/OpenStepMGAModeTransaction.c" || \
    ! -f "$edidRoot/OpenStepMGAEDID.c" || ! -f "$testSource") then
    echo "OPENSTEP_MGA_MODE_TRANSACTION_TARGET_INPUT=missing"
    exit 2
endif

sync
sync
/bin/rm -rf "$tempRoot"
mkdir "$tempRoot"
if ($status != 0) then
    echo "OPENSTEP_MGA_MODE_TRANSACTION_TARGET_TEMP=fail"
    exit 1
endif

cc -Wall -I"$profileRoot" -I"$protocolRoot" -I"$edidRoot" \
    "$profileRoot/OpenStepMGAProfile.c" \
    "$profileRoot/OpenStepMGATimingReview.c" \
    "$profileRoot/OpenStepMGAModeReview.c" \
    "$profileRoot/OpenStepMGAMappingReview.c" \
    "$profileRoot/OpenStepMGARecoveryMatrix.c" \
    "$profileRoot/OpenStepMGAG450CRTCPlan.c" \
    "$profileRoot/OpenStepMGAG450PrimaryCRTCImage.c" \
    "$profileRoot/OpenStepMGAG450PrimaryCRTCReadback.c" \
    "$profileRoot/OpenStepMGAG450PLL.c" \
    "$profileRoot/OpenStepMGAG450PLLEncoding.c" \
    "$profileRoot/OpenStepMGAG450RangePlan.c" \
    "$protocolRoot/OpenStepMGABoundedPoll.c" \
    "$protocolRoot/OpenStepMGAModeTransaction.c" \
    "$edidRoot/OpenStepMGAEDID.c" "$testSource" -o "$testBinary"
if ($status != 0) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_MODE_TRANSACTION_TARGET_BUILD=fail"
    exit 1
endif
echo "OPENSTEP_MGA_MODE_TRANSACTION_TARGET_BUILD=pass"

# A fresh sh avoids old csh's stale executable-path lookup after compilation.
/bin/sh -c "$testBinary"
set testStatus = $status
/bin/rm -rf "$tempRoot"
if ($testStatus != 0) then
    echo "OPENSTEP_MGA_MODE_TRANSACTION_TARGET_TEST=fail"
    exit 1
endif
echo "OPENSTEP_MGA_MODE_TRANSACTION_TARGET_TEST=pass"
exit 0
