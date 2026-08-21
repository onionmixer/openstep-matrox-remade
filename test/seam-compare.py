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
    px, meta = {}, {'V': []}
    for line in open(p):
        f = line.split()
        if line.startswith('# v'):
            meta['V'].append((F(f[2]), F(f[3])))
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
    for sh in ('A', 'B', 'C'):
        print("== shape %s ==" % sh)
        for who in ('hw', 'sw'):
            m1, p1 = load(os.path.join(d, '%s-%s-solo1.txt' % (who, sh)))
            m2, p2 = load(os.path.join(d, '%s-%s-solo2.txt' % (who, sh)))
            mb, pb = load(os.path.join(d, '%s-%s-both.txt'  % (who, sh)))
            mr, pr = load(os.path.join(d, '%s-%s-rev.txt'   % (who, sh)))
            mp, pp = load(os.path.join(d, '%s-%s-plane.txt' % (who, sh)))
            V = m1['V']
            t1 = cover([V[0], V[1], V[2]])
            t2 = cover([V[0], V[2], V[3]])
            a, b = set(p1), set(p2)

            # the proof: each triangle against its own oracle
            miss1, ex1 = len(t1 - a), len(a - t1)
            miss2, ex2 = len(t2 - b), len(b - t2)
            # and no pixel belongs to both
            dbl = len(a & b)
            # cross-checks
            gap = len((t1 | t2) - set(pb))
            order = len([k for k in set(pb) | set(pr) if pb.get(k) != pr.get(k)])
            # continuity: every covered pixel on the shared plane
            worst = 0
            for (x, y), c in pp.items():
                want = F(x,1)/4 + F(y,1)/4 + 120     # the analytic plane
                worst = max(worst, abs(c[0] - float(want)))
            print("   %s: t1 miss %d extra %d | t2 miss %d extra %d | both-owned %d"
                  " | union gap %d | order-dependent %d | plane worst %.2f"
                  % (who, miss1, ex1, miss2, ex2, dbl, gap, order, worst))
            if who == 'hw':
                if miss1 or ex1 or miss2 or ex2:
                    fail.append("shape %s: a triangle differs from its own oracle" % sh)
                if dbl:
                    fail.append("shape %s: %d pixels drawn by both" % (sh, dbl))
                if gap:
                    fail.append("shape %s: %d pixels of the union missing" % (sh, gap))
                if order:
                    fail.append("shape %s: %d pixels depend on order" % (sh, order))
                if worst > 1.5:
                    fail.append("shape %s: plane off by %.2f at worst" % (sh, worst))
    print("\n=== %s ===" % ("nothing to report" if not fail
                            else "%d thing(s) to look at" % len(fail)))
    for f in fail: print("   ! " + f)
    return 1 if fail else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'scratch-seam'))
