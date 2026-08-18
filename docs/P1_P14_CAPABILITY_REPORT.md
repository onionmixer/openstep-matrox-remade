# P1.4 — Target PCI Capability Header Result

기준일: 2026-08-18

## 실행 범위

target i386에서 build한 `OpenStepMGAProbe`를 unique
`/tmp/OpenStepMGAProbeP14` staging root로 복사한 뒤 temporary
`kl_util -a/-l/-u/-d` lifecycle 한 번만 실행했다. `driverLoader`, Configure,
`MatroxMGA` unload, BAR/option-ROM/VRAM/MMIO, mode/DAC/PLL/CRTC, DDC, DMA,
IRQ, PCI device configuration write는 사용하지 않았다.

NFS `nxlogd` latest log에서 P1.4 marker를 회수했고, probe teardown 뒤
`OpenStepMGAProbe`는 deallocated 상태, existing `MatroxMGA`는 loaded 상태임을
확인했다.

## 관찰 결과

| item | result |
| --- | --- |
| MGA PCI function | `04:00.0`, existing P1 inventory와 일치 |
| standard capability header 1 | offset `0xdc`, ID `0x01` |
| standard capability header 2 | offset `0xf0`, ID `0x02` |
| list traversal | 2 hops 뒤 정상 종료 |
| VPD capability ID `0x03` | 관찰되지 않음 |
| VPD contents/data register access | 수행하지 않음 |
| probe cleanup | unloaded and deregistered |
| existing display owner after run | `MatroxMGA` loaded |

log에는 P1.4 capability marker가 존재하며 malformed offset, loop, hop-limit,
VPD marker는 없었다. report에는 raw BAR/framebuffer/module address를 기록하지
않는다.

## 해석과 다음 경계

이 function의 standard capability list에는 VPD capability header가 없으므로,
VPD를 통한 board part number 또는 physical VRAM discovery 경로는 현재 P1.4에서
열리지 않는다. VPD contents read를 시도해서 이 결과를 보완하지 않는다.

이 결과는 physical VRAM type/total, RAMDAC limit, existing scanout allocation,
mapping compatibility를 증명하지 않는다. 따라서 G2와 P3 admission은 계속
미통과다. 허용되는 다음 R2 evidence는 board physical inspection 또는
replacement-only environment에서 공개 명세에 근거한 target-specific
identification뿐이다.
