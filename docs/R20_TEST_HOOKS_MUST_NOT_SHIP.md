# R20 -- the fault-injection API is in the shipped package

Found by cross-review of R19. Written before any code.

---

## 1. What ships today

`pkg/build-accel-pkg.sh:79` copies `mesa/OpenStepMGAMesaHook.h` into the
package's `Headers/`, and that header declares four fault injectors:

```c
void OSMGAMesaHookInjectRefusal(int on);
void OSMGAMesaHookInjectNamed(unsigned long submits);
void OSMGAMesaHookLegacyGeometry(int on);
void OSMGAMesaHookLegacyTailDrop(int on);
```

Their code is compiled into `libGL_mga.a`, which ships beside it. So an
application built against this package can call all four.

`InjectRefusal` predates this session; the other three are mine.

## 2. What each one would do to somebody who called it

| | effect if called in earnest |
|---|---|
| `InjectRefusal` | corrupts the batch magic; the kernel refuses everything and the library falls back to software. Slow, correct picture |
| `InjectNamed` | corrupts one trapezoid's drawing control; same shape, narrower |
| `LegacyGeometry` | bypasses the boundary preflight and restores the pre-R15 counting. Before R19 this reached a use-after-unmap; **after R19 it does not**, so the worst case is more software fallback |
| `LegacyTailDrop` | **throws away pending triangles when a revoke arrives.** The picture silently loses geometry |

Only the last has an outcome nobody could call "slower but right", which is
why cross-review named it the first to retire.

## 3. The tension this has to resolve

The regression tests link **the same `libGL_mga.a` that ships**
(`tools/build-matrox-tests.sh` passes `$GL/libGL_mga.a`). So "compile the
injectors out" and "the tests can still run" pull against each other, and a
rule that depends on remembering which flavour was built last will be got
wrong eventually -- by me, first.

## 4. Proposal

**R1 -- retire `LegacyTailDrop`.** Its question is settled: the fault address
identified the write, and the A/B it existed for has been run. It is the one
injector whose misuse loses a picture, and nothing needs it any more.

**R2 -- gate the rest behind `OSMGA_MESA_TESTHOOKS`**, in both the header and
the implementation, so a library built without the flag neither defines nor
declares them.

**R3 -- the Mesa build script takes `-test`**, which adds the define. Default
is off, so the ordinary build is the shippable one.

**R4 -- the packaging script REFUSES a library that contains them.** This is
the part that matters: a convention about which build to run last is not a
control, and

```sh
nm libGL_mga.a | grep OSMGAMesaHookInject && fail
```

is. It cannot be forgotten, and it fails at the moment the mistake would
otherwise be shipped.

**R5 -- the test build fails loudly** if the library lacks the hooks, rather
than silently building a harness whose injections do nothing -- which is
exactly the failure this session already produced twice, where a test passed
because it never ran.

## 5. What this does not change

The driver. None of this is kernel-side; the injectors only make a client
send batches the validator already knows how to refuse.

## 6. Open questions for cross-review

1. **Is `-test` on the Mesa build the right shape**, or should the test
   harness build its own copy of the three objects and link those, leaving
   the shipped library untouched by any flag?
2. **Should `InjectRefusal` be retired too?** It predates this session and
   something may depend on it -- the teapot's `inject` mode does, and that
   mode is the identity gate between the hardware and software pictures.
3. **Is `nm` available and reliable** on the packaging host for R4, or does
   the check need another shape?
4. **Does the `Headers/` payload need the hook header at all**, or would the
   package be complete with only the buffer and HW3D headers?

---

## 7. What was done, and where the plan was wrong

Written after the work, against the machine.

### The four became one, plus one that stays

| | outcome |
|---|---|
| `LegacyTailDrop` | **retired.** Its A/B had been run and its question was settled |
| `LegacyGeometry` | **retired.** Cross-review's words: it "is not harmless; it is a diagnostic reintroduction of known-bad policy". The plan had proposed keeping it, on the grounds that after R19 its worst case was only more software fallback. That is true and it is not a reason to ship a switch back to a policy we removed on purpose |
| `InjectNamed` | **test-only**, behind `OSMGA_MESA_TESTHOOKS` |
| `InjectRefusal` | **stays in the release**, and this is a correction to the plan -- see below |

Retiring `LegacyGeometry` had a prerequisite the plan did not see: it was the
**only** thing exercising the flush's prefix write, because `InjectNamed`
always spoiled trapezoid zero, which makes the narrowed prefix zero, which
skips that write entirely -- the very statement `gdb` had caught faulting.
So the trap index became a parameter first, a `prefixwrite` scenario was
added, and only then was the switch removed.  Retiring it before that would
have taken the coverage with it and nothing would have said so.

### `InjectRefusal` stays, and the plan's question 2 has an answer

Open question 2 asked whether it should be retired too.  It should not:

- `examples/README_teapot.md` documents `inject` as argument 5 of the shipped
  demo, in **five** places, including a worked example and the counter the
  reader is told to check afterwards.
- It has been in the library since commit `69f6874`, not since this session.
- Its worst case is one process losing acceleration on purpose.  It cannot
  corrupt the display, hang the machine or damage anything: the refused work
  comes back through Mesa's own rasteriser and the picture stays exact.

That last point is in fact why it earns its place.  Refusing *every* batch and
getting a byte-identical file back out is the clearest evidence this project
has that the fallback is exact, and that evidence is worth more in a user's
hands than the hygiene of an empty symbol table.

So the gate is narrower than the plan proposed: one injector, not three.

### Open question 4 was answered by refutation, not by agreement

Cross-review said the package needed neither the hook header nor the HW3D
header.  Trying to refute it succeeded on the first look:

`examples/build-teapot.csh` compiles `openstep-mga-mesa-teapot.c` with
`-I$prefix/Headers`, and that source includes `OpenStepMGAMesaHook.h` and
`OpenStepMGAMesaBuffer.h` in its accelerated form; `OpenStepMGAMesaHook.h`
includes `OpenStepMGAHW3D.h` for `OSMGAHW3DTri`.  Dropping any of the three
breaks the package's own shipped example.  **All three stay.**

The same look caught a regression the gating had just introduced: the demo
calls `OSMGAMesaHookInjectRefusal`, so the first, wider gate would have made
the shipped example fail to compile against the shipped headers.  Narrowing
the gate to `InjectNamed` removed the problem rather than papering over it.

### Two libraries, and the check that reads the archive

`tools/build-matrox-mesa.csh -test` writes `build/mesa-test`; without it the
script writes `build/mesa`.  Separate directories, because one path written
twice makes the shippable artefact depend on which build ran last.

The build asserts in **both** directions -- release must not define
`OSMGAMesaHookInjectNamed`, test must -- because absence alone is a weak
test: a build that failed early and left a stale object also shows nothing.
Measured on the machine after building each flavour:

```
build/mesa      : T _OSMGAMesaHookInjectRefusal
build/mesa-test : T _OSMGAMesaHookInjectNamed
                  T _OSMGAMesaHookInjectRefusal
                  T _OSMGAMesaHookInjectedNamed
```

`pkg/build-accel-pkg.sh` refuses an archive that **defines** the named
injector -- `nm | grep "T _sym$"`, not the name appearing anywhere, so an
undefined reference from another member cannot fail a clean archive.
`pkg/verify-accel-pkg.sh` carries the same check, duplicated on purpose: its
job is to judge a `.pkg` that already exists, possibly built elsewhere or by
an older builder, and a check that lived only in the builder would pass every
package it had never seen.

Only `tnrv` links the test library.  The refusal probe injects nothing, so it
links the shippable one, along with the teapot and everything else -- which is
the point: the suite exercises the artefact that actually goes out.

### One csh trap, caught before it fired

The argument check was first written `if ("$1" == "-test")`.  With no
arguments at all csh does not give an empty string for `$1`; it stops with
*Subscript out of range* -- and **every existing caller passes nothing**, so
that form would have broken the ordinary build for every caller while working
perfectly for the new one.  `$#argv` first, then `$argv[1]`.
