#!/bin/sh
# C89 host regression for the reviewed G450 P-recovery table values.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-g450-recovery-config.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror -I"$project_root/profile" \
   "$project_root/profile/OpenStepMGAG450RecoveryConfig.c" \
   "$script_dir/openstep-mga-g450-recovery-config-test.c" \
   -o "$work_dir/openstep-mga-g450-recovery-config-test"
"$work_dir/openstep-mga-g450-recovery-config-test"
echo "OPENSTEP_MGA_G450_RECOVERY_CONFIG_HOST_TEST=pass"
