# R6 — Opaque Offscreen Allocation Ledger

기준일: 2026-08-18

## 목적과 경계

`protocol/OpenStepMGAOffscreenAllocator.{h,c}`는 future 2D request가 사용할
offscreen surface의 **크기 산술과 수명 ID만** 준비한다. 이는 target allocator,
VRAM mapping, framebuffer address, BAR offset, cache mode, MMIO 또는 engine
submit을 구현하지 않는다.

입력 `OSMGAOffscreenArena`는 future replacement-only mapping 단계가 이미
확정한 영역을 표현하는 opaque record다. `outside_scanout_region_verified`가
참이 아니면 초기화부터 거부한다. 이 flag는 caller가 임의로 주장할 수 있는
runtime shortcut이 아니라 G1--G4 및 source-backed range review 뒤 kernel-owned
allocation metadata로만 채워져야 한다.

## 현재 계약

1. arena byte length는 nonzero이고 alignment는 nonzero power-of-two여야 한다.
2. surface request는 기존 32-bit geometry validator로 width, height, stride와
   required-byte footprint를 검증한다.
3. footprint는 arena alignment로 올림한 뒤 남은 capacity 안인지 확인한다.
4. 성공하면 순증하는 nonzero opaque surface ID와 검증된 geometry만 반환한다.
   주소나 physical offset은 반환하지 않는다.
5. ledger는 최대 16개의 live ID만 추적한다. release는 그 ID를 retire하지만,
   현재의 monotonic bump arena는 `used_bytes`를 되돌려 재사용하지 않는다.
6. future backend는 request 전에 complete surface record가 ledger에 아직 live이며
   발급 당시의 ID, geometry, verification flags와 모두 같은지 확인할 수 있다.
   release 뒤 stale record 또는 geometry가 변한 record는 거부한다.

마지막 규칙은 의도적이다. future hardware fence/idle proof, engine ownership,
그리고 target allocator lifetime이 없는 상태에서 released memory를 재사용하면
명령 완료 전 surface 재사용을 안전하다고 잘못 가정하게 된다. 따라서 capacity는
conservatively one transaction generation 안에서만 소비된다.

## 2D admission과의 관계

allocator가 만든 surface는 `kernel_allocation_verified`와
`outside_scanout_verified`를 설정하지만, 이것만으로 draw가 허용되지는 않는다.
`OpenStepMGAOffscreen2D`는 여전히 `LINEAR_ACTIVE` transaction과 in-bounds
clear/copy geometry를 별도로 요구한다. 반대로 2D admission 성공도 submit,
addressing, VRAM allocation 또는 hardware action을 뜻하지 않는다.

현재 2D clear/copy validator는 allocator pointer를 필수로 받아 live-record
검증을 수행한다. 따라서 future caller는 allocator 밖에서 만든 surface, release된
surface, 또는 발급 뒤 geometry/verification flag가 바뀐 surface를 request에 쓸 수
없다.

## 검증

synthetic 16 KiB arena / 4 KiB alignment에서 64x32x32bpp surface 두 개를
할당하고, byte-capacity exhaustion, duplicate release, invalid stride rejection,
released/mutated live-record rejection을 검증한다. 별도 small fixture는
non-power-of-two alignment와 16 live-ID slot exhaustion도 검증한다.

```text
sh tools/check-offscreen-allocator-no-hardware.sh
sh test/run-offscreen-allocator-host.sh
```

동일 source를 `/ndrv/openstep-matrox-remade`에서 target `cc`로도 compile/run해
`OPENSTEP_MGA_OFFSCREEN_ALLOCATOR_TEST_STATUS=pass`를 확인했다. test binary는
`/tmp`에서 즉시 삭제했고, 이 run은 ordinary process memory만 사용한다.

이 module은 R4 display bundle과 P2 MiG ABI에 연결되지 않았다. 실제 target
allocation을 열기 전에는 G1--G4, exact mapping range/cache evidence, target
allocator API/lifetime audit, 2D engine fence/idle contract와 approved
replacement-only run이 모두 필요하다.
