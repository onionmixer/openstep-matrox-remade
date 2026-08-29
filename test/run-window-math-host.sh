#!/bin/sh
# C89 host regression for the shared offscreen-window arithmetic.
#
# The same translation units the driver and the inspector both compile, built
# with the strictest settings available, so a change that would only fail on
# the target fails here first.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
source_root="$project_root/OSMGADisplay/OSMGADisplay_reloc.tproj"
edid_root="$project_root/edid"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-window-math.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$source_root" -I"$edid_root" \
   "$source_root/OpenStepMGAWindowMath.c" \
   "$source_root/OpenStepMGAManualConfig.c" \
   "$edid_root/OpenStepMGAEDID.c" \
   "$script_dir/openstep-mga-window-math-test.c" \
   -o "$work_dir/openstep-mga-window-math-test"
"$work_dir/openstep-mga-window-math-test"
echo "OPENSTEP_MGA_WINDOW_MATH_HOST_TEST=pass"
