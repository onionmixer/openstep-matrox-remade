SOFTRAPEN, DWGENGSTS, ENDPRDMASTS = 1 << 0, 1 << 16, 1 << 17
MASK  = SOFTRAPEN | DWGENGSTS | ENDPRDMASTS
VALUE = SOFTRAPEN | ENDPRDMASTS

for name, st in (("M1-2a (failed)", 0x80820024), ("D1 (passed)", 0x80820025)):
    print(f"{name}: status {st:#010x}")
    print(f"   SOFTRAPEN  {'set' if st & SOFTRAPEN   else 'CLEAR'}"
          f"   <- the trap fired?")
    print(f"   DWGENGSTS  {'BUSY' if st & DWGENGSTS  else 'idle'}"
          f"   <- drawing engine")
    print(f"   ENDPRDMASTS{' set' if st & ENDPRDMASTS else ' clear'}"
          f"  <- DMA reached PRIMEND")
    print(f"   done? {(st & MASK) == VALUE}\n")

print("so the list ran to the end, the engine went idle, and no trap fired")
print("-> PRIMEND stopped the fetch before the SOFTRAP block\n")

BLK = 5   # dwords per block
def layout(tri):
    state, per, restore, softrap, pad = 1, 7, 1, 1, 1
    blocks = state + per*tri + restore + softrap + pad
    tail_blocks = blocks - pad          # PRIMEND points past SOFTRAP, before pad
    return blocks*BLK, tail_blocks*BLK
for n in (1, 2, 250):
    total, tail = layout(n)
    print(f"  {n:>3} triangle(s): total {total:>5} dwords, "
          f"PRIMEND at dword {tail:>5} ({tail*4} bytes)")
print("\nthe old code put PRIMEND one block earlier, at the START of SOFTRAP,")
print("and emitted no padding block for the card's read-ahead")
