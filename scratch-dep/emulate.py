import sys, math; sys.path.insert(0,'scratch-cov')
from fractions import Fraction as F
exec(open('scratch-dep/depth.py').read().split("for tag in")[0])

def ceildiv(a,b): return -((-a)//b)
def regs(xa,D,Hh,k0):
    mag=2*abs(D); dy=2*Hh; A=2*D*k0+D-Hh
    q=ceildiv(A,dy); r=A-dy*q
    return xa+q, mag, dy, (r if D>=0 else -r-dy+1), (1 if D>=0 else -1)
def N(mag,dy,e,k):
    if k==0: return 0
    v=ceildiv(mag*k+e,dy); return v if v>0 else 0
def traps(V):
    vs=sorted([(y,x) for x,y in V]); (ty,tx),(my,mx),(ly,lx)=vs
    span=ly-ty; cross=(lx-tx)*(my-ty)-(ly-ty)*(mx-tx)
    out=[]
    for (y0,h,short,k0s,k0l) in ((ty,my-ty,(tx,mx-tx,my-ty),0,0),
                                 (my,ly-my,(mx,lx-mx,ly-my),0,my-ty)):
        if h<=0: continue
        longe=(tx,lx-tx,span)
        pair=((longe,k0l),(short,k0s)) if cross<0 else ((short,k0s),(longe,k0l))
        L=regs(*pair[0][0],pair[0][1]); R=regs(*pair[1][0],pair[1][1])
        out.append((y0,h,L,R))
    return out
def osmgaFixed(v):
    s=v*32768.0
    return int(s+0.5) if s>=0 else int(s-0.5)

def emulate(V, Zvert, centre):
    """exactly what the builder programs, then the engine's shifted sum"""
    a = V[0]                                   # the plane's anchor vertex
    dx,dy,k = planeof(V, [F(z) for z in Zvert])
    out={}
    for (y0,h,L,R) in traps(V):
        left = L[0]
        ox = float(left - a[0]) + (0.5 if centre else 0.0)
        oy = float(y0    - a[1]) + (0.5 if centre else 0.0)
        at = float(k + dx*F(a[0]) + dy*F(a[1])) + float(dx)*ox + float(dy)*oy
        if at < 0.0: at = 0.0
        if at > 65535.0: at = 65535.0
        z0 = osmgaFixed(at); zdx = osmgaFixed(float(dx)); zdy = osmgaFixed(float(dy))
        for kk in range(h):
            lx = L[0] + L[4]*N(L[1],L[2],L[3],kk)
            rx = R[0] + R[4]*N(R[1],R[2],R[3],kk)
            for x in range(lx, rx):
                out[(x, y0+kk)] = (z0 + (x-left)*zdx + kk*zdy) >> 15
    return out

for tag in ('1','2'):
    mh,hw = load('scratch-dep/hw%s.txt'%tag)
    V=mh['V']; Zex=[WIN(z) for z in mh['Z']]
    Ztr=[F(int(z)) for z in Zex]                 # the hook's (unsigned long) cast
    print("== shape %s ==" % tag)
    print("   vertex window z exact %s  ->  truncated %s"
          % ([float(z) for z in Zex], [int(z) for z in Ztr]))
    for zn, Z in (("exact vertex z", Zex), ("truncated vertex z", Ztr)):
        for cn, c in (("corner", False), ("centre", True)):
            pred = emulate(V, Z, c)
            common = set(pred) & set(hw)
            same = sum(1 for p in common if pred[p]==hw[p])
            d = [hw[p]-pred[p] for p in common]
            print("   %-19s + %-6s : %4d/%4d pixels exact   mean diff %+8.3f  %s"
                  % (zn, cn, same, len(common), sum(d)/len(d),
                     "EXACT" if same==len(common) and len(common)==len(hw) else ""))
    print()
