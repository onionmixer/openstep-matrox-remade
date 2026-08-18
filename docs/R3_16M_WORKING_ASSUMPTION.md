# R3 16 MiB working-assumption review

## Status

The operator selected `16 MiB` as the current G450 deployment capacity.  It
replaces the previous 8 MiB planning assumption for new calculations and is a
hard upper bound for this release even if a particular G450 board could carry
more memory.  Physical part-number/max-capacity research remains useful, but
is no longer a deployment gate.

The original driver currently selects a `MatroxMGAG400_16MB` compatibility
profile and records `1600x1200@60, RGB:888/32`; those software configuration
facts are useful consistency inputs but are not physical memory proof.

## Current geometry lower bound

| item | value |
| --- | ---: |
| geometry | 1600 x 1200 x 32-bit |
| visible lower bound / tight pitch | 7,680,000 bytes |
| working capacity | 16,777,216 bytes (16 MiB) |
| remaining before cursor/hidden allocation | 9,097,216 bytes |

The existing R3 fixture uses the same 1600x1200x32 geometry, 6400-byte pitch,
16 MiB mapping, and a synthetic 162 MHz clock.  It exercises policy arithmetic
only; exact blanking/head/pitch/mapping evidence remains separate.  The DMT
comparison record is in `R3_DMT_TIMING_CANDIDATE_AUDIT.md`.

## Accepted deployment record

`reports/R3_G450_16M_DEPLOYMENT_MODE.md` now fixes the first R3 record to the
current 1600x1200@60 geometry, a 162 MHz standard DMT shape, 6,400-byte pitch,
8-byte alignment, 16 MiB mapping ceiling and a deliberately conservative
300 MHz clock ceiling.  Its host and target C89 record test passes.

`MGA Memory Size=16` remains intentionally absent from the fail-closed
replacement `Default.table`; no configuration or display action is implied.

The prior 8 MiB calculation remains preserved as a historical conservative
comparison in `R3_8M_WORKING_ASSUMPTION.md`; it is no longer the active
planning capacity.
