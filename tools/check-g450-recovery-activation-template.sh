#!/bin/sh
# Validate the review-only P-recovery configuration template.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
template="$project_root/recovery/OpenStepMGAG450Recovery.table"
staging_builder="$project_root/packaging/openstep/build-recovery-staging-package.csh"

if [ ! -f "$template" ] || [ ! -f "$staging_builder" ]; then
    echo "OPENSTEP_MGA_G450_RECOVERY_TEMPLATE_INPUT=missing" >&2
    exit 2
fi

for entry in \
    '"Location" = "Dev:0 Func:0 Bus:4";' \
    '"Auto Detect IDs" = "0x0525102B";' \
    '"Display Mode" = "Height: 1200 Width: 1600 Refresh: 60Hz ColorSpace: RGB:888/32";' \
    '"MGA Memory Size" = "16";' \
    '"Recovery Profile" = "P-recovery";'; do
    if ! grep -Fqx "$entry" "$template"; then
        echo "OPENSTEP_MGA_G450_RECOVERY_TEMPLATE_STATUS=fail:missing-entry" >&2
        exit 1
    fi
done

if grep -Fq 'OpenStepMGAG450Recovery.table' "$staging_builder"; then
    echo "OPENSTEP_MGA_G450_RECOVERY_TEMPLATE_STATUS=fail:staging-package-leak" >&2
    exit 1
fi

echo "OPENSTEP_MGA_G450_RECOVERY_TEMPLATE_STATUS=pass"
