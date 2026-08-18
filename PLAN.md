# OpenStep Matrox MGA Remade — 구체 작업 계획

기준일: 2026-08-18

## 목표와 완료 정의

최종 목표는 PCI Matrox G400/G450에서 Mesa 3.4.2의 고정 기능 OpenGL 1.2
경로를 가속하고, SDL2의 표준 OpenGL window 사용이 OPENSTEP에서 동작하게
하는 것이다. OPENSTEP은 X11이 아니므로 Linux DRI/DRM ABI를 이식하지 않고,
동등한 책임을 지는 `OpenStepMGA` kernel service와 Mesa backend를 만든다.

최소 완료 기준은 다음 다섯 가지다.

1. 기존 `MatroxMGA` 화면 드라이버와 공존한 상태에서 card reset이나 mode
   변경 없이 offscreen VRAM을 안전하게 사용한다.
2. OpenStepMGA가 acquire/release, submit, fence를 직렬화한다.
3. clear, triangle, depth, texture, blend 결과를 readback으로 검증한다.
4. Mesa 3.4.2가 미지원 operation을 software fallback으로 처리한다.
5. SDL2 OpenGL smoke test가 AppKit window 안에 올바른 결과를 표시한다.

이 완료 기준은 "G400/G450이 OpenGL 1.2를 하드웨어로 전부 처리한다"는
주장이 아니다. 검증한 primitive/state만 hardware path로 보내며, 나머지는
Mesa fallback을 유지한다.

## 비목표와 금지 사항

- AGP/GART, modern Mesa, LLVM, Gallium, shader API.
- Linux `/dev/dri` ioctl 및 X11 DRI/GLX ABI 재현.
- G450 dual-head, TV-out, overlay, DVI, display mode 전환.
- 기존 `MatroxMGA` bundle의 patch, unload, binary link, binary 재배포.
- 검증 전 front buffer, DAC, PLL, CRTC, reset register write.
- 검증 전 여러 process/context가 device register에 접근하는 것.

DDC/EDID는 위 비목표의 예외가 아니다. 현 `MatroxMGA`와 공존하는 3D sidecar가
DAC GPIO를 만지는 것은 금지한다. DDC는 향후 replacement display driver가
device를 단독 소유할 때의 boot-time optional input이며, 상세 판단은
`docs/P0_DDC_EDID_FEASIBILITY.md`를 따른다.

현재 sidecar P3은 VRAM type/size, existing scanout/cursor/hidden allocation,
mapping compatibility 증거가 없어 열리지 않는다. 따라서 다음 실제 driver 단계는
P3을 우회하는 것이 아니라 recovery boot에서 sole-owner replacement display
driver의 안전성을 검증하는 것이다. 상세 runbook, 각 단계의 산출물, failure
rollback은 `docs/RECOVERY_REPLACEMENT_DRIVER_EXECUTION_PLAN.md`를 따른다.

## 저장소 구조

초기에는 문서만 존재한다. 코드 착수 시 아래 구조를 사용한다.

```
OpenStepMGAProbe/                 # P1, read-only LKS
OpenStepMGAProbe_reloc.tproj/
OpenStepMGAService/               # P2-P3, 3D sidecar LKS
OpenStepMGAService_reloc.tproj/
libopenstepmga/                   # Mesa가 사용하는 user-side client library
mesa/                             # Mesa 3.4.2 backend patch, 별도 관리
test/                             # host build scripts + target smoke programs
docs/                             # 실행 기록, register ownership, test reports
refs/                             # 공개 자료 목록만; 원본 binary/source mirror 금지
```

각 `.config` bundle은 OPENSTEP 정석의 `Default.table`, `Load_Commands.sect`,
`*_reloc.tproj` 구조를 사용한다. `OpenStepMGAService`는 P2 이전에는
`Default.table`에 Matrox PCI 자동 감지 ID를 넣지 않는다.

## 공통 안전 절차

모든 실기 작업은 아래 순서를 따른다.

1. 현재 `MatroxMGA`의 Instance table, display mode, `kl_util -s` 상태를
   읽어 실행 기록에 저장한다.
2. NFS kernel log 수집을 먼저 시작한다.
3. 한 번의 load에서는 기능 하나만 시험한다. command-line 인자 또는
   compile-time 단계 번호로 시험 대상을 선택한다.
4. MMIO write를 추가하기 전, 동일 주소의 read와 대상 chip variant를 별도
   로그로 검증한다.
5. hang, 화면 이상, telnet/NFS 이상이 하나라도 있으면 다음 단계로 가지
   않는다. 재부팅 뒤 마지막 known-good build로 되돌린다.
6. 새 bundle은 검증 전 `driverLoader` 자동 구성에 등록하지 않는다.

원격 접속은 DEC Tulip `en0`가 유지되는 현재 구성에서만 수행한다. 화면이
손상돼도 telnet/NFS로 기록을 회수할 수 있어야 한다.

## P0 — 분석 기준선과 구현 명세

### P0.1 — 실기 inventory 고정

산출물: `docs/P0_TARGET_INVENTORY.md`

- PCI config 64-byte header, BAR0/1/2, IRQ, command register를 재수집한다.
- `MatroxMGA.config/Instance0.table`, mode table 이름, display mode,
  module load address/size를 기록한다.
- VRAM 총량, front-buffer stride/height/bytes를 계산하되, 남은 VRAM을
  offscreen 영역으로 가정하지 않는다.

완료 기준: 동일 부팅 세션의 장치·display 정보가 문서와 kernel log에
상호 일치한다.

### P0.2 — 기존 binary의 clean-room 관찰 명세

산출물: `docs/P0_BINARY_BEHAVIOUR.md`

- 공개 배포본의 hash, Objective-C class/selector, DriverKit import만 기록한다.
- mode setup, linear framebuffer setup, 2D engine initialization, G450 PLL
  관련 동작을 "입력/출력/소유 register class" 수준으로 요약한다.
- 디스어셈블, pseudocode, 주소별 명령열, 원본 문자열의 대량 복사는 기록하지
  않는다.

완료 기준: 새 구현자가 원본 binary 없이도 display 호환성의 필요 조건과
금지 register를 이해할 수 있다.

### P0.3 — 공개 참고 구현 분해와 라이선스 표

산출물: `docs/P0_REFERENCE_MATRIX.md`

| 자료 | 사용할 정보 | 구현 반영 전 조건 |
| --- | --- | --- |
| Xorg MGA | PCI identify, mode/2D/3D state 의미 | 파일별 license 확인 |
| FreeBSD legacy DRM | lock, buffer, IRQ, MGA state 분리 | FreeBSD API가 아닌 설계만 채택 |
| Linux MGA DRM/fbdev | register behaviour, hardware errata | GPL code는 복사하지 않음 |
| MatroxMGA binary | OPENSTEP display 동작 관찰 | code/data를 구현에 사용하지 않음 |

완료 기준: 각 새 source file은 참조 출처와 license 판정이 문서에 연결된다.

### P0.4 — 실기 도구 체계 확정

산출물: `docs/P0_TOOLING.md`

- `tools/nxrun.sh`: telnet/csh 기반의 read-only query와 kernel loader 제어.
- `tools/nx.sh`: gcdsd 경유 build output, GUI smoke test, 파일 회수.
- `tools/nx-mount.sh`: 수정된 gnfsd를 사용한 `/ndrv` NFS mount 및 cache refresh.
- `tools/nx-logcatch.sh`: NFS에 fsync하는 kernel log 수집. 모든 LKS load 전에 시작.
- `tools/nx-install-driver.sh`: target `/tmp` build와 bundle byte-size 검증 뒤
  `/private/Devices`에 설치. P1 build가 생긴 뒤에만 사용.

완료 기준: tool의 역할과 사용 시점, GCD/NFS 장애 시 telnet fallback이 문서에
명시된다.

### P0.5 — OPENSTEP 원전 문서와 공식 예제 대조

산출물: `docs/P0_OPENSTEP_DOCUMENTS.md`

- 로컬 `ref/openstep/`의 NextDev LKS 문서, DriverKit headers, 공식 S3,
  QVision, Cirrus, AMD PCI 예제를 1차 설계 근거로 기록한다.
- 실기의 `/NextLibrary/Documentation/NextDev` 및 `/NextDeveloper` 설치본에
  같은 문서·예제가 존재하는지 read-only로 확인한다. 로컬 mirror만 보고 API를
  추정하지 않는다.
- `IOFrameBufferDisplay`가 scanout/mode/cursor owner용 추상 클래스임을
  확인하고, 기존 `MatroxMGA`와 공존하는 sidecar가 그 lifecycle 또는
  framebuffer mapping API를 호출하지 않도록 경계를 고정한다.
- message-based LKS에는 NeXT 문서의 MiG/`SMAP` 또는 `HMAP` 규칙을 사용한다.
  out-of-line message data는 `vm_write()`/`vm_read()` 규칙을 따르며,
  `copyin()`/`copyout()`을 사용하지 않는다.
- `IODirectDevice`/`IOPCIDirectDevice`는 해당 PCI device를 정식 claim한
  display driver의 API다. sidecar P1의 generic config read와 P2 이후의
  ownership 설계를 혼동하지 않는다.

완료 기준: 문서 근거, runtime 관찰, 공개 구현 참고를 구분한 evidence matrix가
있고, P2 protocol이 그 문서 규칙과 모순되지 않는다.

### P0 gate

P1은 P0.1~P0.5 문서가 완성되고, 현재 `MatroxMGA` driver의 backup/recovery
절차가 확인된 경우에만 시작한다. P1 및 독립 `pcils` cross-check는 통과했고,
P0.5 protocol 설계도 완료됐다. 따라서 no-hardware P2 control-plane skeleton은
시작할 수 있다. BAR mapping, VRAM/MMIO, DMA/IRQ는 P2가 끝나도 시작하지 않는다.

## P1 — Read-only Probe LKS

산출물: `OpenStepMGAProbe.config`, `test/run-probe.csh`,
`docs/P1_PROBE_REPORT.md`

### 구현

- `Load_Commands.sect`의 `CALL`로 시작하는 LKS를 만든다.
- PCI config mechanism으로 `102b:0525`를 찾되, DriverKit device matching과
  automatic instance registration은 사용하지 않는다.
- PCI config header와 BAR type/base만 읽는다.
- BAR mapping은 별도 build flag로 분리한다. 처음 build는 mapping도 하지
  않는다.
- map build도 읽기 전용이며, mode/DAC/PLL/engine/front-buffer register에
  접근하지 않는다.

### 검증

| 시험 | 성공 조건 | 실패 시 조치 |
| --- | --- | --- |
| config read | card ID/BAR/IRQ가 inventory와 일치 | read sequence 중단 |
| load/reload 10회 | 화면·telnet·NFS 유지 | bundle 자동 등록 금지 유지 |
| BAR map | mapping result와 virtual address만 기록 | 즉시 unmap, 다음 단계 중단 |

### P1 gate

read-only probe가 반복 성공하고, 기존 display가 모든 시험 후 정상일 때만
P2의 자원 설계를 진행한다. P1에서는 GPU command를 전혀 제출하지 않는다.

#### P1.2 — existing display metadata query 완료 기록

`IODeviceMaster`의 documented read-only getter를 사용해 public display object
`Display0`와 current mode index/count를 확인했다. `IOGetDisplayMemory`와
`IOGetRAMDACSpeed`는 zero를 반환해 existing driver가 runtime metadata를
공개하지 않음을 확인했다. 이 결과는 offscreen range나 mapping 권한을 주지
않으며, 상세는 `docs/P1_DRIVERKIT_DISPLAY_QUERY.md`에 있다.

#### P1.3 — subsystem/VRAM evidence reconciliation 완료 기록

P1 PCI header의 subsystem `102b:0d43`은 공개 pciutils catalogue에서 32 Mb
G450 dual-head PCI board로 표기되지만, existing `MatroxMGA` configuration은
16 MiB profile을 선택한다. 이 둘은 board catalogue와 driver configuration이지
physical memory measurement가 아니며, P1.2 getter도 zero로 uninformative하다.
따라서 physical VRAM type/size gate는 **미통과**다. 이 충돌을 16 MiB 또는
32 MiB로 임의 해소하지 않으며, P3의 BAR mapping/VRAM access/offscreen allocator는
계속 금지한다. 상세 evidence와 후속 경로는
`docs/P1_SUBSYSTEM_MEMORY_RECONCILIATION.md`에 있다.

## P2 — Resource Ownership과 User/Kernel Protocol

산출물: `docs/P2_RESOURCE_OWNERSHIP.md`, `docs/P2_PROTOCOL.md`,
`OpenStepMGAService.config`의 no-submit skeleton, `libopenstepmga` headers.

### P2.1 — VRAM layout 판정

- scanout base, stride, visible height, cursor/reserved region을 구분한다.
- 기존 display driver가 사용하는 범위를 문서나 안전한 관찰로 확인할 수
  없으면 offscreen VRAM 사용을 승인하지 않는다.
- 안전한 offscreen range가 확정될 때까지 P3는 system-memory readback 또는
  별도 시험 카드만 사용한다.

### P2.2 — 단일 소유권 protocol

초기 user API는 아래 다섯 명령만 둔다.

| 명령 | 역할 | 초기 제한 |
| --- | --- | --- |
| `QueryCapabilities` | variant, VRAM, enabled features | read-only |
| `Acquire` | 단일 owner token 획득 | 한 process/context |
| `Submit` | 검증된 command packet 제출 | fixed-size, kernel copy |
| `WaitFence` | polling 기반 완료 대기 | timeout 필수 |
| `Release` | idle 확인 후 owner 반납 | outstanding work 없음 |

P2는 UNIX character-device interface를 새로 만들지 않고, MiG-generated
message-based LKS를 우선 사용한다. load command의 `SMAP`(server interface) 또는
`HMAP`(handler interface)은 `.defs`/MiG 산출물과 함께 정한다. 모든 fixed-size
인라인 request는 MiG가 전달한 kernel-side message data만 해석한다. 큰 command
buffer가 필요해진 경우에는 out-of-line data를 raw pointer처럼 읽지 않고 NeXT
문서의 `vm_write()`/`vm_read()` page-mapping 규칙과 page alignment 요구를 적용한다.
message-based server에서 `copyin()`/`copyout()`은 사용하지 않는다.

register address·length·VRAM range·state transition은 kernel에서 검증하며, raw
MMIO mapping은 user process에 제공하지 않는다. 기존 `MatroxMGA`가 scanout을
소유하는 동안 `IOFrameBufferDisplay`의 mode/linear-framebuffer lifecycle도
sidecar가 호출하지 않는다.

DDC/EDID reader는 P2 protocol이나 Mesa client library에 넣지 않는다. DDC2B
bus 구동은 DAC GPIO write를 포함하므로, 기존 display driver가 적재된 상태에서
sidecar가 수행할 수 없다. monitor auto-selection은 replacement display-driver
track의 D0~D3 gate를 통과한 뒤에만 다룬다. 사용자가 `Display Mode`를 명시한
경우에는 항상 그것을 우선하고, DDC 실패는 known-good fixed mode table의
fallback으로 끝나야 한다.

#### P2.0~P2.6 완료 기록

`protocol_info` 단일 scalar routine의 MiG `.defs`, `SMAP`/`ADVERTISE`, lazy
load, target-native user stub, unload/delete cycle은 통과했다.
P2.1은 software single lease와 stale/busy rejection, P2.2는 client control
port death 후 automatic lease recovery, P2.3은 explicit capability reply
(`features`, `max_leases=1`, `hardware_ready=0`)를 target에서 통과했다.
P2.4는 두 독립 retrying client가 각각 1,000 successful pair를 수행하는
contention stress를 target에서 통과했다. P2.5는 raw short-message와 raw
wrong-type message가 `MIG_BAD_ARGUMENTS`로 거부되고 정상 query가 복구되는
negative test를 target에서 통과했다. P2.6은 위 control-plane test를 하나의
target-native build/load/run/cleanup lifecycle로 재현해 통과했다.
P2.7은 host-side source/table/load-command static guard를 추가해 P2 bundle에
hardware API, device binding, resource table 또는 `START`/`WIRE`가 들어가면
target build 전에 거부한다. 이 guard는 admission 근거가 아니라 P2 범위 보존
장치이며, `docs/P2_STATIC_SAFETY_GATE.md`에 기록한다.
P2.8은 target-native relocatable의 undefined import table을 `nm -u`로 검사해
mapping/PCI/framebuffer/DMA/direct-I/O helper가 없음을 service load 전에
확인한다. 이 guard도 ownership 또는 P3 admission 근거가 아니며,
`docs/P2_BINARY_IMPORT_GATE.md`에 기록한다.
P2.9는 NFS source를 exact temporary directory에 clean-copy/build한 뒤 P2.8과
control-plane suite를 실행하고 cleanup하는 reproducibility runner다. 이 runner는
`/private/Devices`에 설치하지 않으며 MGA hardware API를 호출하지 않는다. target
clean build와 strict import allowlist를 포함한 suite가 통과했다.
각 실행 근거는 `docs/P2_P20_PROTOCOL_REPORT.md`,
`docs/P2_P21_LEASE_REPORT.md`, `docs/P2_P22_PORT_DEATH_REPORT.md`,
`docs/P2_P23_CAPABILITIES_REPORT.md`,
`docs/P2_P24_MULTI_CLIENT_STRESS_REPORT.md`,
`docs/P2_P25_RAW_MIG_NEGATIVE_REPORT.md`,
`docs/P2_P26_CONTROL_REGRESSION_REPORT.md`에 있다. `Submit`은 계속 disabled 상태다.

### P2 gate

`Acquire`/`Release`를 1,000회 반복해도 kernel log warning, leaked owner,
screen 이상이 없어야 한다. command submit은 아직 disabled 상태다.

## P3 — 최소 하드웨어 3D 경로

산출물: `test/mga_clear`, `test/mga_triangle`, `test/mga_depth`,
`test/mga_texture`, `test/mga_blend`, 각각의 target report.

### 순서

1. engine idle 확인과 timeout recovery 경로를 만든다.
2. safe offscreen range에 color clear를 수행하고 readback checksum을 비교한다.
3. 한 triangle을 rasterize하고 reference software image와 비교한다.
4. depth test, texture sampling, blending을 각각 분리해 검증한다.
5. frame마다 fence 완료와 device-idle timeout을 기록한다.

P3.2 clear의 expected pixels/checksum/mismatch oracle은
`reference/OpenStepMGAReference.c`와 `docs/P3_REFERENCE_ORACLE.md`에 먼저
구현한다. 이 source는 hardware address나 DriverKit API를 포함하지 않으며,
P3 admission 전에는 target driver와 연결하지 않는다.

P3 clear/triangle command의 raw-address-free geometry validation은
`protocol/OpenStepMGACommand.c`와 `docs/P3_COMMAND_ENVELOPE.md`에 먼저
구현한다. P3 admission 전에는 P2 MiG ABI나 service implementation에 link하지
않으며, synthetic allocation vector를 target hardware evidence로 사용하지 않는다.

### 초기 전송 방식

- PIO command submission과 polling fence만 사용한다.
- DMA buffer, interrupt/vblank, command queue는 P3에서 사용하지 않는다.
- timeout은 busy-loop가 아니라 bounded polling으로 구현하고, timeout 뒤에는
  새 command를 금지한 채 상태를 기록한다.

### P3 gate

각 test를 cold boot 후와 연속 100회 실행에서 통과해야 한다. 실패 시
front-buffer/mode/DAC를 건드리는 recovery를 시도하지 않고 재부팅으로
복구한다.

## P4 — Mesa 3.4.2 통합

산출물: Mesa backend patch, `libopenstepmga`, `docs/P4_GL_MATRIX.md`,
SDL/Mesa smoke test reports.

### 구현 단위

1. OpenStepMGA screen/context/drawable lifecycle.
2. color/depth/stencil buffer allocation과 software fallback boundary.
3. fixed-function state translation: viewport, scissor, blend, depth/stencil,
   texture environment, fog, raster state.
4. primitive path: points/lines/triangles의 검증된 subset부터 연결.
5. `glReadPixels`, swap/present는 CPU readback 후 SDL/AppKit 경로를 쓴다.

### GL 공개 정책

- `GL_VERSION`과 extension string은 시험 matrix를 통과한 기능만 공개한다.
- unimplemented state/primitive은 error가 아닌 Mesa software path로 간다.
- OpenGL 1.2 compatibility는 전체 conformance와 SDL/Mesa regression 이후에만
  release claim으로 쓴다.

### 검증

- Mesa OSMesa reference image와 MGA readback image를 비교한다.
- `MesaView`, SDL GL cube, texture/depth/blend 전용 demo를 실행한다.
- 장시간 실행 뒤 existing AppKit window, text rendering, mode가 정상인지
  확인한다.

## P5 — 성능과 고급 기능

P4가 안정된 뒤에만 다음을 별도 milestone으로 진행한다.

- PCI bus-master DMA command buffer.
- IRQ 또는 vblank 기반 fence.
- multiple context serialization과 dead-client reclaim.
- VRAM allocator와 texture residency.
- WindowServer와 직접 drawable을 연동할 수 있는지 조사.
- 기존 display driver를 clean-room replacement 할지 별도 의사결정.

P5의 어떤 항목도 P4의 기능성 release를 막는 선행 조건이 아니다.

## 중단 조건

다음 중 하나가 발생하면 해당 milestone을 중단하고 직전 gate로 돌아간다.

- 기존 화면 출력 손상 또는 mode 복구 실패.
- kernel panic, bus hang, telnet/NFS 접근 손실.
- 기존 `MatroxMGA`와 VRAM/register ownership이 충돌한다는 증거.
- 필요한 공개 사양 또는 재배포 권한을 확보할 수 없음.
- Mesa fallback과 비교해 hardware path가 기능상 동일하지 않음.
