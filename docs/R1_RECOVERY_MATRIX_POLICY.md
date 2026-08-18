# R1 — Recovery Snapshot Matrix Policy

기준일: 2026-08-18

## Purpose

`profile/OpenStepMGARecoveryMatrix.{h,c}` makes the sole-owner snapshot
invariant machine-checkable before a future Configure-mediated recovery
cutover is considered. It contains reviewed candidate counts only; it is not a
driverLoader query tool and cannot change an OPENSTEP configuration.

## Required three-snapshot matrix

| snapshot | original `MatroxMGA` candidates | replacement candidates | required result |
| --- | ---: | ---: | --- |
| P-original | 1 | 0 | original-only boot |
| P-recovery | 0 | 1 | replacement-only recovery boot |
| P-failure | 1 | 0 | original restored |

The validator rejects every other combination, including 1/1 dual ownership,
0/0 no-display ownership, and counts above one. It also requires every
snapshot to carry verified bundle, `InstanceN.table`, and rollback-instruction
evidence; the full matrix additionally needs installer atomicity and an
independent recovery-channel review. It additionally requires a verified
Installer rollback and a verified P-failure original boot, so written
instructions or an atomic-install claim cannot substitute for an observed
return to the original profile.

## Boundary

The current target has the exact P-original evidence `R1-20260818-C`. R5
confirms the original boot/recovery path but does not create P-recovery/
P-failure evidence, so it cannot populate a passing matrix. The C89 unit
test's complete matrix is a synthetic branch fixture, not a target G1 result.

This policy performs no target access: it does not inspect `driverLoader`,
call `kl_util`, install a bundle, edit `InstanceN.table`, invoke Configure, or
reboot. Passing it never authorizes any of those actions.

## Verification

```text
sh tools/check-recovery-matrix-no-hardware.sh
sh test/run-recovery-matrix-host.sh
# target: csh -f test/run-recovery-matrix-target.csh /ndrv/openstep-matrox-remade
```

The first check rejects target/device interfaces from the policy source; the
second uses strict C89 compilation and exercises each ownership/evidence
failure path. The target runner passed on 2026-08-18 as an ordinary temporary
process only; it does not change a configuration.
