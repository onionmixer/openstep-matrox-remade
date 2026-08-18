# R5 — Original-Driver Recovery Rehearsal Run Sheet

상태: **preflight verified — cold reboot rehearsal은 실행하지 않음**  
기준일: 2026-08-18

현재 preflight evidence는 `R5-20260818-PREFLIGHT-A`
(`R5_ORIGINAL_PREFLIGHT.md`)다. 이는 이 sheet의 step 1을 충족할 뿐이며, 아래
cold reboot 절차가 실행되기 전에는 R5/G4를 통과시키지 않는다.

## 목적

이 run sheet는 replacement bundle을 enable하거나 load하지 않고, original
`MatroxMGA` boot profile로 cold reboot 후 display와 independent recovery channel이
돌아오는지 확인하기 위한 것이다. R0 read-only baseline이나 NFS mount 성공은 이
rehearsal을 대체하지 않는다.

## 실행 권한과 범위

- operator가 cold reboot와 physical/serial console 사용을 명시적으로 승인했을 때만
  실행한다.
- replacement source/build artifact를 `/private/Drivers/i386` 또는
  `/private/Devices`에 복사하지 않는다.
- `kl_util -u MatroxMGA`, `driverLoader` 재시작, mode switch, PCI/MMIO/DDC access를
  실행하지 않는다.
- 한 run에서 original profile의 boot/recovery만 확인한다. replacement profile을
  같은 run에 섞지 않는다.

## 시작 전 기록

| field | operator 기록 |
| --- | --- |
| run ID | `R5-YYYYMMDD-` |
| R0 baseline evidence ID | `R0-20260818-A` 또는 후속 ID |
| original boot snapshot | existing `MatroxMGA.config/Instance0.table` preserved; no Configure/copy/edit since preflight |
| expected display mode | 1600×1200, 60 Hz, RGB:888/32 (`R0-20260818-A`) |
| independent recovery channel | single-session telnet via `tools/nxrun.sh` (preflight verified; post-reboot reconnect required) |
| NFS log/source path | `/ndrv` 또는 verified fallback |
| original bundle fingerprint | `test/check-r0-original-driver-fingerprint.csh` result; baseline `R0-20260818-A` / current-boot evidence `R0-20260818-B` |
| abort timeout | 300 seconds (operator override 시 exact value 기록) |
| rollback owner | original `MatroxMGA` |

## 절차

1. `test/collect-r5-original-preflight.csh`를 실행해 현재 hostname, original
   `Instance0.table`, `MatroxMGA` owner, `/ndrv`, fingerprint comparator result를
   한 read-only record로 남긴다. 이 collector가 `pass`가 아니면 run을 `ABORT`한다.
2. independent recovery channel을 먼저 확인하고, NFS source/log path가 available한지
   기록한다. 현재 channel은 single-session telnet이며, session마다 `logout`으로
   종료한다. reboot 뒤 새 session이 target prompt까지 도달해야 recovery pass다.
3. operator가 P-original snapshot에 Configure, table edit, bundle copy를 수행하지
   않았음을 확인한다. 이 target의 original boot는 별도 selection UI가 아니라
   existing `MatroxMGA.config/Instance0.table`과 `/etc/rc`의 `driverLoader a`를
   사용한다.
4. cold reboot한다. timeout 안에 GUI display가 보이지 않거나 remote channel이
   끊기면 추가 driver action을 하지 않는다.
5. original profile로 다시 boot한 뒤 display, recovery channel, NFS path를 각각
   확인한다.
6. 같은 preflight collector를 다시 실행하고 `MatroxMGA` loaded 상태와 selected mode를 R0 baseline과 비교한다.
   `test/check-r0-original-driver-fingerprint.csh`로 `MatroxMGA_reloc`, executable,
   `Default.table`, `Instance0.table`의 size/`/usr/bin/sum`을 R0 fingerprint와
   비교한다. 이 target에는 `cksum`/`md5`가
   없으므로 `sum`은 corruption/zero-length detector이며 cryptographic proof가 아니다.
7. 결과를 아래 표에 기록한다. 실패 시 replacement work를 진행하지 않고
   known-good original boot 상태에서 원인을 분리한다.

## 결과 기록

| check | expected | actual | pass/fail |
| --- | --- | --- | --- |
| original snapshot preserved | original `MatroxMGA` `Instance0.table` only; no Configure/copy/edit | pending | pending |
| GUI display | visible/stable | pending | pending |
| independent recovery | usable | pending | pending |
| NFS source/log | reachable | pending | pending |
| `kl_util -s MatroxMGA` | loaded | pending | pending |
| display mode | baseline-consistent | pending | pending |
| unexpected replacement artifact/load | absent | pending | pending |
| timeout/corruption | none | pending | pending |

## Verdict

- `PASS`: 모든 check가 통과했고 original profile에서 cold reboot recovery가
  재현됐다. R5/G4 evidence를 기록할 수 있다.
- `FAIL`: display loss, recovery loss, NFS log loss, mode mismatch, unexpected
  bundle state, timeout 중 하나라도 있다. replacement boot를 시도하지 않는다.
- `ABORT`: operator 승인 또는 independent recovery channel이 준비되지 않았다.
  실행하지 않은 상태이며 failure로 해석하지 않는다.

이 template은 G4를 통과시키지 않는다. 실행 결과와 operator confirmation이
기입된 뒤에만 `RECOVERY_REPLACEMENT_DRIVER_EXECUTION_PLAN.md`의 R5 상태를
변경할 수 있다. 실제 result의 field/acceptance format은
`R5_RECOVERY_REHEARSAL_TEMPLATE.md`를 사용한다.
