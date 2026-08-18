#!/bin/sh
# C89 no-hardware integration harness for fallback dispatch and presentation.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-mesa-presentation.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/mesa" -I"$project_root/reference" \
   "$project_root/mesa/OpenStepMGAMesaBackend.c" \
   "$project_root/reference/OpenStepMGAReference.c" \
   "$script_dir/openstep-mga-mesa-presentation-harness-test.c" \
   -o "$work_dir/openstep-mga-mesa-presentation-harness-test"
"$work_dir/openstep-mga-mesa-presentation-harness-test"
echo "OPENSTEP_MGA_MESA_PRESENTATION_HARNESS_HOST_TEST=pass"
