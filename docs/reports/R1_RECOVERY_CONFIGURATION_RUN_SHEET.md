# R1 — Recovery configuration run sheet

이 문서는 future P-recovery/P-failure Configure run을 위한 기록 양식이다.
현재 target configuration을 바꾸라는 지시가 아니며, 수동 file copy, runtime
unload, driverLoader restart는 허용하지 않는다.

## Preflight already available

| item | evidence | status |
| --- | --- | --- |
| deployment input | PCI G450 primary head, 16 MiB, 1600×1200×32/162 MHz | operator-complete |
| original profile | `R1-20260818-C` exact P-original instance check | pass |
| original cold recovery | `R5-20260818-A` | pass |
| recovery matrix policy | host and OPENSTEP i386 C89 | pass (synthetic only) |
| P-recovery table admission | exact driver/function/mode/16 MiB/profile C89 validator | pass (offline only) |

## Required real snapshots

| snapshot | expected candidate counts | required collected evidence |
| --- | --- | --- |
| P-original | original `1`, replacement `0` | bundle, `InstanceN.table`, owner/mode, rollback instructions |
| P-recovery | original `0`, replacement `1` | Configure-created replacement instance, original exclusion, independent recovery channel, owner/mode |
| P-failure | original `1`, replacement `0` | restored original instance, original boot/owner/mode, replacement exclusion |

## Required outcome flags

The final `OSMGARecoveryMatrix` record may be marked complete only when all
three snapshot rows are evidenced and all four matrix-wide flags are true:

1. atomic Installer installation verified;
2. Installer rollback verified;
3. P-failure original boot verified;
4. independent recovery channel verified.

Any configuration with `1/1`, `0/0`, a missing instance table, ambiguous
matching precedence, missing remote channel, or display anomaly is a stop:
do not retry mode programming or unload a driver. Preserve the evidence and
restore the known-good P-original profile through Configure/Installer.

For the later R6 linear smoke, preserve the result of four separate rollback
reports—display state, PLL state, VGA-safe state, and superclass revert. The
offline transaction now refuses to mark rollback complete without all four;
this does not itself perform the restores.

Before recording P-recovery, extract the Configure-created table values and
apply the exact-value rule in `R1_G450_RECOVERY_CONFIG_POLICY.md`. This is an
additional fail-closed check; it does not replace the required real snapshot.

## Completion record

| field | value |
| --- | --- |
| run ID / operator / time | pending |
| Installer package identity | pending |
| P-recovery Configure result | pending |
| P-failure restoration result | pending |
| rollback result | pending |
| independent channel after each reboot | pending |
| final G1 sole-owner configuration verdict | pending |
