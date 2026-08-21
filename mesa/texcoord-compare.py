#!/usr/bin/env python3
"""
Check the six TMR values the builder emits against an oracle that solves the
plane itself.

The anchor -- the trapezoid's first row and that row's left edge -- is taken
from the builder's own output, because it is a property of the trapezoid and
not of the formula under test.  Everything else here is computed from the
three vertices.

The register order is the engine's, which is NOT (du/dx, dv/dx, du/dy, dv/dy):
the DDX names them sx inc, sy inc, tx inc, ty inc (mga_storm.c:332-335), so
TMR1 is du/dy and TMR2 is dv/dx.  They went in the other way round once and
half the pixels of the first textured triangle came out wrong.
"""
import sys, math

def log2ceil(n):
    l = 0
    while (1 << l) < n and l < 31:
        l += 1
    return l

def main(path):
    cases = []
    cur = None
    for line in open(path):
        f = line.split()
        if not f:
            continue
        if line.startswith('# case'):
            cur = dict(name=f[2], w=int(f[4]), h=int(f[5]), n=int(f[7]),
                       v=[], T=[])
            cases.append(cur)
        elif f[0] == '#' and len(f) > 1 and f[1] == 'v' and cur is not None:
            cur['v'].append((int(f[2]), int(f[3]), float(f[4]), float(f[5])))
        elif line.startswith('T ') and cur is not None:
            cur['T'].append([int(x) for x in f[1:]])
    bad = 0
    for c in cases:
        if c['n'] < 0 or not c['T']:
            print("  %s: refused (%d)" % (c['name'], c['n']))
            continue
        (ax, ay, a_s, a_t), (bx, by, b_s, b_t), (cx, cy, c_s, c_t) = c['v']
        S = 256.0
        x1 = (bx - ax) / S; y1 = (by - ay) / S
        x2 = (cx - ax) / S; y2 = (cy - ay) / S
        den = x1 * y2 - x2 * y1
        us = c['w'] * (1 << (20 - log2ceil(c['w'])))
        vs = c['h'] * (1 << (20 - log2ceil(c['h'])))
        ua, ub, uc = a_s * us, b_s * us, c_s * us
        va, vb, vc = a_t * vs, b_t * vs, c_t * vs
        udx = ((ub - ua) * y2 - (uc - ua) * y1) / den
        udy = ((uc - ua) * x1 - (ub - ua) * x2) / den
        vdx = ((vb - va) * y2 - (vc - va) * y1) / den
        vdy = ((vc - va) * x1 - (vb - va) * x2) / den
        for T in c['T']:
            y, h, left, right, t0, t1, t2, t3, t6, t7 = T
            ox = left - ax / S + 0.5
            oy = y - ay / S + 0.5
            want = [round(udx), round(udy), round(vdx), round(vdy),
                    math.floor(ua + udx * ox + udy * oy + 0.5),
                    math.floor(va + vdx * ox + vdy * oy + 0.5)]
            got = [t0, t1, t2, t3, t6, t7]
            ok = want == got
            if not ok:
                bad += 1
            print("  %-3s y=%-3d left=%-4d %s %s"
                  % (c['name'], y, left, got, "ok" if ok else
                     "MISMATCH, wanted %s" % want))
    print()
    print("=== nothing to report ===" if not bad else "=== %d wrong ===" % bad)
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else '/tmp/tc.out'))
