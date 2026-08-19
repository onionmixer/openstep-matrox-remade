STEP = 1 << (20 - 6)          # one texel for a 64-wide texture
TEXORG, TEXBYTES, CANARY = 6*1024*1024, 64*64*4, 512*1024

print("BAND F -- which origin?")
clip_top, draw_top, draw_len = 20, 25, 10
for r in (0, 5, 9):
    y = draw_top + r
    print(f"  draw row {r} (screen y={y}): primitive -> v={y-draw_top}, "
          f"clip -> v={y-clip_top}")
print("  measured 0 5 9  => primitive origin\n")

print("BAND G -- was the canary actually reachable?")
mag = 8
u_max, v_max = mag*63, mag*19          # 20 rows, 64 px, unclamped
off = (v_max*64 + u_max)*4             # linear-pitch texel address
print(f"  unclamped u_max={u_max}, v_max={v_max}")
print(f"  furthest fetch offset from TEXORG = {off} bytes = {off/1024:.1f} KiB")
print(f"  texture occupies              0 .. {TEXBYTES/1024:.0f} KiB")
print(f"  canary occupies {TEXBYTES/1024:.0f} .. {(TEXBYTES+CANARY)/1024:.0f} KiB")
inside = off < TEXBYTES
print(f"  would an unclamped fetch land in the canary? "
      f"{'NO - test is vacuous' if inside else 'YES - null result is meaningful'}")
first_escape = next(y for y in range(20)
                    if (min(mag*y, 10**9)*64 + 0)*4 >= TEXBYTES)
print(f"  first destination row whose unclamped v escapes the texture: "
      f"y={first_escape}\n")

print("BAND G -- and what clamping predicts")
print(f"  clamped: u=min(8x,63), v=min(8y,63); every value a valid texel")
print(f"  measured 1280 drawn, 0 non-texel values -> matches clamping")
