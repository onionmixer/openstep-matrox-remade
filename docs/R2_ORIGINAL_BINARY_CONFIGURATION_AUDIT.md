# R2 — Original Binary Configuration-Override Audit

기준일: 2026-08-18

## 범위와 artifact identity

이 audit은 target에서 read-only로 NFS에 복사한 original
`/usr/Devices/MatroxMGA.config/MatroxMGA_reloc`의 **정적 분석**이다. target card,
driver state, PCI/MMIO, framebuffer, DDC, display에는 접근하지 않았다.

| item | value |
| --- | --- |
| analysis copy | `reference/original-binaries/MatroxMGA_reloc.R0-20260818` |
| target/local `/usr/bin/sum` | `45628 103` / `45628 103` |
| file size | 104,788 bytes |
| Ghidra loader | Mac OS X Mach-O, x86 little-endian 32-bit |
| local analysis MD5 | `49fd552d8562c85b95cbbb5cafd317d0` |
| reproducible scripts | `analysis/ghidra/MatroxMGAStringXrefs.java`, `MatroxMGAFocusDecompile.java` |

The binary copy is analysis-only and excluded from source-control tracking by
`reference/original-binaries/.gitignore`.

## Observed configuration path

Ghidra string xrefs place `MGA Memory Size`, `determineConfiguration:`, and
`MGAProbe` in the original initialization function at `0x000000f0`.
The decompiled/disassembled path shows this order:

1. initialize the `IOFrameBufferDisplay` superclass and call
   `determineConfiguration:` then `selectMode`;
2. call `MGAProbe`;
3. after a successful probe, call the configuration integer accessor for
   `MGA Memory Size`;
4. accept only nonzero unsigned values in `3..63` (the recovered predicate is
   `value != 0 && value - 3U < 0x3d`);
5. convert the accepted MiB count to the internal mapping-length unit, publish
   the memory range list, then call the legacy framebuffer mapping method.

This is evidence about the original driver's **override parser and lifecycle**,
not evidence for the installed card's VRAM total. Its original mapping call is
also exactly why the replacement R4 source stays fail-closed and does not copy
this runtime path before G1--G4/R6 approval.

## `MGACountRam` boundary

The `MGACountRam:` and `MGAReadBios` selector strings are both referenced by
the original function at `0x00003900`, whose disassembly includes PCI/MMIO
helper calls and assigns a detected memory-size field. This confirms that the
original binary contains a hardware-sensitive detection path; it does **not**
make that path safe to execute while the original display owns the active
screen. The existing R2 rule remains: no replacement VRAM auto-count or probe
is introduced before the physical-evidence and recovery gates.

## Porting consequence

`OSMGAParseManualMemoryMB` now mirrors the original parser's narrow numeric
acceptance range (3..63 MiB) while retaining named failures for missing,
malformed, and out-of-range input. It does not adopt original probing or
mapping behavior. R3 separately requires the parsed byte value to exactly
match the complete physical R2 profile, so a syntactically accepted value still
cannot pass G2/G3 by itself.
