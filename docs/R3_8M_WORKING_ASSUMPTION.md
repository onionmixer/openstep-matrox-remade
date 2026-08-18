# R3 8 MiB working-assumption review (superseded)

## Status

The operator previously supplied `8 MiB` as the working VRAM capacity.  This
document is retained for conservative arithmetic and regression history;
`R3_16M_WORKING_ASSUMPTION.md` is now the active planning record.
coverage, but it is **not** physical board evidence.  In particular, it does
not fill the required R2 board, independent cross-check, VRAM type, VRAM size,
or RAMDAC/head evidence references.  Therefore it changes neither G2 nor G3
to `PASS`, and it does not authorize a replacement table, mapping, mode
programming, or driver load.

## Primary geometry candidate: current target mode

The actual target's original-driver preflight records the current mode as
`Height:1200 Width:1600 Refresh:60Hz ColorSpace:RGB:888/32`.  This is the
primary R3 geometry candidate because it is the resolution already in use on
the real screen.  It is stronger geometry evidence than selecting a different
catalogue mode merely because the working capacity is 8 MiB.

The observed record still does not supply an exact pixel clock, blanking,
pitch, scanout reservation, RAMDAC/head selection, or mapping length.  It is
therefore not a complete R3 timing source and cannot be programmed from this
document alone.

`R3_DMT_TIMING_CANDIDATE_AUDIT.md` records a complete 162 MHz VESA-DMT-shaped
comparison record for this geometry.  The original configuration does not name
that standard or provide its blanking fields, so it remains a candidate rather
than target timing evidence.

For a tightly packed 32-bit framebuffer:

| item | value |
| --- | ---: |
| geometry | 1600 x 1200 |
| bytes per pixel | 4 |
| minimum visible footprint | 7,680,000 bytes |
| working capacity | 8,388,608 bytes (8 MiB) |
| capacity remaining before pitch/cursor/hidden allocations | 708,608 bytes |

The `profile/OpenStepMGAModeReview.*` regression fixture also exercises this
shape with `pitch=6400`, `mapping=8 MiB`, and a **synthetic** 162 MHz clock.
That clock is solely a policy branch input; it is not a timing value proposed
for the card.  The test proves that the validator's exact configured-memory
equality and footprint arithmetic work at 8 MiB.  It additionally rejects a
mapping one byte above 8 MiB and one byte below the visible footprint; it does
not prove that the target can be programmed this way.

## Required evidence before an R3 table review

1. Complete `docs/reports/R2_PHYSICAL_PROFILE_EVIDENCE.md` with physical
   board/VRAM/RAMDAC evidence and independent correlation.  The supplied
   8 MiB value can be recorded there only with its source and reviewer.
2. Obtain an exact timing source for the current 1600x1200@60 mode (or a
   separately justified lower-resolution fallback), including pixel clock and
   blanking applicable to the physical head.
3. Establish pitch alignment, scanout/cursor reservation, and a mapping bound
   without exceeding the physically verified VRAM total.
4. Enter those facts in `R3_MANUAL_MODE_TABLE_REVIEW.md`, then validate the
   record with `OSMGAValidateR3ManualModeReview()`.

Until all four are reviewed, `MGA Memory Size=8` remains an optional future
configuration input only.  It is intentionally not written to the fail-closed
replacement `Default.table`.

The resulting scanout/render split is documented separately in
`R3_8M_RENDER_BUDGET.md`: the current 1600x1200 mode is display-only under the
8 MiB arithmetic assumption, not a separate color/depth rendering candidate.

## Verification

`test/openstep-mga-mode-review-test.c` now contains the explicitly synthetic
8 MiB current-mode branch.  It passed both the host strict-C89 runner and the target-native
OPENSTEP `cc` runner on 2026-08-18.  The target invocation creates its test
binary only in `/tmp` and removes it after the pass; it neither loads a driver
nor accesses device memory, MMIO, DAC, PLL, CRTC, or DDC.
