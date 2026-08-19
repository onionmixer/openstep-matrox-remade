# W1 — Windows G400/G450 driver analysis task

Date: 2026-08-19
Status: task definition. The analysis is carried out by codex + IDA (headless);
results go to `docs/W1_WINDOWS_DRIVER_FINDINGS.md`.

(This file is written in English because the analysis is delegated to codex,
which works more accurately in English. The rest of this project's docs are in
Korean.)

## 0. Why

Several open questions cannot be settled by experiment on the real machine, or
would cost many reboot cycles to settle. Matrox's Windows driver package
contains 3D code for exactly our chip.

- `G450.inf` names `g400icd.dll` / `G400.vxd` / `G400DD32.dll`, so **the G450 is
  driven by the G400 binaries** — matching this project's own decision to treat
  the G450 as a G400.
- Reconnaissance by 32-bit immediate search (presence only, use unverified):

| Binary | Offsets seen |
| --- | --- |
| `G400DD32.dll` (DirectDraw/D3D HAL, 962 KB) | `AR0`, `AR3`, `AR5`, `SGN`, `DR6`, `DR12`, `WIADDR2`, `WVRTXSZ`, `WACCEPTSEQ`, `SECADDRESS`, `FXBNDRY`, … |
| `g400icd.dll` (OpenGL ICD, 958 KB) | almost none — user mode, delegates to the HAL |
| `G400.vxd` (kernel, 1.6 MB) | mostly 2D |

→ **`G400DD32.dll` is the primary target.**

## 1. Boundaries (must hold)

- These are proprietary Matrox binaries. **Analyse only.** Do not copy code,
  constant tables, or disassembly output into this repository as source.
- Record results as a specification of external behaviour: *which register
  receives which value, in what order, derived from what input*.
- This is the same rule the project already applies to the original OPENSTEP
  `MatroxMGA` binary (see `refs/SOURCES.md`).

## 2. Questions, in priority order

### A. Sloped trapezoids — this is what currently blocks step D3-2

1. Find the code that writes `AR0` (0x1c60), `AR1` (0x1c64), `AR2` (0x1c68),
   `AR4` (0x1c70), `AR5` (0x1c74), `AR6` (0x1c78) and `SGN` (0x1c58).
2. How are those values computed from triangle vertices? In particular the
   exact definition and initial value of the quantity X.Org's XAA interface
   calls `e` (a Bresenham error term). We were about to guess `e = 0`.
3. **Does the trapezoid path write `AR3` (0x1c6c)?** This is an open item in
   `REMAINING_WORK.md` §3-5. The X.Org DDX does not write it in its trapezoid
   path, and the original OPENSTEP driver never touches the drawing engine at
   all, so neither source can answer it.
4. How is one triangle split into trapezoids, and on what criterion?

### B. Colour interpolation — cross-check against our hardware measurement

Measured on the real card (`D3_RASTERISER_PATH_PLAN.md` §8, §9):
`DR = colour << 15`; starts in `DR4`/`DR8`/`DR12`; x increments in
`DR6`/`DR10`/`DR14`; y increments in `DR7`/`DR11`/`DR15`.

5. Does this binary use the same scale? **If it differs, say so plainly** —
   the disagreement itself is valuable.
6. What are `DR0` / `DR2` / `DR3` (0x1cc0 / 0x1cc8 / 0x1ccc)? Z interpolators?
7. How are `ZORG` (0x1c0c) and the `DWGCTL` atype `ZI` (3 << 4) used?

### C. The WARP path — nothing else available to us can answer this

8. What values are written to `WVRTXSZ` (0x1dcc), and what vertex structure
   size and composition does each imply?
9. What is the **memory layout of the vertex buffer** submitted through
   `SECADDRESS` (0x2c40) / `SECEND` (0x2c44) — field order, types, fixed point?
10. How and when is a pipe selected through `WIADDR2` (0x1dd8)?
11. Where is the microcode placed and how is its address computed?

### D. 3D `DWGCTL` constants

12. Which `DWGCTL` values are used for triangle rasterisation, and what does
    each bit mean? We have `TRAP|I` = `0x000C7074` working on hardware.

## 3. Deliverable

A single file, `docs/W1_WINDOWS_DRIVER_FINDINGS.md`, numbered to match section
2. For each item give:

- **addresses** (function entry and the specific instruction)
- **observation** and **inference**, separated and labelled
- anything you could not establish, marked **"not determined"**, with a note on
  what evidence would settle it

## 4. Tools

- ida-pro-mcp plugin, or IDA 9.3 headless at
  `/mnt/USERS/onion/DATA_ORIGN/local/ida-pro-9.3/idat`
- Targets under `scratch/matrox-win/`. `scratch/` is gitignored, so put
  intermediate artefacts there.

## 5. Method discipline (applies to the analysis too)

- **Validate a search method with a control before trusting a negative
  result.** This project drew two wrong conclusions in one day from searches
  whose method was silently broken.
- If a register offset does not appear as a 32-bit immediate, consider
  base+displacement addressing, or the DMA index encoding: group 0 registers
  `0x1c00..0x1dff` encode as `(reg - 0x1c00) >> 2`, group 1 registers
  `0x2c00..0x2dff` as `((reg - 0x2c00) >> 2) | 0x80`.
- B5 is an independent check against a hardware measurement, so it needs to be
  right rather than quick.
