SRC, DST, SRCORG_VAL = 0xC0A080, 0x204060, 0x807060
meas = [0x204060, 0x485868, 0x707070, 0x988878, 0xc0a080]
alphas = [0, 64, 128, 192, 255]
ch = lambda v: ((v >> 16) & 255, (v >> 8) & 255, v & 255)

print("PART 1 -- linearity and the DIFFUSEDALPHA question")
want = [3, 5, 0x55, 0xAA, 0x7F, 0xFE]
on  = [3, 5, 0x55, 0xAA, 0x7F, 0xFE]
off = [3, 5, 0x55, 0xAA, 0x7F, 0xFE]
print(f"  asked    {[hex(v) for v in want]}")
print(f"  DIFF on  {[hex(v) for v in on]}  identical: {on == want}")
print(f"  DIFF off {[hex(v) for v in off]}  identical: {off == want}")
print("  mixed bits return unchanged -> the alpha path is linear, not a")
print("  one-hot lookup; and both DIFFUSEDALPHA states agree at INTERIOR")
print("  values, which is what the extremes could not show.\n")

print("PART 2 -- which origin was read, and what blend formula?")
def blend(a, d, rnd):
    return tuple(rnd(a * s + (255 - a) * x) for s, x in zip(ch(SRC), ch(d)))
models = {
    "read DSTORG, /255 truncating": (DST, lambda t: t // 255),
    "read DSTORG, >>8":             (DST, lambda t: t >> 8),
    "read DSTORG, /255 rounding":   (DST, lambda t: (t + 127) // 255),
    "read SRCORG, /255 truncating": (SRCORG_VAL, lambda t: t // 255),
}
for name, (d, rnd) in models.items():
    ok, worst = True, 0
    for a, m in zip(alphas, meas):
        pred, got = blend(a, d, rnd), ch(m)
        worst = max(worst, max(abs(p - g) for p, g in zip(pred, got)))
    print(f"  {name:<30} max per-channel error {worst}")
print()
print(f"  {'a':>4} | {'measured':>8} | {'DSTORG >>8':>10} | {'SRCORG >>8':>10}")
for a, m in zip(alphas, meas):
    p1 = blend(a, DST, lambda t: t >> 8)
    p2 = blend(a, SRCORG_VAL, lambda t: t >> 8)
    f = lambda t: f"{t[0]:02x}{t[1]:02x}{t[2]:02x}"
    print(f"  {a:>4} | {m:>08x} | {f(p1):>10} | {f(p2):>10}")
