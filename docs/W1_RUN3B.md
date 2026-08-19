# W1_RUN3B — Run 3B (narrow opcode source check)

Scope: only
- `baa1ab4d: mov %ecx,0x1d00(%ebx)`
- `baa25f1d: mov %eax,0x1d00(%esi)`

## 1) `baa1ab4d`

Observation
- `0x1d00` is written from `%ecx` after
  - `%ecx` is derived from runtime vertex/range values (`0x24(%esp)` and scaled/clamped values),
  - `%ecx` is then multiplied by `%eax`, where `%eax := [ebx+0x64]`.
- This site and nearby code access many small fields from the same base (`0x64`, `0xd0`, `0xdc`, `0x1cfc`, `0x1d04`, `0x1ce4`, etc.) and no loop/polling on `0x1e10(%ebx)` or `0x1e14(%ebx)` is present around this site.
- The stored value at runtime is arithmetic output, not a fixed/ORed command constant.

Inference
- **Base is not MMIO** for this site.
- Since the base is not MMIO, the value can be from a software structure/shadow context; this site does not correspond to a direct `DWGCTL+EXEC` register emit.
- If this was MMIO, confirming evidence required would be:
  - direct base provenance from a known MMIO pointer path, and
  - nearby polling/traffic on `0x1e10(%ebx)` or `0x1e14(%ebx)` with the same `%ebx`.
- **ARZERO/SGNZERO clearability: not applicable (not MMIO).**

## 2) `baa25f1d`

Observation
- `0x1d00` is written from `%eax` after
  - `%ecx` is a runtime range value (from prior clamp math),
  - `%edi := [esi+0x64]`, then `%eax := %ecx * %edi`.
- Nearby writes in the same block are to `0x1d04(%esi)`/`0x1cfc(%esi)`/`0x1ce4(%esi)` and there is no 0x1e10/0x1e14 poll loop tied to `%esi` at this site.
- Value is not fixed at compile time (runtime arithmetic only).

Inference
- **Base is not MMIO** for this site.
- If the base were MMIO, evidence would be needed from the same two checks as above (known MMIO-provenance plus status-path traffic).
- **ARZERO/SGNZERO clearability: not applicable (not MMIO).**

## Final per-site answer

- `baa1ab4d`: base is **not** MMIO; no `ARZERO/SGNZERO` question.
- `baa25f1d`: base is **not** MMIO; no `ARZERO/SGNZERO` question.
