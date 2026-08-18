#!/bin/sh
# C89 host regression for the data-only primary CRTC snapshot comparison.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-g450-primary-crtc-readback.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror -I"$project_root/profile" \
   -I"$project_root/edid" \
   "$project_root/profile/OpenStepMGAG450PrimaryCRTCReadback.c" \
   "$script_dir/openstep-mga-g450-primary-crtc-readback-test.c" \
   -o "$work_dir/openstep-mga-g450-primary-crtc-readback-test"
"$work_dir/openstep-mga-g450-primary-crtc-readback-test"
echo "OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_HOST_TEST=pass"
