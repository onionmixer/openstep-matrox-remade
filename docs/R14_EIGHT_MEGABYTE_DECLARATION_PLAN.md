# R14 -- adding 8 MB to the declaration

Asked for on 2026-08-26, with G400 support in mind. Written before any code.

**Short answer: the radio cell is two lines; making 8 MB mean something safe
is not.** Three constants in this driver quietly assume at least sixteen
megabytes, and one of them would hand out memory an 8 MiB board does not have
to spare.

---

## 1. What the arithmetic says (python)

### The ceiling falls below the floor

```
ceiling = declared - 4 MiB margin
    8 MB -> 4,194,304   (4 MiB)
   16 MB -> 12,582,912  (12 MiB)
   32 MB -> 29,360,128  (28 MiB)
```

`OSMGA_S1_VRAM_PROVEN` is **7 MiB** and is treated everywhere as a floor that
needs no proof. On an 8 MB board the ceiling is **4 MiB, three megabytes
below it**. Every path that falls back to `PROVEN` -- and the fallback is the
whole point of the two-stage design -- would offer 7 MiB of an 8 MiB board,
leaving one megabyte where the design intends four.

That is the blocker. It is not a wording problem: today's code would open a
window past what the operator declared.

### Which modes an 8 MiB board could actually serve

Every mode this driver publishes can be *scanned out* of 8 MiB -- the largest
visible image is 1600x1200x32 at 7,680,000 bytes. What an 8 MiB board cannot
do is leave anything over:

| mode (32 bpp) | window start | offscreen at a 4 MiB ceiling |
|---|---:|---|
| 640x480 | 1,884,160 | 2,310,144 |
| 800x600 | 2,744,320 | 1,449,984 |
| 1024x768 | 4,194,304 | **none** (start == ceiling) |
| 1280x1024 | 6,553,600 | **none** |
| 1600x1200 | 9,322,496 | **none** |

So an 8 MB declaration buys acceleration at 640x480 and 800x600 and nothing
else. Worth having -- a G400 with 8 MiB is exactly the machine that would
run at 800x600 -- but worth saying plainly rather than implying the radio
makes the board bigger.

### The margin is half the board

4 MiB of 8 MiB is 50%, against 25% at 16 and 12.5% at 32. Cross-review
already settled that the margin stays fixed, because its reason is unknown
top-of-VRAM uses and those do not shrink with the board. That reasoning still
holds at 8 MiB; the consequence is simply that the margin costs more there.
**Not proposing to change it** -- proposing to state it.

---

## 2. What has to change

### C1 -- `PROVEN` must be capped by the declaration

`OSMGA_S1_VRAM_PROVEN` is a static allowance justified by "a 1600x1200x32
scanout works", which is a claim about *this* board. It is not a claim any
8 MiB board supports. The fix is one line of policy in the shared file:

```
effectiveProven = min(OSMGA_S1_VRAM_PROVEN, ceiling)
```

and every use of `PROVEN` as a *window bound* takes the capped value. The
opt-in self-tests that also name the constant (`:3904`, `:4150`, `:6309`,
`:7191`, `:7456`, `:8001`) are not client paths and are out of scope, as R9
§8.4 already records.

### C2 -- "nothing to prove" is not "the proof failed"

With an 8 MB declaration the capped floor and the ceiling are both 4 MiB, so
stage one is asked to prove `4 MiB .. 4 MiB`. `osmgaProveVramTo` refuses
`from >= to` and returns 0, which the staged code reads as a failure --
setting `anyFailed`, triggering a clean, and logging a proof that failed when
in truth there was nothing to attempt.

The staged proof needs three outcomes, not two: **proved**, **refused**, and
**nothing to attempt**.

### C3 -- the parser, the radio, and the table

`OSMGAVramDeclaration` accepts 16 and 32 and clamps everything else to 16.
Adding 8 is small, but note the direction: **8 is smaller than the current
fallback**, so a typo that used to land on 16 must not now land on 8, and a
missing key must still mean 16. Only an explicit `8` may select 8.

The radio grows to three cells. python, on the measured layout: three cells at
54 px with 4 px gaps is 170 px against the grey matrix's 228, so it fits the
same row with room to spare, and `GROW` does not move.

### C4 -- what the panel says

The brief already handles a mode with no window
(`no OpenGL: the offscreen window is too small`, and the geometry reason for
the forecast path). An 8 MB declaration at 1024x768 and above must produce
one of those rather than a number, and the host suite must assert the exact
string for the new combinations -- the same rule R13 adopted.

---

## 3. What this is NOT

**It is not G400 support.** The driver refuses to program a mode on anything
but a G450:

```c
/* :3714 */
if (!chipIsG450) {
    IOLog("... not a G450, refusing mode program\n");
    return NO;
}
```

`chipIsG450` is set from the PCI revision (`:3021`), and the probe already
matches the shared G400/G450 device id `0x0525` (`:51`, `:1186`) -- so a G400
would be *found*, named "pre-G450" in the log, and then refused. What stands
between here and a G400 is the mode programming itself: the PLL, the RAMDAC
sequence, and whatever else the X.Org-derived path assumes. That is a separate
piece of work of a different size, and this plan does not start it.

What R14 does is make the **declaration** able to express 8 MB, so that when
G400 support is attempted the memory side is already honest. Doing it now,
while the arithmetic is fresh and the tests are in place, is cheaper than
doing it under a G400 bring-up.

---

## 4. Order, and why it costs almost nothing to verify

| stage | what | verified by |
|---|---|---|
| 1 | `effectiveProven`, the third proof outcome, and the parser -- all in the shared file | host suite; the existing 240-case truth table and the brief assertions extend to 8 |
| 2 | the driver takes the capped floor | extracted-block compile, as stages 3 and 4 were |
| 3 | the third radio cell | Configure on screen; **no reboot** |
| 4 | a boot with `MGA Memory Size = 8` on this 32 MiB board | the window must be refused above 800x600 and the log must say why |

Stage 4 is the interesting one and it is safe: declaring **less** than the
board has can only narrow what the driver offers. It is also the only honest
test available -- there is no 8 MiB board here, so what is being verified is
that the *declaration* is obeyed, not that an 8 MiB board works.

Rollback is the instance table, as always.

---

## 5. Open questions for cross-review

1. **Is capping `PROVEN` by the declaration right**, or should a declaration
   below the static floor be refused outright as unsupported? Capping means a
   small declaration silently reduces what a large board offers -- which is
   arguably exactly what a declaration is for.
2. **Should 8 MB disable the offscreen window entirely** rather than offering
   it at 640x480 and 800x600? The window is real and proven the same way; the
   argument for refusing would be that an 8 MiB board has other pressures the
   arithmetic does not model.
3. **Is 8 the right third value**, or should the radio offer 4 as well? The
   Matrox boards of this era shipped at 4, 8, 16 and 32; 4 MiB minus a 4 MiB
   margin is zero, so 4 would mean "never any acceleration" -- which may still
   be worth expressing.
4. **Does adding a smaller value change the meaning of a missing key?** The
   proposal is no: absent still means 16, and only an explicit `8` selects 8.


---

## 6. Cross-review verdicts (codex terra, 2026-08-26)

| claim | check | verdict |
|---|---|---|
| **An explicit `8` would behave exactly as 16 today.** `OSMGAAttemptLimit` returns only its conservative or its gated value, and `8 < 32` takes the conservative branch | read `OpenStepMGAWindowMath.c:610-620`; python: declared 8 -> attempt limit **16 MiB** -> ceiling **12 MiB**, not 4 | **accepted -- blocker, and my premise was wrong.** §1's whole table assumed the ceiling follows the declaration. With C1-C3 alone the cdev could hand out 12 MiB on an 8 MiB board |
| `osmgaProbeVramExtent` writes at megabyte boundaries below a literal 16 MiB, gated only on the window state -- not on the declaration | `:9741` (`mb < 16UL`), called at `:3868` | **accepted -- second blocker.** On a physical 8 MiB board it writes past the board before anything has been declared |
| `readManualMemoryConfiguration:` still clamps every non-16 value and would log a false "clamping to 16" | worse than that: **the machine logged it at 12:19:16**, on the very boot that also logged `R12: declaration 32 MiB (ok) ... may attempt 32 MiB` | **accepted -- a live defect we shipped**, harmless to behaviour but flatly contradictory in the log |
| C1's list of `PROVEN` line numbers is stale | true; the only live window bound is `:3896`, the rest are opt-in diagnostics at `:4079`, `:4325`, ... | accepted, corrected |
| `from > to` must stay an invariant violation; only `from == to` is "nothing to attempt", and a skip should advance `reached` without setting `anyFailed` | reading the staged block, a `from == to` today sets `anyFailed`, triggers a needless clean and logs "cleaned after a failed stage" | **accepted -- a better distinction than mine** |
| "no window above 800x600" is true only for RGB:888/32 -- 1024x768 BW:8 starts at 1,048,576 and would register | python: window **3,145,728 bytes**, registered, and refused by Mesa only because it is not 32-bit colour | **accepted -- my claim was unqualified** |
| "acceleration at 800x600" overstates it | python: the 800x600 window is 1,449,984 bytes; an 800x600 colour+depth pair needs 2,885,120 and a 640x480 pair needs 1,843,200. **Only the 320x240 minimum fits.** At 640x480 the window is 2,310,144 and a full-screen pair does fit | **accepted -- so 8 MB buys a full-screen buffer at 640x480 only** |
| `min(PROVEN, ceiling)` is sound as a *declaration policy* on this proven G450, but is not hardware evidence about a future 8 MiB G400 | agreed, and R14 already disclaims G400 support | accepted, stated |
| The panel needs more than a third cell: `osmgaVramTagFor` maps only 32->1, and `vramChanged:` stores only "32"/"16" | read `OSMGADisplayInspector.m:102` and `:210` | accepted |
| Do not add 4 MB now -- a 4 MiB board minus a 4 MiB margin is a zero ceiling | agreed | accepted |

### What R14 becomes

The plan as written fixes the wrong three things. The revised set:

- **E1 -- the attempt limit must be able to return less than 16 MiB.** It is
  currently a two-level function by construction, and that is the blocker. The
  gate still may not assert capacity, so the shape becomes
  "the declaration, bounded above by what the gate permits", with the
  conservative value as the fallback for missing or unusable input -- not as a
  floor.
- **E2 -- the extent probe takes the attempt limit** instead of a literal
  16 MiB. Nothing may write past what was declared.
- **E3 -- `min(PROVEN, ceiling)`** as before, which only becomes reachable
  once E1 lands.
- **E4 -- three proof outcomes**, with `from == to` skipping rather than
  failing, and `from > to` still an error.
- **E5 -- delete the stale clamp** in `readManualMemoryConfiguration:`. It is
  contradicting the R12 line on every boot today.
- **E6 -- the panel's tag map and store** for three values, plus the nib.
- **E7 -- claims qualified**: "no RGB:888/32 window above 800x600", and
  "a full-screen GL buffer at 640x480 only".

### One thing to fix regardless of whether 8 MB proceeds

**E5.** The machine is logging `MGA Memory Size != 16 MiB; clamping to 16`
next to `declaration 32 MiB (ok)`, and nothing clamps anything -- the member
that path writes is not used by the window logic. It is a false statement in
the boot log of a shipped driver, and it costs nothing to remove.
