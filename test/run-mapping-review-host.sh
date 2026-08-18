#!/bin/sh
# C89 host regression for the R6 offline range-configuration policy.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-mapping-review.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/profile" -I"$project_root/edid" \
   "$project_root/profile/OpenStepMGAProfile.c" \
   "$project_root/profile/OpenStepMGATimingReview.c" \
   "$project_root/profile/OpenStepMGAModeReview.c" \
   "$project_root/profile/OpenStepMGAMappingReview.c" \
   "$project_root/edid/OpenStepMGAEDID.c" \
   "$script_dir/openstep-mga-mapping-review-test.c" \
   -o "$work_dir/openstep-mga-mapping-review-test"
"$work_dir/openstep-mga-mapping-review-test"
echo "OPENSTEP_MGA_R6_MAPPING_REVIEW_HOST_TEST=pass"
