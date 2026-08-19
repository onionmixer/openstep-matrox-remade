PITCH, PAGE = 4096, 8192
VIS_END   = 1024 * 768 * 4          # visible framebuffer ends here
WIN_LO, WIN_HI = 4*1024*1024, 7*1024*1024
DSTORG = 5 * 1024 * 1024
ROWS_DRAWN, MARGIN = 80, 64

print("where can a hostile draw actually land?")
print(f"  visible framebuffer  0 .. {VIS_END:#x} ({VIS_END/1024/1024:.2f} MiB)")
print(f"  our window           {WIN_LO:#x} .. {WIN_HI:#x}")
print(f"  DSTORG               {DSTORG:#x}\n")

print("  address = DSTORG + y*pitch + x, so an escape needs a large |x|")
below = DSTORG - VIS_END
print(f"  to reach the visible area from DSTORG it must travel {below} bytes")
print(f"    = {below//4} pixels = {below/PITCH:.0f} rows of pitch")
print(f"  the primitive is {ROWS_DRAWN} rows tall and 64 px wide\n")

lo = DSTORG - MARGIN*PITCH
hi = DSTORG + (ROWS_DRAWN + MARGIN)*PITCH
print("canary coverage")
print(f"  {lo:#x} .. {hi:#x}  ({(hi-lo)/1024/1024:.2f} MiB, "
      f"{(hi-lo)//4} dwords to fill)")
print(f"  inside our window: {lo >= WIN_LO and hi <= WIN_HI}")
print(f"  margin below the draw: {MARGIN} rows; above: {MARGIN} rows")
print(f"  full row width checked: {PITCH//4} columns, not just the drawn 64\n")

print("worst case if the clip does NOT hold")
for name, dx in (("dxL = -2^17", -(1 << 17)), ("dxR = +2^17", 1 << 17)):
    px = dx * ROWS_DRAWN
    print(f"  {name}: x walks {px} px = {abs(px)*4/PITCH:.0f} rows of pitch")
    reach = DSTORG + px*4
    print(f"    unclipped reach {reach:#x}"
          f"  -> {'INSIDE the canary' if lo <= reach <= hi else 'past the canary'}"
          f", visible area {'SAFE' if reach > VIS_END else 'AT RISK'}")
