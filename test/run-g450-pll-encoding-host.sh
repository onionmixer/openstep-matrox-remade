#!/bin/sh
# Strict C89 host regression for the reviewed G450 PLL byte image.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-g450-pll-encoding.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/profile" -I"$project_root/edid" \
   "$project_root/profile/OpenStepMGAG450PLLEncoding.c" \
   "$script_dir/openstep-mga-g450-pll-encoding-test.c" \
   -o "$work_dir/openstep-mga-g450-pll-encoding-test"
"$work_dir/openstep-mga-g450-pll-encoding-test"
echo "OPENSTEP_MGA_G450_PLL_ENCODING_HOST_TEST=pass"
