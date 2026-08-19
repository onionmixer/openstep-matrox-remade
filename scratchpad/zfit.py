# Which (Z element size, Z row pitch) reproduces the measured 960 changed dwords?
# Screen 1024x768 32bpp -> rowBytes 4096, stride 1024 dwords.
# The probe drew x=0..63 for y=0..59 and then scanned, for row in 0..59,
# the dwords zb[row*1024 + col] for col in 0..63 -- i.e. a 256-byte window
# at the start of every 4096-byte row.  3840 dwords sampled.
STRIDE_B, W, ROWS, SAMPLE_B = 4096, 64, 60, 256

def changed(zsize, zpitch_px):
    dirty = set()                                   # byte offsets the engine wrote
    for y in range(ROWS):
        base = y * zpitch_px * zsize
        for x in range(W):
            for b in range(zsize):
                dirty.add(base + x * zsize + b)
    n = 0                                           # dwords our scan would see change
    for row in range(ROWS):
        for col in range(W):
            off = row * STRIDE_B + col * 4
            if any(off + b in dirty for b in range(4)):
                n += 1
    return n

print(f"sampled dwords: {ROWS*W}   measured changed: 960")
for zsize in (1, 2, 4):
    for zpitch in (512, 1024, 2048, 4096):
        n = changed(zsize, zpitch)
        print(f"  Z element {zsize} B, pitch {zpitch:5d} px -> {n:5d}"
              f"{'   <== MATCH' if n == 960 else ''}")
