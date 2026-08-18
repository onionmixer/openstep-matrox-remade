# D1 — Replacement Display Driver Ownership Design

기준일: 2026-08-18

## 결정

DDC/EDID와 mode programming은 `OpenStepMGAService` sidecar에 넣지 않는다.
현재 `MatroxMGA`가 scanout을 소유한 상태에서 같은 PCI function, DAC/CRTC,
framebuffer를 두 번째 driver가 관찰하거나 설정하는 경로는 만들지 않는다.

후속 구현의 유일한 허용 형태는 별도 recovery-capable boot에서 기존
`MatroxMGA`를 대체하는 `IOFrameBufferDisplay` subclass다. 이 문서는 그
replacement bundle의 설계 계약이며, 현재 card에서 compile/load/run하라는
작업 지시가 아니다.

## 1차 근거와 적용 범위

| 원전 | 확인된 계약 | D1 적용 |
| --- | --- | --- |
| `driverkit/IOFrameBufferDisplay.h` | subclass가 `enterLinearMode`, `revertToVGAMode`, `selectMode`, framebuffer mapping, `displayMemorySize`, `ramdacSpeed`를 구현/제공 | 하나의 display bundle이 전체 lifecycle을 소유 |
| 공식 `S3` 예제 | init에서 configuration/mode를 결정하고 framebuffer map; `enterLinearMode`에서 selected mode와 linear framebuffer를 활성화 | validation 완료 전에는 mapping 또는 mode write 금지 |
| 공식 `QVision` 예제 | init 실패 시 `super free`; `free`에서 own mapping 해제; superclass가 mode 전환 전에 VGA revert를 호출 | partial-init rollback과 deterministic teardown 필요 |
| `P2_RESOURCE_OWNERSHIP.md` | 현재 `MatroxMGA`가 PCI/display/scanout owner, VRAM range 미확정 | P2/P3 sidecar와 D1 replacement를 절대 혼합하지 않음 |

S3/QVision은 API/lifecycle 사용법의 근거일 뿐 register sequence나 mode table을
복사하는 source가 아니다. MGA register/DAC/PLL/CRTC programming은 별도의
공개 hardware specification과 clean-room implementation review가 필요하다.

## 운영 상태와 금지 전이

| 상태 | `MatroxMGA` | 새 replacement bundle | 허용 작업 |
| --- | --- | --- | --- |
| S0 현재 production | loaded, screen owner | absent | P0/P2/D0의 read-only/no-hardware 작업만 |
| S1 offline build | target에서 load하지 않음 | source/build artifact만 | compile, source/static review |
| S2 recovery boot preflight | screen owner가 아니어야 함 | 아직 device claim 전 | serial/NFS recovery와 config selection 확인 |
| S3 replacement init | loaded되지 않아야 함 | sole candidate | documented init validation만 |
| S4 linear display | loaded되지 않아야 함 | sole scanout owner | 검증된 mode/framebuffer lifecycle |
| S5 failure recovery | loaded되지 않아야 함 | unload 또는 reboot | VGA-safe revert 또는 reboot, sidecar fallback 금지 |

S0에서 S3/S4로 직접 전이하는 것은 금지한다. `MatroxMGA`를 runtime에 unload해
새 driver를 올리는 방법도 금지한다. 새 driver가 실패해 현재 화면을 잃는 경우를
대비해 boot-time driver selection과 독립 console/remote recovery를 먼저 준비해야
한다.

## D1 lifecycle contract

### A. build 및 boot admission

1. bundle name, PCI matching table, `Default.table`은 staging/recovery 환경에만
   둔다. production `/private/Devices`에 설치하지 않는다.
2. target config는 같은 PCI function에 기존 `MatroxMGA`와 replacement가 동시에
   match하지 않음을 review로 증명한다.
3. serial 또는 물리 console, NFS source, known-good original driver boot option,
   그리고 cold reboot 권한을 준비한다.
4. current physical VRAM type/size와 supported board identity를 independent
   read-only evidence로 확정하기 전에는 PCI memory map, framebuffer map,
   engine/2D/3D command code를 포함하지 않는다.

### B. `initFromDeviceDescription:`

1. `[super initFromDeviceDescription:]` 실패 시 즉시 `[super free]`.
2. new bundle의 device description만 whitelist와 대조한다. current `MatroxMGA`
   object나 `Display0` user RPC를 조회하지 않는다.
3. board profile, physical VRAM size/type, RAMDAC clock limits, mode table의
   immutable configuration을 검증한다. 하나라도 모르면 fail closed 한다.
4. `selectMode`에 **manual Display Mode 우선** table을 전달한다. D0 EDID output은
   D3 전까지 입력하지 않는다.
5. selected `IODisplayInfo`가 profile memory/clock limits를 넘지 않는지 확인한다.
6. 이 시점에서 실패하면 map 또는 register write 없이 `[super free]`로 끝낸다.

`init`은 PCI BAR/VRAM address가 있다는 사실만으로 mapping하지 않는다. S3 예제의
map 순서는 new display driver가 이미 sole owner라는 전제이므로 S0 sidecar에는
재사용할 수 없다.

### C. `enterLinearMode`

`IOFrameBufferDisplay` documentation은 mode switch 전에 superclass가
`revertToVGAMode`를 호출한다고 명시한다. 따라서 replacement 구현은 다음의
one-way transaction으로 제한한다.

1. pending mode/profile을 다시 검증한다.
2. bounded timeout을 가진 VGA-safe baseline/reset 절차를 수행한다.
3. mode timing, PLL/DAC, CRTC, framebuffer pitch를 지정된 순서로 programming한다.
4. physical memory와 validated length를 정확히 한 번 map하고, returned pointer를
   `IODisplayInfo.frameBuffer`에 기록한다.
5. linear framebuffer enable과 palette/gamma state를 완료한다.
6. 어느 단계든 실패하면 new mode를 계속 사용하지 않고 `revertToVGAMode` 또는
   reboot recovery 상태로 간다. 재시도 loop, old-driver reuse, broad BAR probing은
   금지한다.

3~5는 future hardware implementation 단계이며 현재 source에 존재하지 않는다.
특히 VRAM length는 `MGA Memory Size=16` configuration 값이나 PCI board catalogue
label에서 추론할 수 없다.

### D. `revertToVGAMode` 및 `free`

- `revertToVGAMode`는 linear state를 해제하고 documented VGA-safe baseline만
  복원한 뒤 `[super revertToVGAMode]`를 호출한다.
- `free`는 **new bundle이 만든 mapping만** unmap한다. existing `MatroxMGA`의
  mapping, `Display0` pointer, WindowServer cursor area에는 접근하지 않는다.
- init이 mapping 전 실패한 경우에도 teardown이 idempotent해야 한다.
- hardware timeout 또는 screen corruption 뒤에는 automatic mode probing을 하지
  않는다. log marker를 남기고 reboot recovery를 요구한다.

## DDC/EDID와 수동 mode의 연결점

D0 `OSMGAParseBaseEDID`와 `OSMGASelectDisplayMode`는 userspace-independent pure C
policy다. D1/D2/D3에서는 다음 우선순서를 바꾸지 않는다.

1. config의 exact manual `Display Mode`가 fixed, validated table entry와 일치하면
   반드시 그것을 선택한다.
2. manual 값이 없을 때만 valid EDID base block의 preferred DTD를 동일 table과
   intersect한다.
3. DDC NACK, checksum error, unsupported/interlaced preferred mode는 error screen
   이 아니라 profile의 conservative fallback을 선택한다.

DDC electrical transaction은 D2에서 replacement driver의 one-time init window에
한정한다. sidecar, P2 MiG service, SDL2/Mesa client에는 raw EDID 또는 I2C API를
노출하지 않는다.

## 구현을 열기 위한 증거 gate

| gate | 필요한 증거 | 현재 상태 |
| --- | --- | --- |
| G1 sole ownership | recovery boot에서 old/new bundle 동시 match가 없다는 config review | 미시작 |
| G2 physical board profile | VRAM type/size, RAMDAC limit, board identity의 independent read-only evidence | 미통과 |
| G3 mode table | pixel clock/pitch/memory requirement가 G2 limits 내임을 offline 계산·review | 미시작 |
| G4 recovery | original driver boot path 및 serial/remote recovery cold-boot rehearsal | 미시작 |
| G5 DDC safety | D2 address ACK/base-block read가 no-mode-write invariant를 지킴 | 미시작 |
| G6 linear smoke | replacement-only boot에서 one conservative manual mode가 stable | 미시작 |

G1~G4 전에는 D1 source skeleton도 target에 load하지 않는다. G5 전에는 DDC line
access를 넣지 않는다. G6 전에는 Mesa/P3 acceleration, SDL2 presentation 또는
automatic EDID mode selection을 연결하지 않는다.

## 검증 기록 형식

향후 각 run은 다음을 한 줄씩 기록한다: boot profile ID, selected manual/EDID
decision, fixed-mode ID, VRAM limit evidence ID, map length, mode-entry stage,
timeout 여부, teardown/reboot result. raw BAR address, EDID serial number,
framebuffer pointer는 release log에 기록하지 않는다.

이 설계는 P3 admission을 열지 않는다. 그것은 `P2_RESOURCE_OWNERSHIP.md`의
physical-memory와 existing-owner gate를 모두 충족한 뒤, 별도의 recovery hardware
environment에서만 재판정한다.

실제 replacement driver 작업의 단계·산출물·중단 조건은
`RECOVERY_REPLACEMENT_DRIVER_EXECUTION_PLAN.md`에 고정한다. 그 계획도 G1~G4
전에는 target load나 hardware access를 허용하지 않는다.
