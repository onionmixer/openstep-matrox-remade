"""
The sub-pixel encoding, whole, before any of it is written in C.

This is the specification the driver code is transcribed from, kept because
the arithmetic is where this work keeps going wrong and a model that can be
run is worth more than a paragraph that cannot.

What it is checked against:

  * the geometric rule, on 117 triangles with continuous fractional vertices:
    0.2115% of area wrong, against 5.009% for today's truncation
  * today's builder, on 396 integer-vertex triangles: not one pixel differs,
    which is the invariant that says this cannot regress ordinary geometry
  * the geometric rule again on integer vertices, 200 triangles, exact

The scaling is the cheap one and it is proved rather than sampled: with the
error term divided by the grid and rounded up, the coarse ceiling equals the
fine one always, because the half-open interval between a non-integer and the
next integer above it contains no integer for a multiple of the divisor to sit
in.  See M1_4B1_SUBPIXEL_PLAN.md section 7.

One precision for the whole primitive, never per edge: two edges quantising a
shared vertex differently put it in two places, which opens the middle-vertex
seam and cracks against the neighbouring triangle.
"""
from fractions import Fraction as F
def ceilF(v): return -((-v.numerator)//v.denominator)
def cd(a,b): return -((-a)//b)
def N(mag,dy,e,k):
    v=cd(mag*k+e,dy); return v if v>0 else 0

MAXWALK=16384; FIELD=131071

def pick_s(V):
    """one s for the whole primitive: the most every edge allows.
    Per-edge s would put a shared vertex in two places."""
    best=0
    for s in range(0,12):
        M=1<<s
        ok=True
        for i in range(3):
            for j in range(3):
                if i==j: continue
                D=abs(V[j][0]-V[i][0]); H=abs(V[j][1]-V[i][1])
                # after quantising, D and H move by under one grid step
                DD=int(D*M)+1; HH=int(H*M)+1
                if 2*DD > MAXWALK or 2*(DD+HH) > MAXWALK or 2*HH > FIELD: ok=False
            if not ok: break
        if ok: best=s
        else: break
    return best

def quant(V,s):
    M=1<<s
    return [(F(int(x*M),M), F(int(y*M),M)) for x,y in V]

def edge_regs(xa,ya,D,H,r0,s):
    """the phase-aware closed form, at the scaling proved exact"""
    M=1<<s
    XA=int(xa*M); YA=int(ya*M); DD=int(D*M); HH=int(H*M)
    Q2=2*M*HH
    P0=2*XA*HH - M*HH + ((2*r0+1)*M - 2*YA)*DD
    x0=cd(P0,Q2); r=P0-x0*Q2
    e2 = r if DD>=0 else -r-Q2+1
    return x0, 2*abs(DD), 2*HH, cd(e2,M), (1 if DD>=0 else -1)

def build(V0):
    s=pick_s(V0); V=quant(V0,s)
    vs=sorted([(y,x) for x,y in V])
    (ty,tx),(my,mx),(ly,lx)=vs
    if ly<=ty: return s,[]
    cross=(lx-tx)*(my-ty)-(ly-ty)*(mx-tx)
    if cross==0: return s,[]
    r_t=ceilF(ty-F(1,2)); r_m=ceilF(my-F(1,2)); r_l=ceilF(ly-F(1,2))
    longe=(tx,ty,lx-tx,ly-ty)
    out=[]
    for (r0,r1,short) in ((r_t,r_m,(tx,ty,mx-tx,my-ty)),
                          (r_m,r_l,(mx,my,lx-mx,ly-my))):
        if r1<=r0: continue
        if short[3]==0: continue
        pair=(longe,short) if cross<0 else (short,longe)
        L=edge_regs(pair[0][0],pair[0][1],pair[0][2],pair[0][3],r0,s)
        R=edge_regs(pair[1][0],pair[1][1],pair[1][2],pair[1][3],r0,s)
        out.append((r0,r1-r0,L,R))
    return s,out

def render(V0):
    s,traps=build(V0); px={}
    for (y0,h,L,R) in traps:
        for k in range(h):
            lx=L[0]+L[4]*N(L[1],L[2],L[3],k)
            rx=R[0]+R[4]*N(R[1],R[2],R[3],k)
            for x in range(lx,rx): px[(x,y0+k)]=1
    return s,set(px)
