#!/bin/sh
# Verify arithmetic for the observed target Display Mode without target access.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-current-mode.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/edid" \
   "$project_root/edid/OpenStepMGAEDID.c" \
   "$script_dir/openstep-mga-current-mode-footprint.c" \
   -o "$work_dir/openstep-mga-current-mode-footprint"
"$work_dir/openstep-mga-current-mode-footprint"
echo "OPENSTEP_MGA_CURRENT_MODE_HOST_TEST=pass"
