# R6 — Offscreen-Only 2D Request Admission

기준일: 2026-08-18

## Purpose

`protocol/OpenStepMGAOffscreen2D.{h,c}` constrains the first future 2D clear
and copy requests before an engine backend exists. It composes the existing
geometry validator with the R6 transaction state and refuses every request
until a transaction is already in `LINEAR_ACTIVE`.

## Surface contract

An `OSMGAOffscreenSurface` has only:

- a nonzero opaque surface ID;
- validated 32-bit surface geometry/allocation length;
- a kernel-allocation verification flag; and
- an explicit flag that the allocation is outside scanout.

It has no framebuffer/VRAM address, BAR/MMIO offset, device mapping, command
queue, or register state. The flags are future kernel-owned metadata, not
user-provided assertions.

The current clear/copy validators also require the complete surface record to
be live in an `OpenStepMGAOffscreenAllocator` ledger. A nonzero ID and flags
alone are insufficient: a released ID or a record whose geometry/flags differ
from the issued record is rejected before command geometry is accepted.

## Accepted requests

| request | admission rule |
| --- | --- |
| clear | active transaction, verified offscreen surface, and an in-bounds rectangle |
| copy | all clear requirements for independent verified source/destination surfaces, same rectangle dimensions in bounds on both |

Same-surface copies are explicitly rejected for now; overlap direction is
hardware-sensitive and must not be guessed. Clear/copy validation success is
not a draw submission and does not imply an offscreen allocation exists on the
current target.

## Verification and current boundary

The C89 test uses an allocator-issued synthetic 64×32 surface pair and a
synthetic `LINEAR_ACTIVE` transaction state. It proves active-state,
scanout-separation, live-record, geometry, separate copy, self-copy, stale
release, and mutated-record rejection logic only.

```text
sh tools/check-offscreen-2d-no-hardware.sh
sh test/run-offscreen-2d-host.sh
```

The module is not in the R4 display bundle or P2 MiG ABI. R6/P3 hardware work
still needs G1–G4, source-backed offscreen allocation evidence, and a separate
approved replacement-only run.

`OpenStepMGAOffscreenAllocator` is the separate address-free allocation ledger
for this future path. Its verified-arena, monotonic-capacity, and no-reuse
rules are documented in [R6_OFFSCREEN_ALLOCATOR_POLICY.md](R6_OFFSCREEN_ALLOCATOR_POLICY.md).
It remains a pure-C policy module, not a target allocation backend.

The ordinary-memory reference oracle now has matching bounded rectangle clear
and distinct-surface copy functions. It is used only to form expected pixels
and checksums after a future readback; it is not an allocation or hardware
fallback path.
