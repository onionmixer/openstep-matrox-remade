from fractions import Fraction as F
import math
def load(p):
    px={}; meta={'V':[],'Z':[],'want':[]}
    for line in open(p):
        f=line.split()
        if line.startswith('# vertex'):
            meta['V'].append((int(f[3]),int(f[4]))); meta['Z'].append(float(f[6]))
            meta['want'].append(float(f[8]))
        elif line.startswith('# counters'):
            for t in f[2:]:
                k,v=t.split('='); meta[k]=int(v)
        elif line.startswith('P '): px[(int(f[1]),int(f[2]))]=int(f[3])
    return meta,px
def planeof(V,C):
    (x1,y1),(x2,y2),(x3,y3)=[(F(a),F(b)) for a,b in V]; c1,c2,c3=C
    den=(x2-x1)*(y3-y1)-(y2-y1)*(x3-x1)
    dx=((c2-c1)*(y3-y1)-(c3-c1)*(y2-y1))/den
    dy=((c3-c1)*(x2-x1)-(c2-c1)*(x3-x1))/den
    return dx,dy,c1-dx*x1-dy*y1
def ceildiv(a,b): return -((-a)//b)
def regs(xa,D,Hh,k0):
    mag=2*abs(D); dy=2*Hh; A=2*D*k0+D-Hh; q=ceildiv(A,dy); r=A-dy*q
    return xa+q,mag,dy,(r if D>=0 else -r-dy+1),(1 if D>=0 else -1)
def N(mag,dy,e,k):
    if k==0: return 0
    v=ceildiv(mag*k+e,dy); return v if v>0 else 0
def traps(V):
    vs=sorted([(y,x) for x,y in V]); (ty,tx),(my,mx),(ly,lx)=vs
    span=ly-ty; cross=(lx-tx)*(my-ty)-(ly-ty)*(mx-tx); out=[]
    for (y0,h,short,k0s,k0l) in ((ty,my-ty,(tx,mx-tx,my-ty),0,0),(my,ly-my,(mx,lx-mx,ly-my),0,my-ty)):
        if h<=0: continue
        longe=(tx,lx-tx,span)
        pair=((longe,k0l),(short,k0s)) if cross<0 else ((short,k0s),(longe,k0l))
        out.append((y0,h,regs(*pair[0][0],pair[0][1]),regs(*pair[1][0],pair[1][1])))
    return out
def fx(v):
    s=v*32768.0; return int(s+0.5) if s>=0 else int(s-0.5)
def emulate(V,Z,centre,report=False):
    a=V[0]; dx,dy,k=planeof(V,[F(z) for z in Z]); out={}; clamps=[]
    for (y0,h,L,R) in traps(V):
        left=L[0]
        ox=float(left-a[0])+(0.5 if centre else 0.0)
        oy=float(y0-a[1])+(0.5 if centre else 0.0)
        raw=float(k+dx*F(a[0])+dy*F(a[1]))+float(dx)*ox+float(dy)*oy
        at=max(0.0,min(65535.0,raw))
        if at!=raw: clamps.append((y0,h,raw,at))
        z0=fx(at); zdx=fx(float(dx)); zdy=fx(float(dy))
        for kk in range(h):
            lx=L[0]+L[4]*N(L[1],L[2],L[3],kk); rx=R[0]+R[4]*N(R[1],R[2],R[3],kk)
            for x in range(lx,rx): out[(x,y0+kk)]=(z0+(x-left)*zdx+kk*zdy)>>15
    return (out,clamps) if report else out

for tag in ('3','4'):
    mh,hw = load('scratch-dep/hw%s.txt'%tag)
    ms,sw = load('scratch-dep/sw%s.txt'%tag)
    V=mh['V']; want=mh['want']
    print("== shape %s ==  %s   wanted codes %s" % (tag, V, want))
    print("   hardware %d px, software %d px, drawn=%d" % (len(hw),len(sw),mh['drawn']))
    for lbl, Z in (("wanted", [F(int(w)) for w in want]),
                   ("wanted-1 on v0", [F(int(want[0])-1)]+[F(int(w)) for w in want[1:]])):
        for cn,c in (("corner",False),("centre",True)):
            pred,cl = emulate(V,Z,c,True)
            common=set(pred)&set(hw)
            same=sum(1 for p in common if pred[p]==hw[p])
            print("   %-14s + %-6s : %5d/%5d exact   clamps %s"
                  % (lbl,cn,same,len(common), [(y,h,round(r,1),round(a,1)) for y,h,r,a in cl] or "none"))
    print()
