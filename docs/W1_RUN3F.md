# W1_RUN3F — Site Classification (2B / 2B-1)

## baa156ca

**Observation**
- `0x1d00(%ebx)` is written with `%eax` after coordinate math: `mov 0x64(%ebx),%eax; mov %eax,%edx; imul %ecx,%eax; imul -0x34(%ebp),%edx; mov %edx,0x1cfc(%ebx); mov %eax,0x1d00(%ebx)`.
- The site is in the same style as prior confirmed structure sites (`baa1ab4d`, `baa28836`) where `0x64(%base)` participates in scaled offset/Y-stride style arithmetic.
- In this function body, there is no observed poll/read pattern on `0x1e10(%base)` or `0x1e14(%base)` through `%ebx`, and no evidence in this block of `%ebx` being loaded via a `context+0x2a6` MMIO lookup.

**Inference**
- **Structure**.
- Final answer: classify as structure field store; do not attempt MMIO opcode inference.

## baa184db

**Observation**
- Same core pattern appears at the same relative point in a sibling routine: `mov 0x64(%ebx),%eax; mov %eax,%edx; imul %ecx,%eax; imul -0x40(%ebp),%edx; mov %edx,0x1cfc(%ebx); mov %eax,0x1d00(%ebx)`.
- The multiplication is against `[ebx+0x64]`, which is the same field-like, pitch-derived tell used to reject MMIO classification in prior runs.
- No same-function polling on `0x1e10(%ebx)`/`0x1e14(%ebx)` is present in this function scope; only MMIO-control-like register/state ops (`0x1e04`, etc.) appear.

**Inference**
- **Structure**.
- Final answer: classify as structure field store; do not attempt MMIO opcode inference.
