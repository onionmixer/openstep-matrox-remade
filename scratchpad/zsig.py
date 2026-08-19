# The four fits to 960 -- do they differ in the signature V4 measures?
STRIDE_B, W, ROWS = 4096, 64, 60
def sig(zsize, zpitch_px):
    dirty = set()
    for y in range(ROWS):
        base = y * zpitch_px * zsize
        for x in range(W):
            dirty.update(range(base + x*zsize, base + x*zsize + zsize))
    per_row, rows_hit = [], []
    for row in range(ROWS):
        n = sum(1 for col in range(W)
                if any(row*STRIDE_B + col*4 + b in dirty for b in range(4)))
        if n: rows_hit.append(row); per_row.append(n)
    first = min(o for o in dirty)
    gaps = {b-a for a,b in zip(rows_hit, rows_hit[1:])}
    return first, per_row[0], sorted(gaps), len(rows_hit)
print(f"{'layout':>26} | first B | dwords/row | row gap | rows hit")
for z,p in ((1,4096),(2,1024),(2,4096),(4,4096)):
    f,d,g,r = sig(z,p)
    print(f"{f'Z {z} B/elem, pitch {p} px':>26} | {f:7d} | {d:10d} | {str(g):>7} | {r:8d}")
