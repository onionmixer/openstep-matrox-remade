#!/bin/csh -f
# Read-only R1 check: a staged replacement artifact must not be in either
# production driver directory.  This does not inspect matching precedence;
# a recovery-only configuration review is still required for G1.

set present = 0

if (-e /private/Drivers/i386/OSMGADisplay.config) then
    echo "OPENSTEP_MGA_R1_TARGET_ARTIFACT=present:/private/Drivers/i386"
    set present = 1
else
    echo "OPENSTEP_MGA_R1_TARGET_ARTIFACT=absent:/private/Drivers/i386"
endif

if (-e /private/Devices/OSMGADisplay.config) then
    echo "OPENSTEP_MGA_R1_TARGET_ARTIFACT=present:/private/Devices"
    set present = 1
else
    echo "OPENSTEP_MGA_R1_TARGET_ARTIFACT=absent:/private/Devices"
endif

if ($present != 0) then
    echo "OPENSTEP_MGA_R1_TARGET_STAGING_STATUS=fail"
    exit 1
endif

echo "OPENSTEP_MGA_R1_TARGET_STAGING_STATUS=pass"
