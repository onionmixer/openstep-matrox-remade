# R3 — Single Manual-Mode Review Policy

기준일: 2026-08-18

## 목적과 범위

`profile/OpenStepMGAModeReview.{h,c}`는 승인된 deployment profile에 대해 작성할 **하나의**
manual display mode review의 증거와 산술 조건을 C89 pure-C로 고정한다.
이 module은 현재 카드의 mode를 추측하거나 등록하지 않는다. 특히 1600×1200
current-mode footprint나 `MGA Memory Size` 설정값만으로 review를 통과시키지
않는다.

이 module은 DriverKit, PCI/BAR/VRAM mapping, I/O register, DDC/I2C, DMA,
interrupt, mode setting API에 접근하지 않는다. 성공 결과도 hardware mapping,
mode programming, replacement bundle install/load의 권한을 주지 않는다.

## Required inputs

`OSMGAR3ManualModeReview`의 pass에는 아래 모든 항목이 필요하다.

| field/evidence | requirement | evidence source |
| --- | --- | --- |
| `physical_profile` | `OSMGAValidateR2PhysicalProfile` pass | approved deployment profile evidence record |
| `configured_vram_bytes` | accepted manual `MGA Memory Size` byte count와 R2 physical total이 exact equality | same R2 record; configuration is a consistency input, not discovery |
| `MODE_SOURCE` | geometry/format의 exact source | reviewed original/manual configuration or exact board/display documentation |
| `TIMING_SOURCE` | supplied pixel clock/timing의 exact source | exact board mode table or independently reviewed timing record |
| `PITCH_POLICY` | pitch and alignment rule | source-backed hardware/display policy |
| `MAPPING_BOUND` | planned mapping length bound | reviewed physical VRAM bound; no current owner allocation inference |
| `mode` | nonzero width, height, refresh | one proposed mode only |
| pixel clock | nonzero and no higher than R2 RAMDAC limit | same R2 board/head applicability |
| pitch | requested bpp's linear minimum 이상이며 declared alignment의 배수 | pure arithmetic check |
| mapping length | visible footprint 이상, physical VRAM total 이하 | pure arithmetic check |

The validator intentionally has no mode-table array API. A caller submits one
review object; multiple modes must be separate, independently reviewed records.

## Review output contract

Success returns `required_bytes = pitch_bytes * height`. This is only the
visible linear footprint. It is not a claim about cursor, scanout padding,
hidden allocation, usable offscreen memory, or coexistence with `MatroxMGA`.
Those claims remain outside R3 and, for P3, require the existing independent
admission gates.

Failure names distinguish incomplete R2 evidence, manual-memory/profile
mismatch, each missing R3 evidence class, over-limit pixel clock, alignment
error, framebuffer arithmetic failure, and mapping-range error. No fallback
mode or inferred memory capacity exists.

## Current status

The unit test contains synthetic 1600×1200×32, 16 MiB fixtures solely to cover
validator branches.  One uses the active operator-provided 16 MiB working
assumption, the actual target's current geometry, and a deliberately synthetic
162 MHz timing input.  It is not assigned to the target, does not set
`Default.table`, and does not advance G2/G3.  The active calculation and
remaining evidence are recorded in `R3_16M_WORKING_ASSUMPTION.md`; the former
8 MiB branch remains regression history only.

The approved first record is `reports/R3_G450_16M_DEPLOYMENT_MODE.md`; its
separate host/target C89 regression is part of the aggregate no-hardware
suite.  It does not authorize display activation.

`OpenStepMGATimingReview.*` additionally makes a complete timing shape's
totals and integer refresh independently checkable.  The current DMT-shaped
1600x1200@60 fixture is a comparison candidate documented in
`R3_DMT_TIMING_CANDIDATE_AUDIT.md`; it has no target `TIMING_SOURCE` authority.
The R3 validator now embeds that timing record and rejects an invalid shape or
any geometry/refresh/clock mismatch between the timing and manual-mode fields.
R6 mapping, PLL, and transaction policy consequently consume the same complete
R3 record; the transaction compares every porch, sync, and polarity field.

## Verification

```text
sh tools/check-mode-review-no-hardware.sh
sh tools/check-timing-review-no-hardware.sh
sh test/run-mode-review-host.sh
sh test/run-timing-review-host.sh
```

Both checks are host-only C89/source-purity checks. They execute no target
command and have no card or screen side effect.  On 2026-08-18 the active
16 MiB fixture was also compiled and run using target OPENSTEP `cc`, with only a
temporary `/tmp` executable that was removed after the pass.
