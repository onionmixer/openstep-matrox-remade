#!/bin/sh
# Strict C89 host regression for the G450 legacy range publication plan.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-g450-range-plan.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/profile" -I"$project_root/edid" \
   "$project_root/profile/OpenStepMGAProfile.c" \
   "$project_root/profile/OpenStepMGATimingReview.c" \
   "$project_root/profile/OpenStepMGAModeReview.c" \
   "$project_root/profile/OpenStepMGAMappingReview.c" \
   "$project_root/profile/OpenStepMGAG450RangePlan.c" \
   "$project_root/edid/OpenStepMGAEDID.c" \
   "$script_dir/openstep-mga-g450-range-plan-test.c" \
   -o "$work_dir/openstep-mga-g450-range-plan-test"
"$work_dir/openstep-mga-g450-range-plan-test"
echo "OPENSTEP_MGA_G450_RANGE_PLAN_HOST_TEST=pass"
