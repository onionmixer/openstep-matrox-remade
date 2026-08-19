RING = 64 * 1024
# per-triangle fields (dwords): y,h | ar0,ar1,ar2,ar4,ar5,ar6,sgn | fxbndry
#                               | dr[12] | z0,zdx,zdy | a0,adx,ady
TRI_DW  = 2 + 7 + 1 + 12 + 3 + 3
HDR_DW  = 3                      # magic, version, triCount
ST_DW   = 3 + 5 + 9 + 4          # dstorg,zorg,texorg | dwgctl,alphactrl,texctl,texctl2,texfilter | tmr[9] | clip[4]
print(f"triangle struct {TRI_DW} dwords = {TRI_DW*4} bytes")
print(f"header+state    {HDR_DW+ST_DW} dwords = {(HDR_DW+ST_DW)*4} bytes\n")

# encoded DMA: 5 dwords carry 4 register writes
def blocks(nregs): return (nregs + 3) // 4
TRI_REGS = 7 + 1 + 12 + 3 + 3 + 1 + 1     # edges, fxbndry, dr, z, alpha, dwgctl, ydstlen+exec
ST_REGS  = 3 + 5 + 9 + 6                   # origins, modes, tmr, the init-state writes
print(f"registers per triangle {TRI_REGS} -> {blocks(TRI_REGS)} blocks "
      f"= {blocks(TRI_REGS)*5*4} bytes")
print(f"registers of state     {ST_REGS} -> {blocks(ST_REGS)} blocks "
      f"= {blocks(ST_REGS)*5*4} bytes\n")

for batch_kib in (16, 20, 24, 28, 32):
    batch = batch_kib * 1024
    ring = RING - batch
    cap_in  = (batch - (HDR_DW + ST_DW) * 4) // (TRI_DW * 4)
    cap_out = (ring - blocks(ST_REGS) * 20 - 20) // (blocks(TRI_REGS) * 20)
    cap = min(cap_in, cap_out)
    print(f"  batch {batch_kib:>2} KiB / ring {ring//1024:>2} KiB -> "
          f"input holds {cap_in:>3}, encoded holds {cap_out:>3}, usable {cap:>3}"
          f"{'   <== balanced' if abs(cap_in - cap_out) < 25 else ''}")
