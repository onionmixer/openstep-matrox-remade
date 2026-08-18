#!/bin/sh
# Verify that the R4 source tree cannot become a production match by itself.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
bundle_root="$project_root/OpenStepMGAReplacementDisplay"
table="$bundle_root/Default.table"
makefile="$bundle_root/Makefile"
source="$bundle_root/OpenStepMGAReplacementDisplay_reloc.tproj/OpenStepMGAReplacementDisplay.m"

for required in "$table" "$makefile" "$source"; do
    if [ ! -f "$required" ]; then
        echo "OPENSTEP_MGA_R1_STAGING_INPUT=missing:$required" >&2
        exit 2
    fi
done

failure=0
if grep -n -E '"(Auto Detect IDs|Display Mode|FB Address|Location)"[[:space:]]*=[[:space:]]*"[^" ]' "$table"; then
    echo "OPENSTEP_MGA_R1_STAGING_REJECT=matching-or-mode-table" >&2
    failure=1
fi
if ! grep -q '^"Memory Maps" = "";$' "$table" || \
   ! grep -q '^"I/O Ports" = "";$' "$table" || \
   ! grep -q '^"IRQ Levels" = "";$' "$table" || \
   ! grep -q '^"DMA Channels" = "";$' "$table"; then
    echo "OPENSTEP_MGA_R1_STAGING_REJECT=resource-table" >&2
    failure=1
fi
if grep -n -E '^[[:space:]]*INSTALLDIR[[:space:]]*=[[:space:]]*/private/(Drivers/i386|Devices)' "$makefile"; then
    echo "OPENSTEP_MGA_R1_STAGING_REJECT=production-install-target" >&2
    failure=1
fi
if ! grep -F -q '+ (BOOL)probe:deviceDescription' "$source" || \
   ! grep -F -q 'return NO;' "$source"; then
    echo "OPENSTEP_MGA_R1_STAGING_REJECT=probe-not-closed" >&2
    failure=1
fi

if [ "$failure" -ne 0 ]; then
    echo "OPENSTEP_MGA_R1_STAGING_STATUS=fail" >&2
    exit 1
fi
echo "OPENSTEP_MGA_R1_STAGING_STATUS=pass"
