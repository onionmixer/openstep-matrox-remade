# W1 Run 3E

## Site 1: `0xbaa49739`

### Observation
- Displacement is `0x1d00(%ecx)`.
- `ecx` is loaded from context field `0x2a6` here: `mov 0x2a6(%edx),%ecx`.
- Same base uses status polling: `mov 0x1e10(%ecx),%edx` and `and $0x1f,%edx` loop.
- Stored value is `or $0x800c7804,%edx` after `mov 0x2fa(%eax),%edx; and %edi,%edx`.
- The mask operand `%edi` is runtime from stack (`mov 0x24(%esp),%edi`), and its effective bits are not bounded from this site.

### Inference
- **Base classification: MMIO aperture** (confirmed by the `0x2a6` load pattern plus `0x1e10` polling on the same base).
- **Value source: computed**: `(context[0x2fa] & runtime_mask) | 0x800c7804`.
- **Opcode family possible:** base opcode starts from constant `0x4` and can be augmented by low-bit runtime bits that survive the `AND` mask. So in principle `0x4` with additional low bits is possible; exact set is `not determined` without `0x24(%esp)` and `context[0x2a?]` mask provenance.
- **Sloped-edge criterion**: can be ruled out because `0x800c7804` sets both ARZERO (bit 12) and SGNZERO (bit 13), and those bits cannot be cleared by the preceding `AND` before the final `OR`.

## Site 2: `0xbaa542c4`

### Observation
- Displacement is `0x1d00(%eax)`.
- `eax` is loaded from context field `0x2a6` here: `mov 0x2a6(%esi),%eax`.
- Same base uses status polling: `mov 0x1e10(%eax),%ebx; and $0x1f,%ebx` loop.
- Stored value is `or $0x800c7804,%ecx` after `mov 0x2fa(%esi),%ecx; and %edx,%ecx`.
- The `and` mask is `%edx`, which can be either `0` or `context[0x2fa]` depending on branch state near `0xbaa5425c` (not fixed at this point).

### Inference
- **Base classification: MMIO aperture** (confirmed by `0x2a6` load + `0x1e10` polling through same base).
- **Value source: computed**: `(0 or context[0x2fa]) & runtime_mask | 0x800c7804`.
- **Opcode family possible:** low bits derive from constant `0x4` plus any cleared/kept runtime bits from the mask. Exact family is `not determined` without the runtime mask value.
- **Sloped-edge criterion**: can be ruled out because constant `0x800c7804` forces ARZERO and SGNZERO bits set.

## Site 3: `0xbaa559cb`

### Observation
- Displacement is `0x1d00(%eax)`.
- `eax` is loaded from context field `0x2a6`: `mov 0x2a6(%edx),%eax`.
- Same base uses status polling: `mov 0x1e14(%eax),%ecx` and `mov 0x1e10(%eax),%ecx` loops before the draw write.
- Stored value is `or $0x800c7804,%ecx` after `mov 0x2fa(%edx),%edi; and %edi,%ecx`.
- The mask/value source is runtime/structure field, not a static immediate.

### Inference
- **Base classification: MMIO aperture** (confirmed by `0x2a6` load + polling `0x1e14`/`0x1e10` on same base).
- **Value source: computed**: `(context[0x2fa] & context[0x2fa]) | 0x800c7804`, effectively `context[0x2fa] | 0x800c7804`.
- **Opcode family possible:** starts from `0x4` (TRAP) from constant, plus optional low-bit additions permitted by runtime mask; exact set is `not determined` without additional bound on `context[0x2fa]`.
- **Sloped-edge criterion**: can be ruled out because both ARZERO and SGNZERO are set by `0x800c7804` and cannot be cleared by the prior AND.
