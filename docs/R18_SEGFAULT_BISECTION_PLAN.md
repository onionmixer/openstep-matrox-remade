# R18 -- narrowing down the crash

The last open item from the 1600x1200 work. Written before any code.

---

## 1. What is left after R15 and R17

The crash was seen twice, at 1280x1024 and 1600x1200, always as:

```
OpenStepMGA: hardware acceleration revoked (the driver kept refusing batches)
Segmentation fault
```

Since then two things that looked like the cause have been eliminated **by
measurement, not by argument**:

- **not memory** -- the arena had 8.5 MiB spare at the size that crashed, and
  the verdict was `E_TRICOL`, a judgement about a triangle boundary;
- **not revocation itself** -- the teapot's `inject` mode revokes and finishes
  cleanly, and `tnrv` now drives the *narrowing* revoke as well and also
  finishes cleanly.

And one real fault found on the way, fixed, and not the crash: the tail a
revoke used to throw away (R17).

So the crash needs something `tnrv` does not have. The two scenes differ in
five ways at once:

| | `tnrv` | the crash |
|---|---|---|
| triangles | 9 | 17,292 |
| shading | flat, one colour | lit, smooth |
| depth | off | on, 16-bit, shared with the engine |
| trapezoids | one per triangle | split pairs |
| surface | 320x240 | 1280x1024 and 1600x1200 |

Five differences is not a diagnosis. **One at a time is.**

## 2. The method

Grow the harness toward the crash, changing one thing per run, and stop at the
first change that crashes. Every step is a second, not three minutes, and none
of it needs the display.

Cheapest-and-most-suspicious first:

1. **depth on** -- the only one that adds a second VRAM region and a second
   mapping, and a mapping is the kind of thing a teardown can invalidate;
2. **split trapezoids** -- the only one that changes how many trapezoids a
   source owns, which is exactly what the narrowing map indexes;
3. **many triangles** -- pushes past `pendSrc`'s capacity so the flush runs
   more than once, which is the loop the crash lived in;
4. **lighting** -- smooth colour, more interpolators per trapezoid;
5. **surface size** -- last, because the earlier evidence already argues
   against it: 1280x1024 crashed and is exactly 640 pages, so page alignment
   does not separate the crashing sizes.

If none crashes, the honest conclusion is that the crash needed the *geometry*
refusals specifically -- real `E_TRICOL` from real off-screen edges -- and the
next step is a build with R15's preflight compiled out.

## 3. The tool

`tnrv` already forces a named refusal. What it cannot do is any of the five
rows. It gains flags rather than becoming five programs -- depth, split,
count, lighting, size -- each defaulting to what it does today, so the current
PASS keeps its meaning.

The teapot is rejected for this: it has all five at once, which is the thing
to avoid, and its runs are minutes.

## 4. What this must not do

**It must not "fix" anything.** If a step crashes, the run stops there and the
fault gets its own plan. Reaching for a repair while five variables are still
moving is how a coincidence becomes a cargo cult.

**It must not read a clean sweep as "no bug".** R15 removed the route the
original crash took, so nothing here can prove the crash is gone -- only that
these conditions do not produce it.

## 4b. One of the five, measured before the review comes back

`pendSrc` holds `OSMGA_HW3D_MAX_TRI` = **180** sources, and a batch is flushed
when `pendTraps + n > 180` or `pendSrcCount >= 180`
(`OpenStepMGAMesaHook.c:535, 1696`).

| | sources | trapezoids | flushes per bracket |
|---|---:|---:|---|
| `tnrv` | 9 | 9 | **one** |
| the teapot | ~134 per bracket, 17,292 over 129 brackets | up to 2 per source with splits | **repeatedly** |

So "many triangles" is not the same variable as "the flush runs more than
once": 134 sources fit in 180, but 134 sources that each split into two
trapezoids do not. The step that matters is the one that makes the flush loop
run again on the same frame, and only the split makes that happen at this
scene's size.

That moves **split trapezoids** up the order and merges "many triangles" into
it, unless the review says otherwise.

## 5. Open questions for cross-review

1. **Is the order right?** Depth first is a guess dressed as a ranking -- it
   is the only one that adds a mapping, but "adds a mapping" is not evidence
   that the mapping is what fails.
2. **Is `pendSrc` capacity worth its own step**, or is it the same thing as
   "many triangles"?
3. **Should the geometry route be reproduced directly instead** -- a build
   with the preflight disabled, at 1280x1024 -- as step zero rather than as a
   fallback? One rebuild reproduces the exact original, at the cost of not
   being a permanent harness.
4. **Is the fork candidate worth folding in** -- `ReleaseBuffer(0)` leaving an
   inherited OSMesa context's row addresses pointing at unmapped pages?


---

## 6. Cross-review verdicts (codex terra, 2026-08-26)

| claim | check | verdict |
|---|---|---|
| **Disabling the preflight alone does not restore the original route.** R15's B2 pardons a successfully replayed geometry refusal, so real `E_TRICOL` would come back but would never advance the revoke counter -- a clean run would be inconclusive | true of the rule I wrote in R15 and then forgot when planning the reproduction | **accepted -- blocker, and my own doing.** Step zero needs the preflight bypassed AND the pre-R15 counting restored for geometry verdicts, while keeping R17's rescue |
| **The biggest missing variable is not one of my five: work AFTER revocation.** `tnrv` revokes on triangle eight, rescues nine and ends; the teapot has thousands of callbacks and a whole second pot left, all after the command mapping is gone | true of the harness | **accepted -- a variable I did not have** |
| `pendSrc` does not overflow: the hook flushes before append when either capacity is reached, and the ABI cap is the same 180. 181 sources proves repeated flushing, not overflow, and cannot corrupt the narrowing map | read `:1690` and `OpenStepMGAHW3D.h:76` | accepted -- my §4b wording said "pushes past capacity", which is wrong |
| R17 has no source-proven new crash: both rescue sites bracket and restore the four context fields, counts are detached before replay, and `pendInFlush` makes the nested flush a no-op | matches what I implemented | accepted |
| Keep the fork candidate out: only PID-change detection calls `ReleaseBuffer(0)`, and a normal revoke never does | read `Probe.c:212` | accepted -- separate reproducer, separate plan |

### Read while checking the review: all three `batch == 0` sites are guarded

Following codex's post-revocation variable, I read every path a triangle can
take once the command window is gone:

- `osmgaMesaTriangle` (`:1254-1261`) -- counts `hookDeclined`, draws in
  software, revokes again harmlessly, returns. **Never appends**, so nothing
  accumulates and the flush stays a no-op afterwards.
- the flush entry (`:723`) -- now rescues or drops, per R17.
- the narrowing reacquire (`:927`) -- now rescues or drops, per R17.

So the crash is **not an unguarded null batch**. That is worth writing down
because it is the third hypothesis in a row to die, and because it makes the
sheer volume of post-revoke triangles less interesting than it first looked --
each one takes a guarded path.

### The order after the review

0. **the legacy geometry route**, at 1280x1024, with the real scene -- the
   only step that preserves geometry, state, batching and post-revoke
   execution all at once instead of guessing which matters;
1. post-revoke continuation -- a controlled tail of 1, 180 and many more
   triangles, across another render bracket;
2. multi-flush crossing -- 181 one-trapezoid sources, for the repeated flush
   and not for an overflow;
3. smooth lighting -- it exercises the `ColorPtr`/`IndexPtr`/`Specular`
   snapshots R17 installs and restores;
4. split trapezoids -- more than one trap per source, through `firstTrap`;
5. depth -- lower, because a revoke unmaps only the command window;
6. surface size -- last.
