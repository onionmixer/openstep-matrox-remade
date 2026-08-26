# R17 -- the triangles a revoke throws away

Found by cross-review of R16, reproduced on hardware, and separate from the
segfault. Written before any code.

---

## 1. Reproduced, in a second, at 320x240

`test/openstep-mga-mesa-namedrevoke-test.c` draws nine opaque triangles, one
per row band, and injects a refusal that **names a trapezoid** so the flush
takes the narrowing branch:

```
reference : 195 rows lit, 9 drawn by the card
injected  : 176 rows lit, 8 spoiled, 0 to the card, 8 to Mesa
            -> 176 of 195 rows survived
```

**Eight triangles arrive, the ninth does not.** Nineteen rows of the picture
are missing.

The sequence is the one the source predicted:

1. eight named refusals, each narrowed to one source and replayed in software;
2. the eighth reaches `OSMGA_MESA_REFUSAL_LIMIT` and revokes;
3. the flush reacquires the command window, finds it gone, and **breaks**,
   dropping every source it had not reached.

The existing injection cannot produce this: a corrupt batch magic is judged
before any triangle is looked at, so its verdict is batch-level and the flush
takes the other branch entirely -- which is why the teapot's `inject` mode
revokes and finishes cleanly.

## 2. Why the drop is wrong

Two paths clear `probeBatch`, and they are **not** the same:

| | what it releases | can software still draw? |
|---|---|---|
| `OSMGAMesaProbeRevoke` (`Probe.c:246-262`) | the command window, and closes the device | **yes** -- the colour surface is untouched |
| the fork path (`Probe.c:216-224`) | the command window **and** `OpenStepMesaAccelReleaseBuffer(0)` | no -- the surface mapping is gone |

The drop sites treat them alike. The comment at the flush entry says it
plainly:

```c
/* The window went away with work pending; the probe has revoked and
 * the surface is gone, so there is nothing to draw INTO. */
```

That is true of the fork and **false of a revoke this process caused itself**.
A revoke is the back end deciding to stop using the engine; the surface it was
drawing into is still mapped, `savedTriangle` is still set, and every one of
those triangles could be drawn. They are thrown away instead.

Two sites do it: `OpenStepMGAMesaHook.c:715-721` (flush entry) and `:878-882`
(after the narrowing reacquire). Only the second is what the harness measures,
but the reasoning is identical and fixing one would leave the other.

## 3. What to change

**Distinguish "the engine is no longer available" from "there is nowhere to
draw".** When the batch is gone but the surface is still bound to this
context, replay the remaining sources in software instead of dropping them.

The predicate has to be about the surface, not about the batch:
`OSMGAMesaBufferBoundTo(ctx)` -- `bufBound` is cleared by
`OpenStepMesaAccelReleaseBuffer` and by nothing the revoke does. `bufOrigin`
is **not** a safe test: the release leaves it set.

```
batch = OSMGAMesaProbeBatch();
if (batch == 0) {
    if (OSMGAMesaBufferBoundTo(ctx))
        replay every remaining source in software;   /* new */
    else
        drop, as now;                                /* the fork case */
    break;
}
```

and the same at the flush entry, where the whole pending set is the remainder.

## 4. What this does NOT claim

**It is not the segfault.** The harness drove exactly this path -- named
refusal, narrowing, revoke, reacquire, null -- and **did not crash**. So the
dropped tail and the crash are two different faults, and the crash needs
something the harness does not yet have: the scenes that crashed had depth,
lighting, split trapezoids and seventeen thousand triangles, against nine flat
opaque ones here.

There is a hypothesis worth testing rather than believing: **software drawing
into a surface that has been released** would fault, and the fork path
releases it. Whether anything reaches that combination is unknown. R17 must
not assume it, and the fix above must not create it -- which is exactly why
the predicate is `BoundTo` and not `batch == 0`.

## 5. Verification

| claim | how |
|---|---|
| the tail arrives | `tnrv` goes from 176/195 to 195/195 |
| nothing else moved | `tnr` (the frozen sliver) still passes; the teapot at 640x480 stays byte-identical |
| the fork case still drops | cannot be produced here; the predicate is argued from the source and the two release paths are cited |
| the injection is honest | it lies about the drawing control and nothing else -- the geometry handed over is the geometry the builder made |

## 6. Open questions for cross-review

1. **Is `OSMGAMesaBufferBoundTo(ctx)` the right predicate**, or is there a
   state where it is true and drawing would still fault?
2. **Should the flush-entry site change too**, or is its case genuinely
   different -- there the pending work has not been submitted at all, and the
   context may be null rather than the batch.
3. **Is replaying after a revoke safe with respect to ordering?** Everything
   before the revoke went to the engine or to software in order; the tail
   would go to software after. Nothing has been drawn twice, but the argument
   should be stated rather than assumed.
4. **Does the counter want splitting?** `hookFlushOther` currently counts both
   "dropped" and, after this, "rescued", and those are different events.
