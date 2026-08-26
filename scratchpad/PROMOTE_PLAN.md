# Plan: make the machine boot at the speed it has been measured to run

Driver: openstep-matrox-remade/OpenStepMGAReplacementDisplay/OpenStepMGAReplacementDisplay_reloc.tproj/OpenStepMGAReplacementDisplay.m

## The change

Two initialisers.

    osmgaTrackState   0 -> 1     (colour/alpha blocks written only on change)
    osmgaPollDelayUs  0 -> 4     (microseconds between completion-poll reads)

`osmgaPackExec` is already 1. Nothing else moves: no logic, no ABI, no
parameter. All three stay live-settable, so any of them can be turned off on
a running machine without a reboot.

## Why each is ready

**track.** Its evidence is byte identity, not a difference count: each of
twenty scenes drawn twice on the engine, tracker off and on, dumps compared
byte for byte -- including modg, which gives every corner its own colour, and
rgbalin, and the textured, blended, perspective, tiled and seam scenes.
Nothing moved. The fourteen scene baselines and the regression suite also
pass with it on. Measured: 19.97 -> 17.92 ms a frame, the list 1927 -> 1449
dwords a submission, which is the 74.9% the userland count predicted before
the encoder existed.

**delay.** Measured 17.92 -> 16.86 ms with track on, and 19.27 -> 17.80
without, reproducibly, across two reboots. The objection that blocked it --
that the recovery path had never executed, so the enlarged blind window had
no proven net -- is now answered: seven injected timeouts, seven recoveries,
zero latches, and a fresh process fully accelerated afterwards.

The timeout budget does not shrink, because the limit is already divided by
the delay and a slower poll needs proportionally fewer reads:

    delay   limit    largest ever seen   margin used   worst case
      0    100000          2065              2.1%        108.0 ms
      1     50000           823              1.6%        104.0 ms
      2     33333           511              1.5%        102.7 ms
      4     20000           293              1.5%        101.6 ms

## What it buys

    at boot today          19.97 ms   50.1 fps
    at boot after this     16.86 ms   59.3 fps      18.4% faster

Today those milliseconds require someone to set three words by hand after
every restart.

## What I am NOT doing

- `rc3 = 1` on recovery success. It needs the settle read to move with it,
  and it fixes a situation that has never occurred (real completion-poll
  timeouts: zero).
- Widening the delay whitelist past 4. The knee is already flat between 2 and
  4 (17.07 vs 16.86) and every value past it is unmeasured.

## Gates after the reboot

Baselines, the byte-identity check, the regression suite, and the frame --
all with nothing set by hand, because the point of the change is that nothing
is set by hand. Then turn each setting OFF live and confirm the old numbers
come back, which proves the defaults are what changed and not something else.

## Questions

1. Is there a failure mode that only appears when these are on from the FIRST
   submission after boot, rather than turned on later in a running system?
2. The driver's own boot self-tests submit batches before any client does.
   Do any of them assume the untracked or undelayed path?
3. Anything else that should ride along, given a reboot is being spent?
