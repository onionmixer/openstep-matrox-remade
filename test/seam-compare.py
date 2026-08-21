#!/usr/bin/env python3
"""
Do neighbouring triangles meet cleanly?

The proof is NOT the union of the two triangles against the rule: one
triangle under-covering while the other over-covers leaves a perfect union.
Each triangle is drawn alone and compared to its own oracle mask, and their
intersection must be empty.  The union and the two-colour order test remain,
as cross-checks on the pipeline rather than as the proof.

The rational oracle is the only acceptance authority.  The software path is
printed beside it as explanatory data -- a seam Mesa cannot hold is a fact
about Mesa, not a licence for the engine to seam too.
"""
import sys, os, re
from fractions import Fraction as F

def load(p):
    px, meta = {}, {'V': [], 'T': []}
    for line in open(p):
        f = line.split()
        if line.startswith('# v'):
            meta['V'].append((F(f[2]), F(f[3])))
        elif line.startswith('# t'):
            meta['T'].append((int(f[2]), int(f[3]), int(f[4])))
        elif line.startswith('# counters'):
            for k, v in re.findall(r'(\w+)=(\d+)', line):
                meta[k] = int(v)
        elif line.startswith('# mode'):
            meta['mode'] = f[-1]
        elif line.startswith('P '):
            px[(int(f[1]), int(f[2]))] = (int(f[3]), int(f[4]), int(f[5]))
    return meta, px

def cover(V, W=320, H=240):
    """the rule, exactly, for fractional vertices"""
    (ax, ay), (bx, by), (cx, cy) = V
    if (bx-ax)*(cy-ay) - (by-ay)*(cx-ax) < 0:
        (bx, by), (cx, cy) = (cx, cy), (bx, by)
    E = [((ax,ay),(bx,by)), ((bx,by),(cx,cy)), ((cx,cy),(ax,ay))]
    xs, ys = [ax,bx,cx], [ay,by,cy]
    out = set()
    for y in range(max(0,int(min(ys))-1), min(H,int(max(ys))+2)):
        for x in range(max(0,int(min(xs))-1), min(W,int(max(xs))+2)):
            px, py = F(2*x+1,2), F(2*y+1,2); ok = True
            for (x0,y0),(x1,y1) in E:
                e = (x1-x0)*(py-y0) - (y1-y0)*(px-x0)
                if e < 0: ok = False; break
                if e == 0:
                    dx, dy = x1-x0, y1-y0
                    if not ((dy == 0 and dx < 0) or dy < 0): ok = False; break
            if ok: out.add((x,y))
    return out

def main(d):
    fail = []
    for sh in ('A', 'B', 'C', 'D', 'E'):
        base = os.path.join(d, '%s-' + sh + '-%s.txt')
        mb, pb = load(base % ('hw', 'both'))
        n = len(mb['T'])
        print("== shape %s (%d triangles) ==" % (sh, n))
        for who in ('hw', 'sw'):
            mb, pb = load(base % (who, 'both'))
            mr, pr = load(base % (who, 'rev'))
            mp, pp = load(base % (who, 'plane'))
            V, T = mb['V'], mb['T']
            masks = []
            bad = []
            for t in range(n):
                _, ps = load(base % (who, 'solo%d' % (t + 1)))
                want = cover([V[T[t][0]], V[T[t][1]], V[T[t][2]]])
                got = set(ps)
                masks.append(got)
                if want - got or got - want:
                    bad.append("t%d miss %d extra %d"
                               % (t, len(want - got), len(got - want)))
            dbl = 0
            for i2 in range(n):
                for j2 in range(i2 + 1, n):
                    dbl += len(masks[i2] & masks[j2])
            union = set().union(*masks)
            gap = len(union - set(pb))
            order = len([k for k in set(pb) | set(pr) if pb.get(k) != pr.get(k)])
            worst = 0.0
            for (x, y), c in pp.items():
                want = float(F(x, 1) / 4 + F(y, 1) / 4 + 120)
                worst = max(worst, abs(c[0] - want))
            print("   %s: per-triangle %-22s | owned twice %d | union gap %d"
                  " | order-dependent %d | plane worst %.2f"
                  % (who, ("exact" if not bad else "; ".join(bad)[:22]),
                     dbl, gap, order, worst))
            if who == 'hw':
                if bad:   fail.append("shape %s: %s" % (sh, "; ".join(bad)))
                if dbl:   fail.append("shape %s: %d pixels drawn twice" % (sh, dbl))
                if gap:   fail.append("shape %s: %d pixels of the union missing" % (sh, gap))
                if order: fail.append("shape %s: %d pixels depend on order" % (sh, order))
                if worst > 1.5:
                    fail.append("shape %s: plane off by %.2f" % (sh, worst))
    print("\n=== %s ===" % ("nothing to report" if not fail
                            else "%d thing(s) to look at" % len(fail)))
    for f in fail: print("   ! " + f)
    return 1 if fail else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'scratch-seam'))
