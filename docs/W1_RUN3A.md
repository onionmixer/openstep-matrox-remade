# W1 RUN 3A — DWGCTL+EXEC register sites

Scope: two write sites only

- `0xbaa156ca: mov    %eax,0x1d00(%ebx)`
- `0xbaa184db: mov    %eax,0x1d00(%ebx)`

Both are in 32-bit code paths that set `0x1d04(%ebx)` with `(orig_eax<<16)|%esi`, then set YDST/HEIGHT-like values, set EXEC in `0x1ce4(%ebx)`, and issue a final store to `0x1d00(%ebx)`.

## Site 1: `0xbaa156ca`

### Observation (from disassembly)
`0xbaa156c1`-`0xbaa156ca` sequence:
1. `mov 0x64(%ebx), %eax`
2. `mov %eax, %edx`
3. `imul %ecx, %eax`
4. `imul -0x34(%ebp), %edx`
5. `mov %eax, 0x1d00(%ebx)`

There is no immediate loaded into `%eax` immediately before the store.

### Inference
- (a) Source class: **computation**. Final value is `%eax = [driver_ctx+0x64] * ecx`, and `ecx` is computed earlier in the function from local temporaries (`-0x4c`, `-0x34`, etc.) derived from runtime geometry/clip values.
- (b) Possible opcode(s): **not determinable statically** from static code, because low 4 bits come from the runtime product result.
- (c) ARZERO/SGNZERO clear: **not determined**. No static guarantee can be made for bits 12/13 from this path.

To pin down (b)/(c):
- Need to know the runtime contents of `driver_ctx` at `+0x64` (where the base value comes from) **and** the runtime values of the local multiplier terms (`ecx` and `-0x34(%ebp)` context for this iteration).

## Site 2: `0xbaa184db`

### Observation (from disassembly)
`0xbaa184c0`-`0xbaa184db` sequence:
1. `mov 0x64(%ebx), %eax`
2. `mov %eax, %edx`
3. `imul %ecx, %eax`
4. `imul -0x40(%ebp), %edx`
5. `mov %eax, 0x1d00(%ebx)`

There is no immediate loaded into `%eax` immediately before the store.

### Inference
- (a) Source class: **computation**. Final value is `%eax = [driver_ctx+0x64] * ecx`, and `ecx` is computed earlier (same style path as Site 1) from runtime loop/local state (`-0x58`, `-0x40`, etc.).
- (b) Possible opcode(s): **not determinable statically**.
- (c) ARZERO/SGNZERO clear: **not determined**.

To pin down (b)/(c):
- Need to know the runtime contents of `driver_ctx` at `+0x64` and runtime local values used to compute `ecx` (at least `-0x58(%ebp)` and `-0x40(%ebp)` flow into `ecx`).

## Summary

- Resolved sites: **both sites are resolved to source pattern = runtime-computed DWGCTL+EXEC values** (from `ebx+0x64` multiplied by a runtime value).
- Sloped-edge status: **neither site can be proven to emit ARZERO/SGNZERO clear from static analysis alone** (both: `not determined`).
