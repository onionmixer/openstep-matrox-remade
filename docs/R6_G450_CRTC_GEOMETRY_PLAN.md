# R6 — G450 one-mode CRTC geometry plan

`profile/OpenStepMGAG450CRTCPlan.{h,c}` converts only an accepted R3 record
into the geometry boundaries a future G450 recovery writer must consume.  It
does not contain CRTC/DAC register numbers, an I/O primitive, a mapping call,
or a mode-programming sequence.

For the approved PCI G450 16 MiB record it produces:

| field | value |
| --- | ---: |
| pixel clock | 162,000 kHz |
| horizontal display / sync start / end / total | 1600 / 1664 / 1856 / 2160 |
| vertical display / sync start / end / total | 1200 / 1201 / 1204 / 1250 |
| pitch / scanout | 6400 / 7,680,000 bytes |
| sync polarity | positive / positive |

The plan first re-runs `OSMGAValidateR3ManualModeReview`, accepts only the
current 32-bit format, and uses checked additions for every timing boundary.
It rejects a missing/mutated R3 record before returning any values.

Host strict-C89 and target OPENSTEP C89 tests passed on 2026-08-18. The target
binary existed only under `/tmp` and was removed. The separate data-only
primary-head byte encoder is documented in
`R6_G450_PRIMARY_CRTC_IMAGE_POLICY.md`; neither it nor this plan performs a
hardware register write. A future recovery-only writer must separately map its
documented resource, apply the G450 PLL/DAC/CRTC transaction, prove bounded
lock, and retain the P-failure original recovery path.

`OSMGABeginModeTransaction` now carries this checked plan together with its
PLL plan. Therefore a future writer cannot receive a transaction object whose
frequency plan was validated while its geometry was omitted or whose 16-bit
format was silently substituted. This remains an offline C89 invariant, not a
permission to program a CRTC.
