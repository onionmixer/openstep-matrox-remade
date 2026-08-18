# R0 — Production Baseline and Recovery Evidence

기준일: 2026-08-18  
evidence ID: `R0-20260818-A`

## 수집 범위

현재 production boot에서 자동 logout하는 단일 telnet session으로 다음만
read-only로 수집했다.

| 항목 | 결과 | 해석 |
| --- | --- | --- |
| target hostname | `nextonion` | 기준선을 수집한 실기 identity |
| display owner | `MatroxMGA` | 현재 유일한 production screen owner |
| kernel-loader status | loaded | replacement bundle은 등록·load·probe하지 않음 |
| existing relocatable path | `/usr/Devices/MatroxMGA.config/MatroxMGA_reloc` | original driver artifact의 recovery reference |
| selected configuration table | `MatroxMGAG400_16MB` | existing compatibility profile; physical VRAM 측정값 아님 |
| selected display mode | 1600×1200, 60 Hz, RGB:888/32 | 현재 production visual baseline |
| configuration memory field | `16` | existing profile field; physical VRAM total/type 증거 아님 |
| NFS source | `/ndrv`, `hard,intr,timeo=30,retrans=5,rw` | source/log 회수 경로가 현재 mount됨 |

raw module address, framebuffer pointer, BAR address, EDID serial은 이 기록에
포함하지 않는다.

## Original driver fingerprint

target OPENSTEP 4.2에는 `cksum`/`md5`가 없으므로 `/usr/bin/sum` output과
file size/mode를 함께 기록했다. 이는 future recovery 뒤 partial copy 또는
zero-length artifact를 발견하기 위한 same-system baseline이며 cryptographic
identity proof가 아니다.

| bundle file | size | mode class | `/usr/bin/sum` |
| --- | ---: | --- | --- |
| `MatroxMGA_reloc` | 104,788 | read-only regular | `45628 103` |
| `MatroxMGA` | 1,068 | executable regular | `05204 2` |
| `Default.table` | 521 | read-only regular | `60079 1` |
| `Instance0.table` | 617 | regular | `25212 1` |

collector result: `OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_STATUS=pass`.
수집 중 runtime module을 unload 또는 replace하지 않았다.

## Current-boot comparator result

evidence ID: `R0-20260818-B`  
scope: target-native, read-only; original profile cold reboot는 수행하지 않음

`test/check-r0-original-driver-fingerprint.csh`를 `/ndrv`에서 OPENSTEP `csh`로
실행했다. 이 comparator는 R0-20260818-A의 각 file size와 `/usr/bin/sum` 값을
exactly compare하며, file read 외의 target action을 수행하지 않는다.

| check | result |
| --- | --- |
| `MatroxMGA_reloc` | pass |
| `MatroxMGA` | pass |
| `Default.table` | pass |
| `Instance0.table` | pass |
| target script result | `OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK_STATUS=pass` |
| target command status captured before status query | `0` |
| current kernel owner | `SERVER: MatroxMGA`; loaded |

이 결과는 comparator와 current boot artifact가 R0-20260818-A에 일치함을 보인다.
reboot 뒤의 동일 비교, original profile selection, independent recovery channel,
cold-boot recovery 자체는 아직 수행하지 않았으므로 R5/G4 evidence가 아니다.

## 확인한 불변식

1. production display owner는 계속 `MatroxMGA`다.
2. 이번 session에서 `OpenStepMGAReplacementDisplay`을 `/private/Drivers/i386` 또는
   `/private/Devices`에 설치하거나, `kl_util`/`driverLoader`에 전달한 action은
   없었다. target filesystem의 과거 artifact 부재는 R1에서 별도로 read-only
   확인해야 한다.
3. R4 target binary 검사는 file import inspection만 수행했으며 current display
   ownership에 영향을 주지 않았다.
4. NFS는 현재 mount되어 있으나, mount 성공만으로 original boot recovery가
   rehearsed되었다고 보지 않는다.
5. original bundle의 size/mode/`sum` baseline은 확보했지만, cold reboot 뒤의
   comparison은 아직 수행하지 않았다.

## 아직 충족하지 않은 R0 recovery evidence

| requirement | 상태 | 다음 안전한 확인 |
| --- | --- | --- |
| known-good original boot path | 실기 구조 확인 | preserved `MatroxMGA.config/Instance0.table`을 `/etc/rc`의 `driverLoader a`가 읽음; Configure/copy/edit 없이 cold reboot로 rehearsal |
| independent console/serial 또는 원격 관리 recovery | 미rehearsed | original driver boot에서 별도 channel 확인 |
| cold reboot 후 original boot path | R5 programmatic checks 통과 | GUI display visual stability는 operator observation 대기; `R5_RECOVERY_REHEARSAL.md` 참조 |
| failure run sheet | 준비 중 | R1 configuration review와 함께 profile별 rollback 절차 확정 |

따라서 이 report는 R0 기준선의 **read-only 부분만 통과**시킨다. R1 이후 또는
replacement boot를 허용하지 않으며, G1/G4는 여전히 미통과다.
