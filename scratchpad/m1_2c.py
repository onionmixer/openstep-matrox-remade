BAND, W, SLOPE = 20, 64, 800
def area(left0, dxL, right0, dxR, engaged):
    n = 0
    for y in range(BAND):
        l = left0 + (dxL*y if engaged else 0)
        r = right0 + (dxR*y if engaged else 0)
        l, r = max(l, 0), min(r, W-1)          # the hardware clip
        if r >= l: n += r - l + 1
    return n

print("bands as run -- can 'clip clamped it' be told from 'AR was ignored'?")
for name, l0, dxl, r0, dxr in (
        ("control      ", 0, 0, W-1, 0),
        ("left hostile ", 0, -SLOPE, W-1, 0),
        ("right hostile", 0, 0, W-1, SLOPE),
        ("both hostile ", 0, -SLOPE, W-1, SLOPE)):
    e, i = area(l0, dxl, r0, dxr, True), area(l0, dxl, r0, dxr, False)
    print(f"  {name}: engaged {e:>5}, ignored {i:>5}"
          f"   {'INDISTINGUISHABLE' if e == i else 'distinguishable'}")
print("  measured 1280 1280 1280 1280 -- consistent with either reading\n")

print("a band that starts INSIDE and walks out separates them:")
for name, l0, dxl, r0, dxr in (
        ("left from 32 ", 32, -SLOPE, W-1, 0),
        ("right from 31", 0, 0, 31, SLOPE)):
    e, i = area(l0, dxl, r0, dxr, True), area(l0, dxl, r0, dxr, False)
    print(f"  {name}: engaged {e:>5}, ignored {i:>5}"
          f"   {'INDISTINGUISHABLE' if e == i else 'distinguishable'}")
print("\n  engaged   = row 0 narrow, then clamped to full width")
print("  ignored   = every row narrow")
