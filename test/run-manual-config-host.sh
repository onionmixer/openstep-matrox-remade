#!/bin/sh
# C89 host regression for fail-closed manual MGA Memory Size parsing.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
source_root="$project_root/OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay_reloc.tproj"
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-manual-config.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$source_root" \
   "$source_root/OpenStepMGAManualConfig.c" \
   "$script_dir/openstep-mga-manual-config-test.c" \
   -o "$work_dir/openstep-mga-manual-config-test"
"$work_dir/openstep-mga-manual-config-test"
echo "OPENSTEP_MGA_MANUAL_CONFIG_HOST_TEST=pass"
