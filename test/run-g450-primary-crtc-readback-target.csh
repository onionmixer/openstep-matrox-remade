#!/bin/csh -f
# Build/run the data-only primary CRTC snapshot comparator on OPENSTEP i386.

if ($#argv != 1) then
    echo "usage: $0 <openstep-matrox-remade-source-root>"
    exit 2
endif

set sourceRoot = "$argv[1]"
set profileRoot = "$sourceRoot/profile"
set edidRoot = "$sourceRoot/edid"
set testSource = "$sourceRoot/test/openstep-mga-g450-primary-crtc-readback-test.c"
set tempRoot = /tmp/OSMGAPrimaryCRTCReadback
set testBinary = "$tempRoot/test"

if (! -f "$profileRoot/OpenStepMGAG450PrimaryCRTCReadback.c" || \
    ! -f "$profileRoot/OpenStepMGAG450PrimaryCRTCReadback.h" || \
    ! -f "$testSource") then
    echo "OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_TARGET_INPUT=missing"
    exit 2
endif

sync
sync
/bin/rm -rf "$tempRoot"
mkdir "$tempRoot"
if ($status != 0) then
    echo "OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_TARGET_TEMP=fail"
    exit 1
endif

cc -Wall -I"$profileRoot" -I"$edidRoot" \
    "$profileRoot/OpenStepMGAG450PrimaryCRTCReadback.c" \
    "$testSource" -o "$testBinary"
if ($status != 0) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_TARGET_BUILD=fail"
    exit 1
endif
echo "OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_TARGET_BUILD=pass"

# A fresh sh avoids old csh's stale executable-path lookup after compilation.
/bin/sh -c "$testBinary"
set testStatus = $status
/bin/rm -rf "$tempRoot"
if ($testStatus != 0) then
    echo "OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_TARGET_TEST=fail"
    exit 1
endif
echo "OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_TARGET_TEST=pass"
exit 0
