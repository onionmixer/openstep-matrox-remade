# R3 standard-timing candidate audit — current 1600x1200@60 geometry

## Scope and status

This audit provides the standard-timing basis for the operator-approved fixed
replacement deployment record for the target's current `1600x1200@60,
RGB:888/32` geometry.  It does not identify the timing programmed by the
original driver or the connected display's active timing; the record is a
replacement design choice, not a binary-behaviour claim.

The selected shape is the VESA DMT commonly named 1600x1200p60 and its
calculated refresh is exactly 60 Hz.  It is the sole first deployment mode
record; it is not an automatic mode-switch or register-programming sequence.

## Candidate timing shape

The local Linux UAPI `v4l2-dv-timings.h` entry
`V4L2_DV_BT_DMT_1600X1200P60` was read as a public standard-timing reference.
The project does not copy Linux code or invoke Linux interfaces.  The
standards-shaped values are represented only in an offline C89 regression
fixture.

| field | candidate value |
| --- | ---: |
| active pixels | 1600 x 1200 |
| pixel clock | 162,000 kHz |
| horizontal front / sync / back porch | 64 / 192 / 304 pixels |
| horizontal total | 2,160 pixels |
| vertical front / sync / back porch | 1 / 3 / 46 lines |
| vertical total | 1,250 lines |
| HSync / VSync polarity | positive / positive |
| calculated refresh | 60,000 mHz |

The arithmetic is `162,000,000 / (2,160 * 1,250) = 60` Hz.  This matches the
current original-driver configuration's geometry and stated 60 Hz refresh, but
that textual configuration has no DMT identifier or blanking fields.  Matching
geometry and refresh alone must not be interpreted as proof that the original
driver uses this exact shape.

## Offline verifier

`profile/OpenStepMGATimingReview.{h,c}` validates a complete active/porch/
sync/polarity/pixel-clock shape.  It rejects incomplete sync, invalid polarity,
arithmetic overflow, non-integral refresh, and a refresh that does not exactly
match the reviewed manual mode.  The result returns only horizontal total,
vertical total, and calculated refresh.  It contains no DriverKit interfaces,
device addresses, mapping, mode selection, or output programming.

`test/openstep-mga-timing-review-test.c` covers the candidate shape plus those
failure cases.  A passing test proves integer arithmetic and record integrity;
it does not prove applicability to this card, monitor, or output head.

The host strict-C89 runner and the target OPENSTEP `cc` runner both passed on
2026-08-18.  The target run created an executable only under `/tmp` and removed
it immediately after the result.  It did not load a driver or interact with the
display path.

## Deployment boundary

The selected timing, 16 MiB profile and pitch/mapping ceiling are assembled in
`reports/R3_G450_16M_DEPLOYMENT_MODE.md` and validate as one R3 record.  A
later owner-path run must still preserve the original display recovery profile
and may apply only this one reviewed mode.  DDC/EDID may reject or fall back
from it, but may not invent another timing.
