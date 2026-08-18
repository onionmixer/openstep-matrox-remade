# P3 — Geometry-only Command Envelope

기준일: 2026-08-18

## 목적과 현재 범위

`protocol/OpenStepMGACommand.c`는 future P3 kernel command path가 받기 전에
검사할 clear/flat-triangle geometry contract다. surface는 width, height, stride,
already-owned allocation byte length만 가진다. 다음 값은 type 또는 API에 없다.

- user virtual address, physical address, BAR/MMIO offset
- VRAM base address, framebuffer pointer, map handle
- register value, DMA descriptor, fence/interrupt state

따라서 이 module은 normal host memory에서 compile/test할 수 있으며, MGA device를
찾거나 memory를 map하거나 command를 제출하지 않는다. P2 MiG `.defs`에는 아직
연결하지 않았다. P3 admission 전에는 user-visible `Submit` ABI를 넓히지 않는 것이
의도된 설계다.

## validation contract

| API | validation | success 이후에도 금지되는 일 |
| --- | --- | --- |
| `OSMGAValidateSurface32` | nonzero width/height, stride ≥ width, 32-bit `stride*height*4` overflow, allocation length | mapping/allocation/clear |
| `OSMGAValidateClear32` | surface validation과 overflow-safe unclipped rectangle range | register write, raw buffer address 수용 |
| `OSMGAValidateTriangle32` | surface validation, 4095 이하 dimension, all vertices inside surface, non-degenerate area | clipping, rasterization, hardware triangle submit |

모든 failure는 first validation reason을 반환한다. `allocation_bytes`는 caller가
독립 ownership gate 뒤 kernel-side allocation record에서 채운 값이어야 한다.
config table, PCI board label, user message가 이 필드를 신뢰 가능한 값으로 만들지
않는다.

## regression vector와 증명 범위

host C89 test는 1600×1200, stride 1600, allocation `7,680,000` bytes의 **synthetic
surface**를 사용한다. 이는 arithmetic and boundary test vector일 뿐 현재 Matrox
card의 VRAM 또는 offscreen allocation 증거가 아니다.

이 vector는 다음을 검증한다.

1. full-surface clear와 last-pixel clear는 승인된다.
2. right/bottom overrun clear는 거부된다.
3. 1600×1200 안의 non-degenerate triangle은 승인되고 collinear/out-of-range
   vertices는 거부된다.
4. allocation one-byte shortage와 stride shortage는 surface 단계에서 거부된다.

## future kernel integration rule

P3 admission 뒤에도 이 validator는 user request를 그대로 trust하는 mechanism이
아니다. kernel service는 lease token으로 owned surface ID를 찾고, 자신의
allocation metadata로 `OSMGASurfaceGeometry`를 만든 뒤 command geometry를 검증해야
한다. user client는 color, coordinates, depth/state enum처럼 bounded scalar만
전달하며 allocation address/length를 전달하지 않는다.

validation success는 P3 hardware permission이 아니다. `OSMGACanEnterP3`의
five-evidence gate, kernel-owned offscreen allocation, bounded idle timeout, 그리고
one-command-at-a-time test gate가 모두 별도로 필요하다.

R6 단계에서 이 geometry validator를 active transaction과 verified offscreen-only
surface metadata에 결합하는 pure-C admission layer는
`R6_OFFSCREEN_2D_ADMISSION.md`에 있다. 이는 submit ABI나 hardware backend를
추가하지 않는다.
