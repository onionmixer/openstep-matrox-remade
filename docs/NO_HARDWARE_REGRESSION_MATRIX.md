# No-hardware regression matrix

## Purpose

This matrix closes the code-only portion of the current work.  Every listed
runner uses ordinary host or target process memory only.  None installs or
loads a replacement driver, maps device memory, changes a display mode, or
submits a graphics command.

| area | runner | key boundary |
| --- | --- | --- |
| R2/R3 physical/config policy | `run-profile-host.sh`, `run-mode-review-host.sh` | complete physical references, manual total equality, embedded complete timing record |
| approved G450 deployment record | `run-g450-16m-mode-record-host.sh` | fixed 16 MiB cap and one 1600x1200@60 DMT record |
| G450 P-recovery table admission | `run-g450-recovery-config-host.sh` | fixed driver/function/mode/16 MiB/P-recovery values; no table or device access |
| timing | `run-timing-review-host.sh` | active/blanking/sync/polarity/clock consistency |
| 16 MiB layouts | `run-render-budget-host.sh` | scanout/reserved/color/depth byte budget |
| Mesa backend selection | `run-mesa-backend-host.sh` | fixed 1024x768 and mandatory software fallback |
| R3/R6 Mesa admission | `run-mesa-admission-host.sh` | exact reviewed profile/scanout/alignment to fixed two-color/depth budget binding |
| fallback presentation | `run-mesa-presentation-harness-host.sh` | software dispatch plus deterministic scale composition |
| G450 PLL byte image | `run-g450-pll-encoding-host.sh` | one reviewed plan only; byte encoding without DAC I/O or candidate retry |
| G450 legacy range plan | `run-g450-range-plan-host.sh` | exact 16 MiB + VGA/BIOS three-range data plan, no publication/mapping |
| transaction safety | `run-mapping-review-host.sh`, `run-g450-pll-host.sh`, `run-mode-transaction-host.sh` | same complete timing record, primary-CRTC/PLL byte images, required caller-supplied CRTC snapshot, mapping/PLL/poll rollback policy |
| G450 mode geometry | `run-g450-crtc-plan-host.sh` | checked one-mode display/sync/total/pitch derivation |
| G450 primary CRTC byte image | `run-g450-primary-crtc-image-host.sh` | fixed 32-bit primary-head image only; no VGA/MMIO/DAC operation |
| G450 primary CRTC readback comparison | `run-g450-primary-crtc-readback-host.sh` | caller-supplied bytes only; exact CRTC/extended comparison and MiscOut clock-bit mask |
| reference rendering | `run-reference-host.sh` | caller-owned clear/copy/scale/depth/blend/texture oracle |

`test/run-no-hardware-host-checks.sh` runs the complete matrix together with
the existing P1/P2/R4 static gates.  New code must be added to this aggregate
runner and receive a no-hardware static guard before it can be considered part
of the code-only completion set.

## Target compiler subset

The R3 review, timing review, render budget, reference oracle, R6 mapping/PLL/
PLL-byte-image/primary-CRTC-readback/transaction, Mesa backend selector/harness, and R3/R6
Mesa-admission test were
compiled as temporary target-native C programs for their current revisions.
Their successful execution establishes C89/i386 language portability only; it
does not alter the hardware gate verdicts.

The separately installed Mesa 3.4.2 software fallback also has a target-only
package-consumer smoke runner, `run-osmesa-fallback-target.csh`.  It is outside
the host aggregate because it intentionally links target `/LocalDeveloper`
archives, but it is covered by a static no-machine-interface guard and passed
the 1024x768 context/clear/pixel check on 2026-08-18.
