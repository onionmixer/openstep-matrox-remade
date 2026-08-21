#!/usr/bin/env python3
"""
Where is the interpolated colour actually evaluated?

openstep-mga-mesa-attrib-test.c prints every covered pixel from one Gouraud
triangle; run it once with OSMGA_MESA_ACCEL=0 and once without, and point this
at the two dumps.

Neither path is treated as the definition of right.  The plane is solved
exactly from the vertices in Fractions, and both paths are scored against it
at two candidate sample points -- the pixel's corner and its centre.

The shape is built so that three channels have to answer three different ways,
which is what separates an anchoring error from a plain colour bias: red's
gradients sum large and positive, green's large and negative, and blue is a
linear function of (x - y) so its gradients are equal and opposite
identically.  Anchoring cannot move blue at all.

It also checks the two failure modes that a mean residual would hide: a per-row
reset that only adds the row gradient, which would make the residual follow the
left edge rather than being constant, and two trapezoids whose start values
are anchored differently, which is a discontinuity at the split whether or not
it is visible.
"""
import sys, os, re, collections
from fractions import Fraction as F

NAME = ("red", "green", "blue", "alpha")

def load(path):
    px, meta = {}, {}
    meta['V'] = []; meta['COL'] = []
    for line in open(path):
        if line.startswith('# vertex'):
            f = line.split()
            meta['V'].append((int(f[3]), int(f[4])))
            meta['COL'].append(tuple(int(t) for t in f[6:10]))
        elif line.startswith('# mode'):
            meta['mode'] = line.split()[-1]
        elif line.startswith('# counters'):
            for k, v in re.findall(r'(\w+)=(\d+)', line):
                meta[k] = int(v)
        elif line.startswith('P '):
            f = line.split()
            px[(int(f[1]), int(f[2]))] = tuple(int(t) for t in f[3:7])
    return meta, px

def plane(V, COL, comp):
    (x1, y1), (x2, y2), (x3, y3) = [(F(a), F(b)) for a, b in V]
    c1, c2, c3 = [F(COL[i][comp]) for i in range(3)]
    den = (x2 - x1) * (y3 - y1) - (y2 - y1) * (x3 - x1)
    dx = ((c2 - c1) * (y3 - y1) - (c3 - c1) * (y2 - y1)) / den
    dy = ((c3 - c1) * (x2 - x1) - (c2 - c1) * (x3 - x1)) / den
    return dx, dy, c1 - dx * x1 - dy * y1

def cover(verts, W=320, H=240):
    """the coverage rule itself, in integers -- neither path's opinion"""
    v = [(int(x), int(y)) for x, y in verts]
    (ax, ay), (bx, by), (cx, cy) = v
    if (bx-ax)*(cy-ay) - (by-ay)*(cx-ax) < 0:
        (bx, by), (cx, cy) = (cx, cy), (bx, by)
    E = [((ax,ay),(bx,by)), ((bx,by),(cx,cy)), ((cx,cy),(ax,ay))]
    out = {}
    for y in range(max(0, min(ay,by,cy)-1), min(H, max(ay,by,cy)+2)):
        for x in range(max(0, min(ax,bx,cx)-1), min(W, max(ax,bx,cx)+2)):
            ok = True
            for (x0,y0),(x1,y1) in E:
                e = (x1-x0)*(2*y+1-2*y0) - (y1-y0)*(2*x+1-2*x0)
                if e < 0: ok = False; break
                if e == 0:
                    ddx, ddy = x1-x0, y1-y0
                    if not ((ddy == 0 and ddx < 0) or ddy < 0): ok = False; break
            if ok: out.setdefault(y, []).append(x)
    return {y: (min(v2), max(v2)) for y, v2 in out.items() if v2}

def main(d):
    mh, hw = load(os.path.join(d, 'hw.txt'))
    ms, sw = load(os.path.join(d, 'sw.txt'))
    V, COL = mh['V'], mh['COL']
    SPLIT = sorted(y for _, y in V)[1]
    fail = []
    print("shape %s   split at y = %d\n" % (V, SPLIT))

    print("== the engine drew the accelerated run ==")
    ok = (mh['software'] == 0 and mh['declined'] == 0
          and mh['unsupported'] == 0 and mh['drawn'] > 0)
    print("   %s drawn=%d software=%d unsupported=%d declined=%d%s"
          % (mh['mode'], mh['drawn'], mh['software'], mh['unsupported'],
             mh['declined'], "" if ok else "   <-- NOT PURE"))
    if not ok: fail.append("not a pure hardware run")

    print("\n== coverage first: colour is only comparable where both drew ==")
    ref = cover(V)
    refpix = set((x, y) for y, (lo, hi) in ref.items() for x in range(lo, hi + 1))
    for name, px in (("hardware", hw), ("software", sw)):
        miss, extra = refpix - set(px), set(px) - refpix
        print("   %-8s %4d pixels   missing %d  extra %d%s"
              % (name, len(px), len(miss), len(extra),
                 "   EXACT" if not miss and not extra else ""))
        if miss:
            ends = all(x in (ref[y][0], ref[y][1]) for (x, y) in miss)
            print("            missed: %s%s" % (sorted(miss)[:4],
                  "  (all at a run end)" if ends else "  (NOT all at a run end)"))
    both = sorted(set(hw) & set(sw))
    print("   comparing colour on the %d pixels both covered" % len(both))

    print("\n== where each path evaluates the plane ==")
    print("   %-6s %9s %9s | %10s %10s | %10s %10s"
          % ("chan", "dx", "dy", "hw corner", "hw centre", "sw corner", "sw centre"))
    res = {}
    for comp in range(4):
        dx, dy, k = plane(V, COL, comp)
        A = lambda x, y: dx * x + dy * y + k
        cell = []
        for px in (hw, sw):
            rc = [px[p][comp] - A(F(p[0]), F(p[1])) for p in both]
            rz = [px[p][comp] - A(F(2*p[0]+1, 2), F(2*p[1]+1, 2)) for p in both]
            cell += [sum(rc)/len(rc), sum(rz)/len(rz)]
        res[comp] = (dx, dy, cell)
        print("   %-6s %9.4f %9.4f | %10.3f %10.3f | %10.3f %10.3f"
              % (NAME[comp], float(dx), float(dy),
                 float(cell[0]), float(cell[1]), float(cell[2]), float(cell[3])))

    print("\n   predicted for a corner-anchored start: corner 0, centre -(dx+dy)/2")
    for comp in range(4):
        dx, dy, _ = res[comp]
        print("      %-6s -(dx+dy)/2 = %+8.4f" % (NAME[comp], float(-(dx+dy)/2)))

    # blue cannot move under anchoring; if it does, it is not anchoring
    if abs(float(res[2][2][0])) > 0.25 or abs(float(res[2][2][1])) > 0.25:
        fail.append("the control channel moved, so this is not anchoring alone")

    print("\n== does the residual follow the left edge?  (a per-row reset would) ==")
    left = {}
    for (x, y) in hw:
        left[y] = min(left.get(y, 1 << 30), x)
    for comp in (0, 1, 3):
        dx, dy, k = plane(V, COL, comp)
        per = collections.defaultdict(list)
        for (x, y) in hw:
            per[y].append(hw[(x, y)][comp] - (dx*F(x) + dy*F(y) + k))
        rows = sorted(per)
        L = [float(left[y]) for y in rows]
        R = [float(sum(per[y]) / len(per[y])) for y in rows]
        n = len(L); mL = sum(L)/n; mR = sum(R)/n
        vL = sum((v - mL) ** 2 for v in L)
        slope = (sum((L[i]-mL)*(R[i]-mR) for i in range(n)) / vL) if vL else 0.0
        print("   %-6s slope against left(row) = %+0.5f levels/column "
              "(a per-row reset would give %+0.3f)"
              % (NAME[comp], slope, -float(dx)))
        if abs(slope) > abs(float(dx)) * 0.02:
            fail.append("%s residual follows the left edge" % NAME[comp])

    print("\n== do the two trapezoids agree?  (split at y = %d) ==" % SPLIT)
    for comp in range(4):
        dx, dy, k = plane(V, COL, comp)
        top = [hw[p][comp] - (dx*F(p[0])+dy*F(p[1])+k) for p in hw if p[1] < SPLIT]
        bot = [hw[p][comp] - (dx*F(p[0])+dy*F(p[1])+k) for p in hw if p[1] >= SPLIT]
        if not top or not bot: continue
        diff = float(sum(bot)/len(bot) - sum(top)/len(top))
        print("   %-6s top %7.3f (%d px)   bottom %7.3f (%d px)   difference %+0.4f"
              % (NAME[comp], float(sum(top)/len(top)), len(top),
                 float(sum(bot)/len(bot)), len(bot), diff))
        if abs(diff) > 0.5:
            fail.append("%s: the two trapezoids are anchored differently" % NAME[comp])

    print("\n=== %s ===" % ("nothing further to report" if not fail
                            else "%d thing(s) to look at" % len(fail)))
    for f in fail: print("   ! " + f)
    return 1 if fail else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'scratch-att'))
