#!/bin/csh -f
# Read-only target check for the review-only P-recovery template.

if ($#argv != 1) then
    echo "usage: $0 <openstep-matrox-remade-source-root>"
    exit 2
endif

set template = "$argv[1]/recovery/OpenStepMGAG450Recovery.table"
set failure = 0

if (! -f "$template") then
    echo "OPENSTEP_MGA_G450_RECOVERY_TEMPLATE_TARGET_INPUT=missing"
    exit 2
endif

egrep '^"Driver Name" = "OSMGADisplay";' "$template" > /dev/null
if ($status != 0) set failure = 1
egrep '^"Location" = "Dev:0 Func:0 Bus:4";' "$template" > /dev/null
if ($status != 0) set failure = 1
egrep '^"Auto Detect IDs" = "0x0525102B";' "$template" > /dev/null
if ($status != 0) set failure = 1
egrep '^"Display Mode" = "Height: 1200 Width: 1600 Refresh: 60Hz ColorSpace: RGB:888/32";' "$template" > /dev/null
if ($status != 0) set failure = 1
egrep '^"MGA Memory Size" = "16";' "$template" > /dev/null
if ($status != 0) set failure = 1
egrep '^"Recovery Profile" = "P-recovery";' "$template" > /dev/null
if ($status != 0) set failure = 1

if ($failure != 0) then
    echo "OPENSTEP_MGA_G450_RECOVERY_TEMPLATE_TARGET_STATUS=fail"
    exit 1
endif
echo "OPENSTEP_MGA_G450_RECOVERY_TEMPLATE_TARGET_STATUS=pass"
exit 0
