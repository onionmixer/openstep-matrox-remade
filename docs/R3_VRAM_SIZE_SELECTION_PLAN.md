# R3 — a 16/32 MB video-memory selector in the inspector, and what the driver does with it

Status: **plan only.** No code, no nib, no constant is changed yet.

## Why this exists

The driver assumes 16 MiB everywhere.  `MGA_VRAM_16MB` is used in seven
places (`OpenStepMGAReplacementDisplay.m:76` and its uses at `:2924, :2932,
:2984, :3169, :3530, :5649, :9000`), and `-readManualMemoryConfiguration:`
(`:3152`) reads a `"MGA Memory Size"` key that already exists, parses 3..63
with `OSMGAParseManualMemoryMB`, and then **throws the answer away**:

```c
/* Fixed 16 MiB driver: only 16 is consistent with the mapped aperture. */
if (configuredVideoMemoryBytes != MGA_VRAM_16MB) {
    IOLog("... MGA Memory Size != 16 MiB; clamping to 16\n");
    configuredVideoMemoryBytes = (unsigned int)MGA_VRAM_16MB;
}
```

So the key, the parser and the plumbing are already there.  What is missing
is a way to set it, and a driver that acts on it.

G450 boards shipped in 16 MB and 32 MB.  On a 32 MB board this driver
currently uses half the card.

## What it would buy, computed

The registration bound (`OSMGA_S1_VRAM_PROVEN`, 7 MiB) does NOT change: it is
the bound proven by a working 1600x1200x32 scanout, which is a statement
about the bottom of the board, not about its size.  The *ceiling* does:
16 MiB - 4 = 12 MiB today, so 32 MiB - 4 = 28 MiB, keeping the same
top-of-board margin and the same reason for it.

| display mode (32bpp) | window at 16 MB | window at 32 MB |
| --- | --- | --- |
| 1024x768 | 8.00 MiB | 24.00 MiB |
| 1280x1024 | 5.75 MiB | 21.75 MiB |
| 1600x1200 | none (see R2) | none (see R2) |

1600x1200x32 gains nothing, because it fails at the REGISTRATION bound, which
is unrelated to card size -- that is R2's subject, not this one.  Everywhere
else the texture arena roughly triples.

## Part 1 — the nib

`English.lproj/DisplayInspector.nib` is generated, not hand-edited:
`OpenStepMGAReplacementDisplay/nib-src/build-inspector-nib.py` takes
Configure.app's own `DisplayInspector.nib`, renames the File's Owner class to
`OSMGADisplayInspector`, and grafts two switches into box 48 with
`openstep-nibmaker`'s `nibgraft.Nib`.

A radio matrix is a bigger graft than a switch, and it has a **working
precedent in this workspace**:
`openstep-spacesaver2ps2/SpaceSaver2Mouse/nib-src/build-inspector-nib.py`
lines 139-174 graft a four-cell radio matrix taken from BusLogic's
`IntrInspector.nib`.  The decoded template is already committed at
`openstep-spacesaver2ps2/ref/nibtemplates/radio-template-BusLogicIntrInspector.xml`.

Concretely, in `build-inspector-nib.py`:

1. A fifth argument, the radio template path.  (The script's four positional
   arguments are documented in its own docstring; adding a fifth keeps the
   same shape.)
2. `GROW` rises from 70 to about 110 -- the matrix is 15 high and needs a
   label line and spacing above it.  Every frame the script already grows
   (`WINDOW`, `CONTENT`, `MODE_BOX`, `MODE_VIEW`) grows by the new value, and
   `MODE_INNER` lifts by it, so the arithmetic does not change shape.
3. A label, grafted the way the existing caption is (`LABEL_SRC` '23'), text
   `Video memory on this board:`.
4. The matrix, exactly as the SpaceSaver2Mouse script does it:

   ```python
   radio  = Nib(RADIO_TEMPLATE)
   rmat   = [o for o in radio.objs.values() if o.get('cls') == 'Matrix'][0]
   rsuper = [c for c in radio.groups(rmat)[0]][0].get('oid')
   mat = n.graft_from(radio, rmat.get('oid'),
                      obj_map={'10': OURS_HELVETICA_12, rsuper: MODE_VIEW})
   n.add_subview(MODE_VIEW, mat, x, y, 2*70 + 1*4, 15)
   ```

   The template matrix was opened and read rather than assumed.  Its object
   is `oid 12`, and its groups are:

   | group | enc | values | meaning |
   | --- | --- | --- | --- |
   | [3] | `ffff` | 149, 13, 54, 33 | frame: 54 x 33 |
   | [13] | `@:@iiii` | 0, 0, **2, 1** | selRow, selCol, **rows=2, cols=1** |
   | [14] | `ff` | 54, 15 | cell size |
   | [15] | `ff` | 0, 3 | intercell spacing |

   So it is a **vertical two-cell** matrix, and it has exactly the two cells
   we need -- the clone loop the SpaceSaver2Mouse script runs for its third
   and fourth cells is not needed here, which is a simplification rather
   than an extra step.  (That script asserts `len(first_cells) == 2` on this
   same template, which is the same fact from the other side.)

   Two cells side by side reads better than stacked for "16 MB / 32 MB", so
   re-lay it horizontally the way that script does: set the two cell titles,
   set their tags to 0 and 1, point the `selectedCell` slot at cell 0, then
   write `(selRow, selCol, rows, cols) = (0, 0, 1, 2)`, cell size
   `(70, 15)`, intercell `(4, 0)`, and give `add_subview` a width of
   `2*70 + 1*4 = 144` and a height of 15.

   The layout has to be written down or it is not a recipe.  With
   `GROW = 110`, inside `MODE_VIEW`: caption at `y=6`, the two existing
   switches at `y=24` and `y=44`, the matrix at `x=105, y=64`, its label at
   `x=12, y=84`, and the lifted original children starting at `y=119`.  Leaving it vertical is also
   correct and costs one more line of GROW; the horizontal form is chosen
   only because the panel is wide and short.
5. Two connectors, as the switches already do:
   `IBOutletConnector OWNER -> mat` named `vramMatrix`, and
   `IBControlConnector mat -> OWNER` named `vramChanged:`.
6. The existing caption changes from "Both take effect after the next
   reboot." to "These take effect after the next reboot." -- it is now three
   controls, not two.

The script already ends with `xml2nib -r`, `nibroundtrip` and nibmaker's
`validate-xml.py`; those three are the acceptance test for the nib itself and
need no change.

**Two things to record because they are not committed:** the stock
`DisplayInspector.nib` and `switch-template.xml` inputs live outside this
repository (the same arrangement `openstep-spacesaver2ps2/ref/README.md`
documents for its own inputs).  This plan adds a third such input.  The
regeneration commands should be written down in this repository the way that
README writes down its own.

## Part 2 — the inspector class

`OSMGADisplayInspector.{h,m}` today holds two outlets and two actions and one
helper, `osmgaFlagIsOn`, which deliberately reads a flag the same way the
driver does.  The addition follows that principle exactly:

```objc
id vramMatrix;                  /* "MGA Memory Size": 16 or 32 */
- vramChanged:sender;
```

- `-setTable:` selects row 0 or 1 by parsing the key with the SAME acceptance
  rule the driver uses.  The driver uses `OSMGAParseManualMemoryMB`, which
  lives in the reloc subproject
  (`OpenStepMGAReplacementDisplay_reloc.tproj/OpenStepMGAManualConfig.c`);
  the inspector is a separate binary built from the bundle's own Makefile,
  which today compiles `OSMGADisplayInspector.m` and nothing else.
  **Compiling `OpenStepMGAManualConfig.c` into both is the right answer** --
  it is free-standing C with no DriverKit dependency, and a second parser is
  a second thing that can disagree with the driver -- but it is **a build
  change, not an include**, and the Makefile edit is part of the work rather
  than a detail of it.
- `-vramChanged:` writes `"16"` or `"32"` with the existing `-storeFlag:on:`
  pattern, i.e. `NXCopyStringBuffer`, for the reason already documented
  there.
- Anything the table holds that is neither 16 nor 32 selects 16 and the panel
  shows 16, because that is what the driver will do.

## Part 3 — the driver

The change is mechanical but it must be complete, and the seven current uses
of `MGA_VRAM_16MB` do NOT all mean the same thing:

| use | today | what it means | becomes |
| --- | --- | --- | --- |
| `:2924` `ranges[0].size` | 16 MiB | how much aperture is declared | `osmgaVramBytes` |
| `:2932` `mapFrameBufferAtPhysicalAddress:length:` | 16 MiB | how much is mapped | `osmgaVramBytes` |
| `:2984` `end > MGA_VRAM_16MB` | 16 MiB | window must fit the board | `osmgaVramBytes` |
| `:3169` the clamp | 16 MiB | throws the setting away | **deleted; see below** |
| `:3530` `byteEnd > MGA_VRAM_16MB` | 16 MiB | **not the mmap bound** -- it is the optional S1 self-test, already behind `byteEnd > OSMGA_S1_VRAM_PROVEN` at `:3524` | `osmgaVramBytes` |
| `:5649` `-displayMemorySize` | 16 MiB | what DriverKit is told | `osmgaVramBytes` |
| `:9000` `to > MGA_VRAM_16MB` | 16 MiB | proof range bound | `osmgaVramBytes` |

`OSMGA_S1_VRAM_CEILING` stops being a constant and becomes
`osmgaVramBytes - 4 MiB`, which reproduces today's 12 MiB exactly at 16 MB.

That the margin scales is not an assumption -- the comment that sets it
(`:208-224`) says what it is for: *"the top four are left alone because that
is where a board reserves things... What else might be up there is not
known, which is why the margin is there rather than argued away."*  The
margin is about the TOP OF THE BOARD, so it moves with the board.  It is an
absolute reservation, not a proportion, so 4 MiB stays 4 MiB and simply
becomes a smaller fraction (12.5% instead of 25%) -- more conservative in
relative terms, not less.  The one thing the comment ruled out by
measurement, the hardware cursor, is ruled out the same way regardless of
size.
`osmgaProbeVramExtent`'s sampling loop, which today runs `mb < 16UL`, runs to
`osmgaVramBytes / 1 MiB`.  **Its cap does not make the sampling sparser, as
an earlier draft of this plan said -- it makes it STOP.**  The loop walks
upward from `firstMb` two samples at a time and quits at
`OSMGA_VRAM_PROBE_MAX` (32), i.e. after 16 MiB of range (`:9122`).  On a
32 MB board at 1024x768 it would start at 5 MiB and stop at 21 MiB, never
looking at 21..32 MiB at all.  Either the cap rises with the board or the
step does; silently covering half the board and calling it a probe is worse
than not probing.

A second variable is needed, and the distinction is the whole of the safety
story: `osmgaVramClaimBytes` is what the operator selected, and
`osmgaVramProvenBytes` is what the driver will stand behind.  Both are
initialised to 16 MiB **before anything reads them** -- the current parser
leaves `configuredVideoMemoryBytes = 0` on a missing or invalid key
(`:3156`, `:3166`), and a zero reaching the aperture mapping would be worse
than any wrong claim.  Only `osmgaVramProvenBytes` may bound a report, a
sampling loop, or anything handed to a client; the claim is allowed to set
the ceiling the stepped proof aims at, and nothing else.

`-readManualMemoryConfiguration:` accepts **16 or 32 only** and refuses
anything else back to 16 with a log line.  Not 3..63: every other value is a
board this project has never seen, and the parser's range is not a promise
about what the driver can do.

Ordering matters: `-readManualMemoryConfiguration:` is already called at
`:2809`, BEFORE the aperture is mapped at `:2932` and before the "VRAM Mmap"
registration block.  So `osmgaVramBytes` is set early enough for every use
above, and no reordering is needed.  This should be asserted in the code
rather than relied on.

## Part 4 — the part that actually matters: a wrong answer must be harmless

The UI is a **claim**, not a measurement.  Someone will select 32 MB on a
16 MB board.  On MGA the top half then aliases onto the bottom half, and a
write at 20 MiB lands on the visible framebuffer at 4 MiB.  That is exactly
the failure `osmgaProveVramTo` was built to catch: before opening a region it
plants witnesses across everything below it, every 512 KiB, and checks them
twice -- the first check after only two pages have been written, so the
damage from an aliasing board is two words rather than the whole region
(`:9016`, and the reasoning in the comment above it).

So the safety argument is: **nothing above `OSMGA_S1_VRAM_PROVEN` is ever
handed out until the proof passes, and the proof detects aliasing.**

That was traced rather than asserted:

- the only thing a client can reach is the character device, and its `d_mmap`
  handler refuses any offset outside `[osmgaMmapWindowStart,
  osmgaMmapWindowEnd - PAGE_SIZE]` (`:2269-2272`);
- `osmgaMmapWindowEnd` is written in exactly two places -- `:3077`, where
  init sets it to the 7 MiB conservative bound, and `:3432`, which is
  the M1-4F1 widening and runs only when `osmgaProveVramTo` has returned
  true;
- `:3530`, the other `MGA_VRAM_16MB` bound, is a second check behind
  `byteEnd > OSMGA_S1_VRAM_PROVEN` (`:3524`), so the proven bound dominates
  it and turning it into `osmgaVramBytes` cannot loosen anything;
- `:2984` is the registration test, and at init `end` is the 7 MiB bound, so
  a larger claimed size cannot raise what init publishes.

The claimed size therefore reaches a client's hands ONLY through the
ceiling, and only after the proof.

### But "a wrong claim costs two words" is FALSE, and that was the plan's worst error

Cross-review refuted it and the mechanism was then read at `:9040-9070`.
`osmgaProveVramTo` writes **two pages** first -- the first page at `from` and
the last page at `to - page` -- checks the witnesses, and only then writes
**every page from `from` to `to`** before checking anything again.

The witnesses cover `[0, from)`.  So with a 32 MB claim on a 16 MB board the
proof range is `[7 MiB, 28 MiB)` and:

- the two early pages land at 7 MiB (real) and at ~28 MiB, which wraps to
  ~12 MiB -- **neither is witnessed**, because both are at or above `from`;
- the early check therefore PASSES;
- the full pass then writes 7..28 MiB, and 16..28 MiB wraps onto 0..12 MiB,
  clobbering thousands of words including the visible framebuffer;
- the final witness check catches it and the bound is kept, but only the ~14
  witness words are restored.  The rest of the clobbering is not.

The visible half of that mess is wiped by the framebuffer clear at `:3454`,
which runs a few milliseconds after unblank -- so the cost is a flash of
garbage rather than lasting corruption.  That is still not "two words", and
the plan is not allowed to claim a guarantee the code does not give.

### The fix: prove in steps, never in one 21 MiB leap

Extend the region a **step at a time**, and re-plant witnesses each step so
that everything below the new step -- including everything already proven --
is witnessed:

```
for (base = 12 MiB; base < ceiling; base += STEP)     /* STEP = 4 MiB */
    if (!osmgaProveVramTo(fbPhysical, base, base + STEP, mmio))
        break;                    /* keep whatever the last step proved */
```

With `from` rising each step, `[0, from)` grows to cover the ground already
proven, so a wrap into it is caught by that step's own witnesses, and the
damage before detection is bounded by one step rather than by the whole
range.  A 16 MB board claiming 32 fails at the first step that crosses
16 MiB and keeps everything below it.

This also removes the objection that a bigger ceiling enlarges the unsafe
write range: the unsafe range is one STEP, whatever the ceiling is.

### And one path does use the claimed size before any proof

`osmgaProbeVramExtent` runs at **every mode set**, whether or not the mmap
device was offered (`:3404`), and writes its samples.  Widening its loop to
the claimed size means writing at 16..31 MiB on a board that may only have
16 -- before anything has been proven.

It is not as bad as it first looks: that routine saves every sampled word
BEFORE writing any of them (the two-pass structure at `:9160-9200`, and the
comment there says exactly why), so an alias is both detected and restored.
But it is still a write above the proven bound driven by an unverified
claim, and the earlier draft's blanket sentence -- "nothing uses the claimed
size before proof" -- was simply wrong.

**So the probe's loop bound must come from the PROVEN size, not the claim.**
It may look above 16 MiB only after the stepped proof has got there.

Two places do use it without a proof and both are fine, but for reasons that
should be written down rather than assumed:

- the aperture mapping (`:2924`, `:2932`) maps address space, it does not
  write; mapping 32 MiB of a 16 MiB board is a mapping of aliases, which is
  harmless until something writes there.
- `-displayMemorySize` is a report, and the question of whether
  over-reporting it is harmful is now **partly settled** from the local
  OPENSTEP mirror.  `IOFrameBufferDisplay.h:139` declares it with one line of
  contract -- *"Subclass should return appropriate values"* -- and names no
  consumer; nothing else in the mirror's headers or examples calls it.  The
  shipped QVision example returns `installedVRAMBytes`
  (`ref/openstep/examples/QVision/QVision_reloc.tproj/QVision.m:163`), i.e.
  the INSTALLED size, not the usable one.

  So the semantics are "how much memory is on the board", the claimed size
  is the right thing to return, and today's driver already reports 16 MiB
  while never using more than 12 -- over-reporting relative to what is
  usable is the existing behaviour, not something this change introduces.

  **That is not the end of it, and the earlier draft stopped too early.**
  `displayDefs.h:219` defines `IO_GET_DISPLAY_MEMORY "IOGetDisplayMemory"`,
  so the value is a DriverKit PARAMETER that anything outside the driver can
  query.  "No consumer in the mirror" is not "no consumer", and returning an
  unproven claim publishes it.

  So `-displayMemorySize` returns a PROVEN number, never the claim: a new
  `osmgaVramProvenBytes` that starts at 16 MiB -- today's value, so 16 MB
  boards see no change whatsoever -- and rises to the claimed size only once
  the stepped proof below has actually validated the region above 16 MiB.

## What has to be established first

0. **Does an oversized aperture on a G450 wrap, or does it fault?**  The
   whole safety argument assumes the top half ALIASES onto the bottom --
   that a write lands somewhere it should not but the bus completes.  If it
   instead returns all-ones, drops writes, or raises a bus abort, then
   witnesses cannot catch it and the probe itself could hang the machine.
   Nothing in this repository or in `ref/` establishes which.  Cross-review
   independently reached the same conclusion.  **This is the first thing to
   settle, because the test plan below assumes the machine survives the
   test.**

1. **Is BAR0's aperture actually 32 MiB on this board?** The driver reads
   BAR0's base at `:2798` and never its size.  Probing a BAR's size means
   writing all-ones and reading the mask back, which is a write to a live
   display device's configuration -- not something to do casually on the
   development machine, and forbidden by this project's own
   non-destructive rule while the display is up. If the aperture is 16 MiB,
   `mapFrameBufferAtPhysicalAddress:length:32 MiB` is wrong regardless of
   how much VRAM is fitted, and the plan changes shape.
2. **What does `mapFrameBufferAtPhysicalAddress:length:` do with a length
   larger than the BAR?** Unknown, and it is the call whose failure once hung
   the boot window server (`:2918` comment).
3. ~~**Does the window server use `-displayMemorySize`?**~~ Answered above
   from the local mirror: no documented consumer, and the shipped example
   returns the installed size.  Low risk; not a blocker.

## Test plan, and its honest limit

The development machine has a 16 MB board.  So:

- **Testable now:** select 32 MB on the 16 MB board and confirm the driver
  REFUSES to widen -- `osmgaProveVramTo` should report witnesses disturbed
  and keep the conservative bound, and the machine should come up normally
  at the 7 MiB window.  That is the safety case, and it is the one that
  matters most.
- **Testable now:** select 16 MB and confirm every log line is byte-identical
  to today's, including `S4a: VRAM window 1048576..7340031` and
  `M1-4F1: offscreen window widened to 1048576..12582912`.  A change that
  alters the 16 MB path at all has gone wrong.
- **Testable now:** the nib -- `nibroundtrip` and `validate-xml.py` are
  offline checks, and Configure.app showing three controls with the right
  labels is a look at the screen.
- **NOT testable here:** that a 32 MB board actually gets a 24 MiB window.
  Without such a board that claim can be reasoned about and not measured, and
  the release documentation must say so rather than implying it was tested.

## Staging

1. Nib and inspector first, with the driver still clamping to 16.  The panel
   then writes the key and nothing acts on it -- entirely reversible, and it
   proves the nib graft before any driver behaviour changes.
2. Then the driver, in one commit, with the 16 MB regression above as the
   gate.
3. Documentation last: `PORT-NOTES.md`'s per-mode table gains a 32 MB column
   marked as computed, not measured.

## Cost

The nib graft is a day's careful work with a working precedent.  The driver
change is small but touches the aperture mapping, which is the call that once
hung the boot window server.  The benefit on this machine is **zero** -- the
board is 16 MB -- and the benefit on a 32 MB board is a texture arena roughly
three times larger at 1024x768 and 1280x1024.

## Cross-review, 2026-08-26 — verdict and what it changed

The review's verdict was **block Part 3** on the grounds that the
claimed-size safety argument was false as written.  Checked against the
source, it was right, and the plan above is rewritten rather than defended.

| the review said | checked | outcome |
| --- | --- | --- |
| a wrong claim writes above the proven bound before any proof, via `osmgaProbeVramExtent` | `:3404` runs it at every mode set | **accepted** -- its loop bound now comes from the proven size, not the claim |
| "two words of damage" is false; the early check is two pages at `from` and `to-page`, neither witnessed, then a full pass writes everything | read at `:9040-9070` | **accepted** -- this was the plan's worst error; the fix is to prove in 4 MiB steps so the unsafe window is one step |
| `-displayMemorySize` is published as the `IOGetDisplayMemory` parameter, so an unproven claim would be published | `displayDefs.h:219` | **accepted** -- it now returns a proven number that starts at today's 16 MiB |
| the 32-sample probe does not get sparser, it STOPS after 16 MiB of range | `:9122` | **accepted** -- the earlier wording was wrong |
| the `:3530` row was mislabelled "mmap bound" | it is the S1 self-test | **accepted** |
| the parser is in the reloc subproject and the inspector Makefile builds only one file | | **accepted** -- called out as a build change |
| `osmgaVramBytes` needs an explicit fallback; the parser leaves 0 | `:3156`, `:3166` | **accepted** |
| the matrix `x,y` were unspecified, so Part 1 was not reproducible | | **accepted** -- coordinates written down |
| whether an oversized MGA aperture wraps, faults or reads all-ones is not established anywhere available | | **accepted** -- promoted to open question 0, ahead of everything else |
| the "leave the top 4 MiB" reasoning does not justify `32 - 4` | | **partly** -- the comment does say the margin is about the top of the board, which moves with the board; but the review's real point, that a bigger ceiling enlarges the unsafe write range, is answered by the stepped proof rather than by argument |

One thing the review got wrong, and it is worth recording so it is not
re-litigated: it wrote that the clobbered region "can leave probe signatures
in visible VRAM before unblank".  The order in `-programLinearMode` is
unblank, `IODelay(10000)`, then clear (`:3440-3457`), so signatures in the
visible image are wiped a few milliseconds AFTER unblank.  The cost is a
flash, not a lasting artefact.  That does not rescue the "two words" claim,
which is refuted regardless.

## Where this leaves the work

The nib and the inspector (Parts 1 and 2) are unaffected by any of the
above: they write a config key and change no driver behaviour.  They can be
built and looked at on the machine without risk, and doing them first proves
the graft.

Part 3 stays **blocked** until open question 0 is answered.  Every remaining
argument -- the stepped proof, the proven-versus-claimed split, the probe's
loop bound -- assumes an oversized aperture aliases coherently.  If it
faults instead, the driver must refuse a claim larger than the aperture
outright and the selector becomes a two-value control with only one usable
value on this machine, which is a different and much smaller piece of work.
