# R13 -- stage 5: the panel says what the driver will do

Implements `R9_VRAM_DECLARATION_BUILD_PLAN.md` stage 5, the last one. Written
before any code.

The condition R9 set for this stage is now met: the panel goes in **after**
the driver semantics it describes, so an operator cannot store a `32` that the
running driver silently narrows. Stage 4 was proved on the machine at
2026-08-26 12:19 --

```
R12:    declaration 32 MiB (ok), gate open -> may attempt 32 MiB, ceiling 29360128
M1-4F1: 12582912..29360128, 2048 pages, 0 wrong
M1-4F1: offscreen window OPENED 9322496..29360128 (19568 KiB)
R10:    1600x1200 RGB:888/32 -- 32.0 MB gives 19.1 MB offscreen, full-screen GL
caps:   vram +9322496, 20037632 bytes -> VERDICT: hardware
```

## 1. Why this stage carries almost no risk

Nothing in it touches the kernel driver. The inspector is old-AppKit code
loaded **into Configure.app**; if it were wrong, Configure shows a bad panel
and the display keeps working. It also needs **no reboot to test**: the
bundle's inspector and nib are read when Configure opens it.

That is the opposite of stages 3 and 4, and it is worth saying out loud so the
care is spent where it belongs -- on the arithmetic being the same arithmetic,
not on the risk of the change.

## 2. The panel cannot ask the driver anything

It is a Configure.app bundle that reads the instance table. So it recomputes
what the driver will do, from the same file the driver compiles --
`OpenStepMGAWindowMath.c` -- with `haveActual = 0`, and **every sentence it
produces is conditional**: *would give*, never *gives*. It knows neither the
board's aperture nor the result of any proof.

### Build plumbing, which is the only real work

The bundle target has no `CFILES` at all today (`Makefile:32-33`), and the
shared file needs two more translation units:

- `OpenStepMGAManualConfig.c` -- the digits of the declaration;
- `OpenStepMGAEDID.c` -- `OSMGAParseManualDisplayMode`, the
  `Height:/Width:/Refresh:` form. 471 lines of C89 with no libc and no kernel
  headers, so compiling it into a userland bundle costs nothing.

`build-matrox-driver.sh` already copies EDID/WARP/HW3D into the reloc
subproject; it gains three more copies into the **top-level** build directory,
where `make` compiles the inspector. Same files, copied at build time, so the
driver and the panel cannot drift.

## 3. The status line does not fit, and that decides the layout

**Measured, not estimated.** The first draft assumed 0.60 em per character,
which was pessimistic by a quarter. Real advance widths, summed per glyph from
URW Nimbus Sans -- the metric clone of Adobe Helvetica, which is the Helvetica
NeXT ships -- at 12 pt, against the 340 px the existing switches already use:

| string | chars | px | |
|---|---:|---:|---|
| `1600x1200 RGB:888/32 -- 32.0 MB would give 19.1 MB offscreen, full-screen GL` | 76 | **430.2** | **over by 90** |
| `no OpenGL: no offscreen window was registered` | 45 | 259.5 | fits |
| `16.0 MB would give 10.2 MB, up to 1280x1024` | 43 | 248.8 | fits |
| `32.0 MB would give 19.1 MB, full-screen` | 39 | 214.8 | fits |
| `1600x1200 RGB:888/32` (the mode row) | 20 | 128.7 | fits |
| `Publish offscreen VRAM as a character device` (existing) | 44 | 247.4 | fits |

The driver's one-line sentence is 430 px and cannot be shown. Everything the
panel needs fits with room to spare -- the widest is 259.5 px, three quarters
of the budget. 340 px holds **51 digits**, which is the worst realistic case
since digits are the widest glyphs here.

The 47-character figure in the first draft was an underestimate of the budget
AND an overestimate of the risk. The layout below is unchanged by the
correction, because the decision it drives -- the full sentence does not fit,
so the mode goes on its own row -- is the same either way.

So `OSMGAAccelVerdict` gains a second, bounded output -- `brief`, at most 47
characters, with no mode prefix -- and the panel shows the mode on its own row
above it. **One function still produces both**, which is the point of the
shared file; what changes is that it produces two lengths instead of one.

`would` survives the shortening. It is the word that makes the sentence
honest, so the abbreviation drops "offscreen" and the mode prefix rather than
the conditional.

## 4. Layout

python (`scratchpad/r9nib.py`) from the real constants. Current rows top out
at 81 with `GROW = 95`, i.e. 14 px of headroom.

| row | y | h | top |
|---|---|---|---|
| caption | 6 | 14 | 20 |
| VRAM Mmap switch | 24 | 15 | 39 |
| Storm switch | 44 | 15 | 59 |
| Gray levels matrix | 66 | 15 | 81 |
| **VRAM size matrix** | **88** | **15** | **103** |
| **status: mode** | **110** | **14** | **124** |
| **status: brief** | **126** | **14** | **140** |

`GROW` becomes **154** for two status rows (138 would fit only one, and one is
not enough). Two cells, `16` and `32`, tags 0 and 1, one row -- the same
`radio-template-BusLogicIntrInspector.xml` graft the grey matrix uses.

**A latent trap to fix while here:** the script places Storm at `Y_MMAP` and
mmap at `Y_STORM` (`build-inspector-nib.py:154-157`) -- the two constants are
misnamed. The rows are correct on screen today; the names are not, and the
next row added by reading them would land in the wrong place.

## 5. Behaviour

- `setTable:` selects the radio from `MGA Memory Size` and recomputes both
  status rows.
- Every control action recomputes them, so toggling `VRAM Mmap` changes the
  answer immediately.
- **The stock resolution picker is not observed.** `setTable:` is the only
  place the panel reads the mode (`OSMGADisplayInspector.m:91`), and the
  picker lives outside our view. The mode row is what makes a stale line
  visibly stale rather than silently wrong -- the reason it is a row of its
  own rather than dropped for space.
- An unsupported value in the table (`8`, `63`) selects nothing and the status
  says the declaration is being read as 16, which is what the driver does.

## 6. Verification

| claim | how |
|---|---|
| the box still fits Configure's panel 59 px taller | on screen; no reboot |
| the radio writes `16`/`32` | read the instance table after clicking |
| the panel's sentence matches the driver's | compare the panel against the `R10:` line from the same configuration -- same function, so a difference is a bug |
| the arithmetic is unchanged | the host suite, which already covers `OSMGAAccelVerdict` |
| the driver is untouched | `diff` the reloc sources; only the bundle Makefile and the build script change |

Rollback is the previous bundle: `$DST.prev` is kept beside it by the
installer.

## 7. Open questions for cross-review

1. **Is compiling `OpenStepMGAEDID.c` and `OpenStepMGAManualConfig.c` into an
   old-AppKit bundle safe** -- any symbol clash with libNeXT/libsys, or any
   reason a kernel-side file should not be linked into an application bundle?
2. ~~Is 47 characters the right budget?~~ **Settled by measurement** -- see
   §3. Real per-glyph advances put the worst case at 259.5 px of 340. The
   remaining assumption is that the target's Helvetica has Adobe metrics,
   which URW Nimbus Sans is a clone of; if it did not, every existing label in
   this panel would already be mis-sized.
3. **Two status rows or one?** One row of 47 characters cannot carry both the
   mode and the verdict; dropping the mode makes a stale line invisible.
4. **Should the panel refuse to store `32` when it cannot know the board?**
   It cannot know, and the driver narrows a wrong declaration safely -- so the
   proposal is to store what the operator asks and say *would*.


---

## 8. Cross-review verdicts (codex terra, 2026-08-26)

| claim | check | verdict |
|---|---|---|
| `16.0 MB would give 3.1 MB, up to 1280x1024` is arithmetically impossible -- 3.1 MB carries 800x600 | my own host test asserts it: `verdict-1600-16-largest-w` expects **800** (`test/openstep-mga-window-math-test.c:323`) | **accepted -- my error**, and a careless one: the test that contradicts it is mine |
| `no OpenGL: no offscreen window was registered` is emitted only inside `if (in->haveActual)`, so the inspector can never produce it | read `OpenStepMGAWindowMath.c:514-516`; the forecast branch emits `"no OpenGL: " + geom.reason` instead (`:524-525`) | **accepted -- my §3 sample listed a driver-only string.** The briefs must be defined for the forecast paths that actually exist |
| The build must copy **six** files, not three -- each `.c` needs its header at the top level too | `OpenStepMGAWindowMath.c:3-5` includes both other headers | **accepted** |
| The inspector must set `in.pageBytes` explicitly; zero or a userland page size gives the wrong answer | the field feeds both the geometry and the surface fit | **accepted -- an omission that would have produced a confidently wrong panel** |
| `GROW` headroom is not `GROW - top`: the original children are lifted to `their y + GROW` | the stock nib puts them at **y = 9** (`CustomView` oid 54 frame `10,9,362,19`); python: GROW 138 leaves **7** units above two status rows, GROW 154 leaves **23** | **accepted.** And 23 is exactly the gap the panel has today (81 + 23 = 104 = 9 + 95), so 154 reproduces the current spacing rather than inventing one |
| `data.classes` is already stale -- it lists only `mmapSwitch`/`stormSwitch` and misses the shipped `grayMatrix`/`grayChanged:` | read it; true | **accepted**, fixed while adding the new outlets |
| `setStringValue:` for the grafted TextFields, not `setTitle:` | `compat/appkit/appkit.h:53-64` declares both | accepted |
| Adding `CFILES` cannot clash: bundle and reloc are separate link products with separate object directories, and every exported symbol is `OSMGA`-prefixed | true of the Makefiles | accepted |
| `selectCellWithTag:` with an absent tag is not established to clear the selection | true -- the compat header declares the selector, nothing more | **accepted**: an unsupported value selects **16**, which is what the driver will actually use, rather than relying on undefined deselection |
| The width figures | measured per glyph in §3 and agreeing with codex's AFM numbers to within a pixel | agreed |

### Decisions that changed

**D1 -- the page size is named, not assumed.** The inspector runs in userland
where `vm_page_size` is not promised to be the kernel's, and this arithmetic
depends on 8192. The shared header gains
`OSMGA_WIN_TARGET_PAGE` with that value and the reason; the driver keeps
passing its own `PAGE_SIZE`, so a disagreement is visible rather than silent.

**D2 -- `brief` is produced for every path the caller can actually reach**,
forecast and actual, and the host suite asserts the exact string for each --
not merely that it is short enough. A brief nobody asserted is a brief that
drifts.

**D3 -- `GROW = 154`**, justified by reproducing today's 23-unit gap rather
than by an invented headroom figure.

**D4 -- an unsupported declaration selects 16** and says so, instead of
leaving the matrix in an undefined state.
