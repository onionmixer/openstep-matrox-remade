#!/bin/sh
# Keep the P-original snapshot verifier strictly read-only.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
collector="$script_dir/../test/check-r1-original-snapshot-target.csh"

if [ ! -f "$collector" ]; then
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_STATIC_GUARD_INPUT=missing" >&2
    exit 2
fi
if grep -n -E '(driverLoader|Configure|kl_util[[:space:]]+-u|[[:space:]]cp[[:space:]]|[[:space:]]mv[[:space:]]|[[:space:]]rm[[:space:]]|Installer|/private/Devices/.*OpenStepMGAReplacementDisplay)' \
        "$collector"; then
    echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_STATIC_GUARD_STATUS=fail" >&2
    exit 1
fi
echo "OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_STATIC_GUARD_STATUS=pass"
