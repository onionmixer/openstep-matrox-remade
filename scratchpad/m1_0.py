PAGE, VS, VE = 8192, 0x400000, 0x700000
CMD_BASE, CMD_BYTES, CMD_PHYS = 0x40000000, 65536, 0x20000
W, H, BPP, GUARD_ROWS = 1024, 768, 4, 256

print("VRAM window placement")
vis = W * H * BPP
guard = GUARD_ROWS * W * BPP
want = (vis + guard + PAGE - 1) & ~(PAGE - 1)
print(f"  visible framebuffer 0 .. {vis:#x} ({vis/1024/1024:.2f} MiB)")
print(f"  + {GUARD_ROWS}-row guard          -> window starts {want:#x}")
print(f"  reported start {VS:#x}  match: {want == VS}")
print(f"  window {VS:#x}..{VE:#x} excludes the visible area: {VS >= vis}")
print(f"  window end vs proven 7 MiB bound: {VE == 7*1024*1024}\n")

print("command window")
print(f"  ring physical {CMD_PHYS:#x} = {CMD_PHYS//1024} KiB")
print(f"  page aligned ({PAGE}): {CMD_PHYS % PAGE == 0}  (pfn {CMD_PHYS//PAGE})")
print(f"  spans {CMD_PHYS:#x}..{CMD_PHYS+CMD_BYTES:#x}"
      f" = {CMD_PHYS//1024}..{(CMD_PHYS+CMD_BYTES)//1024} KiB")
print(f"  inside conventional memory (<640 KiB): "
      f"{CMD_PHYS + CMD_BYTES <= 640*1024}")
print(f"  size is the D1 ring size 0x10000: {CMD_BYTES == 0x10000}\n")

print("offset space -- the handler takes a signed int")
print(f"  cmd base {CMD_BASE:#x} = {CMD_BASE}, < 2^31: {CMD_BASE < 2**31}")
print(f"  cmd end  {CMD_BASE+CMD_BYTES:#x}, < 2^31: {CMD_BASE+CMD_BYTES < 2**31}")
print(f"  ranges disjoint (VRAM ends {VE:#x} << cmd base): {VE < CMD_BASE}")
gap = CMD_BASE - VE
print(f"  gap between the two windows: {gap:#x} = {gap/1024/1024:.0f} MiB")
print(f"  a 16 MiB VRAM window would still not reach the cmd base: "
      f"{16*1024*1024 < CMD_BASE}")
