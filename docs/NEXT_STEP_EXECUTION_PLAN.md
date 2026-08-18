# Immediate Next-Step Execution Plan — Evidence Before Replacement Boot

기준일: 2026-08-18  
상태: **Work package A 구현·current-boot 검증 완료 — target configuration/hardware 변경 금지**

## 이 작업 패키지의 결론

다음 replacement-driver 작업은 새 driver code를 더 추가하거나 `MatroxMGA`를
unload하는 일이 아니다. 먼저 아래 두 독립 gate의 증거를 확보한다.

1. **G4 / R5** — 기존 original profile만으로 cold reboot 뒤 screen, 독립 복구
   channel, NFS 회수가 재현되는가.
2. **G1 / R1** — original/recovery/failure Configure snapshot에서 old/new display
   candidate가 정확히 하나인지, Installer rollback까지 재현되는가.

두 gate가 통과할 때까지 replacement bundle을 production driver directory에
설치하지 않고, Configure의 display selection을 바꾸지 않으며, PCI BAR/VRAM,
PLL/CRTC/DAC, DDC, 2D/3D engine에는 접근하지 않는다. 현재의 `MatroxMGA`는
production display owner로 유지한다.

## 시작 상태와 gate ledger

| gate | 현재 근거 | verdict | 이 패키지에서 할 일 |
| --- | --- | --- | --- |
| R0 baseline | owner/config/mode/original bundle fingerprint 및 `/ndrv` 확보 | read-only 통과 | reboot 전후 동일 기준선 비교를 자동화 |
| G1 hardware precondition | PCI G450 primary-head, conservative 16 MiB bound, fixed 1600×1200×32/162 MHz input | 완료 (operator decision) | `G1_HARDWARE_PRECONDITION_STATUS.md`의 fixed deployment input을 사용; live probing·mode programming은 하지 않음 |
| G1 sole owner / recovery configuration | staging artifact/package, isolated exact P-recovery template, snapshot model, P-original exact field/production-artifact-absence evidence (`R1-20260818-C`), target C89 matrix policy | 미통과 | recovery-capable bundle 뒤 P-recovery/P-failure Configure snapshot, atomic Installer install+rollback, P-failure original boot과 cold recovery path를 실기에서 확인 |
| G2 physical profile research | exact P/N/max-capacity research | 별도 추적 | operator-approved PCI G450 16 MiB deployment cap을 바꾸지 않음 |
| G3 manual mode | `R3_G450_16M_DEPLOYMENT_MODE`, current 1600×1200×32, standard 162 MHz DMT timing, 16 MiB cap | PASS (offline) | replacement mode programming은 G1 및 별도 activation approval 뒤에만 가능 |
| G4/R5 recovery | R5 original-only cold reboot의 programmatic postflight + operator GUI normal confirmation (`R5-20260818-A`) | PASS | G4 통과 |
| R4 skeleton | host gate, target i386 build/import inspection 통과 | source/build 완료 | install/load/probe 금지 유지 |

The 16 MiB limit is now the operator-approved conservative PCI G450 deployment
cap. It is not enlarged by the 32 MiB catalogue candidate; physical-board
research remains separate from the completed offline G3 record.

R3의 offline one-mode review policy source (`profile/OpenStepMGAModeReview.*`)와
the approved `R3_G450_16M_DEPLOYMENT_MODE` record both passed C89 host and
target regression. The record remains a design input, not an activation.

R6의 local DriverKit mapping API audit도 완료했다. `mapMemoryRange`/
`unmapMemoryRange` pair는 future recovery-only candidate이나, required memory
range/index/length/cache data가 없으므로 R4 source 또는 configuration은 바꾸지
않는다. `R6_DRIVERKIT_MAPPING_AUDIT.md`를 따른다.

동 문서의 range configuration admission은 pure-C `OpenStepMGAMappingReview.*`
로도 고정했다. R3/R1 evidence와 reviewed range/cache policy가 모두 없으면
fail closed하며 target API를 호출하지 않는다.

G1의 P-original/P-recovery/P-failure candidate-count invariant도
`OpenStepMGARecoveryMatrix.*`로 offline 검증 가능하게 준비했다. 현재는
P-original만 real evidence가 있으므로 이 policy의 synthetic test pass는 G1
verdict를 바꾸지 않는다.

R6 Storm 2D source audit은 X.Org의 FIFO/idle/state-init path를 no-DMA,
bounded-wait, offscreen-first staged plan으로 분해했다. G3은 approved
16 MiB deployment record로 offline pass했지만 G1/G2와 mapping evidence가
미통과이므로 register header나 MMIO code는 추가하지 않는다.

G450 PLL/DAC source audit도 exact timing, head selection, bounded stable-lock,
one-candidate rollback transaction으로 분해했다. operator-approved R3 fixed
record의 162 MHz input으로 pure-C frequency plan, primary PLL M/N/P byte image,
그리고 primary CRTC/extended byte image까지 offline으로 생성·회귀 검증했다.
이는 승인 record의 data-only representation일 뿐 DAC/MMIO register value의 write
order나 target programming code가 아니다.

PLL lock 및 2D FIFO/idle에 공통으로 사용할 terminal bounded-poll policy도
hardware-independent source로 준비했다. actual timeout/sample count와 register
reader는 R6 replacement-only run review 전까지 미설정·미구현으로 유지한다.

R6 mapping/primary-CRTC-image/PLL/lock/linear-result/rollback의 one-mode state
transition도 pure-C로 결합했다. R6 mapping flag뿐 아니라 complete G1 recovery
matrix가 pass해야 하며, 동일한 R2/R3 review에서 나온 값만 함께 쓸 수
있으며 stable PLL lock 뒤에는 caller-provided primary CRTC snapshot이 approved
image와 일치해야 linear result를 허용한다. timeout 또는 mismatch는 retry가 아닌
rollback state로 끝난다. complete primary image, PLL image, three-range plan은
모두 unwritten data다. rollback completion에는 display/PLL/VGA-safe/superclass
revert의 네 성공 report가 모두 필요하다. 이 source는 R4 bundle에 아직 연결하지 않는다.

first 2D clear/copy request도 active transaction과 kernel-owned/outside-scanout
surface evidence 없이는 host-side에서 거부하도록 준비했다. 이는 geometry admission
뿐이며 submit ABI, allocation, MMIO engine code를 아직 만들지 않는다.

그 request에 사용할 offscreen allocation은 address-free opaque ledger로만
준비했다. externally verified arena, checked 32-bit footprint/alignment,
sequential ID, bounded live-ID count를 검증하며 release 뒤 arena byte를 재사용하지
않는다. 따라서 fence/idle/lifetime evidence 없이 reuse를 허용하지 않는다. 이는
target allocation이나 mapping이 아니며 R4/P2와 연결하지 않는다.

hardware readback 이후 exact expected pixels/checksum을 만들 수 있도록 reference
oracle에 bounded clear/copy를 추가했다. ordinary host memory만 사용하며 target
offscreen allocation이나 2D fallback을 구현하지 않는다.

## 공통 운영 규칙

- GCD 연결이 정상일 때는 이를 target command channel로 우선 사용한다. telnet은
  단일 read-only fallback에 한정하고, 사용한 session은 반드시 `logout`으로
  종료한다. 여러 telnet session을 병렬로 열지 않는다.
- reboot, chassis opening, Configure 변경, replacement installation은 operator의
  명시적 run 승인 후에만 실행한다. 이 문서의 작성 또는 preflight가 그 승인을
  대신하지 않는다.
- log/source 회수는 `/ndrv`를 사용하되, target의 display 또는 network 상태에
  영향을 주는 daemon 재시작을 이 패키지의 정상 절차로 삼지 않는다.
- raw BAR address, framebuffer pointer, EDID serial, board serial은 공개 report에
  기록하지 않는다. board serial은 필요하면 redaction/hash 여부만 기록한다.
- timeout, unexpected bundle owner, screen corruption, NFS log loss가 발생하면
  자동 retry나 runtime unload를 하지 않는다. log를 보존하고 original boot profile
  recovery로 돌아간다.

## Work package A — Reboot 전 read-only comparator 준비

### 목적

R5 전후에 original bundle이 partial copy, zero-byte file, 다른 configuration으로
바뀌지 않았음을 같은 target의 baseline으로 판정한다. `/usr/bin/sum`은
cryptographic identity proof가 아니라 same-system corruption detector다.

### 구현 순서

1. `test/check-r0-original-driver-fingerprint.csh`를 새로 만들었다. 이 script는
   `/usr/Devices/MatroxMGA.config/` 아래 다음 **네 파일만 read-only**로 확인한다.
   - `MatroxMGA_reloc`: 104788 bytes, `sum` = `45628 103`
   - `MatroxMGA`: 1068 bytes, `sum` = `05204 2`
   - `Default.table`: 521 bytes, `sum` = `60079 1`
   - `Instance0.table`: 617 bytes, `sum` = `25212 1`
2. old csh와 OPENSTEP 4.2 userland에서 실행 가능하도록 POSIX-only `egrep`
   character class, Bash syntax, `cksum`/`md5` 의존을 넣지 않는다.
3. script의 final marker는 하나만 사용한다.
   `OPENSTEP_MGA_R0_ORIGINAL_FINGERPRINT_CHECK_STATUS=pass|fail`
4. 현재 boot에서 한 번 실행해 baseline comparator 자체가 동작하는지를 확인한다.
   이 실행은 file read와 console output만 허용하며 reboot나 driverLoader action을
   포함하지 않는다.
5. script source와 결과를 `R0_BASELINE_AND_RECOVERY.md` 및 R5 결과 report의
   evidence ID에 연결한다.

현재 상태: target-native `csh` execution에서 네 file 모두 `pass`, final marker
`pass`, command status `0`을 확인했다(`R0-20260818-B`). 이 결과는 current boot
preflight를 충족하지만 R5 cold reboot를 실행하거나 G4를 통과시키지 않는다.

R5 직전에는 `test/collect-r5-original-preflight.csh`로 hostname, original
`Instance0.table`의 identity/mode fields, `kl_util -s MatroxMGA`, `/ndrv`, comparator를
한 read-only record로 다시 수집한다. 이 collector의 `pass`도 cold reboot나
independent recovery-channel readiness를 대신하지 않는다.

현재 상태: collector는 target에서 두 번 `pass`를 반환했다
(`docs/reports/R5_ORIGINAL_PREFLIGHT.md`, `R5-20260818-PREFLIGHT-A/B`). 따라서
Work package B의 **preflight code/evidence**는 준비됐고, 남은 B 조건은 operator의
original-only cold reboot 승인, independent recovery channel 확인, timeout/rollback
기록이다.

### 완료 조건 / 중단 조건

- 완료: 네 file의 presence, size, `/usr/bin/sum`이 R0-20260818-A와 모두 일치하고
  final marker가 `pass`다.
- 중단: file missing, mismatch, NFS source mismatch, current owner가
  `MatroxMGA`가 아니거나 target identity가 달라지면 R5를 시작하지 않는다. 차이를
  기록하고 original boot/profile 상태를 먼저 복구한다.

## Work package B — R5 original-profile cold-boot recovery rehearsal

### 범위

원본 `MatroxMGA`만 active인 known-good profile을 선택하여 cold reboot한 뒤,
원본 display 경로와 독립 복구 경로가 살아나는지만 확인한다. replacement bundle은
설치, load, Configure 노출, probe 모두 하지 않는다.

### run 전 operator checklist

| check | required evidence | fail 처리 |
| --- | --- | --- |
| original boot snapshot | existing `MatroxMGA.config/Instance0.table`을 보존하고 `/etc/rc`의 `driverLoader a` 경로를 사용 | Configure/copy/edit 이력이 있으면 ABORT |
| independent recovery | single-session telnet (`nxrun.sh`) 또는 serial/physical console. 현재 telnet preflight는 verified이며 reboot 뒤 재접속으로 최종 판정 | unavailable이면 ABORT |
| NFS | `/ndrv` source/log path 확인 | unavailable이면 ABORT |
| baseline | Work package A comparator `pass`; `kl_util -s MatroxMGA`와 selected mode 기록 | mismatch면 ABORT |
| rollback | original profile을 다시 선택하는 방법과 담당자 확인 | unknown이면 ABORT |
| timeout | GUI recovery 대기 상한 300초(override 시 기록)와 abort 판단자 | unset이면 ABORT |

### 정확한 실행 순서

1. operator가 P-original snapshot이 보존됐음을 기록한다. 현재 target은 별도
   boot-profile selection UI가 아니라 existing `MatroxMGA.config/Instance0.table`과
   boot-time `driverLoader a`로 original display candidate를 구성한다. Configure,
   table edit, bundle copy를 수행하지 않는다.
2. read-only preflight를 한 세션에서 수집한다: hostname, `kl_util -s MatroxMGA`,
   selected mode, `/ndrv`, Work package A comparator result.
3. 독립 recovery channel이 현재 실제 입력/출력을 받는지 먼저 확인한다. 현재는
   `nxrun.sh`의 single-session telnet을 사용한다. 이 channel은 GUI와 독립적이지만,
   reboot 뒤 재접속이 성공할 때에만 R5 recovery check를 pass로 기록한다.
4. operator가 cold reboot한다. Codex는 reboot 중 driver-related command,
   `kl_util -u`, `driverLoader` 재시작, config file copy를 실행하지 않는다.
5. timeout 안에 original GUI display가 나타나는지 operator가 판정한다. 병행하여
   independent recovery channel 및 `/ndrv` availability를 확인한다.
6. reboot 뒤 동일한 read-only preflight와 comparator를 다시 실행한다.
7. R5 result table에 profile, mode, owner, comparator, recovery channel, NFS,
   timeout, operator verdict를 기록한다. `R5_RECOVERY_REHEARSAL.md`는 이 run에
   실제로 사용한 evidence만 담는다.

### R5 판정

- `PASS` / G4 통과: original display, independent recovery, NFS, original owner,
  file comparator가 모두 정상이고 timeout/corruption이 없다.
- `FAIL`: 하나라도 실패한 경우. replacement 작업은 시작하지 않고 original
  profile의 안정화를 먼저 한다.
- `ABORT`: 권한, profile selection, independent recovery, timeout 중 하나가
  준비되지 않은 경우. 실행하지 않은 것이며 failure로 계산하지 않는다.

R5 성공도 G1/G2 또는 replacement boot를 허용하지 않는다. G3은 별도의
offline fixed-mode design pass이며 live replacement activation은 아니다.

## Work package C — R2 physical board profile 확정

### 범위

이는 Work package B와 독립된 evidence track이다. normal shutdown, power-off,
AC disconnect, ESD-safe 상태에서만 board label/marking을 확인하며 live board를
만지거나 뽑지 않는다.

### 실행 순서

1. `R2_PHYSICAL_INSPECTION_RUN_SHEET.md`의 B1~B4를 현물 사진/marking으로 채운다.
   B2 sticker P/N 또는 B4 memory package marking 중 적어도 하나를 readable하게
   확보한다.
2. 공개 report에는 serial을 redact하고, front/back orientation 및 focus 상태만
   기록한다.
3. B2/B4를 `R2_SUBSYSTEM_CANDIDATE_PROFILE.md`의 `G45FMDVP32DSF` candidate와
   대조한다. 일치하지 않으면 candidate를 즉시 폐기한다.
4. exact P/N 또는 memory part number에 대한 primary source(B5)와 서로 독립된
   cross-check(B6)를 확보한다.
5. `R2_PHYSICAL_PROFILE_EVIDENCE.md`를 새로 작성한다. field는 board identity,
   VRAM type, VRAM total, RAMDAC/pixel-clock applicability, B1~B6, unresolved item,
   reviewer, verdict다. 기록 형식은
   `docs/reports/R2_PHYSICAL_PROFILE_EVIDENCE_TEMPLATE.md`를 사용한다.

### G2 판정

- `PASS`: target physical identity가 exact source와 연결되고 VRAM type/total 및
  applicable RAMDAC limit이 independent evidence에서 일치한다.
- `UNRESOLVED`: marking을 읽을 수 없거나 source가 board-specific이 아닌 경우.
  이 결과는 정상적인 stop이며 16 MiB/32 MB 어느 쪽도 implementation constant로
  승격하지 않는다.
- `FAIL`: physical board가 candidate와 다르거나 source 사이 값이 충돌하는 경우.
  candidate를 archive하고 R2를 새 identity로 재시작한다.

## Work package D — G2 결과 뒤의 코드/문서 분기

| G2 결과 | 다음 행동 | 명시적으로 하지 않는 일 |
| --- | --- | --- |
| PASS | R3에서 exact physical limit 안의 단일 conservative manual mode를 offline 계산·review | target mode programming, VRAM map, replacement install |
| UNRESOLVED | R2 evidence를 보존하고 replacement hardware work를 hold | catalogue candidate를 default table/map length에 반영 |
| FAIL | candidate 폐기 후 physical identity 재조사 | 더 큰 VRAM/더 높은 clock 가정 |

R3가 열리면 `R3_MANUAL_MODE_TABLE_REVIEW.md`에는 하나의 mode만 기록한다:
geometry, bpp, pitch alignment, visible footprint, mapping length, pixel clock,
memory/clock margin, rejected alternatives. G3가 통과하기 전에는 mode sequence
source code를 만들지 않는다.

R2 proof가 값으로 정리된 뒤에는 먼저 pure-C
`OSMGAValidateR2PhysicalProfile`을 통과시키고, 그 evidence ID와 reviewer를 R3
report에 연결한다. 이 validation은 R2 proof의 누락을 막는 policy일 뿐, mode
programming/mapping/P3 admission을 허용하지 않는다. 상세 contract는
`R2_PROFILE_ADMISSION_POLICY.md`를 따른다.

## Work package E — recovery-only configuration review (G2/G3/G4 이후)

G2, G3, G4가 모두 `PASS`가 된 **뒤에만** R1의 future snapshot을 설계한다.

1. P-original, P-recovery, P-failure 각각의 complete bundle/`InstanceN.table`/
   Configure result를 one-page matrix로 만든다.
2. 각 snapshot에서 exact MGA PCI function의 candidate count를 old/new `1/0` 또는
   `0/1`로 명시한다. load order 또는 `Default.table`만으로 판단하지 않는다.
3. installer package의 atomic install/rollback behavior를 target production path를
   변경하지 않는 review environment에서 검증한다.
4. reviewer가 P-failure의 original owner 복귀 절차를 independent recovery channel로
   읽고 수행할 수 있을 때에만 G1을 재판정한다.

G1이 통과해도 replacement boot(R6)는 별도 명시 승인, exact one-mode G3 evidence,
그리고 R6 run sheet가 있어야 한다.

## 산출물 순서와 책임 경계

| 순서 | 산출물 | 생성 시점 | target 변경 |
| ---: | --- | --- | --- |
| 1 | `test/check-r0-original-driver-fingerprint.csh` | Work package A | 없음 (read-only 실행) |
| 2 | R0/R5 preflight result | A 완료 후 | 없음 |
| 3 | `docs/reports/R5_RECOVERY_REHEARSAL.md` (template: `R5_RECOVERY_REHEARSAL_TEMPLATE.md`) | operator-approved B 종료 후 | cold reboot만 |
| 4 | `docs/reports/R2_PHYSICAL_PROFILE_EVIDENCE.md` (template: `R2_PHYSICAL_PROFILE_EVIDENCE_TEMPLATE.md`) | C evidence 확보 후 | 없음 |
| 5 | `docs/reports/R3_MANUAL_MODE_TABLE_REVIEW.md` | G2 PASS 뒤 | host/offline only |
| 6 | recovery snapshot matrix | G2/G3/G4 PASS 뒤 | review only |
| 7 | R6 run sheet 및 minimal implementation | G1~G4 PASS와 별도 승인 뒤 | replacement-only boot에서만 |

## 다음 실행의 고정 우선순위

Work package A와 current-boot R5 preflight는 완료됐다. 다음 실행은 아래 순서다.

1. **R2.1 automatic-detection research를 기준으로 삼는다.** 완료된
   `R2_AUTODETECTION_RESEARCH.md`는 FreeBSD DRM이 capacity detector가 아니고,
   X.Org-style count가 current owner와 병행할 수 없는 VRAM write probe임을
   확정한다. target-original static audit도 original의 `MGACountRam`/BIOS
   selector path가 hardware-sensitive임을 보강했으며, 이 사실은 target action을
   승인하지 않는다.
2. **R2 physical inspection을 별도 run으로 실행한다.** R5 결과와 섞지 않고,
   power-off/AC-disconnected/ESD-safe 상태에서 B1~B4 marking evidence를 확보한 뒤
   B5/B6 exact documentary correlation을 작성한다.
3. **gate 결과를 판정한다.** R5는 `PASS`로 G4를 통과했다. R2 `PASS`는 G2만 통과시킨다.
   G2 `PASS` 뒤 R3의 단일 fixed-mode review를 열고, G2/G3/G4가 모두 `PASS`가 된
   뒤에만 R1 recovery-only snapshot review를 연다.

어느 단계도 replacement driver를 target에 install/load/probe하지 않는다. R6
replacement-only boot는 G1~G4와 별도 명시 승인이 있어야 하므로 이 작업 패키지의
다음 실행 대상이 아니다.

## 관련 문서

- 전체 R0~R7 gate: `RECOVERY_REPLACEMENT_DRIVER_EXECUTION_PLAN.md`
- original baseline: `reports/R0_BASELINE_AND_RECOVERY.md`
- R5 run form: `reports/R5_RECOVERY_REHEARSAL_RUN_SHEET.md`
- configuration snapshot contract: `R1_DRIVERLOADER_CONFIGURATION_MODEL.md`
- physical evidence form/candidate: `reports/R2_PHYSICAL_INSPECTION_RUN_SHEET.md`,
  `reports/R2_SUBSYSTEM_CANDIDATE_PROFILE.md`
- current verified facts: `TEST_STATUS.md`
