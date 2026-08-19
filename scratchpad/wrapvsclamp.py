# D3-4b band A magnified 2x on a 64-texel texture and measured u at
# x = 0,16,32,48,63 as 0 32 63 63 63.  Which model does that pick?
print("D3-4b band A, 2x, 64-texel texture")
print(f"{'x':>4} | {'raw 2x':>7} | {'clamp':>6} | {'wrap':>5} | measured")
meas = {0: 0, 16: 32, 32: 63, 48: 63, 63: 63}
clamp_ok = wrap_ok = True
for x in (0, 16, 32, 48, 63):
    raw = 2 * x
    clamp, wrap = min(raw, 63), raw % 64
    m = meas[x]
    clamp_ok &= (clamp == m)
    wrap_ok  &= (wrap == m)
    print(f"{x:>4} | {raw:>7} | {clamp:>6} | {wrap:>5} | {m}")
print(f"\nclamp model matches all: {clamp_ok}")
print(f"wrap  model matches all: {wrap_ok}")

print("\nD3-4c band G, 8x -- which rows carry the canary evidence?")
TEX_TEXELS = 64 * 64
for n in (0, 7, 8, 19):
    v, u = 8 * n, 8 * 63
    idx = v * 64 + u                      # unclamped linear texel index
    print(f"  band row {n:>2}: unclamped v={v:>3}, max idx={idx:>6} texels"
          f" -> {'past' if idx >= TEX_TEXELS else 'still inside'} the 16 KiB texture")
print("  so rows 8..19 are the ones whose null result is conclusive;")
print("  rows 0..7 could stay inside the texture even unclamped.")
