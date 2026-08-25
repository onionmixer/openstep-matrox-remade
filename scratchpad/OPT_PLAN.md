# Plan: cut the userland half of the frame, without changing a single pixel

Repo: /mnt/USERS/onion/DATA_ORIGN/Workspace/NeXT_DRIVER
Files: openstep-matrox-remade/mesa/OpenStepMGAMesaTriangle.c (all edits below)
       openstep-matrox-remade/mesa/OpenStepMGAMesaHook.c (A1 counter only)
Target machine: OPENSTEP 4.2, NeXT Mach 4.2, i386, cc 2.7.2.1, C89 only.

## The measurement this comes from (already done, on the machine)

Frame 21.8 ms at 640x480, teapot grid 4, 987 source triangles, 1843 trapezoids:
  user  9.2 ms   our trapezoid encoder 7.6 | Mesa geometry 1.4 | libsys 0.2
  sys  13.0 ms   fixed 2.8 | per-trapezoid 10.2, of which under 1 ms is the
                 engine actually writing pixels (proved by scissoring to 8x8
                 with the trapezoid count unchanged at 109681)
Both halves are linear in trapezoid count (least squares over grid 2/3/4/6/8):
  user ms = 0.12 + 4.95 us x traps      sys ms = 2.82 + 5.52 us x traps

gprof flat profile of user time (1000 frames; repeated at 600 frames, same
order within 1-2 points):
  23.1% OSMGAMesaBuildTriangleTex   5.7% osmgaClampSlope   5.0% osmgaMesaTriangle
  13.2% osmgaFixed                  5.0% floor (libm)      4.8% osmgaTrapezoid
   9.6% de_casteljau_surf (Mesa)    9.0% osmgaFloorDiv     7.0% osmgaCeilDiv
   6.0% osmgaFix                    2.1% osmgaEdgeRegs
Ours is 83% of user time; Mesa is 16%.

## Codegen facts I verified on the machine (cc -O -S), not assumed

- `x / 256.0` compiles to a real `fdivrl`. gcc 2.7.2.1 does NOT strength-reduce
  division by a power-of-two double constant.
- `osmgaFloorDiv(a, b)` with a runtime `b` compiles to a real `idivl` plus the
  correction branch (one idivl, quotient and remainder both used).
- `(long)double` compiles to the classic `fnstcw / fldcw / fistpl / fldcw`
  pair-of-control-word-loads sequence.
- Constants are reached through a per-function PIC base (one `call` per
  function), so a literal double is a base+offset load, not a call each time.

## Edits proposed, all intended to be VALUE-PRESERVING

E1. In `osmgaEdgeRegs` (Triangle.c:241-262) the divisor is `step = 1L << shift`
    with `shift = OSMGA_MESA_SUBBITS - s`. `s` comes only from `sub`, which is
    set to `OSMGA_MESA_SUBBITS` (8) at Triangle.c:1156 and decremented to 0 at
    :1174 -- so shift is always 0..8 and step is always a positive 2^k.
    Replace the four `osmgaFloorDiv(x, step)` with `x >> shift`.
    Verified in python: for k=0..8 over a in [-5000,5000] plus the extremes,
    the existing implementation equals math.floor(a/2^k) equals `a >> k`, 0
    mismatches in 90054 pairs.

E2. Same function: `osmgaCeilDiv(e2, M)` with `M = 1L << s` becomes
    `-((-e2) >> s)`. Same python check, 0 mismatches. The other CeilDiv call,
    `osmgaCeilDiv(P0, Q2)` with `Q2 = 2*M*HH`, is NOT a power of two and stays
    a division. `osmgaCeilDiv` and `osmgaFloorDiv` stay in the file for their
    remaining callers.

E3. `OSMGAMesaBuildTriangleTex` (Triangle.c:775-) computes the same four
    differences `x1,y1,x2,y2 = (b-a)/256, (c-a)/256` FIVE separate times (5
    occurrences of each declaration). Hoist to one block at the top and use it
    everywhere.

E4. Replace `/ (double)OSMGA_MESA_SUBONE` (23 occurrences in that function
    alone) with a multiply by the exact reciprocal `(1.0 / 256.0)`. 256 is a
    power of two, so both forms are exact in binary floating point for every
    value that cannot underflow, and the operands here are differences of
    coordinates bounded by OSMGA_MESA_RULE_COORD_MAX * 256.

NOT doing, deliberately:
 - replacing `/ den` (10 occurrences) with a reciprocal multiply: not
   value-preserving.
 - touching the float-to-int conversion (`osmgaFix`, `osmgaFixed`, the `floor`
   call -- together 24% of user time): every faster form I know changes tie
   behaviour, and I will not trade a pixel for a millisecond.

## A1, first and separately: find out what the kernel's 5.52 us/trapezoid is

The submit ioctl already returns `dwords` and `spins` per submission
(hw3d/OpenStepMGAHW3D.h:890; the driver fills osmgaHW3DLast[3] from its
DMA-completion spin loop). Add a test-only accumulator in the Mesa hook that
sums submissions, dwords and spins, exposed like the existing test counters,
and read it from the profiling probe. If spins per submission is large the
kernel time is the busy-wait on the engine; if it is near zero the time is
CPU-side list building and validation. This needs no driver change and no
reboot, and it decides whether the kernel work is worth planning at all.

## Gates before any of this is believed

 - the existing scene baselines byte-identical (14 scenes)
 - the regression suite at 0 problems
 - hook counters identical: drawn, batches, traps, software, declined,
   unsupported
 - only then re-measure the frame and re-take the gprof profile

## Questions

1. E4: is `x * (1.0/256.0)` exact-equal to `x / 256.0` for every value that can
   reach it here, including signed zero, and does anything change if gcc keeps
   the intermediate in an 80-bit x87 register rather than storing a double?
2. E3: on x87 with gcc 2.7.2.1, can hoisting five identical expressions into
   one variable change the VALUE of later arithmetic through excess precision
   (register-kept 80-bit versus spilled 64-bit)? If it can, how would you
   detect it other than by the byte-identical gate?
3. E1/E2: is there any path in this file where the divisor is not the 2^k I
   claim, or where `s` could leave 0..8?
4. What else in that profile is value-preserving and worth doing, that I have
   not listed?
5. Is A1 a sound way to attribute the kernel time, or does `spins` measure
   something narrower than I think?
