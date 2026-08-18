# Mesa 3.4.2 fixed-target backend and fallback contract

## Existing Mesa port boundary

The separately packaged `opennstep-mesa342` port builds Mesa 3.4.2's OPENSTEP
target and includes OSMesa in `libGL.a`.  OSMesa renders to caller-owned main
memory, so it is the required software fallback for this project; no GLX,
X11, or current-card access is involved.  The historical OSMesa header's
default width limit is 1280, so the fixed 1024x768 target is within that
declared bound.

## Selector contract

`mesa/OpenStepMGAMesaBackend.{h,c}` fixes the first target to:

| field | value |
| --- | --- |
| render size | 1024 x 768 |
| color | 32-bit |
| depth | 16-bit |
| current dispatch | software fallback |

The selector refuses any other first-target shape.  The ordinary caller-facing
structure remains useful for a later integration boundary, but
`mesa/OpenStepMGAMesaAdmission.{h,c}` now builds it from an actual accepted
R6 mapping record plus an R3-derived render-budget request.  Admission
requires all of the following to agree:

1. the R6 review and its embedded R3 timing/profile record pass;
2. `available_bytes` equals the R3 physical-profile total;
3. `scanout_bytes` equals the R3 linear-footprint result;
4. pitch alignment equals the R3 record; and
5. the request is exactly 1024x768, 32-bit color, 16-bit depth, two color
   surfaces, and passes the render budget.

Only then can the selector return `HARDWARE_CANDIDATE` when the explicit
presentation and fence attestations are also true.  The admission test uses a
complete **synthetic** 16 MiB record: it yields 15,544,320 bytes total and
1,232,896 bytes remaining before reservations.  This proves policy linkage,
not target physical-profile evidence or hardware permission.
This is a bookkeeping value only: it neither creates a Mesa context nor
authorizes mapping, rendering, or hardware access.  Until those records exist,
software fallback is mandatory; disabling it rejects the request.

## Presentation harness

`test/openstep-mga-mesa-presentation-harness-test.c` combines the current
software decision with `OSMGAReferenceScaleNearest32()` on small ordinary-memory
buffers.  It proves the interface composition without linking Mesa or a driver:

```text
software render buffer -> deterministic CPU scale -> caller-owned desktop buffer
```

The host strict-C89 harness passed.  A target-native `cc` run is likewise a
plain `/tmp` executable and is not a graphics or driver test.

On 2026-08-18 both the selector and the composed fallback-presentation harness
also passed target OPENSTEP `cc`; their temporary executables were removed in
the same session.

## Installed OSMesa fallback smoke

`test/openstep-mga-osmesa-fallback.c` is a package-consumer test for the
already installed Mesa 3.4.2 headers and static libraries.  It allocates a
caller-owned `1024x768x4` buffer, creates and binds an `OSMesa` RGBA context,
uses top-left Y orientation, clears it to opaque red, and verifies the first
RGBA pixel.  It opens no window and contains no replacement-driver interface.

On 2026-08-18 the target runner
`test/run-osmesa-fallback-target.csh /ndrv/openstep-matrox-remade
/LocalDeveloper` passed both its i486 build and render check.  The binary was
created only under `/tmp/OSMGAMOSMesaFallback` and removed by the runner.
The historical target `csh` can fail to dispatch a pathname it created in the
same script, so the runner deliberately invokes the completed test through a
fresh `/bin/sh`; that change affects only process launching, not Mesa state.

## Deferred implementation

The eventual Mesa hardware driver must be introduced as a separately licensed
Mesa 3.4.2 integration after the replacement-display run gates.  It must retain
the OSMesa fallback for unsupported state, failed preflight, timeout, or any
recovery condition.  This repository intentionally contains no Mesa core patch,
register stream, or target context creation before those gates.
