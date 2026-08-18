# R5 — Original-Driver Cold-Boot Recovery Result

상태: **template — 실행 결과 아님**

이 file은 operator-approved original-profile-only cold reboot가 끝난 뒤에
`R5_RECOVERY_REHEARSAL.md`로 복사해 실제 관찰값만 채운다. replacement bundle
installation/load/probe와 동일 run에 섞지 않는다.

## Run identity and authority

| field | value |
| --- | --- |
| R5 run ID | `R5-YYYYMMDD-<suffix>` |
| operator | pending |
| target hostname | pending |
| start / end time | pending |
| original boot snapshot | existing `MatroxMGA.config/Instance0.table` preserved; `/etc/rc` `driverLoader a`; no Configure/copy/edit |
| rollback method / owner | pending |
| independent recovery channel | single-session telnet preflight verified; post-reboot reconnect pending |
| GUI recovery timeout / abort authority | 300 seconds / pending (override 시 exact value) |
| R0 baseline | `R0-20260818-A` |
| preflight evidence | `R5-20260818-PREFLIGHT-A` or new ID |

## Pre-reboot record

| check | expected | actual | verdict |
| --- | --- | --- | --- |
| preflight collector | `OPENSTEP_MGA_R5_PREFLIGHT_STATUS=pass` | pending | pending |
| loaded owner | `MatroxMGA` only | pending | pending |
| original instance/table | baseline-consistent | pending | pending |
| manual mode | 1600×1200, 60 Hz, RGB:888/32 | pending | pending |
| `/ndrv` | mounted | pending | pending |
| fingerprint comparator | pass | pending | pending |
| replacement artifact/load | absent | pending | pending |

## Cold-boot observation

| check | expected | actual | verdict |
| --- | --- | --- | --- |
| original snapshot preserved | yes; no Configure/copy/edit | pending | pending |
| GUI display | visible/stable before timeout | pending | pending |
| independent recovery | usable after reboot | pending | pending |
| `/ndrv` | reachable after reboot | pending | pending |
| unexpected display owner/load | none | pending | pending |
| corruption/hang/timeout | none | pending | pending |

## Post-reboot read-only record

Run `test/collect-r5-original-preflight.csh` again and record its exact final marker.

| check | expected | actual | verdict |
| --- | --- | --- | --- |
| postflight collector | `OPENSTEP_MGA_R5_PREFLIGHT_STATUS=pass` | pending | pending |
| `MatroxMGA` loaded owner | baseline-consistent | pending | pending |
| selected table/mode | baseline-consistent | pending | pending |
| original fingerprint comparator | pass | pending | pending |
| NFS source/log recovery | reachable | pending | pending |

## Verdict and follow-up

`PASS` requires every row above to pass, including visible original display,
independent recovery, and post-reboot comparator. `FAIL` means freeze, corruption,
timeout, owner mismatch, or recovery loss; do not attempt replacement boot. `ABORT`
means a prerequisite or operator authority was absent and is not a failure run.

| field | value |
| --- | --- |
| final verdict | pending |
| failure marker / observation | pending |
| original-profile recovery performed | pending |
| evidence/log location | pending |
| G4 verdict | pending |
| authorization for any subsequent action | pending |
