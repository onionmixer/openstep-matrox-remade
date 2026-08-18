# P3 — Software Reference Oracle

기준일: 2026-08-18

## 목적

P3 최초 hardware test는 safe offscreen range에 32-bit color clear를 제출한 뒤
readback 결과를 software reference와 비교하는 것이다. 이 문서는 그 comparison
side만 정의한다. `reference/OpenStepMGAReference.c`는 ordinary caller-owned
memory만 다루며 PCI, VRAM mapping, DriverKit, MGA register, DDC를 알지 못한다.

따라서 이 module의 host test 통과는 hardware access admission이나 P3 success
claim이 아니다. P3가 시작할 수 있는 조건은 여전히
`P2_RESOURCE_OWNERSHIP.md`와 `OSMGACanEnterP3`의 five-evidence gate다.

## API contract

| API | 입력 | 보장 | 하지 않는 일 |
| --- | --- | --- | --- |
| `OSMGAReferenceClear32` | `width`, `height`, `stride_pixels`, color | active pixel만 32-bit color로 설정; row padding 보존 | framebuffer map, cache flush, hardware clear |
| `OSMGAReferenceChecksum32` | active pixel rows | byte order를 low→high로 고정한 32-bit FNV-1a checksum | hardware readback 또는 DMA |
| `OSMGAReferenceClearRect32` | caller-owned surface의 in-bounds rectangle | active pixels만 clear하고 padding 보존 | target clear submit |
| `OSMGAReferenceCopyRect32` | 서로 다른 caller-owned surface의 in-bounds rectangle | padding 보존 copy; self-copy/overlap은 거부 | target copy submit |
| `OSMGAReferenceCompare32` | expected/actual memory | first row-major active-pixel mismatch index, 또는 compared pixel count | padding 비교, device state 변경 |
| `OSMGAReferenceFillTriangle32` | integer vertices, color | pixel-center sampled triangle expected image; winding-normalized | hardware triangle command, clipping state, depth/texture |
| `OSMGAReferenceDepthTestWrite16` | one pixel, stored/incoming 16-bit depth, function | `NEVER`, `LESS`, `LEQUAL`, `ALWAYS` compare and optional depth write | Z buffer map, hardware depth state |
| `OSMGAReferenceBlendSrcAlpha32` | non-premultiplied `AARRGGBB` src/dst | integer-rounded `SRC_ALPHA`/`ONE_MINUS_SRC_ALPHA` color and source-over alpha | blend register/programming |
| `OSMGAReferenceSampleTextureNearestClamp32` | caller-owned texels, unsigned 16.16 `u/v` | nearest-floor, clamp-to-edge `AARRGGBB` texel | texture upload/address register, repeat/filter state |

geometry가 null/zero/invalid stride이거나 32-bit row-offset 범위를 넘으면 모두
fail closed 한다. checksum/compare output pointer가 null이면 실패하며 caller
memory를 수정하지 않는다.

triangle oracle은 OPENSTEP i386 signed-32-bit edge arithmetic 범위를 보장하기 위해
framebuffer dimension과 vertex를 `4095` 이하/절대값 `4095` 이하로 제한한다. 이 범위는
현재 1600x1200 validation target을 포함한다. degenerate triangle은 실패하고,
completely clipped triangle은 padding을 건드리지 않은 성공으로 끝난다.

## 고정 regression vector

host C89 regression은 `width=4`, `height=3`, `stride=6`, color `0x11223344`를
사용한다.

- active 12 pixels만 clear되고 각 row의 두 padding pixels는 `0xdeadbeef`로 유지된다.
- active bytes의 canonical checksum은 `0xb32af285`이다.
- identical result의 compare index는 `12`이며, 마지막 row의 두 번째 active pixel
  하나를 바꾸면 first mismatch index는 `9`다.

이 vector는 target byte order, framebuffer pitch interpretation, readback test
format의 source-level contract를 고정한다. 실제 MGA readback은 P3 admission 후
동일한 selected mode/color format에서 별도 test program으로 받는다.

triangle regression은 `(0,0)`, `(4,0)`, `(0,3)`의 six active pixels와 reversed
winding의 동일 결과를 고정한다. depth, texture, blend는 clear/flat triangle의
hardware repeated-run 검증 뒤에 별도 oracle로 추가한다.

depth regression은 stored value `100`에서 `LESS` `50`이 color/depth를 모두
갱신하고, `LESS` `120`이 둘 다 보존하며, equal `LEQUAL` color-only write와
`ALWAYS` depth write를 분리한다. 이 oracle은 one pixel operation만 다루므로,
future P3 depth triangle test는 raster coverage 결과와 depth operation을 명시적으로
합성해 비교해야 한다.

blend regression은 half-alpha red over alpha-`0x40` blue를 canonical
`0xa080007f`로 고정한다. fully transparent source는 destination을 보존하고,
fully opaque source는 source를 보존한다. blend hardware test는 first clear/flat
triangle/depth test가 stable한 뒤 이 exact non-premultiplied format으로만 시작한다.

texture regression은 2x2 `AARRGGBB` texels에서 origin, 1.9375 floor sample, 그리고
out-of-range clamp-to-bottom-right sample을 고정한다. REPEAT, bilinear filtering,
texture environment combine, mipmap은 이 reference/API의 범위가 아니며 Mesa
software fallback으로 남긴다.

## future P3 clear test sequence

1. `OSMGACanEnterP3`가 ready가 되기 전에는 test binary를 build/load하지 않는다.
2. kernel side가 independently owned offscreen base/length를 validate하고,
   command range가 그 allocation을 벗어나면 submit을 거부한다.
3. user test는 clear parameter와 oracle expected buffer/checksum만 준비한다.
   user process가 raw BAR/VRAM address를 전달하지 않는다.
4. kernel readback result는 bounded copied buffer로 user side에 반환한다.
5. `OSMGAReferenceCompare32`와 checksum 둘 다 일치해야 clear pass다. timeout,
   mismatch, range violation은 다음 command를 금지하고 recovery record를 남긴다.

clear, flat triangle, depth, source-alpha blend, nearest/clamp texture의
software oracle만 구현했다. hardware target에서는 clear부터 cold boot와 repeated
run으로 안정화한 뒤, 한 state마다 독립적으로 readback을 비교한다.
