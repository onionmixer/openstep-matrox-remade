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

