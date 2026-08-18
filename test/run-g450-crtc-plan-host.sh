#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
build_root=${TMPDIR:-/tmp}/openstep-mga-g450-crtc-plan-host

rm -rf "$build_root"
mkdir -p "$build_root"
trap 'rm -rf "$build_root"' EXIT HUP INT TERM

cc -std=c89 -Wall -Wextra -Werror -pedantic \
   -I"$project_root/profile" -I"$project_root/edid" \
   "$project_root/profile/OpenStepMGAProfile.c" \
   "$project_root/profile/OpenStepMGATimingReview.c" \
   "$project_root/profile/OpenStepMGAModeReview.c" \
   "$project_root/profile/OpenStepMGAG450CRTCPlan.c" \
   "$project_root/edid/OpenStepMGAEDID.c" \
   "$script_dir/openstep-mga-g450-crtc-plan-test.c" \
   -o "$build_root/test"
"$build_root/test"
