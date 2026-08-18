# R2.1a — Manual `MGA Memory Size` Configuration

## Purpose

The replacement display driver adopts an explicit configuration-table value
instead of an automatic VRAM write/read count.  The compatible key is:

```text
"MGA Memory Size" = "16";
```

For port fidelity, the replacement parser accepts exactly the original
driver's nonzero `3` through `63` MiB range. Static analysis of the target
baseline binary's configuration method found that it accepts this unsigned
range before converting MiB to its mapping-length representation. The current
target's original bundle separately has a G400 `8MB or greater` table/mode
catalogue and G400/G450 `16MB or greater` tables with an explicit
`MGA Memory Size=16` key. It has no original 32 MiB table; table inventory does
not define the parser range.

This 3..63 MiB envelope is a fail-closed **input vocabulary**, not a claim that
the current target has any one of those totals. It converts an explicit
supported value to bytes with no fallback. Missing, malformed, and out-of-range
values return zero and a named failure status; they never silently become a
default capacity. The exact static-analysis evidence is recorded in
`R2_ORIGINAL_BINARY_CONFIGURATION_AUDIT.md`.

The current offline planning assumption is 16 MiB.  It is deliberately not
written to the fail-closed replacement `Default.table`; see
`R3_16M_WORKING_ASSUMPTION.md` for the explicit boundary and required evidence.

## Meaning and boundary

This is an operator-declared configuration input, not a hardware discovery
claim.  It does not establish installed VRAM type, physical total, RAMDAC
limit, usable offscreen range, or a right to map the framebuffer.  The operator
must record the physical/documentary evidence that justifies the selected value
in the R2 profile record.

The parser is a C89 pure-C module and has no DriverKit, PCI, BAR, VRAM, MMIO,
DDC, DMA, IRQ, or mode-programming access.  The R4 display skeleton reads it
from `deviceDescription`'s configuration table using the documented
`valueForStringKey:` pattern, but `+probe:` still returns `NO` and init still
frees the object.  Therefore this change neither installs nor loads a driver,
nor accesses the target card.

## Future lifecycle rule

When R6 is separately authorized, an accepted value may become the sole input
to framebuffer mapping-length and mode-footprint validation.  No auto-count,
option-ROM read, or “best effort” fallback may be introduced. Before that
future code is enabled, the selected byte value must exactly equal a complete
R2 physical profile total (board identity, independent cross-check, VRAM
type/total, RAMDAC limit), which the R3 validator now enforces, in addition to
the G1/G3/G4 gates.

## Verification

`test/run-manual-config-host.sh` compiles the parser in strict C89 mode and
covers missing, whitespace, original minimum/maximum (3/63 MiB), common
8/16/32 MiB values, out-of-range capacity, suffix, and overflow cases.
`tools/check-manual-config-no-hardware.sh` rejects hardware
APIs from the parser.  Both are part of `run-no-hardware-host-checks.sh`.

2026-08-18 target evidence: the same parser/test pair compiled as i386 on
OPENSTEP and returned both `OPENSTEP_MGA_MANUAL_CONFIG_TEST=pass` and
`OPENSTEP_MGA_MANUAL_CONFIG_TARGET_TEST=pass`.  The temporary `/tmp` test
binary was removed in the same logged-out telnet session.  The replacement
bundle also clean-built with `OpenStepMGAManualConfig.c` and passed its
read-only `nm -u` import gate; it was not loaded or installed.

After aligning the accepted range with the original binary, the target parser
test again returned
`OPENSTEP_MGA_MANUAL_CONFIG_TEST=pass`; the R4 bundle was then clean-built as
i386 from `/ndrv` and again returned
`OPENSTEP_MGA_REPLACEMENT_R4_IMPORT_STATUS=pass`. Neither run installed,
loaded, or probed the bundle.
