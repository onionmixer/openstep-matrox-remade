# R15 -- triangles that start left of the screen

Found on 2026-08-26 while verifying what R9-R14 bought. Written before any
code.

---

## 1. What was measured

The teapot draws at 640x480 and 1024x768 and **dies** at 1280x1024 and
1600x1200:

```
rendering at 1600x1200
   surface is the engine's : yes
OpenStepMGA: hardware acceleration revoked (the driver kept refusing batches)
Segmentation fault
```

The driver logs no reason -- the submit path returns `IO_R_INVALID_ARG` and
moves on -- so `test/openstep-mga-mesa-refusal-probe.c` was written to ask the
back end, which keeps the last refusal the kernel handed back. One coarse
teapot (grid 4, one pot) at four sizes:

| size | drawn by the card | left to Mesa | refusals |
|---|---:|---:|---|
| 800x600 | 976 | 0 | -- |
| 1024x768 | 978 | 0 | -- |
| 1280x1024 | 980 | **1** | `E_TRICOL` |
| 1600x1200 | 979 | **5** | `E_TRICOL` x5 |

**Not an aspect-ratio effect**: 1600x1200 is 4:3, the same as the frustum, and
it fails hardest.

## 2. The cause, exactly

The refusal record carries a copy of the triangle:

```
fxbndry : 028efd97  left=64919 right=654 (width 1280)
```

`0xfd97` is **-617** read as a signed 16-bit value. The left boundary is a
negative x -- the triangle starts off the left edge of the surface -- and it
is packed into an unsigned field:

```c
/* mesa/OpenStepMGAMesaTriangle.c:662 */
t->fxbndry = (((unsigned long)right) << 16) |
             ((unsigned long)left & 0xffffUL);
```

There is no clamp. The validator then judges the wrapped value:

```c
/* hw3d/OpenStepMGAHW3D.c:557-560 */
if (left > lim->clipX1 + 1UL || right > lim->clipX1 + 1UL || left > right)
    return OSMGA_HW3D_E_TRICOL;
```

64919 > 1280, so E_TRICOL. **The validator is right**; what is wrong is that a
value it cannot express was sent to it at all.

Why more often at larger surfaces: the same geometry covers more pixels, so an
edge that extrapolates past x=0 does so by more, and rounds to a negative
integer where at 800x600 it rounded to zero or above.

## 3. And then it crashes

`OSMGA_MESA_REFUSAL_LIMIT` is **8 consecutive refusals**, reset on any success
(`mesa/OpenStepMGAMesaHook.c:197, 616-620, 710, 760`). The coarse probe never
reaches eight in a row; the full scene -- 16,106 triangles over two pots --
does, so acceleration is revoked. **The software path then segfaults.**

That is a second, independent defect: a revoke is supposed to be the safe
fallback, and it is currently fatal.

## 4. What to do about the boundary

Three ways, and they are not equally honest.

**(a) Clip the trapezoid to x >= 0 in the back end.** Keeps the triangles on
the card. It means moving the left boundary to zero AND advancing every
interpolator anchor to the new first column -- colour (`dr[]`), depth (`z0`),
alpha (`a0`) and, for textured primitives, `tu0/tv0/tq0`, all of which are
defined at the trapezoid's own first row and left edge. Real work, and every
one of those anchors is a place to be subtly wrong.

**(b) Detect it in the builder and hand the triangle to software.** The path
already exists and is already used for cases the engine cannot express --
`osmgaMesaSoftly`, counted as "left to Mesa" rather than as a refusal
(`OpenStepMGAMesaHook.c:1129-1134` is the pattern). Small, removes the
refusals, removes the revoke, and removes the crash trigger. Costs those
triangles to the software rasteriser, which is what already happens today --
only today it costs a kernel round trip and a fatal side effect as well.

**(c) Teach the validator to accept a signed left.** Rejected: what the
hardware does with a negative FXLEFT is not established, and FXBNDRY's halves
are 16-bit unsigned fields, so -617 has no representation to accept.

**Proposed: (b) now, (a) as separate work.** (b) is a strict improvement over
today with no new risk; (a) is where the performance is, and it deserves its
own plan rather than being smuggled in as a bug fix.

## 5. What (b) must get right

- **Both edges.** The measured case is a negative left, but the same check
  refuses `right > width`. A triangle off the RIGHT edge has to be handled by
  the same test, and the plan must not fix only the half that was measured.
- **`left > right`.** The third arm of the validator's condition. Whether the
  builder can produce it is not established and must be checked rather than
  assumed.
- **The counter.** It should be counted where "the engine cannot express
  this" is counted, not where refusals are, or the statistics will say the
  driver refused work it was never asked to do.
- **Both trapezoids of a split triangle.** A triangle cut at its middle
  vertex produces two; if one is off-screen and the other is not, they must
  not be half-drawn -- the batch contract already says a refusal of the second
  draws neither.

## 6. What is NOT in this plan

The segfault. It has to be understood, but it is a different defect in a
different place, and fixing the boundary will hide it -- with (b) in place the
revoke stops happening in this scene, and a latent fatal fallback is exactly
the kind of thing that then goes unnoticed for a year. **It gets its own
investigation, and it should keep a reproducer.**

## 7. Verification

| claim | how |
|---|---|
| the refusals go to zero | `refusal 1280 1024` and `refusal 1600 1200`: "left to Mesa" rises by the same count, "refused" stays 0 |
| the picture is unchanged where it already drew | the 640x480 and 1024x768 TIFFs, byte-for-byte against the ones from before |
| the teapot completes at 1600x1200 | it currently does not run at all |
| nothing regressed | the host suites, and the existing scene baselines |

## 8. Open questions for cross-review

1. **Is (b) right, or is (a) close enough to reach for now?** The cost of (b)
   is a handful of triangles per frame on the software path; at 1600x1200 that
   was 5 in 984 for one coarse pot, and unmeasured for the real scene.
2. **Where exactly should the test go** -- in the trapezoid builder, or before
   it in the hook where the other "cannot express" tests live?
3. **Is `left > right` reachable**, and if so does it mean something different?
4. **Does the software path draw these triangles identically** to what the
   engine would have drawn, or is there a visible seam where a partly
   off-screen triangle meets an on-screen one?


---

## 9. Cross-review verdicts (codex terra, 2026-08-26)

| claim | check | verdict |
|---|---|---|
| **§5 misses `E_TRICROSS`.** The validator walks EVERY row and rejects an edge that leaves `[0, width]` there, reporting `E_TRICROSS`, not `E_TRICOL` | read `hw3d/OpenStepMGAHW3D.c:614-620`: the row loop calls `osmgaHW3DStep` and returns `E_TRICROSS`. And `test/openstep-mga-mesa-narrowreplay-test.c` is a **frozen real-builder TRICROSS case**, found by deterministic search | **accepted -- blocker.** I looked at the first row and stopped |
| The measured `0xfd97` makes `left > right` true as well as `left > width`, so the verdict does not prove which arm fired | 64919 > 1280 **and** 64919 > 654 | accepted -- §2 overstated what the code proves |
| `right > width` is independently reachable; `left > right` is not, from an unwrapped builder result (`osmgaFirstDrawn` skips until `left <= right - 1`) | read `OpenStepMGAMesaTriangle.c:411` | accepted |
| Do **not** call `osmgaMesaSoftly` from the trapezoid builder -- it is context-free, has no Mesa indices, and is used by tests and the clear path | true of the builder's callers | **accepted -- I would have put it in the wrong place** |
| Never software-render only the offending trapezoid; the whole source triangle goes, or ordering and depth break | the invariant is already documented at `OpenStepMGAMesaHook.c:637-642` | accepted |
| Option (a) is understated: `ar0/ar1/ar2/sgn` still describe the unclipped edge, and x clipping can need more pieces than the builder's two-output contract allows | read the builder | accepted |
| §7's "refused stays 0" is ambiguous -- the probe labels `hookUnsupported` "refused" while kernel refusals are counted elsewhere | true of my own probe's labels | accepted |
| Mesa's clip path was never meant to protect this boundary; the splitter has no surface width at all | consistent with the source | accepted |

### The reframing this forced

Reading the narrowing code with codex's citation in hand changes what the
defect **is**. `test/openstep-mga-mesa-narrowreplay-test.c` exists because a
refused sliver is *supposed* to cost one software triangle -- the machinery
already handles inexpressible geometry correctly, and it has a frozen test.

So the refusals are not a correctness failure. What fails is the **backstop**:

```c
/* OpenStepMGAMesaHook.c:616-620 */
if (++hookRefusedRun >= OSMGA_MESA_REFUSAL_LIMIT)   /* 8 */
    OSMGAMesaProbeRevoke("the driver kept refusing batches");
```

`hookRefusedRun` is reset **only by a batch that actually submits** (`:710`,
`:761`). Every refusal counts, including one that was narrowed to a single
triangle and correctly replayed. The backstop was written to catch a driver
refusing *everything*; a scene with eight inexpressible triangles in a row and
no successful batch between them looks identical to it.

That is why the failure scales with resolution: bigger surfaces produce more
off-screen edges, and eventually eight land in a row.

### The plan after the review

**B1 -- a cheap preflight, in the right place.** In `osmgaMesaTriangle`, after
`OSMGAMesaBuildTriangleTex` returns and before anything is appended: for each
emitted trapezoid, test the **unpacked signed** boundaries --
`left < 0 || right > width || left > right`. That is O(1), it is stated in the
values the builder still has before packing, and it catches exactly the class
that wraps. If any trapezoid fails, the **whole source triangle** goes to
software.

Not the full row walk. Codex is right that the walk is where `E_TRICROSS`
lives, but the measured evidence says the walk is not what is happening here:
all five refusals at 1600x1200 were `count[9 E_TRICOL] = 5`. Duplicating the
validator's recurrence in the back end would be the largest and most
divergence-prone part of this change, to catch a class the existing narrowing
already handles with a frozen test.

*(If it ever needs doing, it should link the validator itself rather than
restate it -- `OpenStepMGAHW3D.c` is pure C with no kernel dependency and
`mesa/test-mesa-texreach.c` already builds it alongside the back end.)*

**B2 -- the backstop stops misfiring.** A refusal that was narrowed to a
single triangle and replayed is the machinery working, not the driver
misbehaving, and must not count toward the revoke. This is needed **whether or
not B1 lands**, because `E_TRICROSS` slivers exist and are meant to be
handled this way.

**B3 -- the segfault keeps its own investigation**, and codex is right that
B1/B2 must not be allowed to hide it: they make the fallback *less* travelled.
It gets a retained reproducer built on the existing
`OSMGAMesaHookInjectRefusal`, which forces refusals on demand.

### Verification, corrected

- a direct offscreen-left case, pixel-compared against forced software,
  including a split triangle and depth;
- `hookSoftware`/`hookUnsupported` rise while the kernel's `E_TRICOL` count
  stays zero -- naming the counters exactly, not "refused";
- the forced-eight-refusal reproducer, retained until B3 is fixed.


### Two facts gathered while the second review runs

**The insertion point already exists, with its counter.** `osmgaMesaTriangle`
at `OpenStepMGAMesaHook.c:1397-1405` already has exactly the shape B1 needs,
for the case the builder declares inexpressible:

```c
if (n == OSMGA_MESA_TRI_UNSUPPORTED) {
    hookUnsupported++;
    (void)osmgaMesaSoftly(ctx, v0, v1, v2, pv);
    return;
}
```

-- and its comment says why: the two answers used to be one, "so a triangle
whose coordinates ran past the range was quietly lost instead of being drawn
by the software path". This is the same class of problem one step further out,
and it belongs in the same place.

**Sign extension is lossless here, so the test may be made on the packed
value.** `OSMGA_MESA_RULE_COORD_MAX` is **8192**
(`OpenStepMGAMesaTriangle.c:229`) and vertices are refused outside
`+/- 8192 * 256` in 1/256 units, so any boundary the builder can emit has
`|left| <= 8192`, comfortably inside a signed 16-bit field. Reading
`(short)(fxbndry & 0xFFFF)` therefore recovers the true value exactly -- the
measured `0xfd97` is exactly -617 and not an artefact of a wider wrap.

That matters because it means B1 does **not** need a signature change to the
builder: the hook can test what was packed, with no new parameter and no new
call-site churn across the seven other places the builder is used.

What it does not decide is *where the test belongs* -- the builder knows the
packing, the hook knows the surface width -- and that is one of the questions
in the second review.
