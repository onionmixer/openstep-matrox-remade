#!/bin/sh
# C89 host regression for the R6 one-mode transaction policy.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-mode-transaction.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/profile" -I"$project_root/edid" -I"$project_root/protocol" \
   "$project_root/profile/OpenStepMGAProfile.c" \
   "$project_root/profile/OpenStepMGATimingReview.c" \
   "$project_root/profile/OpenStepMGAModeReview.c" \
   "$project_root/profile/OpenStepMGAMappingReview.c" \
   "$project_root/profile/OpenStepMGARecoveryMatrix.c" \
   "$project_root/profile/OpenStepMGAG450CRTCPlan.c" \
   "$project_root/profile/OpenStepMGAG450PrimaryCRTCImage.c" \
   "$project_root/profile/OpenStepMGAG450PrimaryCRTCReadback.c" \
   "$project_root/profile/OpenStepMGAG450PLL.c" \
   "$project_root/profile/OpenStepMGAG450PLLEncoding.c" \
   "$project_root/profile/OpenStepMGAG450RangePlan.c" \
   "$project_root/protocol/OpenStepMGABoundedPoll.c" \
   "$project_root/protocol/OpenStepMGAModeTransaction.c" \
   "$project_root/edid/OpenStepMGAEDID.c" \
   "$script_dir/openstep-mga-mode-transaction-test.c" \
   -o "$work_dir/openstep-mga-mode-transaction-test"
"$work_dir/openstep-mga-mode-transaction-test"
echo "OPENSTEP_MGA_MODE_TRANSACTION_HOST_TEST=pass"
