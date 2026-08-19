# Exact model of osmgaDevMmap's command branch, swept over the cmdBytes and
# cmdPhysical corners the on-hardware test could not vary (both immutable
# once published).
PAGE, SHIFT, CMD_BASE = 8192, 13, 0x40000000
U32 = 0xFFFFFFFF

def cmd_branch(off, cmd_phys, cmd_bytes):
    if cmd_phys == 0 or off < CMD_BASE:
        return None                        # falls through to the VRAM branch
    rel = off - CMD_BASE
    if cmd_bytes < PAGE or rel > cmd_bytes - PAGE:   return -1
    if cmd_phys > U32 - rel:                          return -1
    phys = cmd_phys + rel
    if phys & (PAGE - 1):                             return -1
    if (phys >> SHIFT) > 0x7FFFFFFF:                  return -1
    return phys >> SHIFT

bad = 0
for cb in (0, 1, PAGE - 1, PAGE, PAGE + 1, 2*PAGE, 2*PAGE - 1, 65536, 65537):
    for cp in (0, 0x20000, PAGE, 1, U32 - PAGE + 1, U32 - 2*PAGE + 1,
               0xFFFF0000, 0x7FFFE000):
        for k in (-1, 0, 1, cb // PAGE - 1, cb // PAGE, cb // PAGE + 1):
            off = CMD_BASE + k * PAGE
            if off < 0:
                continue
            r = cmd_branch(off, cp, cb)
            if r is None or r == -1:
                continue
            # every accepted page must lie wholly inside the allocation
            p = r << SHIFT
            inside = cp <= p and p + PAGE <= cp + cb
            if not inside:
                print(f"  ESCAPE cb={cb} cp={cp:#x} off={off:#x} -> pfn {r} "
                      f"(phys {p:#x}, alloc {cp:#x}..{cp+cb:#x})")
                bad += 1
            if cp == 0:
                print(f"  LEAK cb={cb} off={off:#x} accepted with cmd_phys 0")
                bad += 1
    # sweep every byte offset across two pages: only page-aligned starts may
    # be accepted, and each must resolve to that same page of the allocation
    for d in range(-2, 2 * PAGE + 3):
        r = cmd_branch(CMD_BASE + d, 0x20000, cb)
        if r is None or r == -1:
            continue
        p = r << SHIFT
        if d % PAGE != 0:
            print(f"  UNALIGNED ACCEPTED cb={cb} d={d} -> pfn {r}")
            bad += 1
        elif p != 0x20000 + d:
            print(f"  WRONG PAGE cb={cb} d={d} -> phys {p:#x}")
            bad += 1
print(f"swept command-branch corners; escapes: {bad}")

print("\nVRAM branch cannot be stolen only if windowEnd <= CMD_BASE:")
for we in (0x700000, 16*1024*1024, CMD_BASE - PAGE, CMD_BASE, CMD_BASE + PAGE):
    print(f"  windowEnd {we:#x}: {'safe' if we <= CMD_BASE else 'STOLEN'}")
