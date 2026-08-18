# R6 — G450 legacy range-publication plan

기준일: 2026-08-18

## Static-analysis evidence

The local analysis-only original `MatroxMGA_reloc` was reprocessed by Ghidra on
2026-08-18. It confirms, at a behavior level, that after successful original
configuration/probe/mode selection the old driver publishes a three-entry
legacy range list: framebuffer, VGA aperture, and VGA BIOS window, then uses
the legacy framebuffer mapping API.

The active `MatroxMGAG450_16MB.table` itself leaves `Memory Maps` and
`FB Address` empty. Therefore that table cannot supply a framebuffer physical
base, range index, or mapping length. No decompiled implementation, object
code, physical BAR address, or original mapping sequence is copied into this
project.

## New independently written plan

`profile/OpenStepMGAG450RangePlan.{h,c}` represents the necessary recovery
configuration as data only:

| index | logical role | length |
| ---: | --- | ---: |
| 0 | verified framebuffer base supplied by a future recovery snapshot | 16 MiB |
| 1 | legacy VGA aperture | 128 KiB |
| 2 | legacy VGA BIOS window | 64 KiB |

It requires a passing R6 mapping review, exactly three ranges with framebuffer
index zero, the approved exact 16 MiB mapping length, and a nonzero page-aligned
caller-supplied framebuffer base. The code deliberately contains no actual
target BAR value; regression uses a synthetic aligned base.

The plan cannot call `setMemoryRangeList:`, map memory, or access VGA/BIOS
memory. A future recovery-only writer may consume it only after G1 snapshot
evidence identifies the DriverKit device-description contract and an explicit
activation run is approved.

## Verification

```text
sh tools/check-g450-range-plan-no-hardware.sh
sh test/run-g450-range-plan-host.sh
# OPENSTEP: csh -f test/run-mode-transaction-target.csh /ndrv/openstep-matrox-remade
```

Host strict-C89/static checks and target integrated C89 transaction both passed
on 2026-08-18. No device memory was published or mapped.
