# R16 -- the crash after a revoke

The one thing R15 deliberately did not fix. Written before any code.

---

## 1. What is known, and what turned out to be wrong

Before R15's preflight, the teapot at 1280x1024 and 1600x1200 ended:

```
OpenStepMGA: hardware acceleration revoked (the driver kept refusing batches)
Segmentation fault
```

**The obvious reading is wrong twice over.**

**Not memory.** The arena test at the same size bound a full-screen surface
and reported 8,511,488 bytes of texture arena still free, out of a
20,037,632-byte window against 11,524,096 of colour and depth. And the
verdict was `E_TRICOL` -- a judgement about a *triangle boundary*, not about
memory. No `E_DSTSIZE`, `E_DSTORG` or `E_ZORG` appeared at all.

**Not "revoke then crash".** Tested rather than assumed: the teapot's own
`inject` mode forces refusals and **does** revoke --

```
surface is the engine's : yes
OpenStepMGA: hardware acceleration revoked (the driver kept refusing batches)
... replayed after refusal : 634, share drawn by the card : 0%
```

-- and completes normally, writes its file, and does not crash. So
revocation on its own is safe.

## 2. What that leaves

The two paths differ in exactly one thing:

| | `inject` (E_MAGIC) | the crash (E_TRICOL) |
|---|---|---|
| verdict | batch-level | names a triangle |
| `osmgaMesaVerdictNamesTriangle` | false | **true** |
| branch taken | "cannot place it: replay everything, break" | **the narrowing loop** |
| iterations | one, then out | loops, slides the batch down, re-enters |

`inject` never enters the narrowing loop. The crash only ever happened on
scenes that do. **That is the region to investigate**, and it is a much
sharper statement than "after a revoke".

One narrowing round is known good: `tnr`
(`test/openstep-mga-mesa-narrowreplay-test.c`) drives a frozen real-builder
sliver through it and passes -- `drawn 30, replayed 1, narrowed 1`, and the
picture matches the limit-1 reference to the pixel. So the loop is correct
once. What has never been exercised is **many rounds, and a revoke arriving
in the middle of one**: both limits are eight
(`OSMGA_MESA_REFUSAL_LIMIT`, `OSMGA_MESA_NARROW_LIMIT`), so the round that
exhausts the narrowing budget is also the round that can revoke.

## 3. Why this needs a tool, not a bigger teapot

R15's preflight means `E_TRICOL` no longer reaches the kernel, so the
narrowing loop is now **less** travelled than before -- the crash is not
fixed, it is unreachable in this scene. Reproducing it by removing the
preflight would mean a Mesa rebuild and a three-minute run per attempt.

What is missing is an injection that produces a **narrowable** refusal.
`OSMGAMesaHookInjectRefusal` corrupts the batch magic
(`OpenStepMGAMesaHook.c:819`), which is deliberately batch-level: it is the
wrong shape for this, and that is why `inject` cannot reproduce the crash.

**Proposed: a second injection mode that makes the kernel refuse one named
triangle**, so the narrowing loop runs for real at 640x480 in thirty seconds
and under a debugger. The obvious way is to corrupt one trapezoid's
`fxbndry` on its way out -- the same fault the real geometry produced -- with
the count and the stride settable so that eight consecutive refusals can be
demanded on purpose.

## 4. What the plan is NOT

It is not a fix. Nothing here proposes a change to the narrowing loop,
because **nothing yet says what is wrong with it**. Three readings of that
code produced three guesses and all three were speculation:

- the local `batch` dangling after a revoke inside the prefix path -- but it
  is reacquired and null-checked before the next use (`:791-796`);
- re-entering `osmgaMesaFlushPending` from a replay -- but `pendInFlush`
  guards it (`:680-682`);
- the slide reading past the end -- not checked yet.

Guessing at it from the source has already cost more than the reproducer
will. **Build the reproducer, then look.**

## 5. Order

1. the injection mode, and a test that asserts it really produces a
   *narrowable* verdict and reaches eight;
2. reproduce the crash at 640x480;
3. only then, diagnose;
4. the fix gets its own review.

## 6. Open questions for cross-review

1. **Is corrupting `fxbndry` the right injection?** It reproduces the exact
   fault that was measured, but it also means the injected triangle is one
   the preflight would now decline -- so the injection has to sit *after*
   R15's check, or the check has to be bypassed for injected triangles.
2. **Should the injection instead force the narrowing budget to zero**, which
   would exercise "cannot place it" without any geometry lie?
3. **Is 640x480 enough**, or does the crash need the larger surface for a
   reason not yet understood -- in which case the reproducer must be at size
   and the thirty-second cycle is not available.
4. **Is there an existing debugger on this machine** worth planning around,
   or should the investigation be by instrumentation only?


---

## 7. Cross-review verdicts (codex terra, 2026-08-26)

| claim | check | verdict |
|---|---|---|
| **After a revoke mid-narrow the code reacquires the batch, gets NULL, and DROPS every remaining source triangle** -- and the comment justifying it is false, because `OSMGAMesaProbeRevoke` unmaps only the command window, never the colour/depth surface, so there IS still somewhere to draw | read `OpenStepMGAMesaHook.c:844-848` (`if (batch == 0) { hookFlushOther++; break; }`) against `OpenStepMGAMesaProbe.c` -- revoke `vm_deallocate`s `probeBatch` and closes the fd, nothing else | **accepted -- a source-proven correctness bug, and not the one I was looking for.** Triangles are silently lost from the picture |
| My `fxbndry` injection **cannot reach the revoke** under R15's B2: `E_TRICOL` is pardoned after a successful replay, so eight of them exhaust the narrowing budget, the ninth takes "cannot place it", counts once, and exits | follows from the B2 rule I wrote yesterday | **accepted -- the proposed reproducer would not have reproduced anything** |
| Use an invalid opcode nibble instead: `E_DWGCTL` is **named** (so narrowable) but not a geometry verdict (so it still counts), which is the combination that reaches a revoke mid-narrowing | read `OpenStepMGAHW3D.c:459-475`: `*badTri = i` is set at the top of the per-triangle loop and the opcode/atype test is inside it | accepted |
| The three guesses in §4 are all refutable from the source -- dangling `batch` (reacquired and null-checked), replay re-entry (`pendInFlush` guards it), slide overrun (`base <= nb < ntraps`, forward copy, bounded) | read each | accepted, and worth the paragraph: refusing to act on them was right, but they can be **closed** rather than left open |
| "`inject` differs by exactly one thing" is no longer true after R15 -- `E_MAGIC` also fails before per-triangle validation, counts *before* replaying the remainder, and still counts under B2 | true | accepted; the honest statement is "narrowing **plus the old counting policy**" |
| The colour mapping length is size-specific | python: 640x480, 1024x768 and 1280x1024 are **exact** page multiples (150, 384, 640); only 1600x1200 is ragged at 937.5 | ⚖️ **accepted as a caution, weakened by the data**: 1280x1024 crashed too and is exactly 640 pages, so alignment does not separate the crashing sizes. Still worth one run at 1600x1200 before calling size irrelevant |

### What this changes

**R16 gains a second subject, and it is the more certain one.** The dropped
tail is provable from the source and costs a picture its triangles whenever a
revoke lands mid-flush; the segfault is still only an observation. They are
separate and the tail should not wait for the crash.

**The reproducer changes shape.** Two injections, not one:

1. **`E_DWGCTL` by an invalid opcode nibble**, applied after B1 and only to
   the main/remainder submit -- named, narrowable, and still counted, so it
   reaches a revoke mid-narrowing. At least **nine** sources: eight to refuse
   and a ninth so that execution reaches the post-revoke reacquire, which is
   where the tail is dropped.
2. **`fxbndry`**, still worth having, but for what it actually tests: real
   `E_TRICOL`, repeated slides, and B2's pardon -- not the revoke.

Both must be injected in the flush immediately before submit, **not** in the
builder, because B1 now runs before anything is appended.

**And a warning to keep**: a clean 640x480 run must not be read as "fixed".
R15 already removed the route the original crash took, so the absence of a
crash there proves nothing about the crash. Only the named-revoke harness
does.
