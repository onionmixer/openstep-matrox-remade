# R6 — Bounded Poll and Stability Policy

기준일: 2026-08-18

## Purpose

`protocol/OpenStepMGABoundedPoll.{h,c}` is the target-independent state
machine that future PLL-lock and 2D FIFO/idle code must use instead of an
unbounded busy loop. It accepts only caller-measured elapsed milliseconds and
a sampled ready/not-ready result. It does not read a register, obtain time,
delay execution, or access an OpenStep device API.

## Contract

The caller supplies a nonzero timeout and a nonzero required count of
consecutive ready samples.

- A not-ready sample resets the stability count.
- A ready sample before the deadline increments it.
- Success occurs only after the required consecutive ready samples.
- A sample at the deadline, or later, is a timeout even if it reports ready.
- READY and TIMEOUT are terminal states; callers cannot turn a timeout into a
  success by presenting later samples.

The state machine deliberately has no retry mechanism. A future hardware
caller must treat timeout as a mode-transition/engine failure, stop issuing
new work, and take its separately reviewed rollback path.

## Intended later use

| future caller | readiness sample | required external input |
| --- | --- | --- |
| G450 PLL transaction | PLL-lock observation | reviewed clock transaction, calibrated monotonic time source |
| Storm 2D initialization | FIFO availability or engine-idle observation | reviewed MMIO range, selected timeout, recovery action |

No specific timeout or sample count is set here. Those values must be measured
and approved for the exact replacement-only target run; X.Org iteration
counts are not an OpenStep timing contract.

## Verification

```text
sh tools/check-bounded-poll-no-hardware.sh
sh test/run-bounded-poll-host.sh
```

The C89 unit test covers stability reset, success, deadline handling, terminal
timeout, and invalid policy rejection. It never accesses the target.
