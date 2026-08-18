# R3 — Current Production Mode Footprint

기준일: 2026-08-18

target R0 baseline의 current configuration은 다음 manual Display Mode다.

```text
Height: 1200 Width: 1600 Refresh: 60Hz ColorSpace: RGB:888/32
```

`test/openstep-mga-current-mode-footprint.c`는 이 exact string을 pure-C policy로
parse하고 32 bpp, exact minimum pitch `1600 * 4 = 6400` bytes를 적용한다.

| arithmetic item | result |
| --- | --- |
| visible footprint | `6400 * 1200 = 7,680,000` bytes |
| existing config profile lower bound | 16 MiB = `16,777,216` bytes |
| arithmetic difference | `9,097,216` bytes |
| parser/footprint host test | pass |

이 결과가 보이는 current framebuffer가 16 MiB minimum compatibility profile의
순수 산술 범위 안에 있음을 보일 뿐이다. difference는 cursor, hidden allocation,
alignment, pitch expansion, scanout reservation, actual physical VRAM total/type,
또는 usable offscreen region을 증명하지 않는다. 따라서 P3 admission과
replacement mapping length에는 사용할 수 없다.

R3 fixed manual mode table은 G2 physical profile이 통과한 뒤에만 이 lower-bound
계산을 board-specific memory/clock limit과 결합해 review한다.

## Offline review implementation boundary

`profile/OpenStepMGAModeReview.{h,c}`는 R2 physical-profile pass, mode/timing/
pitch/mapping evidence, pixel-clock limit, alignment, visible footprint와
mapping bound를 동시에 요구하는 one-mode validator다. 이 policy는 G2가 아직
미통과인 현 상태에서 target 값이나 mode table을 만들지 않도록 설계되어 있다.

synthetic unit-test의 16 MiB/300 MHz/1600×1200×32 값은 target assertion이
아니다. 실제 R3 record에는 R2 evidence ID, geometry, bpp, pitch/alignment,
pixel clock, mapping length, rejected alternatives를 모두 연결해야 하며, G3
판정 전 mode programming source code는 추가하지 않는다. 상세 contract는
`R3_MANUAL_MODE_REVIEW_POLICY.md`를 따른다.
