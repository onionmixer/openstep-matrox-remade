# P0.2 — Existing MatroxMGA Behaviour Specification

기준 binary: 공개 `MatroxMGA-1.0.tgz` 안의 `MatroxMGA_reloc`

```
SHA-256: 7f2eaf2f8cb061dc75116dc346ba58b6b9b9e716caf9b55654e86825dad24823
```

## 분석 경계

이 문서는 공개 binary의 외부 동작과 ABI 수준 관찰만 담는다. binary, IDA
database, disassembly, decompiler 출력, 주소별 instruction/register sequence는
저장소에 넣지 않는다. 새 구현은 이 문서를 구현 명세가 아닌 compatibility
checklist로만 사용한다.

## 관찰된 DriverKit 계약

정적 분석에서 다음 public dependency가 확인됐다.

- `IOFrameBufferDisplay`
- `IOMapPhysicalIntoIOTask()` / `IOUnmapPhysicalFromIOTask()`
- `IOMalloc`, `IODelay`, `IOLog`
- Objective-C `IODevice`/display configuration selectors

이는 기존 driver가 OPENSTEP display driver로서 linear framebuffer와
mode setting을 담당한다는 사실과 맞는다.

## 관찰된 기능 영역

| 영역 | 관찰 | 새 구현에서의 취급 |
| --- | --- | --- |
| PCI/BIOs discovery | MGA identity와 video BIOS를 확인 | P1 read-only probe의 참고 |
| linear framebuffer | physical framebuffer mapping과 origin/pitch 설정 | 기존 driver 소유, 접근 금지 |
| DAC/PLL | G400/G450 mode clock 설정 | P0-P4에서 접근 금지 |
| 2D Storm engine | display format별 초기화와 idle synchronization | register ownership 분류의 참고 |
| gamma/brightness | display transfer table 경로 | 가속 service 범위 밖 |

## 현재까지 관찰되지 않은 영역

초기 static analysis의 import/class/selector/call graph 범위에서는 다음의
근거를 찾지 못했다.

- `IOPhysicalFromVirtual()` 기반 DMA buffer setup
- DriverKit interrupt handler 또는 vblank completion path
- user client, MiG service, character-device ioctl/mmap interface
- Mesa/GL context 또는 3D command submission interface

이는 "존재하지 않는다"는 최종 증명은 아니다. 다만 기존 binary를 새 DRM의
기반으로 확장할 수 없고, 독립 `OpenStepMGAService`가 필요하다는 충분한
설계 근거다.

## 호환성 checklist

새 display driver 대체를 검토하는 마지막 단계에서만 아래 항목을 재현
대상으로 삼는다.

- PCI G450 identify와 기존 16 MiB memory configuration profile. 이 값은
  compatibility input이지 physical VRAM total/type의 독립 판정값이 아니다.
- 현재 사용 중인 1600x1200 RGB:888/32 mode.
- linear framebuffer publication과 WindowServer attach.
- G450 clock/mode programming의 정상 복귀.

이 checklist는 P1-P4 sidecar 구현의 선행 조건이 아니다.
