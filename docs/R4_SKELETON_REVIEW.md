# R4 — Fail-Closed Replacement Display Skeleton Review

기준일: 2026-08-18

## 범위

`OpenStepMGAReplacementDisplay`은 future replacement display driver의 DriverKit
lifecycle/build skeleton이다. 이것은 production `MatroxMGA`를 수정하거나 공존해
관찰하는 sidecar가 아니며, target driver-loader에 등록·load·probe하지 않았다.

R4의 목적은 다음 두 가지뿐이다.

1. OPENSTEP 4.2의 `IOFrameBufferDisplay` subclass와 standard DriverKit bundle
   Makefile가 target compiler에서 build되는지 확인한다.
2. G1~G4 이전에 source와 relocatable binary가 hardware path를 열지 않도록
   fail-closed contract를 고정한다.

## 구현 계약

| 항목 | R4 동작 |
| --- | --- |
| bundle table | `Family=Display`이나 `Auto Detect IDs`, `Memory Maps`, `I/O Ports`, `Display Mode` 없음 |
| `+probe:` | 항상 `NO`; matching/claim을 거부 |
| `initFromDeviceDescription:` | superclass init 뒤 즉시 `super free`; map/mode write 전 종료 |
| `enterLinearMode` | log만 남기고 반환 |
| `revertToVGAMode` | local hardware state 없이 superclass lifecycle만 호출 |
| manual memory parser | original-compatible strict 3..63 MiB `MGA Memory Size` parse; hardware read/write 없음 |
| memory/RAMDAC query | staging object는 probe/init에서 해제된다. parsing된 memory value는 operator declaration일 뿐 physical profile/RAMDAC 증명이 아님 |
| brightness | 거부 (`nil`) |
| `free` | 자체 mapping/allocation이 없으며 superclass cleanup만 수행 |
| load command | `WIRE`만 존재; `START` 및 `CALL` 없음 |

따라서 skeleton에는 PCI/VRAM mapping, port I/O, PCI configuration access,
mode/PLL/CRTC/DAC programming, DDC/EDID, 2D/3D command submission이 없다.
manual memory parser의 contract는 `R2_MANUAL_MEMORY_CONFIGURATION.md`에 있다.

## 검증

### Host source gate

다음 명령은 comments를 제거한 source/header와 bundle table/load command를
검사하고, host의 csh가 있으면 target import-gate script의 구문도 확인한다.

```sh
sh test/run-r4-skeleton-host.sh
```

검사 항목은 required lifecycle methods, unconditional `+probe: NO`, init의
`[super free]` failure path, empty resource table, no automatic matching/mode
table, no `START`/`CALL`, mapping/device/port-I/O/display-programming symbol
부재다.

최신 결과:

```text
OPENSTEP_MGA_REPLACEMENT_R4_STATUS=pass
OPENSTEP_MGA_REPLACEMENT_R4_CSH_SYNTAX=skipped-no-host-csh
OPENSTEP_MGA_REPLACEMENT_R4_HOST_CHECKS=pass
```

현재 host에는 csh가 없으므로 import-gate script의 parsing은 OPENSTEP target에서
확인했다. target i386 relocatable에 대한 최신 결과는 다음과 같다.

```text
OPENSTEP_MGA_REPLACEMENT_R4_IMPORT_STATUS=pass
```

이 결과는 target의 read-only `nm -u` allowlist 검사이며, bundle load 권한을
부여하지 않는다.

### Target compile과 import gate

NFS `/ndrv` source에서 OPENSTEP 4.2 i386 compiler로 다음만 실행했다.

```csh
cd /ndrv/openstep-matrox-remade/OpenStepMGAReplacementDisplay
make clean
make
csh ../test/check-replacement-skeleton-imports.csh \
    OpenStepMGAReplacementDisplay.config/OpenStepMGAReplacementDisplay_reloc
```

`make`는 `OpenStepMGAReplacementDisplay_reloc`을 생성했고 compile/link는
성공했다. 이어서 target의 `/bin/nm -u`로 아래 import 목록을 read-only로
확인했다. binary를 `kl_util` 또는 `driverLoader`에 전달하지 않았고 bundle을
`/private/Drivers/i386` 또는 `/private/Devices`에 설치하지 않았다.

import gate의 allowlist는 Objective-C message dispatch와 `IOLog`뿐이다.
`_objc_msgSend`는 configuration table의 `valueForStringKey:` dispatch 때문에
필요하며 device/PCI/framebuffer API가 아니다:

```text
.objc_class_name_IODevice
.objc_class_name_IOFrameBufferDisplay
.objc_class_name_Object
_IOLog
_objc_msgSend
_objc_msgSendSuper
```

이 결과는 frame buffer 또는 device ownership을 증명하지 않는다. 다만 target
build artifact가 R4가 금지한 map/register/DDC/engine import 없이 만들어졌음을
보인다. 같은 allowlist를 자동 검사하는 `check-replacement-skeleton-imports.csh`는
manual parser가 추가된 2026-08-18 i386 clean build에도 적용했고
`OPENSTEP_MGA_REPLACEMENT_R4_IMPORT_STATUS=pass`를 재확인했다. binary는 여전히
`kl_util` 또는 `driverLoader`에 전달하지 않았다.

## 다음 변경의 admission

R4 source에서 아래 중 하나를 추가하려면 이 review를 수정하는 것만으로는
부족하다.

| 변경 | 필요한 선행 gate |
| --- | --- |
| PCI matching 또는 recovery config table | G1 + R0 recovery rehearsal |
| physical VRAM/RAMDAC constant | G2 |
| fixed `Display Mode`, pitch/footprint | G3 |
| target driver-loader registration/load | G1~G4와 별도 실행 승인 |
| framebuffer map, VGA/mode programming | G1~G4와 R6 run sheet 승인 |
| DDC base-block transaction | G5 |
| 2D/3D engine 또는 P3 client binding | P3 admission gate 전부 |

상세 순서와 failure rollback은
`RECOVERY_REPLACEMENT_DRIVER_EXECUTION_PLAN.md`를 따른다.

R6에서 사용할 DriverKit mapping API의 local-header/sample audit은
`R6_DRIVERKIT_MAPPING_AUDIT.md`에 분리했다. 결론은 configured memory range의
`mapMemoryRange`/`unmapMemoryRange` pair를 future candidate로 삼는 것이며,
R4 source gate는 두 API와 legacy framebuffer map API를 모두 계속 거부한다.

R6의 pure-C one-mode transaction policy는
`R6_MODE_TRANSACTION_POLICY.md`에 별도 구현되어 있으나, R4 bundle Makefile에는
포함하지 않았다. 이 policy는 sampled result를 받아 state만 변경하며 R4의
`+probe:NO`/immediate-free contract나 target hardware boundary를 완화하지 않는다.

같은 source gate는 `OpenStepMGAOffscreen2D`와
`OpenStepMGAOffscreenAllocator` linkage도 거부한다. 두 module은 future
replacement-only 2D path의 host/target pure-C policy일 뿐이며, R4 bundle이
allocation, mapping, draw submission 또는 surface lifecycle을 시작하는 근거가
될 수 없다.
