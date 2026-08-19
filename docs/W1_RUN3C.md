# W1 Run 3C

## Site `baa28836`

### Observation
- Instruction is `mov %ecx,0x1d00(%ebx)` at function address `0xbaa28836`.
- `ebx` is loaded from stack (`mov 0x30(%esp), %ebx`) and treated as a software state structure in nearby code.
- The same function performs software-structure setup on `0x64(%ebx)` and uses `imul 0x64(%ebx), %ecx` immediately before this store.
- In this function body there is no polling/readback through this base at `0x1e10(%ebx)` or `0x1e14(%ebx)` (the MMIO confirmation pattern used in this batch).

### Inference
- **Base kind:** software/context structure, not MMIO.
- Per instruction: **not MMIO**.

This is a complete answer for this site.

---

## Site `baa41e3a`

### Observation
- This store is inside `0xbaa418f0`.
- At function entry: `mov 0x4(%esp),%eax` then `mov 0x2a6(%eax),%ecx`.
- The same function repeatedly reads `0x1e14(%ecx)` and `0x1e10(%ecx)` and tests bits before issuing engine writes, matching prior MMIO-confirmed behavior.
- The store is reached only from:
  - `mov 0x44(%esp), %eax`
  - `cmp $0xc0000, %eax`
  - `jne 0xbaa41e35`
  - `add $0x84004018, %eax`
  - `mov %eax,0x1d00(%ecx)`

### Inference
- **Base kind:** MMIO.
- **Where the value comes from:** runtime-computed value from `%eax` loaded from `0x44(%esp)` and added to immediate `0x84004018` at `0xbaa41e35`.
  - `0x44(%esp)` is a stack value; in this same function it can be written from a loop counter (`mov %ebx,0x44(%esp)`) and decremented, so without full callsite/path correlation its runtime value at this site is **not determined**.
- **Possible opcode(s):** low 4 bits of this final value are runtime dependent (`low4(0x84004018 + %eax)`), so opcode is **not statically determined** at this site.
- **ARZERO / SGNZERO clear?:** **not determined** from static analysis here because AR bits depend on the runtime `%eax` added at this site. Immediate base `0x84004018` has ARZERO set and SGNZERO clear, but that can change with runtime `%eax`.

What would pin it down:
- prove `%eax` at `0xbaa41e35` is constant, or at least constrain its low 13 bits, from a complete path-sensitive trace of all call sites into `0xbaa418f0`.
