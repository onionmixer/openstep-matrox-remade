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

## 2A. Localisation already done (start here)

A first attempt at this task timed out after 30 minutes with no output,
almost certainly because the scope was too wide. The candidate sites have
since been narrowed with objdump, so **start from these addresses** rather
than searching the whole binary.

Method used: disassembled `G400DD32.dll` with objdump, then separated
**stores** (`mov %reg,disp(%base)`) from **loads**, because MMIO register
programming appears as stores. Load-only sites are almost certainly driver
context-structure fields, not registers.

Store sites, by register offset:

| Offset | Name | Store addresses |
| --- | --- | --- |
| 0x1c58 | SGN | baa41b17 baa41d80 baa4ccf7 baa4fd74 |
| 0x1c60 | AR0 | baa1515f baa18015 baa1a364 baa41b23 baa41bba baa41d44 baa420b2 baa4280d |
| 0x1c64 | AR1 | baa34006 |
| 0x1c68 | AR2 | baa12fa5 baa15159 baa1800f baa1a35e |
| 0x1c6c | AR3 | baa12fab baa22f2b baa22f6d baa22f97 baa33f64 baa33f92 baa33fae baa41b1d |
| 0x1c70 | AR4 | baa12fb7 baa1518c baa18042 baa1a391 |
| 0x1c74 | AR5 | baa12fbd baa41b2d baa41d4e baa420ec baa42837 baa42c5a baa4ccb5 baa4fd4f |
| 0x1c78 | AR6 | baa12fc3 |
| 0x1c84 | FXBNDRY | baa41b49 baa41bd0 baa41d67 baa420ce baa4281d baa42c2e baa49406 baa4970e |
| 0x1c88 | YDSTLEN | baa41d76 baa49415 baa4971d |
| 0x1cd8 | DR6 | baa22c9f baa22e37 |
| 0x1cf0 | DR12 | baa0e265 baa0e293 baa13ba0 baa25ea7 baa28690 |
| 0x1dcc | WVRTXSZ | baa355a3 baa356f5 |
| 0x1dd8 | WIADDR2 | baa35724 |

Clusters worth looking at first:

1. **baa41b17..baa41b49** — SGN, AR3, AR0, AR5, FXBNDRY within ~50 bytes.
   This is the strongest candidate for sloped-trapezoid setup, and it
   includes an AR3 store, which bears directly on question A3.
2. **baa41d44..baa41d80** — AR0, AR5, FXBNDRY, YDSTLEN, SGN.
3. **baa355a3..baa35724** — WVRTXSZ twice then WIADDR2: WARP pipe selection.
4. **baa12f73..baa12fd5** — a dense sweep of many nearby offsets including
   `movl $0x1807,0x1df4(%esi)`. **Treat this one with suspicion**: 0x1807 is
   the known G400 WVRTXSZ value, but it is being written to 0x1df4 while
   WVRTXSZ is 0x1dcc, so this may be a shadow register file inside a driver
   context structure rather than MMIO. Determining which it is would itself
   be a useful result.

**First thing to establish for any of these: is the base register the MMIO
aperture, or a driver context structure?** Every conclusion below depends on
that, and the answer is not assumed here.

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

## 2B. Run 2 — search by opcode, not by register

Run 1 answered its questions accurately but reached a wrong conclusion,
and the fault was in this briefing. It pre-labelled an address cluster
"strongest candidate for sloped-trapezoid setup", and the analysis
inherited the label: the function at `0xbaa418f0` writes SGN/AR3/AR0/AR5/
FXBNDRY, but the next three instructions are LEN, YDST, and
`DWGCTL+EXEC = 0x840c4008` — opcode 8, **BITBLT**. Not a trapezoid at all.

The right discriminator is the `DWGCTL` opcode, not which registers appear
nearby. Opcodes (low 4 bits): `0x4` TRAP, `0x6` TEXTURE_TRAP, `0x8` BITBLT,
`0x9`/`0xd`/`0xe`/`0xf` ILOAD family, `0xa` IDUMP. atype is bits 4-6:
`0` RPL, `1` RSTR, `3` ZI, `7` I. `ARZERO` is bit 12, `SGNZERO` bit 13,
`CLIPDIS` bit 31.

**Sloped edges require ARZERO and SGNZERO to be CLEAR.** Everything found
so far in this binary has them set, i.e. axis-aligned.

A census of *constant* writes to `DWGCTL` (0x1c00) and `DWGCTL+EXEC`
(0x1d00) is complete: 13 sites, 7 distinct values, all BITBLT or ILOAD
except one — `0xbaa620fb` writes `0x000c7076` (TEXTURE_TRAP, atype I,
ARZERO and SGNZERO set).

### The remaining question

Eleven sites write `DWGCTL` from a **register**, so their opcode cannot be
read statically:

```
baa156ca  baa184db  baa1ab4d  baa25f1d  baa28836  baa41e3a
baa42c7c  baa4942b  baa49739  baa542c4  baa559cb
```

For each: what value reaches `DWGCTL`, and specifically —

13. Which opcode(s) can each site emit? Where does the value come from
    (constant table, computed, cached in a context field)?
14. **Does any site emit a value with ARZERO or SGNZERO clear?** That is
    the sloped-trapezoid path, and it is what this project needs.
15. If such a site exists, what writes AR0/AR1/AR2/AR4/AR5/AR6/SGN before
    it, and how are those values derived?
16. If no such site exists — i.e. the HAL never draws sloped edges through
    the DWG engine — say so. That is a real answer: it would mean the
    shipping driver leaves all triangle setup to WARP, and that this
    project's direct-rasteriser approach has no reference implementation
    on Windows either.

Do not assume 16 is false because it would be inconvenient.
