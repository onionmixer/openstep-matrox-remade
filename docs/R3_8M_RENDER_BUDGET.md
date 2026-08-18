# 8 MiB display and render-memory budget (superseded)

## Result

With the current target geometry (`1600x1200`, 32-bit scanout), the former 8 MiB
working capacity is sufficient for the visible scanout lower bound only.  It
is not sufficient for a separate full-size render color surface, with or
without a depth surface.

| layout, before cursor/hidden reservations | bytes | result at 8 MiB |
| --- | ---: | --- |
| current 1600x1200x32 scanout | 7,680,000 | 708,608 bytes remain |
| scanout + one 1600x1200x32 color surface | 15,360,000 | reject |
| scanout + one 1600x1200x32 color + 16-bit depth | 19,200,000 | reject |
| 1024x768x32 scanout + one 32-bit color + 16-bit depth | 7,864,320 | 524,288 bytes remain |

The 1024x768 row is an arithmetic comparison, not an approved mode change.
It still needs an R2 profile, exact timing, pitch, scanout reservation, and
mapping review.  It is useful because the target-original 8 MiB catalogue has
a 1024x768x32 geometry entry, while the current 1600x1200 screen mode leaves
too little memory for Mesa-style separate render/depth surfaces.

## Policy implementation

`profile/OpenStepMGARenderBudget.{h,c}` takes only reviewed caller-supplied
byte totals: available capacity, scanout allocation, explicit cursor/hidden
reservation, render dimensions/formats, surface count, and pitch alignment.
It returns aligned pitches, each surface size, total use, and remaining bytes.
It rejects overflow, invalid format/alignment, and any over-budget layout.

It cannot determine physical VRAM, current scanout ownership, cursor storage,
or actual memory mapping.  A passing result therefore does not authorize a
surface allocation, display mode, driver load, or rendering submission.

The host strict-C89 and target OPENSTEP `cc` runners passed on 2026-08-18.  The
target test executable was created only in `/tmp` and deleted immediately; no
driver or display path was touched.

## Consequence for staged work

1. A future replacement-display linear smoke may keep the current 1600x1200
   geometry only as a display-only candidate, after G1/G2/G3/G4 and explicit
   replacement-only approval.
2. A Mesa color-plus-depth test under the 8 MiB assumption needs either a
   separately reviewed lower output/render geometry or a deliberately scoped
   system-memory fallback.  It must not claim the 708,608-byte remainder as
   usable offscreen memory.
3. Double buffering at 1024x768 with a 16-bit depth surface exceeds this
   simple 8 MiB budget and is not a first test candidate.
