#!/bin/csh -f
#
# Rebuild the P2 service from NFS source in an exact temporary directory, then
# run the control-plane regression.  This script does not install under
# /private/Devices and has no MGA hardware access.
#
# Usage:
#   csh -f run-p2-clean-regression.csh /ndrv/openstep-matrox-remade

if ($#argv != 1) then
    echo "usage: $0 <openstep-matrox-remade-source-root>"
    exit 2
endif

set sourceRoot = "$argv[1]"
set sourceService = "$sourceRoot/OpenStepMGAService"
set testRunner = "$sourceRoot/test/run-p2-control-regression.csh"
set tempRoot = /tmp/OpenStepMGAService-P29
set serviceRoot = "$tempRoot/OpenStepMGAService"

if (! -d "$sourceService" || ! -f "$testRunner") then
    echo "OPENSTEP_MGA_P29_CLEAN_INPUT=missing"
    exit 2
endif

sync
sync
/bin/rm -rf "$tempRoot"
mkdir "$tempRoot"
if ($status != 0) then
    echo "OPENSTEP_MGA_P29_CLEAN_TEMP=fail"
    exit 1
endif

cp -r "$sourceService" "$tempRoot"
if ($status != 0) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_P29_CLEAN_COPY=fail"
    exit 1
endif
sync
sync

cd "$serviceRoot"
make clean
if ($status != 0) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_P29_CLEAN_BUILD=clean-fail"
    exit 1
endif
make
if ($status != 0) then
    /bin/rm -rf "$tempRoot"
    echo "OPENSTEP_MGA_P29_CLEAN_BUILD=make-fail"
    exit 1
endif
echo "OPENSTEP_MGA_P29_CLEAN_BUILD=pass"

csh -f "$testRunner" "$serviceRoot"
set runnerStatus = $status

/bin/rm -rf "$tempRoot"
if ($runnerStatus != 0) then
    echo "OPENSTEP_MGA_P29_CLEAN_SUITE=fail"
    exit 1
endif

echo "OPENSTEP_MGA_P29_CLEAN_SUITE=pass"
exit 0
