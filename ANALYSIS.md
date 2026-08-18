# 초기 분석 결과

기준일: 2026-08-18

이 문서는 공개 배포된 `MatroxMGA-1.0.tgz`와 실기에서 읽은 구성 정보를
바탕으로 한다. 원본 바이너리, 디스어셈블 결과, 복원된 소스는 이 프로젝트에
저장하지 않는다.

## 실기 장치와 기존 소유자

`pcils`의 과거 기록과 P1의 현재 부팅 read-only probe 결과:

| 항목 | 값 |
| --- | --- |
| PCI 위치 | `04:00.0` |
| PCI ID | `102b:0525` (MGA G400/G450 계열) |
| subsystem | `102b:0d43` |
| PCI revision | `0x85` |
| IRQ | 11, pin A |
| command | `0x0007` — I/O, memory, bus master 활성 |
| BAR0 | `0xf8000000`, prefetchable VRAM aperture |
| BAR1 | 과거 `0xe8300000`; P1 현재 부팅 raw 값은 `0xe8200000` |
| BAR2 | `0xe8800000`, non-prefetchable memory |
| 현재 display driver | `MatroxMGA` v1.0, `IOFrameBufferDisplay` 계열 |
| 현재 설정 | G400 16MB table, 1600x1200, RGB:888/32 (물리 VRAM 측정값 아님) |

P1은 현재 부팅에서 `f8000008 e8200000 e8800000`을 기록했다. BAR1의 1 MiB
차이는 PCI resource 재배치인지 과거 scan 오류인지 아직 판정하지 못했으므로,
이 표의 어느 BAR도 mapping 기준으로 사용하지 않는다. 정확한 원자료와 금지
경계는 `docs/P0_TARGET_INVENTORY.md`를 기준으로 한다.

기존 DriverKit 번들은 커널에 적재되어 있으며 framebuffer/MMIO를 소유한다.
따라서 별도 드라이버가 동일 장치를 자동 감지하여 병렬 probe하는 것은
금지한다. screen mode 설정, reset, DAC/PLL programming도 초기 단계에서
금지한다.

### 대상 variant의 잠정 판정: PCI G450

기존 Instance table의 `G400` 이름은 실제 chip variant의 확정 근거가 아니다.
FreeBSD stable/12 legacy `mga_drv.c`는 `102b:0525`가 HiNT `3388:0021`
PCI-to-PCI bridge 뒤에 있으면 이를 PCI G450로 판정한다. 실기의 topology는
정확히 이 조건과 일치한다 (`03:0d.0` bridge → `04:00.0` MGA).

따라서 이후 구현과 시험은 이 카드를 **PCI G450로 취급**한다. read-only
probe가 chip revision/BIOS 정보를 독립적으로 확인하기 전까지는 이를 잠정
판정으로 유지한다. 이 사실은 FreeBSD legacy driver가 AGP 필수 구조 때문에
이 조합을 지원하지 않았던 이유이기도 하다.

## DriverKit 가능 요소

현재 작업공간의 OPENSTEP 실기 검증으로 다음이 확인되어 있다.

- loadable kernel server와 `kl_util`/`driverLoader` 기반 적재.
- `IOMapPhysicalIntoIOTask()`에 의한 PCI BAR MMIO mapping.
- `IOPhysicalFromVirtual()`에 의한 bus-master DMA 물리 주소 확보.
- PCI interrupt와 kernel I/O thread.
- kernel/user 경계의 MiG 메시지 인터페이스.

따라서 Linux DRM의 커널 기능 중 장치 접근, command serialization,
DMA buffer 관리, fence 완료 통지는 OPENSTEP 방식으로 구현할 수 있다.
Linux `/dev/dri` ioctl ABI를 재현할 필요는 없다.

## 공개 MatroxMGA 1.0 정적 분석

공개 배포본 SHA-256:

```
7f2eaf2f8cb061dc75116dc346ba58b6b9b9e716caf9b55654e86825dad24823
```

IDA 초기 분석은 88개 함수를 식별했다. 관찰된 외부 DriverKit 의존성은
`IOFrameBufferDisplay`, `IOMapPhysicalIntoIOTask`,
`IOUnmapPhysicalFromIOTask`, `IOMalloc`, `IODelay`, `IOLog`이다.

기능적으로 다음을 확인했다.

- G400/G450 mode table, DAC/PLL, BIOS, linear framebuffer 초기화.
- G450 clock programming 경로.
- display pitch/origin과 pixel format에 맞춘 Storm 2D engine 초기화 및
  idle synchronization.

반대로 1차 분석에서는 DMA 물리 주소 변환, interrupt handler, user client,
MiG endpoint, Mesa/GL entry point, 3D command submission의 근거를 찾지
못했다. 이는 기존 드라이버가 display mode/2D 초기화 드라이버라는 판단을
강하게 뒷받침하지만, 동적 분석 전의 잠정 결론이다.

## 왜 OpenGL 1.2가 적합한가

G400/G450은 programmable shader 이전의 고정 기능 세대다. Mesa 3.4.2의
OpenGL 1.2 fixed-function state와 triangle rasterization, texture,
depth/stencil, blending, fog 등의 경로를 목표로 삼을 수 있다. 이 프로젝트는
shader/LLVM/Gallium을 요구하지 않는다.

단, OpenGL 1.2 "전체" 지원은 하드웨어가 직접 처리하지 않는 기능을 Mesa
software fallback으로 보완해야 한다. 드라이버가 advertise하는 extension과
GL version은 실제 state/primitive/readback 검증 후에만 정한다.

## DDC/EDID와 수동 mode 설정

G400/G450 계열의 DDC2B 가능성은 공개 Xorg MGA source에서 확인했다. 하지만
그 버스 구동은 RAMDAC GPIO control/data register write를 필요로 한다. 현재
`MatroxMGA`가 display lifecycle을 소유하는 상태에서 3D sidecar가 이를 읽는
것은 안전하지 않다. 따라서 DDC는 현 Mesa sidecar의 기능이 아니라 향후
replacement display driver의 boot-time 기능으로 분리한다.

OPENSTEP `IOFrameBufferDisplay`는 config table의 `Display Mode`와 driver의
고정 mode list를 통한 선택을 제공한다. DDC가 없거나 오류여도 이 수동 선택
경로는 유지한다. 유효 EDID가 있어도 명시 수동 mode가 우선이며, 자동 선택은
EDID preferred timing과 검증된 고정 mode table의 교집합으로만 제한한다.
세부 gate와 근거는 `docs/P0_DDC_EDID_FEASIBILITY.md`에 기록한다.

## 핵심 위험

1. **화면 소유권** — WindowServer와 기존 display driver가 scanout VRAM을
   사용한다. 3D 서비스는 front buffer, mode registers, DAC를 건드리면 안 된다.
2. **offscreen VRAM 예약** — OpenStep configuration은 16 MiB지만 PCI subsystem
   `102b:0d43`의 board catalogue 표기는 32 Mb다. physical total/type과
   scanout/cursor/기존 드라이버 사용 영역을 모르는 상태에서 주소를 가정하면
   화면 손상 위험이 있다.
3. **G400/G450 식별** — PCI ID만으로는 정확한 variant를 확정할 수 없다.
   physical VRAM type/size도 별도 evidence가 필요하다. 대조 결과와 허용되는
   해소 경로는 `docs/P1_SUBSYSTEM_MEMORY_RECONCILIATION.md`에 기록한다.
4. **hang 복구** — MMIO write 또는 engine reset 실패는 화면과 원격 GUI를 모두
   잃게 할 수 있다. 매 단계마다 재부팅 가능한 screen recovery 절차가 필요하다.
5. **라이선스** — 공개 MatroxMGA는 source distribution이 아니므로 구현 코드의
   출처로 사용하지 않는다. 공개 Xorg/FreeBSD/Linux 자료도 파일별 라이선스를
   검토한 뒤에만 참고 또는 이식한다.

## 설계 판정

| 항목 | 판정 |
| --- | --- |
| Linux/FreeBSD DRI ABI 그대로 포팅 | 하지 않음 |
| OPENSTEP native DRM-유사 kernel service | 가능, 연구 진행 |
| 기존 MatroxMGA binary patch/extension | 하지 않음 |
| 기존 driver와 offscreen 3D sidecar 공존 | 미검증, 최우선 검증 대상 |
| 기존 driver 완전 대체 | 최후 단계, 별도 결정 필요 |
