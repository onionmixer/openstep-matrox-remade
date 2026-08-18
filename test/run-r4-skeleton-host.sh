#!/bin/sh
# Host-only checks for the R4 replacement-display staging source.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

sh "$project_root/tools/check-replacement-skeleton.sh"

if command -v csh >/dev/null 2>&1; then
    csh -n "$script_dir/check-replacement-skeleton-imports.csh"
    echo "OPENSTEP_MGA_REPLACEMENT_R4_CSH_SYNTAX=pass"
else
    echo "OPENSTEP_MGA_REPLACEMENT_R4_CSH_SYNTAX=skipped-no-host-csh"
fi

echo "OPENSTEP_MGA_REPLACEMENT_R4_HOST_CHECKS=pass"
