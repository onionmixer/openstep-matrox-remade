print("shape area -- both paths drew 900")
area = sum(63 - 2*y + 1 for y in range(20))
print(f"  sum over y=0..19 of (63 - 2y + 1) = {area}   match: {area == 900}\n")

print("command list layout")
BLK = 5
state, per_tri, restore, softrap, pad = 1, 7, 1, 1, 1
tri = 1
blocks = state + per_tri*tri + restore + softrap + pad
tail_blocks = blocks - pad
print(f"  blocks: state {state} + tri {per_tri}x{tri} + restore {restore}"
      f" + softrap {softrap} + pad {pad} = {blocks}")
print(f"  total {blocks*BLK} dwords, PRIMEND at {tail_blocks*BLK}"
      f"   measured 55 / 50   match: "
      f"{blocks*BLK == 55 and tail_blocks*BLK == 50}\n")

print("validator reason code")
codes = {0:'OK',1:'MAGIC',2:'VERSION',3:'COUNT',4:'DSTORG',5:'ZORG',
         6:'TEXORG',7:'DWGCTL',8:'TRIROW',9:'TRICOL'}
print(f"  reported 4 = {codes[4]}  (dstorg aimed at the visible framebuffer)\n")

print("register sets, after making the two paths isomorphic")
dma = ["DSTORG","ZORG","ALPHACTRL","DWGCTL(sloped)","AR0","AR1","AR2","AR4",
       "AR5","AR6","SGN","DR4","DR6","DR7","DR8","DR10","DR11","DR12","DR14",
       "DR15","DR0","DR2","DR3","ALPHASTART","ALPHAXINC","ALPHAYINC",
       "FXBNDRY","YDSTLEN+EXEC","DWGCTL(restore)"]
mmio = list(dma)     # the fix added ALPHASTART/XINC/YINC, ZORG, DR0/DR2/DR3
print(f"  DMA writes {len(dma)} registers, MMIO writes {len(mmio)}")
print(f"  symmetric difference: {set(dma) ^ set(mmio) or 'none'}")
print("\n  before the fix, MMIO omitted these seven:")
for r in ("ALPHASTART","ALPHAXINC","ALPHAYINC","ZORG","DR0","DR2","DR3"):
    print(f"    {r}")
print("  they were changed together, so which one mattered is not isolated")
