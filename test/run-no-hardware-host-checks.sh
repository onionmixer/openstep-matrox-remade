#!/bin/sh
# Run every host-only regression that must remain independent of target VGA.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

sh "$project_root/tools/check-p2-no-hardware.sh"
#
# Three guards were removed from this runner on 2026-08-29, all for one
# reason: they enforced the R1/R4 STAGING contract, under which the driver
# was deliberately fail-closed and must not be able to claim a card.  The
# driver shipped in v1.0 and claiming the card is its purpose, so each of
# them asserted the opposite of the product.
#
#   check-r1-staging-isolation.sh   "cannot become a production match"
#   check-replacement-skeleton.sh   no PCI binding, no port I/O, no display
#                                   programming, empty memory size, return NO
#   run-r4-skeleton-host.sh         the wrapper that ran the second one
#
# check-recovery-staging-package.sh was NOT removed: its subject -- the
# recovery package staging under /LocalDeveloper rather than /private -- is
# still true, so only its matching-table clause went.
#
# check-p1-config-readonly.sh was here and is retired.  It asserted that the
# P1 probe stays config-header read-only, and the probe has mapped MMIO since
# the initial commit -- so the guard NEVER PASSED, and `set -e` stopped this
# runner on it.  Every check below this line had therefore never run.
#
sh "$project_root/tools/check-d0-no-hardware.sh"
sh "$project_root/tools/check-profile-no-hardware.sh"
sh "$project_root/tools/check-mode-review-no-hardware.sh"
sh "$project_root/tools/check-timing-review-no-hardware.sh"
sh "$project_root/tools/check-render-budget-no-hardware.sh"
sh "$project_root/tools/check-mesa-backend-no-hardware.sh"
sh "$project_root/tools/check-mesa-admission-no-hardware.sh"
sh "$project_root/tools/check-osmesa-fallback-no-hardware.sh"
sh "$project_root/tools/check-mapping-review-no-hardware.sh"
sh "$project_root/tools/check-recovery-matrix-no-hardware.sh"
sh "$project_root/tools/check-g450-pll-no-hardware.sh"
sh "$project_root/tools/check-g450-pll-encoding-no-hardware.sh"
sh "$project_root/tools/check-g450-range-plan-no-hardware.sh"
sh "$project_root/tools/check-g450-crtc-plan-no-hardware.sh"
sh "$project_root/tools/check-g450-primary-crtc-image-no-hardware.sh"
sh "$project_root/tools/check-g450-primary-crtc-readback-no-hardware.sh"
sh "$project_root/tools/check-bounded-poll-no-hardware.sh"
sh "$project_root/tools/check-mode-transaction-no-hardware.sh"
sh "$project_root/tools/check-offscreen-2d-no-hardware.sh"
sh "$project_root/tools/check-offscreen-allocator-no-hardware.sh"
sh "$project_root/tools/check-manual-config-no-hardware.sh"
sh "$project_root/tools/check-reference-no-hardware.sh"
sh "$project_root/tools/check-command-no-hardware.sh"
sh "$project_root/tools/check-r1-original-snapshot-collector.sh"
sh "$project_root/tools/check-recovery-staging-package.sh"
sh "$project_root/tools/check-g450-recovery-activation-template.sh"
sh "$project_root/tools/check-g450-recovery-config-no-hardware.sh"
sh "$script_dir/run-edid-policy-host.sh"
sh "$script_dir/run-current-mode-footprint-host.sh"
sh "$script_dir/run-profile-host.sh"
sh "$script_dir/run-mode-review-host.sh"
sh "$script_dir/run-g450-16m-mode-record-host.sh"
sh "$script_dir/run-g450-recovery-config-host.sh"
sh "$script_dir/run-timing-review-host.sh"
sh "$script_dir/run-render-budget-host.sh"
sh "$script_dir/run-mesa-backend-host.sh"
sh "$script_dir/run-mesa-admission-host.sh"
sh "$script_dir/run-mesa-presentation-harness-host.sh"
sh "$script_dir/run-mapping-review-host.sh"
sh "$script_dir/run-recovery-matrix-host.sh"
sh "$script_dir/run-g450-pll-host.sh"
sh "$script_dir/run-g450-pll-encoding-host.sh"
sh "$script_dir/run-g450-range-plan-host.sh"
sh "$script_dir/run-g450-crtc-plan-host.sh"
sh "$script_dir/run-g450-primary-crtc-image-host.sh"
sh "$script_dir/run-g450-primary-crtc-readback-host.sh"
sh "$script_dir/run-bounded-poll-host.sh"
sh "$script_dir/run-mode-transaction-host.sh"
sh "$script_dir/run-offscreen-2d-host.sh"
sh "$script_dir/run-offscreen-allocator-host.sh"
sh "$script_dir/run-manual-config-host.sh"
sh "$script_dir/run-reference-host.sh"
sh "$script_dir/run-command-host.sh"
echo "OPENSTEP_MGA_NO_HARDWARE_HOST_CHECKS=pass"
