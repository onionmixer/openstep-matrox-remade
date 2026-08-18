#!/bin/sh
# C89 host regression for the R2 physical-evidence admission policy.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-profile.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/profile" \
   "$project_root/profile/OpenStepMGAProfile.c" \
   "$script_dir/openstep-mga-profile-test.c" \
   -o "$work_dir/openstep-mga-profile-test"
"$work_dir/openstep-mga-profile-test"
echo "OPENSTEP_MGA_PROFILE_HOST_TEST=pass"
