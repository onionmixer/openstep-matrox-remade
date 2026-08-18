#!/bin/csh -f
# Read-only R5 preflight.  It must never alter driverLoader, kernel-server state,
# target configuration, PCI state, or network daemons.

set failed = 0
set bundle = /usr/Devices/MatroxMGA.config
set comparator = /ndrv/openstep-matrox-remade/test/check-r0-original-driver-fingerprint.csh

echo "OPENSTEP_MGA_R5_PREFLIGHT=begin"
hostname

if (! -e "$bundle/Instance0.table") then
    echo "OPENSTEP_MGA_R5_PREFLIGHT_INSTANCE=missing"
    set failed = 1
else
    echo "OPENSTEP_MGA_R5_PREFLIGHT_INSTANCE=begin"
    grep 'Driver Name' "$bundle/Instance0.table"
    grep 'Default Table' "$bundle/Instance0.table"
    grep 'Display Mode' "$bundle/Instance0.table"
    echo "OPENSTEP_MGA_R5_PREFLIGHT_INSTANCE=end"
endif

echo "OPENSTEP_MGA_R5_PREFLIGHT_OWNER=begin"
/usr/etc/kl_util -s MatroxMGA
if ($status != 0) then
    echo "OPENSTEP_MGA_R5_PREFLIGHT_OWNER_STATUS=fail"
    set failed = 1
else
    echo "OPENSTEP_MGA_R5_PREFLIGHT_OWNER_STATUS=pass"
endif
echo "OPENSTEP_MGA_R5_PREFLIGHT_OWNER=end"

mount | grep /ndrv
if ($status != 0) then
    echo "OPENSTEP_MGA_R5_PREFLIGHT_NFS=missing:/ndrv"
    set failed = 1
else
    echo "OPENSTEP_MGA_R5_PREFLIGHT_NFS=pass:/ndrv"
endif

if (! -e "$comparator") then
    echo "OPENSTEP_MGA_R5_PREFLIGHT_COMPARATOR=missing"
    set failed = 1
else
    csh "$comparator"
    if ($status != 0) then
        echo "OPENSTEP_MGA_R5_PREFLIGHT_COMPARATOR_STATUS=fail"
        set failed = 1
    else
        echo "OPENSTEP_MGA_R5_PREFLIGHT_COMPARATOR_STATUS=pass"
    endif
endif

if ($failed != 0) then
    echo "OPENSTEP_MGA_R5_PREFLIGHT_STATUS=fail"
    exit 1
endif

echo "OPENSTEP_MGA_R5_PREFLIGHT_STATUS=pass"
exit 0
