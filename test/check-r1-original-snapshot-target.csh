#!/bin/csh -f
# Read-only verifier for the presently active P-original configuration record.
# It only reads the current instance table and production-artifact presence.

set instance = /private/Drivers/i386/MatroxMGA.config/Instance0.table
set replacement = /private/Drivers/i386/OpenStepMGAReplacementDisplay.config
set failure = 0

if (! -f "$instance") then
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_INPUT=missing-instance"
    exit 2
endif

egrep '^"Default Table" = "MatroxMGAG400_16MB";' "$instance" > /dev/null
if ($status == 0) then
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=pass:default-table"
else
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=fail:default-table"
    set failure = 1
endif

egrep '^"Driver Name" = "MatroxMGA";' "$instance" > /dev/null
if ($status == 0) then
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=pass:driver-name"
else
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=fail:driver-name"
    set failure = 1
endif

egrep '^"Auto Detect IDs" = "0x0525102B";' "$instance" > /dev/null
if ($status == 0) then
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=pass:device-id"
else
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=fail:device-id"
    set failure = 1
endif

egrep '^"Location" = "Dev:0 Func:0 Bus:4";' "$instance" > /dev/null
if ($status == 0) then
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=pass:location"
else
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=fail:location"
    set failure = 1
endif

egrep '^"Display Mode" = "Height: 1200 Width: 1600 Refresh: 60Hz ColorSpace: RGB:888/32";' "$instance" > /dev/null
if ($status == 0) then
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=pass:display-mode"
else
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=fail:display-mode"
    set failure = 1
endif

if (-e "$replacement") then
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_REPLACEMENT=fail:production-present"
    set failure = 1
else
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_REPLACEMENT=pass:production-absent"
endif

if ($failure != 0) then
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_STATUS=fail"
    exit 1
endif
echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_STATUS=pass"
exit 0
