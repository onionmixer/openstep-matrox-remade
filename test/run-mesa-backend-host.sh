#!/bin/sh
# C89 host regression for the fixed-target Mesa backend selector.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-mesa-backend.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/mesa" \
   "$project_root/mesa/OpenStepMGAMesaBackend.c" \
   "$script_dir/openstep-mga-mesa-backend-test.c" \
   -o "$work_dir/openstep-mga-mesa-backend-test"
"$work_dir/openstep-mga-mesa-backend-test"
echo "OPENSTEP_MGA_MESA_BACKEND_HOST_TEST=pass"
