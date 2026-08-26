# R19 -- the crash: a write through a freed command window

Found 2026-08-26, after four hypotheses died. Written before any code.

---

## 1. The reproducer, and what it separated

`teapot ... legacy 1280x1024` crashes **5 runs out of 5**, always after
"hardware acceleration revoked". The same command with `legacydrop` (R17's
tail rescue turned off) also crashes 5 of 5 -- so **R17 is neither the cause
nor the cure**, which is the conclusion I had reached and which this refutes.

What separates crashing from not crashing is one thing: **where the refusal
count sits**, and therefore where the revoke can fire.

| build | count position | result |
|---|---|---|
| shipped (R15 B2) | after the named source is replayed | completes |
| `legacy` | before narrowing, as before R15 | **crashes, 5/5** |

## 2. The mechanism

Under the legacy ordering, in `osmgaMesaFlushPending`:

```c
if (hookLegacyGeometry)
    osmgaMesaCountRefusal();        /* :897 -- MAY REVOKE */
...
prefix = pendSrc[s2].firstTrap - base;
if (prefix != 0UL) {
    batch->triCount = prefix;       /* :930 -- writes through `batch` */
```

`batch` is a local copy of `probeBatch`, taken once before the loop.
`osmgaMesaCountRefusal` can reach `OSMGAMesaProbeRevoke`, which does

```c
(void)vm_deallocate(task_self(), (vm_address_t)probeBatch,
                    (vm_size_t)OSMGA_CMD_WINDOW_LEN);
probeBatch = 0;
```

so `batch` is left pointing at **unmapped memory**, and the next statement to
run writes to it. That is the segmentation fault.

## 3. Why every earlier observation fits

- **`inject` never crashed**: `E_MAGIC` is batch-level, so the flush takes the
  "cannot place it" branch and `break`s before reaching the prefix write.
- **`tnrv` never crashed**: it revokes on the eighth of nine one-trapezoid
  sources, where the named source is first in the remainder, so `prefix == 0`
  and the write is skipped.
- **the shipped build does not crash**: R15's B2 moved the count to *after*
  the replay, so the revoke can no longer fire before that write.
- **cross-review's earlier refutation was right**, for the code it was reading:
  post-R15, no path that can revoke uses the old `batch` afterwards. Under the
  legacy ordering one does.

## 4. What this means for the shipped driver

**The bug is already unreachable, and it is unreachable by accident.** Nothing
in the current source says "do not use `batch` after something that can
revoke"; it is true only because the counter happens to sit lower down. Moving
that call back -- for any reason, by anyone -- brings the crash back, and the
next person has no way to know that.

So the fix is not to change behaviour but to **make the safety explicit**: after
anything that can revoke, the stale pointer must not be used. Either
re-acquire and null-check before the prefix write, or leave the loop.

## 5. How to confirm it before changing anything

The mechanism predicts precisely: guard the prefix write and the `legacy`
crash disappears, **while the count stays where the legacy route puts it**.
That separates "the write is the fault" from "the ordering is the fault".

If the crash survives the guard, the mechanism is wrong and this document is
worth nothing.

## 6. Open questions for cross-review

1. **Is the mechanism right** -- does `vm_deallocate` of the command window
   make `batch->triCount = prefix` fault, or would that region stay mapped?
2. **Is the guard the whole of it**, or are there other uses of `batch` in the
   same iteration reachable after a revoke?
3. **Should the shipped build gain the guard**, given the path is currently
   unreachable? My view is yes -- unreachable-by-accident is a latent fault --
   but it is a change to a path that cannot execute, which deserves scepticism.
4. **Does anything else in this file hold a pointer across a call that can
   revoke?**


---

## 7. Cross-review verdicts (codex terra, 2026-08-26)

| claim | check | verdict |
|---|---|---|
| The mapping mechanism is correct: `batch` comes from `probeBatch`, the region is 24 KiB / three 8192-byte pages, revoke `vm_deallocate`s that exact base and length, and `triCount` is at offset 8 -- inside the unmapped first page | read all four citations | **accepted** |
| A debugger PC is still needed before saying line 930 was the observed fault rather than another stale use | true | accepted -- the source proves a *sufficient* fault, not the observed one |
| **§4 is wrong: the shipped build is NOT safe.** Two more stale lifetimes exist in shipped code | verified both -- `osmgaMesaTriangle` takes `batch` at `:1290`, can flush at `:1778`, and appends through it at `:1797`; `osmgaMesaClearOnEngine` takes it at `:2603` and flushes at `:2626` | **accepted -- blocker, and my claim was wrong** |
| The clear path's `if (batch == 0)` after the flush does not protect it | read `:2628`: it tests the **stale local**, which is still non-null after the revoke freed the region | **accepted, and worse than stated** -- there is a guard there that looks like protection and is not |
| §5's test is not a discriminator on its own: guarding only the prefix may simply move the fault to the triangle append | follows from the above | accepted |
| `inject` and `tnrv` fit, but as explanations of those two tests rather than universal guarantees | agreed | accepted |
| A current-build reproducer should be possible with a small batch limit, forcing a pre-append flush on the eighth refusal | plausible from the source; `tnrv` avoids it by flushing at bracket end | accepted as the next step |

### What changes

**The finding is bigger than the crash.** I set out to explain a segfault in a
route the shipped build cannot take, and the review found the same fault
pattern twice more in code that ships:

```c
OSMGAHW3DBatch *batch = OSMGAMesaProbeBatch();   /* :1290 */
...
    osmgaMesaFlushPending();                      /* :1778 -- can revoke */
...
    batch->tri[pendTraps + j] = built[j];         /* :1797 -- stale write */
```

Nothing in that function knows the flush can pull the mapping out from under
it. The clear path has the same shape and a `batch == 0` check that reads the
local copy, so it inspects the value the pointer had *before* the region was
freed.

**So the plan is no longer "guard line 930".** It is: every pointer into the
command window that is held across a call which can revoke must be
re-acquired and re-checked -- the prefix write, the triangle append, and the
clear -- and the discriminator is a captured fault address on the
deterministic reproducer, not the disappearance of a crash after one guard.

**And the priority inverts.** The legacy-route crash is a reproducer for a
fault that ships; it is no longer an artefact of a test switch.


---

## 8. Confirmed by fault address (2026-08-26)

`/bin/gdb` on the target, on the deterministic reproducer
(`teapot /tmp/gdbx.tiff hw 12 180 legacy 1280x1024`):

```
OpenStepMGA: hardware acceleration revoked (the driver kept refusing batches)
Program generated(1): Memory access exception on address 0xf06008 (invalid address)
0x536d in osmgaMesaFlushPending ()
#1  0x712f in osmgaMesaTriangle ()
...
#10 0x3131 in teapot ()

edx  0xf06000        <- the command window's base
```

**`edx + 8`.** Offset 8 in `OSMGAHW3DBatch` is `triCount`. The faulting
statement is

```c
batch->triCount = prefix;      /* :930 */
```

immediately after `osmgaMesaCountRefusal()` revoked and `vm_deallocate`d those
three pages. Not inferred -- the base register still holds the freed mapping
and the write lands on its `triCount`.

Two controls agree: the frame below is `osmgaMesaTriangle` calling into the
flush, **not** the triangle append at `:1797`; and `tnrv` never reached this
line because its named source is always first in the remainder, so
`prefix == 0`.

---

## 9. The fix

One disease at three sites: **a pointer into the command window held across a
call that can revoke.**

| site | held from | can revoke at | uses it at |
|---|---|---|---|
| flush, prefix | `:760` (entry) | `osmgaMesaCountRefusal` `:897` | `:930` `batch->triCount` |
| triangle append | `:1290` (entry) | `osmgaMesaFlushPending` `:1778` | `:1797` `batch->tri[...]` |
| engine clear | `:2603` | `osmgaMesaFlushPending` `:2626` | `:2716-2729` |

The clear site is the worst of the three: it has a `batch == 0` test after the
flush (`:2628`) that reads the **stale local**, so it looks guarded and is not.

**The rule to apply everywhere: re-acquire, then decide.** Never test or use a
copy taken before something that can revoke.

- **flush/prefix** -- re-acquire after the count; if it is gone, take the path
  the later reacquire already takes: rescue the remainder and leave.
- **triangle append** -- re-acquire after the flush; if it is gone, do what
  the entry check at `:1254` does: draw this triangle in software and return.
- **clear** -- re-acquire after the flush and test that, not the local.

In the shipped build the first is a no-op, because R15's B2 already moved the
count below the prefix work. It goes in anyway: the safety must be stated, not
inherited from where a counter happens to sit.

## 10. How it will be verified

1. the same A/B, five runs of `legacy` -- the crash must be gone **with the
   legacy count still in its old place**, which is what separates "the write
   was the fault" from "the ordering was the fault";
2. a shipped-path reproducer for `:1797` -- a small batch limit so the flush
   happens from inside a triangle callback, with refusals forced to eight;
3. `tnr`, `tnrv` and the 640x480 teapot unchanged.

## 11. Open questions for cross-review

1. **Is "re-acquire and decide" right at all three**, or does the flush/prefix
   case want to leave the loop rather than rescue -- its prefix has already
   been counted as drawn?
2. **Does the triangle-append case need more than the entry treatment?**
   `pendTraps` and `pendSrcCount` were reset by the flush, so the state it
   returns to is not the state it left.
3. **Is there a fourth site** -- anything else holding a command window
   pointer across a call that can reach `OSMGAMesaProbeRevoke`?
4. **Should the test-only switches stay** once the fix lands? They are the
   only reproducer for a fault that has now been seen once.
