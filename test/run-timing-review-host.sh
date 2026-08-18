#!/bin/sh
# C89 host regression for exact offline timing-shape arithmetic.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-timing-review.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/profile" -I"$project_root/edid" \
   "$project_root/profile/OpenStepMGATimingReview.c" \
   "$script_dir/openstep-mga-timing-review-test.c" \
   -o "$work_dir/openstep-mga-timing-review-test"
"$work_dir/openstep-mga-timing-review-test"
echo "OPENSTEP_MGA_TIMING_REVIEW_HOST_TEST=pass"
