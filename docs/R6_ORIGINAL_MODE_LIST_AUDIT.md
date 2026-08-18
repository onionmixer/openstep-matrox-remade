# R6 — Original G450 16 MiB mode-list audit

기준일: 2026-08-18

## Read-only target result

The installed original bundle contains both `MatroxMGAG400_16MB.modes` and
`MatroxMGAG450_16MB.modes`; each is a text list of accepted `Display Mode`
selection strings. The G450 16 MiB list has exactly one entry for the approved
recovery selection:

```text
Height:1200 Width:1600 Refresh:60Hz ColorSpace:RGB:888/32
```

`test/collect-r6-original-mode-list.csh` checks only the existence and count
of that string. Its target run returned
`OPENSTEP_MGA_R6_ORIGINAL_MODE_LIST_STATUS=pass` on 2026-08-18.

## Consequence

This is direct evidence that the existing driver exposes the selected mode for
the matching G450 16 MiB compatibility profile. It is not a source of CRTC,
VGA, DAC, PLL, framebuffer range, cache, output-routing, or restore register
values. The file must therefore not be treated as a register image or copied
as a replacement-writer implementation.

The first recovery driver remains constrained to this one selection, the
approved DMT geometry plan, and the reviewed primary PLL byte image. The
missing actual writer inputs are documented separately in
`R6_G450_PLL_MODE_SOURCE_AUDIT.md` and `R6_DRIVERKIT_MAPPING_AUDIT.md`.
