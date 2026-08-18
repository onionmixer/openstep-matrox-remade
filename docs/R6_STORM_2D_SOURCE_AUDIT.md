# R6 — Storm 2D Engine Source Audit

기준일: 2026-08-18  
상태: 공개 source의 register/lifecycle 분석 완료. target MMIO 또는 engine 접근은
시작하지 않음.

## Source and license boundary

Analysis used the local unpacked official X.Org `xf86-video-mga-2.0.0`
archive, specifically `src/mga_storm.c`, `src/mga_reg.h`, and
`src/mga_macros.h`. Its `COPYING` begins with permissive XFree86/Open Group
terms. This project records behaviour-level findings only: no X.Org source,
macro, or register-programming sequence is copied into project code. Any
future implementation must retain its own attribution/license review.

## What the source establishes

The X.Org 2D path has an explicit engine initialization boundary, separate
from display mode/PLL setup and separate from DRI DMA work.

| area | observed X.Org behaviour | implication for OpenStep design |
| --- | --- | --- |
| synchronization | it waits for engine idle; its DRI quiescence hook is conditional | first OpenStep 2D path must be synchronous/polled and must not import Linux/DRI DMA assumptions |
| FIFO | it reads a FIFO-status byte and waits before multi-register emission | a bounded wait/timeout policy is required before any write sequence; infinite busy loops are unacceptable |
| layout state | it derives pitch, destination origin and pixel-access state from already selected layout | R3/R6 reviewed mode/mapping data must be the only source of these values; configuration defaults cannot substitute |
| basic state | initialization sets pitch, destination origin, pixel access, plane mask, foreground/background, operation mode and clipping bounds | these are writes that can alter rendering state; they belong after a known-good replacement display mode, never in probe/init of R4 |
| G400 path | the source groups G400 with later chips for source/destination-origin setup | this is family-level implementation evidence, not proof that the target's exact G450 board/range layout is safe |
| 2D primitives | fill/copy operations program drawing-control and coordinate/boundary registers, then use an execution-triggering write | first hardware test must use a formally bounded offscreen surface, not current scanout |

The source's register definitions identify distinct groups: drawing state and
coordinates around the `0x1c00` block, FIFO/status around `0x1e10`, operation
mode around `0x1e54`, and source/destination origin around `0x2cb4`. These are
reference facts, not OpenStep constants and not permission to map a target
MMIO range.

## OpenStep implementation constraints derived from the audit

1. **No DRI/DMA port.** The initial 2D bring-up must not reuse X.Org DRI
   quiescence, DMA buffers, SAREA, IRQ, or XAA/EXA integration.
2. **No inherited engine state.** The replacement driver must establish every
   state field it relies on after its own successful mode transition. It must
   not assume state left by `MatroxMGA`.
3. **Bound every wait.** FIFO/idle checks need a monotonic bounded retry
   policy and a recovery transition on timeout; an unbounded `while busy`
   loop is not acceptable in a boot display driver. The pure-C terminal
   timeout/stability state machine is `R6_BOUNDED_POLL_POLICY.md`; it does not
   choose actual timeout values.
4. **Start offscreen only.** No draw/copy test may name an address in visible
   scanout. It first requires the P3 existing-owner/offscreen and mapping
   evidence gates, plus a separately reviewed replacement-only allocation.
5. **One primitive at a time.** The planned order is engine synchronization →
   known-state initialization → offscreen solid clear → checksum/readback via
   the reference oracle → offscreen copy. Textures, blending, triangles, DMA,
   cursor, overlay and dual-head remain out of the first 2D milestone.

## Admission sequence

| stage | required prior evidence | permitted action | stop condition |
| --- | --- | --- | --- |
| S0 | G1–G4 PASS + explicit R6 approval | compile reviewed replacement-only source | any configuration/matching mismatch |
| S1 | R6 mapping configuration review PASS | map reviewed framebuffer range; do not access engine registers | map/range/cache mismatch |
| S2 | source-backed MMIO range review, separate approval | bounded read-only status/FIFO characterization | invalid read, timeout, display anomaly |
| S3 | S2 stable + replacement display output/recovery verified | initialize only minimal 2D state, no drawing | timeout or state/output anomaly |
| S4 | reviewed offscreen allocation and P3 admission | one bounded offscreen solid-clear/readback oracle | checksum mismatch or any visible change |

No stage authorizes a target action merely because this document exists. G3 is
an offline 16 MiB fixed-mode pass only; S0 is not currently open because G1,
G2 and mapping/ownership evidence are still pending.

## Current implementation status

There is deliberately no Storm register header, MMIO helper, FIFO wait loop,
or draw command in `OpenStepMGAReplacementDisplay` or `OpenStepMGAService`.
The R4 static gate continues to reject mapping/device/display-programming
symbols. This keeps the existing `MatroxMGA` as the only production display
owner.
