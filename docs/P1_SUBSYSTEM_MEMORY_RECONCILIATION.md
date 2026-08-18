# P1.3 — PCI Subsystem/VRAM Evidence Reconciliation

기준일: 2026-08-18

## 결론

현재 카드의 물리 VRAM 크기와 memory type은 **확정하지 못했다**. 기존
`MatroxMGA` 설정의 16 MiB와 PCI subsystem ID `102b:0d43`의 공개 보드
catalogue 표기가 서로 다르므로, 어느 하나를 offscreen allocator나 P3 admission의
물리 메모리 근거로 사용해서는 안 된다.

이 문서는 PCI configuration header, DriverKit getter, 기존 display 설정, 공개
catalogue를 대조한 read-only 분석이다. MGA BAR mapping, ROM mapping, VRAM/MMIO,
DAC/PLL/CRTC/engine, PCI write, DMA, IRQ, DDC에는 접근하지 않았다.

## 관찰값과 증명 범위

| evidence | 관찰값 | 증명하는 것 | 증명하지 않는 것 |
| --- | --- | --- | --- |
| P1 PCI header | `102b:0525`, subsystem `102b:0d43`, revision `0x85` | 장착된 PCI function과 board-specific subsystem ID | VRAM controller가 실제로 검출한 memory type/size |
| `Instance0.table` | `MatroxMGAG400_16MB`, `MGA Memory Size=16` | 기존 OpenStep driver가 선택한 configuration profile | 현재 카드의 독립 물리 측정값 |
| DriverKit getter | `IOGetDisplayMemory` transport success, value `0` | 기존 binary가 runtime value를 publish하지 않음 | 0 byte VRAM 또는 16 MiB/32 MiB 중 어느 값 |
| pciutils `pci.ids` catalogue | `102b:0d43` = `Millennium G450 32Mb Dual Head PCI` | 이 subsystem ID에 대한 공개 보드명 | 이 실기의 memory controller register가 보고한 값, memory chip 종류 |

`pci.ids`의 문자열은 board-ID catalogue이며, 메모리 probing 결과가 아니다. 반대로
OpenStep table도 driver의 boot/configuration selection이다. 두 값이 충돌하므로
"16 MiB configuration"과 "32 Mb board catalogue label"을 각각 보존하되, 물리
VRAM total을 선언하지 않는다.

## target configuration bundle 재확인

2026-08-18에 target의 `/private/Drivers/i386/MatroxMGA.config`를 read-only로
목록·`cat`했다. 이 작업은 driver load/unload, BAR mapping, mode change를 하지
않았다.

| 대상 | 관찰값 | 해석 |
| --- | --- | --- |
| 현재 `Instance0.table` | `Default Table=MatroxMGAG400_16MB`; title `Matrox MGA G400 (16MB or greater)`; `MGA Memory Size=16` | 현재 driver가 16 MiB 이상을 대상으로 한 G400 profile을 선택함 |
| G400 16 MiB table | title 외 `Auto Detect IDs=0x0525102B`, `MGA Memory Size=16`, PCI/DAC 필드가 G450 table과 같음 | `0525` device ID만으로 G400/G450과 physical VRAM을 구분하지 않음 |
| G450 16 MiB table | title `Matrox MGA G450 (16MB or greater)`; 나머지 확인 필드는 G400 table과 동일 | G450용 최소 16 MiB compatibility profile도 제공함 |
| bundle profile 목록 | G400/G450은 8 MiB와 16 MiB profile만 존재하고 32 MiB profile은 없음 | bundle의 profile 목록은 board memory probing table이 아님 |

특히 `16MB or greater`라는 target 원문은 `MGA Memory Size=16`이 **최소 profile
조건**일 수 있음을 직접 보인다. 두 table이 같은 PCI auto-detect ID를 공유하고,
`Memory Maps`와 `FB Address`도 비어 있으므로 이 configuration만으로 card variant,
physical total, offscreen partition을 판정할 수 없다.

같은 read-only session의 `/usr/etc/kl_util -s` 결과에서 existing `MatroxMGA`는
`0x2112a000`에 `0x12000` bytes로 계속 loaded되어 있었고,
`OpenStepMGAService`는 `Deallocated`였다. 따라서 이 재확인은 기존 display
owner나 sidecar의 hardware state를 바꾸지 않았다.

## memory type에 대한 판정

G450 family가 여러 SDRAM/SGRAM 구성과 PCI board variant를 가질 수 있다는 공개
자료는 존재하지만, 그것은 이 카드의 정확한 device population을 판별하지 않는다.
Xorg MGA driver가 G450을 `mgag400` family 및 chip revision `0x80` 계열로 다루고
subsystem ID로 SDRAM 관련 heuristic을 적용하는 점도, subsystem ID만으로 target의
정확한 memory type을 확정할 수 없다는 보조 근거다.

따라서 현 단계의 `VRAM memory type`은 **unknown**이다. DDR/SDR/SGRAM 중 하나를
가정한 register sequence, bandwidth claim, allocator alignment rule을 만들지
않는다.

## P3 admission 영향

P3 조건의 "physical variant와 VRAM type/size의 read-only 확정"은 **미통과**다.
또한 scanout/cursor/hidden allocation을 포함한 existing owner의 reserved range도
불명이다. 그러므로 다음 행위는 계속 금지된다.

- BAR 또는 option ROM mapping, VRAM/MMIO read/write
- offscreen range 계산·예약·clear/readback
- engine command submission, DMA, IRQ, reset

P2 control-plane의 `hardware_ready=0` 상태도 유지한다.

## 허용되는 후속 해소 경로

1. 기존 display driver를 건드리지 않는 문서/firmware metadata가 physical size와
   type을 명시하는지 확인한다. 단 board catalogue만으로는 충분하지 않다.
2. 독립적으로 소유한 동일/호환 시험 카드 또는 clean-room replacement display
   driver 환경에서, vendor documentation에 근거한 read-only hardware identification을
   수행한다.
3. physical total이 확인되어도 existing `MatroxMGA`의 scanout/cursor/hidden
   allocation 및 mapping compatibility를 별도로 확정한다. total size만으로 P3를
   열 수 없다.

위 조건을 충족하기 전에는 system-memory-only software/reference path 외의 3D
hardware 작업을 시작하지 않는다.

## 참고

- [pciutils `pci.ids` mirror — `102b:0d43` board label](https://fuchsia.googlesource.com/third_party/github.com/pciutils/pciids/%2B/d331f4cd7b37e4c10fec9c410ed85fc27801af93/pci.ids)
- [Xorg `mga(4)` — G450 family handling and subsystem-based SDRAM heuristic](https://xorg.freedesktop.org/archive/X11R7.5/doc/man/man4/mga.4.html)
- target-native DriverKit result: `docs/P1_DRIVERKIT_DISPLAY_QUERY.md`
- target-native PCI inventory: `docs/P0_TARGET_INVENTORY.md`
