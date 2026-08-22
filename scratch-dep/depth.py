import sys, math; sys.path.insert(0,'scratch-cov')
from fractions import Fraction as F

def load(p):
    px={}; meta={'V':[],'Z':[]}
    for line in open(p):
        f=line.split()
        if line.startswith('# vertex'):
            meta['V'].append((int(f[3]), int(f[4]))); meta['Z'].append(float(f[6]))
        elif line.startswith('# counters'):
            for t in f[2:]:
                k,v=t.split('='); meta[k]=int(v)
        elif line.startswith('# mode'): meta['mode']=f[-1]
        elif line.startswith('P '): px[(int(f[1]),int(f[2]))]=int(f[3])
    return meta, px

WIN = lambda zobj: F(655350,20) * (1 - F(str(zobj)))   # 32767.5*(1-z), exactly
def planeof(V, C):
    (x1,y1),(x2,y2),(x3,y3)=[(F(a),F(b)) for a,b in V]
    c1,c2,c3=C
    den=(x2-x1)*(y3-y1)-(y2-y1)*(x3-x1)
    dx=((c2-c1)*(y3-y1)-(c3-c1)*(y2-y1))/den
    dy=((c3-c1)*(x2-x1)-(c2-c1)*(x3-x1))/den
    return dx,dy,c1-dx*x1-dy*y1

for tag in ('1','2'):
    mh,hw = load('scratch-dep/hw%s.txt'%tag)
    ms,sw = load('scratch-dep/sw%s.txt'%tag)
    V = mh['V']; Zw = [WIN(z) for z in mh['Z']]
    print("== shape %s ==  vertices %s" % (tag, V))
    print("   window z at the vertices (exact): %s" % [float(z) for z in Zw])
    print("   hardware: drawn=%d software=%d declined=%d   %s"
          % (mh['drawn'], mh['software'], mh['declined'], mh['mode']))
    both = sorted(set(hw)&set(sw))
    print("   pixels: hw %d, sw %d, both %d" % (len(hw), len(sw), len(both)))
    print("   any fragment at the far value 65535?  hw %s  sw %s"
          % (max(hw.values())==65535, max(sw.values())==65535))
    dx,dy,k = planeof(V, Zw)
    A = lambda x,y: dx*x+dy*y+k
    for nm, px in (("software", sw), ("hardware", hw)):
        rc=[px[p]-A(F(p[0]),F(p[1])) for p in both]
        rz=[px[p]-A(F(2*p[0]+1,2),F(2*p[1]+1,2)) for p in both]
        print("   %-8s vs exact plane: corner mean %+10.3f   centre mean %+10.3f"
              % (nm, float(sum(rc)/len(rc)), float(sum(rz)/len(rz))))
    # the gate: does software equal round(exact plane at the centre) per pixel?
    bad=[p for p in both if sw[p]!=math.floor(A(F(2*p[0]+1,2),F(2*p[1]+1,2))+F(1,2))]
    print("   GATE: software == round(exact plane at the centre): %d of %d disagree%s"
          % (len(bad), len(both), ("  e.g. %s" % [(p, sw[p], float(A(F(2*p[0]+1,2),F(2*p[1]+1,2)))) for p in bad[:3]]) if bad else ""))
    print("   dz/dx = %.4f   dz/dy = %.4f   half-pixel = %.4f codes"
          % (float(dx), float(dy), float((dx+dy)/2)))
    print()
