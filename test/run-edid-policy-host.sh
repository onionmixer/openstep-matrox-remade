#!/bin/sh
# Compile the pure-C D0 parser/policy with C89 constraints on the host.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/openstep-mga-edid.XXXXXX")
trap 'rm -rf "$work_dir"' EXIT HUP INT TERM

cc -std=c89 -pedantic -Wall -Wextra -Werror \
   "$script_dir/openstep-mga-edid-loader-probe.c" \
   -o "$work_dir/openstep-mga-edid-plain-probe"
"$work_dir/openstep-mga-edid-plain-probe"
echo "OPENSTEP_MGA_D0_HOST_PLAIN_LOADER=pass"
cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/edid" \
   "$project_root/edid/OpenStepMGAEDID.c" \
   "$script_dir/openstep-mga-edid-policy-test.c" \
   -o "$work_dir/openstep-mga-edid-policy-test"
cc -std=c89 -pedantic -Wall -Wextra -Werror \
   -I"$project_root/edid" \
   "$project_root/edid/OpenStepMGAEDID.c" \
   "$script_dir/openstep-mga-edid-loader-probe.c" \
   -o "$work_dir/openstep-mga-edid-loader-probe"
"$work_dir/openstep-mga-edid-loader-probe"
echo "OPENSTEP_MGA_D0_HOST_LINKED_LOADER=pass"
if nm -u "$work_dir/openstep-mga-edid-policy-test" | \
   grep -E '(_?memset|_?memcpy|_?strcmp)'; then
    echo "OPENSTEP_MGA_EDID_HOST_IMPORT_GUARD=fail" >&2
    exit 1
fi
echo "OPENSTEP_MGA_EDID_HOST_IMPORT_GUARD=pass"
"$work_dir/openstep-mga-edid-policy-test"
