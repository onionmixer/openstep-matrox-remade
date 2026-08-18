# 16 MiB display and render-memory budget

## Result

Under the current 16 MiB working assumption, the actual 1600x1200x32 scanout
can coexist arithmetically with one separate full-size 32-bit color surface,
but not with an additional full-size 16-bit depth surface.

| layout, before cursor/hidden reservations | bytes | remaining at 16 MiB |
| --- | ---: | ---: |
| current 1600x1200x32 scanout | 7,680,000 | 9,097,216 |
| scanout + one 1600x1200x32 color | 15,360,000 | 1,417,216 |
| scanout + one 1600x1200x32 color + 16-bit depth | 19,200,000 | reject |
| 1024x768x32 scanout + two 32-bit colors + 16-bit depth | 11,010,048 | 5,767,168 |

These are byte-accounting comparisons only.  The 1600x1200 color-only row
does not establish that the remaining 1,417,216 bytes are free of cursor,
hidden, or hardware reservations.  The 1024x768 double-color/depth row is a
promising future Mesa layout, but requires its own R2/R3 timing/pitch/mapping
record before it can be considered for a target run.

## Policy verification

`OpenStepMGARenderBudget` now has C89 regression coverage for all rows above,
as well as the prior 8 MiB negative cases.  It passed the host runner and the
target OPENSTEP `cc` runner on 2026-08-18; the target executable was confined
to `/tmp` and removed after the test.  No driver, display, mapping, or device
access occurred.

## Staged consequence

The first display-only replacement smoke may retain 1600x1200 after the
separate ownership/recovery and R2/R3 gates.  The first Mesa color-plus-depth
candidate should use a separately reviewed lower resolution rather than assume
the current high-resolution screen has enough remaining VRAM.

The renderer-internal choice is now fixed at 1024x768.  Its separate
presentation contract, including the no-assumption CPU scaling oracle for a
1600x1200 desktop, is `P3_1024_RENDER_TARGET_PRESENTATION.md`.

For the future hardware-candidate admission path, the R3-reviewed current
1600x1200 scanout plus the fixed 1024x768 double-color/depth renderer totals
15,544,320 bytes and leaves 1,232,896 bytes before explicit reservations.
`OpenStepMGAMesaAdmission` rejects the request unless its available total,
scanout footprint and alignment exactly match the accepted R3/R6 record.
This is still synthetic 16 MiB policy coverage, not a target allocation claim.
