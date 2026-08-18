# P3 fixed 1024x768 render target and presentation contract

## Decision

Mesa's first fixed-function render target is fixed at `1024x768`, independent
of the physical desktop resolution.  This is a renderer-internal surface
choice, not an instruction to change the OPENSTEP display mode.

It keeps the initial Mesa color/depth target bounded and makes tests
reproducible.  Physical VRAM capacity still determines whether those surfaces
can live in VRAM; a system-memory fallback remains valid for rendering but is
not hardware acceleration.

## Presentation paths

| desktop output | first permitted presentation | prohibited assumption |
| --- | --- | --- |
| 1024x768 | same-size copy to reviewed scanout | implicit ownership or mapping |
| 1600x1200 | CPU nearest-neighbor scale from 1024x768, then reviewed output copy | Matrox stretch-blit/scaler availability |
| other output | explicit presentation policy required | arbitrary hardware scaling or mode switch |

The fixed target and the current 1600x1200 output have the same 4:3 aspect
ratio.  The CPU reference scale therefore fills the destination without
letterboxing.  It is intentionally nearest-neighbor: it supplies a
deterministic correctness oracle, not a final quality or performance claim.

## No-hardware reference implementation

`OSMGAReferenceScaleNearest32()` scales between two distinct caller-owned
32-bit surfaces, preserves destination padding, and rejects aliasing and
unsafe coordinate multiplication.  It has no display, DriverKit, mapping, or
device dependency.  The host and target OPENSTEP C89 runs passed on
2026-08-18; the target executable was confined to `/tmp` and deleted after the
test.

This function is not connected to the replacement display driver.  A future
call that writes the real scanout must wait for G1/G2/G3/G4 and explicit
replacement-only authorization.  Hardware stretch-blit may be considered only
after an independent 2D capability and rollback review; it is not a fallback
for the CPU reference path.

## Memory consequence under the 16 MiB working assumption

The 1024x768 32-bit color plus 16-bit depth target requires 4,718,592 bytes.
With a 1024x768 scanout and two color surfaces, the pure budget policy reports
11,010,048 bytes total, leaving 5,767,168 bytes before reviewed reservations.
With the existing 1600x1200 scanout, the same render target is only an
arithmetic possibility until scanout/cursor/reservation and mapping records are
completed.  `R3_16M_RENDER_BUDGET.md` is the authoritative budget record.
