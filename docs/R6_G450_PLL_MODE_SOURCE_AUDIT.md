# R6 — G450 PLL and Mode-Transition Source Audit

기준일: 2026-08-18  
상태: offline source/lifecycle analysis and reviewed byte-image encoding only.
PLL/DAC/CRTC register access와 mode-setting code는 시작하지 않음.

## Sources and scope

The official X.Org `xf86-video-mga-2.0.0` archive was re-acquired solely as a
temporary analysis input on 2026-08-18 (SHA-256
`15b0f4cf3ee22eaefb45d54d1a0bf67ee710a292479a273fe3fd86f9fa802f41`) and reviewed
at `src/mga_g450pll.c`, `src/mga_dacG.c`, `src/mga_driver.c`, and `src/mga_reg.h`.
The local OPENSTEP 4.2 `IOFrameBufferDisplay.h` and S3/QVision examples provide
the lifecycle comparison. Findings are paraphrased design evidence; source and
register-write sequences are not copied into the project. The license boundary
is recorded in `refs/XORG_MGA_LICENSE_NOTICE.md`.

## Established facts

| topic | source finding | implementation consequence |
| --- | --- | --- |
| PLL input | G450 code computes candidate M/N/P tuples from a requested output clock and then ranks candidates by frequency error | a future calculator must consume an R3-reviewed **exact timing pixel clock**, not infer one from width × height × refresh |
| head selection | the source writes a pixel-PLL register set for the primary CRTC and a video-PLL set for a secondary CRTC | single-head versus secondary-head routing is a board/connector fact; PCI G450 family or dual-head catalogue wording cannot select it |
| lock | after programming it checks a lock status bit with bounded iteration and stability sampling | OpenStep needs a calibrated elapsed-time timeout plus a distinct lock-failure result; it must not retain a failed candidate or loop indefinitely |
| DAC path | PLL programming changes DAC-indexed state and clock selection/power control, not just a single MMIO word | DAC state is part of the display transaction and must be saved/reverted as a unit in replacement-only boot |
| mode state | X.Org mode code coordinates VGA/CRTC/DAC/PLL and framebuffer layout around the mode transition | R6 must not treat successful framebuffer mapping as evidence that a display mode can be programmed |

The X.Org source uses a 27 MHz reference and family-specific candidate bounds.
For the operator-approved offline PCI G450 16 MiB/1600×1200@60 record, the
reviewed one-head input now derives one fixed byte image in
`R6_G450_PLL_ENCODING_POLICY.md`. This does not establish a physical board
maximum or output/head routing. No runtime candidate search is permitted.

## Required future transaction

After G1–G4 pass and only in an approved replacement-only run, a single mode
transaction must be reviewed as an indivisible state machine:

1. verify selected R3 mode, applicable RAMDAC limit, primary/secondary output
   selection, and complete timing source;
2. enter the documented VGA-safe pre-transition state and save the limited
   replacement-owned state needed for rollback;
3. program reviewed CRTC/DAC/PLL values with display output sequencing that
   prevents partial visible output;
4. wait for PLL lock by a bounded wall-clock deadline; validate stable lock
   observations, not a single transient bit;
5. enable the reviewed linear mode and verify the first display output;
6. on any error, stop further writes, execute the reviewed VGA-safe rollback,
   call superclass revert lifecycle, and use the G1 failure snapshot rather
   than trying more PLL candidates on the active screen.

This plan deliberately differs from a general-purpose X.Org server: the first
OpenStep recovery test selects exactly one offline-reviewed timing. Candidate
search/retry in the live driver is prohibited.

## Current blockers

- exact physical P/N/max capacity remains separate from the operator-approved
  16 MiB deployment cap.
- primary output routing remains a recovery-profile constraint, not a result
  of automatic connector detection.
- G1 has no replacement-only configuration snapshot.
- No R6 rollback-state record exists.

Therefore no port/MMIO register header or writer is added to the replacement
source. The R4 gate continues to reject such code. The offline calculator,
CRTC geometry, and byte-image encoder are not linked into R4.

## Offline frequency-plan implementation

`profile/OpenStepMGAG450PLL.{h,c}` now implements only the arithmetic review
portion. It enumerates frequency candidates from the documented 27 MHz
family reference, 256–1300 MHz VCO envelope, and post-divider envelope, then
returns the lowest-error **frequency plan**. The plan contains requested and
achieved kHz, error ppm, VCO kHz, and abstract dividers. It intentionally does
not return M/N/P register bytes, DAC indices, MMIO offsets, port values, or a
write order.

The function first requires a passing R3 manual-mode review, a reviewed PLL
source flag, and an explicit primary/secondary head decision. Thus neither the
current geometry, nor `MGA Memory Size`, nor the PCI family label can obtain a
plan. The 162 MHz plan is the approved offline deployment record, not a live
target timing readback. A plan result does not authorize a DAC/PLL write.

`profile/OpenStepMGAG450PLLEncoding.{h,c}` subsequently encodes only a
reviewed plan into M/N/P bytes and names the primary pixel-PLL C or secondary
video-PLL target. It cannot perform a write and does not retry lock failures.
The 162 MHz primary result is the exact fixed image documented in
`R6_G450_PLL_ENCODING_POLICY.md`.

```text
sh tools/check-g450-pll-no-hardware.sh
sh test/run-g450-pll-host.sh
```

These are strict C89/source-purity checks only.

PLL lock later uses the generic terminal timeout/stability contract in
`R6_BOUNDED_POLL_POLICY.md`; no polling source is wired to DAC hardware yet.
