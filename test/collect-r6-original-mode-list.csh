#!/bin/csh -f
#
# Read-only inventory of the original G450 16 MiB mode-selection list.
# It does not parse a register image because the original .modes format is a
# list of user-visible Display Mode strings, not a hardware mode program.
#
# Usage: csh -f collect-r6-original-mode-list.csh [bundle-root]

if ($#argv == 0) then
    set bundleRoot = /usr/Devices/MatroxMGA.config
else if ($#argv == 1) then
    set bundleRoot = "$argv[1]"
else
    echo "usage: $0 [MatroxMGA.config-root]"
    exit 2
endif

set modeList = "$bundleRoot/MatroxMGAG450_16MB.modes"
set targetMode = 'Height:1200 Width:1600 Refresh:60Hz ColorSpace:RGB:888/32'

if (! -f "$modeList") then
    echo "OPENSTEP_MGA_R6_ORIGINAL_MODE_LIST_INPUT=missing"
    exit 2
endif

set targetCount = `grep "$targetMode" "$modeList" | wc -l`
if ("$targetCount" != "1") then
    echo "OPENSTEP_MGA_R6_ORIGINAL_MODE_LIST_TARGET_COUNT=fail:$targetCount"
    exit 1
endif

echo "OPENSTEP_MGA_R6_ORIGINAL_MODE_LIST_FILE=pass"
echo "OPENSTEP_MGA_R6_ORIGINAL_MODE_LIST_TARGET_COUNT=pass:1"
echo "OPENSTEP_MGA_R6_ORIGINAL_MODE_LIST_FORMAT=display-mode-selection-list"
echo "OPENSTEP_MGA_R6_ORIGINAL_MODE_LIST_STATUS=pass"
exit 0
