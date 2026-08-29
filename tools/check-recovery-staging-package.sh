#!/bin/sh
# Keep the recovery review package inactive until an explicit G1 cutover run.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
package_root="$project_root/packaging/openstep"
table_file="$project_root/OSMGADisplay/Default.table"

for required in \
    "$package_root/OpenStepMGARecoveryStaging.info" \
    "$package_root/OpenStepMGARecoveryStaging.pre_install" \
    "$package_root/build-recovery-staging-package.csh" \
    "$package_root/verify-recovery-staging-package.csh" \
    "$package_root/installer-architecture-marker.c" \
    "$table_file"; do
    if [ ! -f "$required" ]; then
        echo "OPENSTEP_MGA_RECOVERY_STAGE_STATIC_GUARD_INPUT=missing:$required" >&2
        exit 2
    fi
done

if ! grep -qx 'DefaultLocation /LocalDeveloper' \
        "$package_root/OpenStepMGARecoveryStaging.info" || \
   ! grep -Fq 'DriverStaging/OSMGADisplay.config' \
        "$package_root/verify-recovery-staging-package.csh" || \
   ! grep -Fq 'must not install below /private' \
        "$package_root/OpenStepMGARecoveryStaging.pre_install"; then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_STATIC_GUARD_STATUS=fail:staging-contract" >&2
    exit 1
fi

if grep -n -E '"(Auto Detect IDs|Display Mode|FB Address)"[[:space:]]*=' \
        "$table_file"; then
    echo "OPENSTEP_MGA_RECOVERY_STAGE_STATIC_GUARD_STATUS=fail:matching-table" >&2
    exit 1
fi

echo "OPENSTEP_MGA_RECOVERY_STAGE_STATIC_GUARD_STATUS=pass"
