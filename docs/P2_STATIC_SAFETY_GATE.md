# P2.7 — Static No-Hardware Safety Gate

기준일: 2026-08-18

## 목적

P2 control-plane은 existing `MatroxMGA`와 병렬로 존재하지만, MGA PCI function,
BAR, VRAM, framebuffer, engine, DMA, IRQ, DDC에 접근하지 않는다는 계약을 갖는다.
`tools/check-p2-no-hardware.sh`는 이 계약이 이후의 source 변경으로 우연히
깨지는 것을 build 전에 발견하기 위한 host-side static gate다.

이 검사는 P1 read-only PCI probe와 향후 P3 source를 검사하지 않는다. 또한
정적 문자열 검사는 target ownership evidence, code review, target-native regression을
대체하지 않는다.

## 검사 범위

검사 대상은 다음 P2 service bundle 파일로 한정한다.

- `OpenStepMGAService_reloc.tproj/OpenStepMGAService.m`
- `OpenStepMGAService_reloc.tproj/OpenStepMGA.defs`
- `OpenStepMGAService_reloc.tproj/OpenStepMGAProtocol.h`
- `OpenStepMGAService/Default.table`
- `OpenStepMGAService_reloc.tproj/Load_Commands.sect`

guard는 C/C++ comment를 먼저 제외하고 다음을 거부한다.

| 범주 | 거부 예 |
| --- | --- |
| DriverKit hardware API/type | `IOMapPhysicalIntoIOTask`, `IOPhysicalFromVirtual`, `IOPCIDirectDevice`, `IOFrameBufferDisplay` |
| 직접 I/O·PCI access | `in*`/`out*` port call, PCI config helper, MGA PCI/MMIO helper |
| device binding table | 비어 있지 않은 Auto Detect ID, Location, FB Address |
| 자원 table | 비어 있지 않은 IRQ, DMA, memory map, I/O port 항목 |
| load command | `START`, `WIRE` |
| MiG ABI surface | `protocol_info`, `query_capabilities`, `acquire`, `release` 이외의 routine |

`PORT_DEATH`와 `SMAP`/`ADVERTISE`는 Mach message lifecycle이므로 허용한다.

## 실행

host workspace에서 실행한다.

```
sh openstep-matrox-remade/tools/check-p2-no-hardware.sh
```

정상 결과는 다음 한 줄이다.

```
OPENSTEP_MGA_P2_STATIC_GUARD_STATUS=pass
```

2026-08-18 host workspace에서 `sh -n` 뒤에 실행한 결과는 위와 같이 `pass`였다.
이 검사는 source 변경만 대상으로 하므로 target에 LKS를 load하거나 MGA/기존
display driver에 접근하지 않았다.

P2 source, service table, load command를 바꿀 때에는 이 guard와 target-native
P2.6 regression을 모두 통과시켜야 한다. guard failure는 P3 승인이 아니라
P2 design 범위 이탈로 처리하며, hardware 접근 코드를 P2에서 제거하거나 P3의
별도 admission review로 분리한다.
