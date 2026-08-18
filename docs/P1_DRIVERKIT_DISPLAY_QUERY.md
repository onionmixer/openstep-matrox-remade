# P1.2 — Read-only DriverKit Display Query Report

실행일: 2026-08-18

## 목적과 API 경계

P1.2는 이미 loaded된 `MatroxMGA`가 OPENSTEP의 documented user-space
`IODeviceMaster` interface로 어떤 display metadata를 공개하는지 확인한다.
새 driver나 LKS를 load하지 않으며, existing device에 setter·mode change·device
claim을 요청하지 않는다.

근거 header는 target과 byte-for-byte 대조된 다음 원전이다.

- `driverkit/IODeviceMaster.h`: user-space `lookUpByDeviceName` 및
  `getIntValues` RPC.
- `driverkit/displayDefs.h`: `IOGetDisplayMemory`, `IOGetRAMDACSpeed`,
  `IOGetCurrentDisplayMode`, `IOGetDisplayModeNum` read-only parameter names.
- `driverkit/IOFrameBufferDisplay.h`: `displayMemorySize`와 `ramdacSpeed`가
  device subclass가 적절한 값을 반환해야 하는 method임을 명시.

P1.2 client는 `IOGetDisplayInfo`를 호출하지 않는다. 그 struct에는 framebuffer
pointer가 포함되므로, pointer 값도 read-only inventory로 수집하지 않는 것이
현재 ownership 경계에 맞는다. 또한 `setIntValues`, display-mode setter, BAR map,
VRAM/MMIO/PCI register/DMA/IRQ/DDC API는 전혀 사용하지 않는다.

## target-native build

`test/openstep-mga-display-info-probe.m`을 target Objective-C compiler와
`/usr/lib/libDriver.a` (`-lDriver`)로 build했다.

```
OPENSTEP_MGA_P12_BUILD_PROBE_STATUS=0,0
```

`libDriver.a`의 non-writable `__TEXT` external relocation warning은 OPENSTEP
static DriverKit archive의 legacy linker diagnostic이며, source type warning은
수정 후 없었다.

## target 결과

```
OPENSTEP_MGA_DISPLAY_LOOKUP candidate=MatroxMGA0 result=-704
OPENSTEP_MGA_DISPLAY_LOOKUP candidate=MatroxMGA result=-704
OPENSTEP_MGA_DISPLAY_LOOKUP candidate=Display0 result=0 object=20 kind=Linear Framebuffer
OPENSTEP_MGA_DISPLAY_MEMORY result=0 count=1 bytes=0
OPENSTEP_MGA_DISPLAY_RAMDAC result=0 count=1 hz=0
OPENSTEP_MGA_DISPLAY_CURRENT_MODE result=0 count=1 index=115
OPENSTEP_MGA_DISPLAY_MODE_COUNT result=0 count=1 modes=212
OPENSTEP_MGA_P12_BUILD_PROBE_STATUS=0,0
```

`Display0` is the existing device's public DriverKit name, object number is
`20`, and device kind is `Linear Framebuffer`. Current mode index `115` is
within the public runtime mode count `212`.

`IOGetDisplayMemory` and `IOGetRAMDACSpeed` transport calls both succeeded,
but returned zero. This is **not** evidence that the card has zero video
memory or a zero-Hz DAC. It shows that this binary driver's standard getter
path does not publish those values at runtime. The Instance0 table separately
states `MGA Memory Size=16` and `DAC Speed=300MHz`, but that configuration is
not an independent runtime measurement.

The existing `MatroxMGA` remained loaded at `0x2112a000` for `0x12000` bytes
after the query.

## P3 admission effect

P1.2 establishes a documented, read-only metadata channel and identifies the
public display object. It does **not** establish a safe framebuffer range.

| P3 prerequisite | P1.2 result |
| --- | --- |
| physical PCI G450 identity | unchanged: topology/config evidence only |
| VRAM size | config says 16 MiB; standard runtime getter is uninformative (`0`), and PCI subsystem catalogue reconciliation remains pending |
| VRAM memory type | not published |
| offscreen range | not published; remains unresolved |
| mapping compatibility with existing owner | not established |

Therefore P3 BAR mapping, VRAM access, engine command submission and any
offscreen allocation remain prohibited. A later replacement-display-driver
track or a separately owned test card is required before those gates can be
opened. The independent reconciliation record is
`docs/P1_SUBSYSTEM_MEMORY_RECONCILIATION.md`.
