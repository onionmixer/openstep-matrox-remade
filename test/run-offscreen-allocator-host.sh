#!/bin/sh
# C89 host regression for opaque offscreen surface ledger.

set -eu
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-offscreen-allocator.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/profile" -I"$project_root/edid" -I"$project_root/protocol" \
   "$project_root/protocol/OpenStepMGACommand.c" \
   "$project_root/protocol/OpenStepMGAOffscreenAllocator.c" \
   "$script_dir/openstep-mga-offscreen-allocator-test.c" \
   -o "$work_dir/openstep-mga-offscreen-allocator-test"
"$work_dir/openstep-mga-offscreen-allocator-test"
echo "OPENSTEP_MGA_OFFSCREEN_ALLOCATOR_HOST_TEST=pass"
