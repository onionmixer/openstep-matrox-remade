# R5 — Original-Profile Read-Only Preflight

기준일: 2026-08-18  
evidence ID: `R5-20260818-PREFLIGHT-A`  
scope: target-native read-only; **cold reboot 및 replacement action 없음**

## 실행한 collector

`/ndrv/openstep-matrox-remade/test/collect-r5-original-preflight.csh`를 OPENSTEP
target에서 실행했다. collector는 hostname, original `Instance0.table` identity/mode,
`kl_util -s MatroxMGA`, `/ndrv` mount, R0 exact fingerprint comparator를 읽기만 한다.
`driverLoader`, `kl_util -u`, Configure, target configuration file, PCI/MMIO/DDC에는
접근하지 않는다.

preflight collector는 `tools/nxrun.sh`의 single-session telnet으로 실행됐고 target
prompt까지의 command/reply를 확인했다. 따라서 telnet은 R5의 independent remote
recovery channel 후보로 verified다. 단, cold reboot 뒤 새 telnet session이 다시
성공하기 전에는 recovery pass가 아니다.

## 결과

| check | observed result | verdict |
| --- | --- | --- |
| target identity | `nextonion` | pass |
| configured driver | `MatroxMGA` | pass |
| configured table | `MatroxMGAG400_16MB` | baseline-consistent |
| manual mode | 1600×1200, 60 Hz, RGB:888/32 | baseline-consistent |
| loaded kernel owner | `SERVER: MatroxMGA` | pass |
| NFS source/log mount | `/ndrv` mounted | pass |
| `MatroxMGA_reloc` comparator | pass | pass |
| executable/table comparator | all remaining three files pass | pass |
| collector final marker | `OPENSTEP_MGA_R5_PREFLIGHT_STATUS=pass` | pass |

## Interpretation

R5 run sheet의 reboot **전** read-only preflight는 준비되었다. 이 evidence는
`R0-20260818-A` / `R0-20260818-B`와 consistent하지만 다음 항목을 증명하지 않는다.

- exact original boot-profile selection method
- independent recovery channel의 실제 interaction
- cold reboot 뒤 GUI display / NFS / owner / comparator 재현
- G4/R5 PASS 또는 replacement boot authorization

따라서 다음 target action은 operator가 original-only cold reboot를 명시적으로
승인하고 independent recovery channel과 timeout을 기록한 경우의 R5 run이다.

## Follow-up preflight

evidence ID: `R5-20260818-PREFLIGHT-B`  
scope: single telnet fallback session, target-native read-only collector, automatic logout

R5 run 전 연결 상태를 다시 확인하기 위해 같은 collector를 재실행했다. target
identity, original `MatroxMGA` instance/table/mode, loaded owner, `/ndrv`, 네 original
bundle comparator가 모두 앞선 preflight와 일치했고 final marker는 다시
`OPENSTEP_MGA_R5_PREFLIGHT_STATUS=pass`였다.

이 follow-up은 cold reboot를 수행하지 않았고, target configuration, driverLoader,
PCI/MMIO/DDC, replacement bundle 상태를 변경하지 않았다. R5/G4 verdict는 계속
미통과다.
