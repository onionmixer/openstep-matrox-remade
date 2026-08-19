# W1 Run 3D — Targeted scope: baa42c7c, baa4942b

## Site 1: `0xbaa42c7c`

- **Instruction:** `mov %eax, 0x1d00(%ebp)`
- **MMIO / structure decision (STEP 0):** **MMIO**

Observation:
- Same `%ebp` base is also used for `mov 0x1e10(%ebp)` polling loops and writes to nearby control fields (`0x1e54`, `0x1c60`, `0x1c74`, `0x1c84`).
- The store is a direct register write, not a plain context-copy sequence.

Inference:
- The code writes DWGCTL+EXEC through the MMIO aperture at `%ebp`.

- **Value source for stored DWORD (0x1d00):**
  - If `eax == 0xc0000`: `movl $0x40c6009, 0x1d00(%ebp)` (immediate).
  - Else: `%eax` from `0x3c(%esp)` is used as `add $0x4006019, %eax` then stored (computed at runtime).

- **Observed opcode(s):**
  - From immediate: `0x40c6009` → opcode `9` (ILOA[D] family), not trapezoid opcodes `4`/`6`.
  - From computed path: `not determined` (depends on runtime `0x3c(%esp)` value before add). This would be pinned down by tracing caller-provided `0x3c(%esp)` at all call sites.

- **Sloped condition (opcode 4 or 6 with ARZERO clear and SGNZERO clear):**
  - Immediate path: **not met** (`opcode=9`, ARZERO=1, SGNZERO=1).
  - Computed path: **not determined** from this disassembly slice alone.

## Site 2: `0xbaa4942b`

- **Instruction:** `mov %edx, 0x1d00(%ecx)`
- **MMIO / structure decision (STEP 0):** **MMIO**

Observation:
- Same base `%ecx` is used for `mov 0x1e10(%ecx)` status polling after the DWGCTL+EXEC store.
- Nearby writes target MMIO-like registers `0x1c8c(%ecx)`, `0x1c84(%ecx)`, `0x1c88(%ecx)`, `0x1c04(%ecx)`.

Inference:
- The store is a DWGCTL+EXEC write through MMIO at `%ecx`.

- **Value source for stored DWORD (0x1d00):**
  - `mov 0x2fa(%ebx), %edx`
  - `and 0x20(%esp), %edx`
  - `or $0x800c7804, %edx`
  - `mov %edx, 0x1d00(%ecx)`
  - This is a runtime-computed value (context/argument-derived), not a plain immediate.

- **Observed opcode(s):**
  - Base constant contributes low nibble `4`.
  - Because bits are masked from `0x2fa(%ebx)` by runtime stack mask `0x20(%esp)` before OR, low bits could vary if mask permits; therefore opcode is **not determined** purely statically.

- **Sloped condition (opcode 4 or 6 with ARZERO clear and SGNZERO clear):**
  - `0x800c7804` has ARZERO and SGNZERO set (bits 12 and 13 = 1), and OR with a masked value cannot clear those bits.
  - Therefore, the computed value cannot satisfy the sloped-edge requirement (opcode 4/6 with both bits clear).
