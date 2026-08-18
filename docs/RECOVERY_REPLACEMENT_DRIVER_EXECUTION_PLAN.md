# 다음 단계 — Recovery-Boot Replacement Display Driver 작업계획

기준일: 2026-08-18

## 목적과 실행 경계

이 문서는 기존 `MatroxMGA`와 병행 실행하지 않는 replacement display driver의
다음 작업을, 실제 hardware enable 이전까지의 순서로 고정한다. 목표는
`IOFrameBufferDisplay` subclass가 recovery-capable boot에서 **유일한** display
owner로 동작할 수 있는 최소 근거를 만드는 것이다.

이는 P3 3D acceleration을 여는 계획이 아니다. 현재 P3는 existing
scanout/cursor/hidden allocation과 mapping compatibility의 독립 증거가 없어
계속 차단된다. The operator-approved G450 16 MiB deployment cap closes the
conservative R3 design input only; it does not establish any live allocation.

이 문서는 작업계획만 정의한다. 아래 단계는 별도 실행 승인 전에는 target
configuration, `/private/Devices`, PCI mapping, mode register, DDC line을
변경하거나 접근하지 않는다.

현재 실행 가능한 세부 순서와 각 run의 abort 기준은
[`NEXT_STEP_EXECUTION_PLAN.md`](NEXT_STEP_EXECUTION_PLAN.md)에 고정한다. 그 문서는
R5 cold-boot recovery와 R2 physical evidence를 분리하고, 첫 구현 항목을
original-driver fingerprint comparator로 한정한다. 두 작업 모두 replacement
bundle의 target install/load/probe를 허용하지 않는다.

## H1 재프레이밍 (VGA 기준선) — 2026-08-18 operator

이 계획서는 원래 "`MatroxMGA` = known-good production owner"를 가정해 작성됐다.
`docs/H1_HARDWARE_INTERROGATION_DECISION.md`의 operator 결정으로 기준선이 바뀌었고,
아래 매핑이 이 문서 전체에 우선한다:

| 원래 표현 | H1 기준선에서의 의미 |
| --- | --- |
| known-good / original display owner | **generic SVGA (IOVGADisplay, VGA.config)** |
| "original driver boot profile" | **VGA가 display owner인 현재 부팅 설정** |
| owner 전환(교체 활성화) | System.config `Active Drivers`에서 `VGA`를 `OpenStepMGAReplacementDisplay`로 교체 + cold reboot |
| 복구 채널 | **단일 telnet(Pro1000 network) 생존** + NFS log 회수. 별도 physical/serial console 없음 |
| 복구 동작 | telnet으로 `Active Drivers`를 `VGA`로 되돌림 + cold reboot → VGA 화면 복귀 |

불변식은 그대로 유효하되 "old `MatroxMGA`" 자리에 "VGA display driver"를 대입해
읽는다. 즉 한 부팅 프로파일에서 display owner는 `VGA` 또는
`OpenStepMGAReplacementDisplay` 중 정확히 하나이며, 둘 다 `Active Drivers`에 동시에
두지 않는다. `MatroxMGA`는 이미 미로드/비소유이므로 후보가 아니다.

복구가 GUI가 아니라는 원문 조건은 유지된다: 복구는 telnet(원격) + NFS log +
known-good VGA 부팅 설정으로 구성한다. telnet이 유일 채널이므로 실패 시 로그를
최대한 남기는 것이 최선의 완화책이다.

## 공통 불변식

1. 실행 중인 `MatroxMGA`를 unload하여 replacement를 올리지 않는다. owner 전환은
   boot-time configuration selection과 cold reboot에서만 한다.
2. 하나의 boot profile에서는 old bundle 또는 replacement bundle 중 하나만 PCI
   function에 match할 수 있다. 둘 다 match하거나 둘 다 display owner가 되는
   configuration은 허용하지 않는다.
3. recovery 경로는 GUI가 아니라 independent console/serial 또는 원격 관리,
   NFS source/log 회수, known-good original-driver boot option으로 구성한다.
4. 먼저 evidence를 수집하고 나서 code를 연다. `16 MiB` configuration profile,
   `102b:0d43` board catalogue label, 또는 추측한 BAR 길이는 physical VRAM
   type/size의 증거가 아니다.
5. 실패 후에는 자동 mode probing, 반복 reset, existing-driver mapping 재사용을
   하지 않는다. log를 보존하고 known-good boot profile로 cold reboot한다.
6. release log에는 raw BAR address, framebuffer pointer, EDID serial number를
   쓰지 않는다. 식별이 필요하면 boot/profile/evidence ID를 사용한다.

## 단계 개요

| 단계 | 목적 | target 영향 | 다음 단계 허용 조건 |
| --- | --- | --- | --- |
| R0 | known-good 기준선과 recovery 경로를 고정 | read-only | original boot path를 독립적으로 복구 가능 |
| R1 | old/new bundle의 sole-owner configuration을 검토 | source/config review만 | 동일 PCI function의 동시 match가 없음 |
| R2 | 실제 card의 conservative physical profile을 확정 | read-only evidence만 | VRAM type/size·RAMDAC limit·board identity가 독립 출처로 일치 |
| R3 | manual fixed mode table을 offline 검토 | host/no-hardware | memory/pitch/pixel-clock 계산이 R2 profile 안에 있음 |
| R4 | fail-closed replacement skeleton을 build/review | build artifact만 | mapping/mode/DDC 없는 init/teardown lifecycle 검토 통과 |
| R5 | replacement boot 전 recovery rehearsal | boot selection만 | original display로 복귀하는 cold-boot 절차를 실제 확인 |
| R6 | 한 가지 conservative manual linear-mode smoke | replacement-only recovery boot | bounded 성공 또는 즉시 recovery가 재현 가능 |
| R7 | DDC와 P3 재판정 준비 | R6 뒤 별도 승인 | G5 및 P3 admission gate를 별도로 충족 |

## R0 — 기준선과 recovery 준비

### 수행 항목

- 현재 production boot의 `MatroxMGA` instance/configuration, selected display
  mode, module identity, P1 read-only inventory의 evidence ID를 하나의 기준선에
  묶는다.
- original driver가 선택되는 known-good boot profile의 이름·선택 방법·복귀 기준을
  문서화한다. replacement용 configuration을 이 profile에 덮어쓰지 않는다.
- independent console/serial 또는 원격 관리와 NFS log/source 회수 경로를 각각
  한 번씩 rehearsal한다. telnet은 임시 read-only fallback으로만 사용하며, 세션은
  명시적으로 logout한다.
- failure 발생 시 operator가 실행할 복구 순서(boot profile 선택 → cold reboot →
  original display 확인 → NFS log 회수)를 한 장으로 정리한다.

### 산출물과 gate

- 산출물: `docs/reports/R0_BASELINE_AND_RECOVERY.md` (`R0-20260818-A`)
- 기록 필수 항목: baseline ID, original boot profile ID, console/remote 확인 시각,
  NFS log 회수 결과, operator recovery result.
- 현재 상태: production owner/configuration/NFS의 read-only baseline은 재수집했다.
  original boot selection, independent recovery channel, cold-boot rehearsal은
  아직 미확인이다.
- 중단: original display boot 또는 independent recovery 중 하나라도 rehearsal에
  실패하면 R1 이후로 진행하지 않는다.

## R1 — sole-owner boot configuration review

### 수행 항목

- replacement bundle의 name, PCI matching rule, `Default.table`, install location을
  staging/recovery 전용으로 설계한다. production `/private/Devices`에 설치하지
  않는다.
- OPENSTEP의 `Default.table`과 `InstanceN.table` 역할을 구분한다. recovery
  profile은 Configure/`driverLoader`가 만든 configuration snapshot이며,
  `Default.table`만 복사·편집한 상태가 아니다.
- old `MatroxMGA` selection과 replacement selection을 profile별 표로 만든다.
  각 profile에서 해당 PCI function을 match할 수 있는 bundle은 정확히 하나여야
  한다.
- driver-loader가 bundle을 load하지 않아도 **matching candidate만 두 개**인
  configuration이 없는지 static review한다. load order의 우연에 의존하지 않는다.
- source/build artifact의 target load는 아직 하지 않는다. 이 단계의 판단은
  configuration text와 documented DriverKit matching semantics에 한정한다.

### 산출물과 gate

- 산출물: `docs/reports/R1_SOLE_OWNER_CONFIG_REVIEW.md`,
  `docs/R1_DRIVERLOADER_CONFIGURATION_MODEL.md`,
  `tools/check-r1-staging-isolation.sh`, `test/check-r1-staging-target.csh`
- 표의 최소 열: boot profile ID, enabled bundle, disabled/excluded bundle, PCI
  matching scope, expected display owner, reviewer, verdict.
- G1 통과: recovery profile과 original profile 모두에서 old/new 동시 match가
  불가능하다는 review evidence가 있고, R0 recovery rehearsal가 통과한 경우.
- 현재 상태: source-level staging isolation과 target production-directory
  artifact absence check는 통과했다. recovery-only profile review가 남아 있어
  G1은 미통과다.
- 중단: matching precedence가 불명확하거나 production profile을 덮어써야 하면
  configuration을 폐기하고 R0 상태로 돌아간다.

## R2 — physical board profile evidence

### 수행 항목

- 공개 hardware documentation 및 독립적인 read-only source로 card identity,
  physical VRAM type, physical VRAM total, RAMDAC/pixel-clock limit을 확인한다.
- source 하나가 configuration profile 또는 catalogue label을 재인용한 것인지
  확인한다. 같은 추정의 재서술 두 개는 independent evidence가 아니다.
- 값마다 source, applicability 조건, confidence, unresolved errata를 기록한다.
  G400/G450 family-level 정보와 현 카드에 실제로 적용되는 board-level 정보를
  구분한다.
- driver code는 physical profile이 확정될 때까지 BAR/VRAM mapping, engine/2D/3D
  command, PLL/CRTC/DAC programming을 포함하지 않는다.
- P1.4 capability-header probe는 VPD capability availability만 보조 evidence로
  기록할 수 있다. VPD contents read 또는 device configuration write는 R2/G2를
  우회하는 경로가 아니며 P1.4 범위 밖이다.

### 산출물과 gate

- 산출물: `docs/reports/R2_PHYSICAL_PROFILE_SOURCE_AUDIT.md`,
  `docs/reports/R2_PHYSICAL_INSPECTION_RUN_SHEET.md`,
  `docs/reports/R2_SUBSYSTEM_CANDIDATE_PROFILE.md`,
  `docs/R2_ORIGINAL_BINARY_CONFIGURATION_AUDIT.md`
- G2 통과 후 산출물: `docs/reports/R2_PHYSICAL_PROFILE_EVIDENCE.md`
- 최소 필드: PCI/subsystem identity, VRAM type, VRAM total, RAMDAC limit,
  primary source, independent cross-check, unresolved item, decision.
- G2 통과: VRAM type/size, RAMDAC limit, board identity가 현 card에 적용됨을
  독립 read-only evidence로 보일 수 있을 때만 통과다.
- 현재 상태: PCI G450 family의 16/32 MB DDR 및 360/230 MHz RAMDAC envelope은
  독립 공개 자료로 재확인했지만 target-specific physical profile은 선택할 수
  없어 G2는 미통과다. P1.4 standard capability header probe에도 VPD capability가
  없어 software-only VPD route는 닫혔다. subsystem/part-number candidate는 32 MB
  DDR PCI G450을 가리키지만 physical P/N/marking 대조 전에는 implementation input이
  아니다.
- 중단: 값이 서로 충돌하거나 physical measurement가 아닌 추정만 남으면
  `UNRESOLVED`로 기록한다. 더 큰 VRAM이나 더 높은 clock을 가정하지 않는다.

## R3 — conservative manual mode table의 offline 검토

### 수행 항목

- R2 profile에 맞춰 하나의 conservative fixed manual mode 후보만 우선 선정한다.
  EDID preferred mode나 automatic selection은 이 단계의 입력이 아니다.
- 각 mode에 대해 width, height, bpp, pitch, visible footprint, required linear
  mapping length, pixel clock, RAMDAC limit, profile memory margin을 계산한다.
- 기존 pure-C policy인 `OSMGAModeFitsLinearMemory`와
  `OSMGAValidateSurface32`를 검토 보조로 사용하되, 이 함수의 통과를 physical
  memory 증거로 해석하지 않는다.
- pitch alignment, overflow, palette/gamma 요구, unsupported interlace/doublescan
  여부를 명시적으로 확인한다. unknown timing/register requirement는 후보에서
  제외한다.
- current production 1600×1200×32 mode의 16 MiB profile lower-bound arithmetic은
  `docs/R3_CURRENT_MODE_FOOTPRINT.md`에 별도 기록한다. 이는 R2/G2를 통과시키지
  않으며 offscreen range를 만들지 않는다.

### 산출물과 gate

- 미래 산출물: `docs/reports/R3_MANUAL_MODE_TABLE_REVIEW.md`
- mode table의 최소 열: mode ID, geometry, bpp, pitch, footprint, pixel clock,
  R2 VRAM margin, R2 clock margin, allowed/rejected reason.
- G3 통과: 선택된 한 mode가 R2의 확정 memory/clock limit 안에 있고, 계산과
  rejection reason이 재현 가능할 때만 통과다.
- 중단: profile limit 또는 pitch/timing 값이 미확정이면 mode를 추가하지 않고 R2로
  되돌아간다.

## R4 — fail-closed replacement skeleton

### 수행 항목

- `IOFrameBufferDisplay` subclass의 buildable skeleton을 별도 staging source로
  만든다. 이름/PCI matching은 R1 review와 동일하게 유지한다.
- `initFromDeviceDescription:`은 `[super initFromDeviceDescription:]` 실패 시
  `[super free]`하고, profile/mode validation이 하나라도 실패하면 mapping 또는
  register write 없이 종료하도록 한다.
- `enterLinearMode`, `revertToVGAMode`, `free`는 lifecycle 순서와 idempotent
  teardown을 표현하되, R6 전에는 PCI/VRAM map, mode/PLL/CRTC/DAC write, DDC,
  engine submission을 호출하지 않는다.
- target compiler build와 host/static review는 가능하지만 target driver-loader에
  load하거나 automatic registration을 시도하지 않는다.
- local `ref/openstep/`의 `IOFrameBufferDisplay.h`, S3, QVision 예제는 DriverKit
  lifecycle/API 사용의 근거로만 사용한다. MGA register sequence나 binary driver
  code/data를 복사하지 않는다.

### 산출물과 gate

- 산출물: `OpenStepMGAReplacementDisplay/`, `docs/R4_SKELETON_REVIEW.md`,
  `tools/check-replacement-skeleton.sh`,
  `test/check-replacement-skeleton-imports.csh`
- static review 항목: ownership check, fail-closed init, no-map-before-validation,
  no-DDC-before-G5, no-engine-before-P3 gate, idempotent cleanup.
- 현재 상태: source gate, target i386 build, target `nm -u` allowlist review,
  target automatic import gate까지 통과했다. bundle은 target에
  install/load/probe하지 않았으므로 G1~G4 및 R5는 계속 미통과다.
- 중단: skeleton이 old `MatroxMGA` object, `Display0` pointer, existing mapping,
  runtime unload에 의존하면 source를 수정하기 전 R5로 진행하지 않는다.

## R5 — recovery boot rehearsal

### 수행 항목

- R0의 recovery 절차를 replacement bundle을 enable하지 않은 상태에서 다시
  rehearsal한다. 이 rehearsal은 boot-time owner 전환 직전의 복구 가능성만
  확인하며, driver load 시험이 아니다.
- operator가 original boot profile을 선택해 cold reboot한 뒤 display, console/
  remote, NFS log 회수가 모두 살아 있는지 확인한다.
- replacement boot는 별도 명시 승인 후에만 수행한다. 실행 전 fixed timeout,
  failure marker, original-profile rollback decision을 run sheet에 기입한다.

### 산출물과 gate

- 준비 산출물: `docs/reports/R5_RECOVERY_REHEARSAL_RUN_SHEET.md`
- 실행 후 산출물: `docs/reports/R5_RECOVERY_REHEARSAL.md`
- G4 통과: original driver boot path와 independent recovery가 cold-boot 기준으로
  실제 재현되었을 때만 통과다.
- 중단: 화면 손상, remote loss, NFS log 미회수, operator 절차 불명확 중 하나라도
  있으면 replacement boot를 시도하지 않는다.

### R5 직전의 고정 순서

R5를 실행하기 전에는 `NEXT_STEP_EXECUTION_PLAN.md`의 Work package A를 먼저
완료한다. 즉, target-native original fingerprint comparator를 만들어 현재 boot에서
통과시킨 뒤, original profile selection, independent recovery channel, timeout,
rollback 담당자를 기록한다. comparator mismatch 또는 preflight 불명확은 `ABORT`이며
cold reboot를 시작하지 않는다.

## R6 — replacement-only conservative linear-mode smoke

### 사전 조건

- G1, G2, G3, G4가 모두 통과했고, replacement-only recovery boot가 명시적으로
  승인되었다.
- old `MatroxMGA`는 해당 boot profile에서 PCI matching 후보가 아니며, selected
  mode는 R3의 정확히 한 conservative manual mode다.

### 수행 항목

1. replacement init은 immutable R2 profile과 R3 mode ID를 검증한다.
2. `enterLinearMode`는 bounded timeout을 둔 documented VGA-safe baseline 뒤
   R3에서 review된 sequence만 수행한다.
3. physical memory는 validated length로 한 번만 map하고, 그 mapping만
   `IODisplayInfo.frameBuffer`에 기록한다.
4. 성공은 fixed manual mode에서 stable display가 유지되고 lifecycle log가
   expected stage까지 도달한 경우다. Mesa, SDL2, DDC, 2D/3D engine은 연결하지
   않는다.
5. 어느 단계의 timeout/오류/화면 이상도 automatic retry 없이 failure marker를
   남기고 original profile cold reboot로 처리한다.

### 산출물과 gate

- 미래 산출물: `docs/reports/R6_LINEAR_SMOKE.md`
- 기록 필수 항목: boot profile ID, mode ID, profile evidence ID, validated map
  length, final lifecycle stage, timeout 여부, recovery result.
- G6 통과: 단일 manual mode의 entry/revert/recovery가 bounded 조건에서 재현되고
  old/new 동시 owner가 없다는 log와 R1 review가 일치할 때만 통과다.
- 중단: freeze, corruption, unexpected owner, mode mismatch는 모두 실패다. 다음
  mode 후보를 자동으로 시도하지 않는다.

## R7 — DDC와 P3의 별도 재판정

R6 통과 뒤에도 DDC와 P3은 독립 feature다.

- DDC는 G5가 별도로 통과한 뒤 replacement driver init의 one-time, base-block-only
  read로 제한한다. manual `Display Mode`는 언제나 EDID보다 우선하며, NACK/checksum
  error/unsupported mode에서는 R3 fallback을 유지한다.
- P3은 R6만으로 열리지 않는다. `OpenStepMGA` sidecar의
  `OSMGACanEnterP3`가 요구하는 physical VRAM size/type, existing owner의
  scanout/cursor/hidden allocation, mapping compatibility 증거를 모두 별도로
  충족해야 한다.
- P3 준비는 기존 `reference/` oracle과 `protocol/` command envelope의
  no-hardware regression을 유지하는 것부터 시작한다. raw VRAM address를 받는
  user API나 unbounded command submission은 추가하지 않는다.

## 실행 전 checklist와 즉시 중단 조건

실제 target 단계(R5 이후)를 시작하기 직전, 담당자는 아래를 모두 확인한다.

- [ ] R0 original boot path, console/remote, NFS log 회수 rehearsal이 기록되어 있다.
- [ ] R1 review가 현재 boot configuration과 일치하고, old/new 동시 match가 없다.
- [ ] R2 profile의 VRAM type/size와 RAMDAC limit이 추정이 아닌 증거로 확정됐다.
- [ ] R3의 하나의 manual mode가 memory/clock/pitch review를 통과했다.
- [ ] R4 skeleton은 map/mode/DDC/engine 동작이 없는 fail-closed 상태로 review됐다.
- [ ] cold reboot 및 original profile rollback에 필요한 operator 권한과 시간이 있다.

아래 중 하나라도 발생하면 그 run을 종료하고 known-good original profile로
복구한다: owner mismatch, unexpected bundle load, missing recovery channel, PCI/VRAM
mapping 실패, timeout, display corruption, NFS log 회수 실패, 또는 evidence ID와
실제 configuration의 불일치.

## 문서 연결

- lifecycle와 implementation contract: `D1_REPLACEMENT_DISPLAY_OWNERSHIP.md`
- DDC/manual-mode policy: `P0_DDC_EDID_FEASIBILITY.md`, `D0_EDID_PARSER_POLICY.md`
- VRAM conflict와 P3 blocker: `P1_SUBSYSTEM_MEMORY_RECONCILIATION.md`,
  `P2_RESOURCE_OWNERSHIP.md`
- P3 software-only policy/oracle/envelope: `D0_EDID_PARSER_POLICY.md`,
  `P3_REFERENCE_ORACLE.md`, `P3_COMMAND_ENVELOPE.md`
- current verified state: `TEST_STATUS.md`
