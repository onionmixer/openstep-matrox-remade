# R12 -- stage 4: the ceiling follows the declaration and the survey

Implements `R9_VRAM_DECLARATION_BUILD_PLAN.md` stage 4. Written before any
code. Stages 1-3 are built, installed and verified on the machine; the boot
of 2026-08-26 11:49 ended with

```
M1-4F1: offscreen window OPENED 9322496..12582912 (3184 KiB), proof passed
R10:    1600x1200 RGB:888/32 -- 16.0 MB gives 3.1 MB offscreen, GL up to 800x600
caps:   ENABLED yes / MMAP yes / CMD yes / READY yes -> VERDICT: hardware
```

This stage is what turns that 3.1 MB into 19.1 MB, and 800x600 into a
full-screen buffer.

---

## 1. What the machine has already told us

The survey has run on three boots and said the same thing every time:

```
survey 0:30.0 window f8000000..fa000000 (ours)
survey 3:13.0 window f8000000..fa000000 (ours)
survey of f8000000..fa000000 over 9 claims: clear
base alignment allows up to 131072 KiB
```

Both ancestor bridges forward **exactly** `0xf8000000..0xfa000000` -- 32 MiB,
exactly the range in question -- and the only function behind them is the MGA
itself, whose only prefetchable base register is BAR0. That is a second,
completely independent confirmation of the 32 MiB BAR0 that `TEST_STATUS.md`
H1 S1 measured by sizing, obtained **without writing a single byte to
configuration space**.

It is still not proof that the *memory* is there. The survey says it is safe
to ask; the boot proof is what answers.

## 2. What changes

```
surveyed  = survey clear for 32 MiB ? 32 MiB : 16 MiB
declared  = OSMGAVramDeclaration("MGA Memory Size")      -- 16 or 32
ceiling   = OSMGAWindowCeiling(declared, surveyed, PAGE_SIZE)
          = min(declared, surveyed) - 4 MiB, page aligned
```

| declared | surveyed | ceiling | 1024x768 window | 1600x1200 window |
|---|---|---|---|---|
| 16 | 32 | 12 MiB | 8,388,608 | 3,260,416 |
| **32** | **32** | **28 MiB** | **25,165,824** | **20,037,632** |
| 32 | 16 | 12 MiB | 8,388,608 | 3,260,416 |

The last row is the safety case: a declaration the machine does not support
is narrowed by the survey, never the other way round.

The 4 MiB margin is unchanged. Its reason (`:206-224`) is unknown
top-of-VRAM uses, which is not a proportional quantity.

## 3. The proof becomes two stages, and still opens once

R11 inherited the rule from cross-review: **every proof that may write below
the region it is testing must finish before the first successful `open`.**

```
state == UNOPENED, at mode set, in this order:
    ok1 = prove(PROVEN .. min(ceiling, 12 MiB))
    ok2 = ceiling > 12 MiB && ok1 && prove(12 MiB .. ceiling)
    end = ok2 ? ceiling : (ok1 ? 12 MiB : PROVEN)
    open once with that end
```

Stage two is attempted only if stage one passed: a board that disagrees below
12 MiB has no business being asked about 28.

python, stage sizes: 7->12 MiB is **640 pages / 1,280 word writes** (the count
the machine has logged three times), 12->28 MiB is **2,048 pages / 4,096 word
writes**.

## 4. The failure that has to be handled: a 16 MB board declaring 32

Stage two writes 12..28 MiB. On a board with only 16 MiB the aperture
aliases, and those writes land at 0..12 MiB. python (R8, re-derived for the
staged shape): of 4,096 word writes, **1,024 are real, 3,072 wrap**, none
land inside stage two's own compare range, and **all 24 witnesses below
12 MiB are written over** -- so the failure is caught with certainty.

But the wrapped writes land in 0..12 MiB, which is the region stage one
proved and the window is about to be opened on. **796 of them fall inside the
1600x1200 window** `[9,322,496, 12 MiB)`.

`osmgaProveVramTo` deliberately does not zero a range that failed, and it is
right not to -- a failing range may be somebody else's memory. But the region
that was *contaminated* here is not the failing range; it is our own, proven,
about-to-be-published window.

**Proposal: zero the published window immediately before opening it.** It is
ours by then, nothing holds it, and it makes one invariant true that is
currently only usually true -- *what a client is handed starts as zeros*.
Cost is one pass over at most 24 MiB of uncached video memory at boot, on the
same mapping machinery the proof already uses for up to 16 MiB at once.

## 5. The range list follows the survey

`ranges[0].size` is `MGA_VRAM_16MB` (`:3146`) while the cdev's pfn path
computes `fbPhysical + off` with no reference to the declared ranges
(`:2288-2296`). Cross-review called this a blocker in R9 and it is: above
16 MiB the pfn handed to a client would be outside every range the driver
declared, and whether DriverKit permits that is not established.

Rather than establish it, **remove the question**: declare
`ranges[0].size = surveyed`. The survey has already shown nothing else claims
that space, so declaring it takes nothing from anyone.

`mapFrameBufferAtPhysicalAddress:length:` **stays at 16 MiB**. Checked: the
CPU mapping it returns is used in exactly one place, the visible clear at
`:3804`, whose largest extent is 7,680,000 bytes. Nothing reaches offscreen
through it -- the window goes out as pfns and the proof uses its own uncached
alias.

The registration invariant `end > MGA_VRAM_16MB` becomes
`end > surveyed`, via `OSMGAWindowMayRegister`, which already takes the
aperture as an argument.

## 6. What is NOT in this stage

- The Configure.app panel. It is stage 5, and it must go in **after** the
  driver semantics it describes, or an operator can store a `32` that the
  running driver silently narrows.
- Moving `OSMGA_S1_VRAM_PROVEN` off the opt-in self-tests (`:3600`, `:5962`,
  `:7109`, ...). They are not client paths; recorded in R9 §8.4.

## 7. Verification

| claim | how |
|---|---|
| 16 declared behaves exactly as today | boot at 1600x1200 with the key absent: window 3,260,416, unchanged |
| 32 declared reaches 28 MiB | boot with `MGA Memory Size = 32`: `M1-4F1` logs a second stage and the window opens at 20,037,632 |
| the caps agree | `caps-client`: `vram +9322496, 20037632 bytes` |
| a full-screen GL buffer becomes possible | the R10 verdict line says `full-screen GL` |
| the survey still narrows a wrong declaration | cannot be produced on this machine; host test with a synthetic bus that puts a neighbour inside the range |
| nothing regresses at 1024x768 | boot at 1024x768, window unchanged |

Rollback is `MGA Memory Size` back to `16`, or removing the key.

## 8. Open questions for cross-review

1. **Is raising `ranges[0].size` to 32 MiB at init safe?** It is set before
   `mapFrameBufferAtPhysicalAddress:`. If DriverKit validates or reserves the
   declared ranges, a larger one could fail where the current one succeeds --
   and the failure mode is a machine that boots without a display.
2. **Is zeroing the window before opening it worth it**, or does it hide a
   contamination that should instead refuse the window outright?
3. **Should stage two be attempted at all on a 16 MB *declaration*?** As
   written it is not (the ceiling is 12 MiB, so there is no second stage).
   That means a 16 MB declaration on a 32 MB board never learns more -- which
   is the point of a declaration, but worth stating.
4. **Is `min(declared, surveyed)` the right composition**, or should a
   declaration larger than the survey be refused loudly rather than narrowed
   quietly?


---

## 9. Cross-review verdicts (codex terra, 2026-08-26)

| claim | check | verdict |
|---|---|---|
| `osmgaProveVramTo` refuses outright when `to > MGA_VRAM_16MB`, so stage two cannot run at all until that cap is redesigned | read `:9431-9432`: `if (to > MGA_VRAM_16MB) return 0;` | **accepted -- hard blocker I missed entirely.** The design proposed a 12 -> 28 MiB stage that the function would have refused without a word |
| `surveyed = clear ? 32 MiB : 16 MiB` is not a valid inference: the survey proves no foreign claim and that the ancestors forward the range, not that the card DECODES it. A 16 MiB board behind a 32 MiB bridge window surveys clear | structurally true, and true of this machine if its board were 16 MiB | **accepted.** The survey is a **gate**, not a capacity, and §2's "the survey narrows a wrong declaration" safety case was wrong |
| Sourcing `ranges[0].size` from `surveyed` means a normal 16 MiB declaration still declares 32 MiB on this machine, so removing the config key is not a rollback | true of what §5 wrote | **accepted -- a real bug in the design** |
| Raising `ranges[0].size` is unproven and sits before the framebuffer map, whose failure frees the driver (`:3157-3160`) -- a machine with no display | true; `R6_DRIVERKIT_MAPPING_AUDIT.md` establishes ownership, not setter semantics | **accepted as risk**; made opt-in below rather than dropped |
| "At most 24 MiB" to zero is wrong: at 640x480 a 28 MiB ceiling gives a **26.203 MiB** window | python: `29,360,128 - 1,884,160 = 27,475,968` | **accepted -- my error.** And it exceeds the largest mapping the proof has ever made (16 MiB), so the cleanup must be chunked |
| Witnesses are restored before `osmgaProveVramTo` returns | read `:9524-9529` -- both exits restore | agreed; "written over" is about detection, which happens in `osmgaCheckWitnesses` before the restore |
| Zeroing the published window is required cleanup, not evidence-hiding -- but it must fail closed | | accepted |
| The superclass software cursor also dereferences `IODisplayInfo.frameBuffer` (`:4475-4484`), so §5's "used in exactly one place" is too strong | read it; it stays inside the visible image, so 16 MiB is still sufficient | **accepted -- my evidence was overstated, the conclusion survives** |
| Citations drifted | `ranges[0].size` is `:3148` not `:3146`; the pfn is `:2430`; the `OSMGA_S1_VRAM_PROVEN` self-test uses are `:3904`, `:4150`, `:6309`, `:7191`, `:7456`, `:8001` | accepted, corrected |
| The arithmetic (640/1,280 and 2,048/4,096 writes, 24 witnesses, 796 inside the window) | python reproduces all of it exactly | agreed |

### The design after the review

**S1 -- the survey is a gate.** Not `min(declared, surveyed)`:

```
gate32   = survey was clear for 32 MiB        /* may we ATTEMPT above 16? */
ceiling  = (declared == 32 && gate32) ? 28 MiB : 12 MiB
```

A refused gate means observed routing or ownership blocks the attempt, and it
is logged loudly rather than folded silently into a minimum. A clear gate
permits an attempt and asserts nothing about capacity -- **the proof is the
only thing that decides what is there**, and a wrong 32 on a 16 MiB board is
expected to survey clear and be caught by stage two.

**S2 -- the proof's cap moves with the ceiling.** `osmgaProveVramTo` gains the
bound as an argument instead of testing `MGA_VRAM_16MB`, and the caller passes
what the declaration and the gate allow. Without this, stage two is refused
silently, which is the worst of both: no gain and no diagnosis.

**S3 -- `ranges[0].size` changes only when 32 is declared.**

```
ranges[0].size = (declared == 32 && gate32) ? 32 MiB : MGA_VRAM_16MB
```

With the key absent or `16` -- which is this machine today -- the declaration
is **byte-identical to what has booted many times**. Only a deliberate `32`
touches the unknown, and removing the key really is the rollback. The risk is
real and is not argued away: if the framebuffer map fails, the driver frees
itself and the display is gone, recoverable only by the documented
`Active Drivers -> VGA` config-edit reboot.

**S4 -- cleanup is chunked and fails closed.** Zero the published window
before opening it, in bounded pieces rather than one mapping of up to
26.203 MiB, and if any piece cannot be mapped or written, the state stays
`FAILED` and the window never opens.

### What this stage must NOT be asked to prove

That the board has 32 MiB. Nothing here can: the survey gates, the
declaration asks, and the proof answers -- for the pages it writes, at this
boot, at two words a page. That is the same contract stages 1-3 ship under.
