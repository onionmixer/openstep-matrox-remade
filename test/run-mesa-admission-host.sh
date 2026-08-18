#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
build_root=${TMPDIR:-/tmp}/openstep-mga-mesa-admission-host

rm -rf "$build_root"
mkdir -p "$build_root"
trap 'rm -rf "$build_root"' EXIT HUP INT TERM

cc -std=c89 -Wall -Wextra -Werror -pedantic \
   -I"$project_root/mesa" -I"$project_root/profile" -I"$project_root/edid" \
   "$project_root/mesa/OpenStepMGAMesaBackend.c" \
   "$project_root/mesa/OpenStepMGAMesaAdmission.c" \
   "$project_root/profile/OpenStepMGAProfile.c" \
   "$project_root/profile/OpenStepMGATimingReview.c" \
   "$project_root/profile/OpenStepMGAModeReview.c" \
   "$project_root/profile/OpenStepMGAMappingReview.c" \
   "$project_root/profile/OpenStepMGARenderBudget.c" \
   "$project_root/edid/OpenStepMGAEDID.c" \
   "$project_root/test/openstep-mga-mesa-admission-test.c" \
   -o "$build_root/test"
"$build_root/test"
