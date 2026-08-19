m = "...............01234567G........"
print(f"map length {len(m)} (want 32)\n")
print("ALPHASTART bit -> alpha bit")
for i, c in enumerate(m):
    if c != '.':
        print(f"  bit {i:>2} -> {'alpha bit '+c if c.islower() or c.isdigit() else 'IMPURE ('+c+')'}")
lows = [i for i, c in enumerate(m) if c.isdigit() or (c.islower())]
print(f"\nfirst contributing bit = {min(lows)}, so ALPHASTART = alpha << {min(lows)}")
print(f"contiguous single-bit run = {len(lows)} bits -> {len(lows)}-bit alpha")
print("clean single bits throughout the run => NOT pre-multiplied (a^2/255"
      " would not be a power of two)\n")

# ALPHACTRL field decode, from mga_reg.h:586-608
SRC = {0:'ZERO',1:'ONE',2:'DST_COLOR',3:'ONE_MINUS_DST_COLOR',4:'SRC_ALPHA',
       5:'ONE_MINUS_SRC_ALPHA',6:'DST_ALPHA',7:'ONE_MINUS_DST_ALPHA',
       8:'SRC_ALPHA_SATURATE'}
DST = {0:'ZERO',1:'ONE',2:'SRC_COLOR',3:'ONE_MINUS_SRC_COLOR',4:'SRC_ALPHA',
       5:'ONE_MINUS_SRC_ALPHA',6:'DST_ALPHA',7:'ONE_MINUS_DST_ALPHA'}
def decode(v):
    return (f"src={SRC[v & 0xf]:<20} dst={DST[(v >> 4) & 7]:<20} "
            f"ALPHACHANNEL={'set' if v & 0x100 else 'clear'}")
print("what each control band actually asked for:")
for name, v, meas in (("band 32", 0x101, 255), ("band 33", 0x000, 0)):
    print(f"  {name} ALPHACTRL={v:#05x}: {decode(v)}")
    src_f = v & 0xf
    pred = 255 if src_f == 1 else (0 if src_f == 0 else None)
    print(f"      white source over black dst -> predicted {pred}, measured {meas}")
print("\nALPHACTRL = 0 is not 'blending off' -- it is SRC_ZERO | DST_ZERO,")
print("which is black by definition.  The control band was mis-specified.")
