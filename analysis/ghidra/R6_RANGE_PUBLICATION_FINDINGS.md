# Original range-publication findings (behavior-level)

Date: 2026-08-18

## Artifact and method

- Input: `reference/original-binaries/MatroxMGA_reloc.R0-20260818`
- Identity: target/local `sum` `45628 103`, MD5
  `49fd552d8562c85b95cbbb5cafd317d0`
- Tool: Ghidra 12.1 headless import plus `MatroxMGAFocusDecompile.java`
- Scope: static local analysis only; no OPENSTEP target connection or runtime
  driver execution.

## Cross-checked findings

The initialization path at the pre-existing focus address confirms this
behavioral order:

1. configure and select a display mode;
2. run original hardware-sensitive probe logic;
3. accept an explicit nonzero manual memory size only in the already recorded
   3..63 MiB range;
4. derive a framebuffer range length from that accepted size;
5. publish three legacy ranges: framebuffer, VGA aperture, and VGA BIOS;
6. invoke the legacy framebuffer mapping API with the derived framebuffer
   base/length.

The recovered code is not copied here. It is not an authorization to replay
the original probe, map, or mode-setting flow.

## New-code consequence

`profile/OpenStepMGAG450RangePlan.{h,c}` is independently written data-only
code. It fixes the approved recovery deployment shape to 16 MiB plus legacy
VGA/BIOS windows, requires an externally verified synthetic-or-reviewed
page-aligned framebuffer base, and performs neither DriverKit publication nor
memory mapping. Details and test boundary are in
`docs/R6_G450_RANGE_PUBLICATION_PLAN.md`.
