# P0.5 — OPENSTEP 원전 문서·예제 대조

기준일: 2026-08-18

## 목적과 증거 등급

이 문서는 추정이나 다른 운영체제 코드가 아니라 OPENSTEP에 포함된 개발 문서와
공식 DriverKit 예제를 P2 이후 설계의 1차 근거로 삼는다. 다음 세 종류의 증거를
섞지 않는다.

| 등급 | 근거 | 이 문서에서 확정하는 범위 |
| --- | --- | --- |
| A | NeXT NextDev 문서와 DriverKit headers | LKS/DriverKit API의 계약 |
| B | NeXT 공식 DriverKit example | OPENSTEP에서의 API 사용 패턴 |
| C | 실기 P1 kernel log | 현재 카드의 PCI 식별·자원 값 |
| D | Xorg/FreeBSD/Linux/공개 binary 분석 | hardware 및 구조 참고; OPENSTEP API의 근거 아님 |

## 로컬 원전과 실기 설치본

로컬 1차 자료는 다음 경로에 보존돼 있다.

| 용도 | 로컬 경로 |
| --- | --- |
| LKS overview/design/kernel utility | `ref/openstep/nextdev-doc/NextDev/OperatingSystem/Part2_WritingLKSs/` |
| framebuffer, PCI, kernel DriverKit header | `ref/openstep/headers/NextDeveloper/Headers/driverkit/` |
| display driver official example | `ref/openstep/examples/S3`, `QVision`, `CirrusLogicGD542X` |
| PCI/DMA official example | `ref/openstep/examples/AMDPCSCSIDriver` |

2026-08-18에 telnet read-only query로 다음이 실기에 설치돼 있음을 확인했다.

| 실기 경로 | 결과 |
| --- | --- |
| `/NextLibrary/Documentation/NextDev/OperatingSystem/Part2_WritingLKSs/` | 존재; 05~10장 및 appendices 확인 |
| `.../10_KernelFunctions/KernelFunctions.rtf` | 존재 |
| `/NextDeveloper/Headers/driverkit/IOFrameBufferDisplay.h` | 존재 |
| `/NextDeveloper/Headers/driverkit/i386/IOPCIDirectDevice.h` | 존재 |
| `/NextDeveloper/Examples/DriverKit/{S3,QVision,CirrusLogicGD542X,AMDPCSCSIDriver}` | 모두 존재 |

실기에는 `cksum` utility가 없으므로 byte checksum 비교는 하지 않았다. 경로와
구성은 확인됐지만, 버전 차이를 의심해야 하는 API를 사용할 때에는 해당 target
header를 직접 compile하는 것으로 다시 검증한다.

그 대신 NFS mount를 통해 target의 원전과 로컬 mirror를 `cmp -s`로 직접
비교했다. 2026-08-18 `sync; sync` 뒤에 다음 다섯 항목은 모두 status 0으로
일치했다.

1. `06_Designing/Designing.rtf`
2. `_ApA_Utilities/Utilities.rtf`
3. `IOFrameBufferDisplay.h`
4. `i386/IOPCIDirectDevice.h`
5. `ProAudioSpectrum16_reloc.tproj/Load_Commands.sect`

### 원격 자료를 로컬로 보존하는 절차

실기에만 있는 자료가 P2 이후에 필요하면 target에서 `/ndrv/ref/openstep/`의
대응 원전 경로로 복사한다. 이 저장소는 NFS export이므로 복사 전과 복사 후에
telnet 경유 `sync; sync`를 실행한다. 그 뒤 host에서 `find`/`rg`로 목록과
필요한 파일 크기를 확인한다. 누락 또는 중간 복사가 의심되면 destination을
추가·병합하지 않고 같은 원전 경로를 다시 복사한 뒤 다시 `sync`와 검증을
수행한다.

이 규칙은 원격 source tree와 host의 NFS cache가 서로 다른 상태일 수 있다는
점을 명시적으로 다룬다. 원전 파일은 `ref/openstep/`에만 두고, 이 프로젝트의
`refs/`에는 출처·복사 일자·검증 결과만 기록한다.

문서가 언급하는 `/NextLibrary/Documentation/NextDev/Examples/{MiG,Log,
ServerVsHandler}`는 현재 실기 설치본에 존재하지 않았다. `find`의 GNU 전용
`-iname`은 target utility에 없으므로 사용하지 않는다. 현 단계에서는 로컬과
실기에 모두 있는 `ProAudioSpectrum16`/`SoundBlaster8`의 `SMAP` + `ADVERTISE`
load-command 예제를 LKS port 연결의 build-pattern 근거로 사용하며, 부재한
예제를 추정하여 복원하거나 외부에서 대체하지 않는다.

## display driver ownership 경계

`IOFrameBufferDisplay.h`는 device subclass가 `enterLinearMode`,
`revertToVGAMode`, `mapFrameBufferAtPhysicalAddress:length:`, display mode
selection, cursor/brightness lifecycle을 구현하도록 정의한다. 공식 S3 예제는
그 lifecycle 안에서 device description의 memory range를 얻어 framebuffer를
map하고, mode를 설정한다. QVision 예제도 memory range와 display lifecycle을
소유한다.

따라서 현재 screen을 구동 중인 `MatroxMGA`는 이 card의 scanout/mode/framebuffer
owner다. 새 `OpenStepMGAService`는 기존 driver와 공존하는 동안 다음을 하지
않는다.

- `IOFrameBufferDisplay` subclass로 같은 PCI function을 claim하지 않는다.
- `enterLinearMode`, `revertToVGAMode`, mode selection, DAC/PLL/CRTC 변경을
  호출하지 않는다.
- display framebuffer mapping, cursor 영역, front buffer를 자신의 자원으로
  가정하지 않는다.

이것은 P2/P3 sidecar의 안전 경계다. 언젠가 clean-room display driver 교체를
검토할 경우에는 별 milestone에서 S3/QVision/Cirrus의 full display lifecycle을
별도로 검증해야 하며, 현 sidecar 작업과 혼합하지 않는다.

## PCI와 DMA의 문서상 의미

`IOPCIDirectDevice.h`는 `IODirectDevice`에 정식 device description을 전제로
PCI config space를 읽고 쓰는 API를 제공한다. AMDPCI SCSI 공식 예제는 config
space를 읽은 뒤 port range와 interrupt list를 그 driver 자신의 device
description에 설정하고, DMA에는 `IOPhysicalFromVirtual()`을 사용한다.

이것은 장래 P5의 DMA/IRQ 구현 참고로는 적합하지만, 기존 `MatroxMGA`가 이미
claim한 device를 sidecar가 재claim하거나 config/write ownership을 얻는 근거는
아니다. P1의 PCI config scan은 그러한 claim 없이 identity를 읽기 위한 한시적
read-only probe다. P2에서는 BAR mapping, interrupt registration, bus-master
DMA를 추가하지 않는다.

## LKS와 P2 통신 규칙

NextDev "Designing Loadable Kernel Servers"는 message-based interface에는 MiG
사용을 권장하며, UNIX file-system interface가 필요할 때만 UNIX-style server를
선택하도록 설명한다. OpenStepMGA의 초기 API는 graphics client library와 LKS
사이의 control protocol이므로 message-based interface가 적합하다.

NextDev "Utilities"는 load command script에 `HMAP`, `SMAP`, `START` 중 하나가
필요하다고 규정한다.

- `SMAP`: port를 server-interface function에 연결한다.
- `HMAP`: port를 handler-interface function에 연결한다.
- `ADVERTISE`: 필요한 경우 port를 name server에 advertise한다.
- `START`: port 없이 즉시 시작하는 LKS에 맞는다.
- `WIRE`: interrupt handler가 있을 때만 필요하다. P2 polling-only skeleton은
  interrupt handler를 포함하지 않으므로 관성적으로 WIRE를 넣지 않는다.

같은 Designing 문서는 message-based server가 out-of-line data를 sender task의
pointer로 직접 읽을 수 없으며, kernel map에 `vm_write()`로 가져오고 sender task
map으로 돌려줄 때 `vm_read()`를 사용해야 한다고 명시한다. page alignment도
요구한다. 또한 message-based server에서는 `copyin()`/`copyout()`을 쓰지 말라고
명시한다.

따라서 P2의 정확한 protocol 규칙은 다음과 같다.

1. `.defs`와 MiG-generated client/server stub을 source tree에 함께 둔다.
2. `QueryCapabilities`, `Acquire`, `Release`, `WaitFence`는 bounded, fixed-size
   inline data만 받는다.
3. `Submit`은 P2에서는 거부 상태로 남긴다. 활성화할 때도 먼저 small inline
   command format만 도입하고, out-of-line buffer는 별도 review와 `vm_write`/
   `vm_read` 시험 뒤에만 허용한다.
4. user virtual address, physical address, register offset을 client가 지정하는
   interface를 만들지 않는다.
5. port death와 client crash의 owner-token 회수 절차를 `.defs`/load command와
   함께 명세화한다.

## P1과 다음 gate

P1은 제어용 `CALL` + `START` + `WIRE` load command로 read-only PCI probe를
실행해 성공했다. `START`가 즉시 시작의 문서상 근거다. P1의 `WIRE`는 초기
skeleton의 resident load 방식으로 남아 있지만, 이후 service에서 필요한지
여부는 실제 interrupt handler 유무로 다시 판정한다.

P2 시작 전 남은 문서 기반 gate는 두 가지다.

1. 동일 부팅에서 별도 read-only scanner로 BAR1 `0xe8200000`과 과거 기록
   `0xe8300000`의 차이를 재확인한다. **통과:** `pcils/PCIscan`이 P1의
   64-byte header 및 BAR0/1/2와 일치했다. 결과는 `P1_PCIL_RECHECK.md` 참조.
2. target `mig` 도구, `kernserv` headers 및 가장 작은 message-based LKS 예제를
   문서·실기 양쪽에서 확인한 뒤 `.defs` skeleton의 ABI를 고정한다. 이 항목의
   toolchain 존재와 non-advertised generation은 통과했으며, subsystem number와
   client-death prototype은 아직 미결이다.

그 전에는 현재 P1 결과 이외의 card BAR mapping/VRAM/MMIO/IRQ/DMA 시험을
진행하지 않는다.
