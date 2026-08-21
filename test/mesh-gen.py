#!/usr/bin/env python3
"""
Generate one mesh, as data, read by both the drawing test and the judge.

A long mesh cannot be judged the way four triangles were: a solo run each is
one run per triangle.  Instead every triangle is given a colour, and the
colouring is chosen so that NO TWO TRIANGLES SHARING AN EDGE OR A VERTEX EVER
SHARE A COLOUR.  Then a pixel names its writer, and a pixel one triangle took
from its neighbour is visible in a single run.

Four colours are not assumed to be enough -- the colouring is computed and
the number of colours it needed is reported, so the C side never has to guess.
"""
import sys
from fractions import Fraction as F

def build(rows, cols, W, H, jitter=True, ongrid=True):
    """
    ongrid: every vertex an exact multiple of 1/256.

    This is not cosmetic.  The back end carries vertices in 1/256 of a pixel,
    so a mesh whose corners are not on that grid is drawn from coordinates
    that differ from the ones the judge evaluates, and the difference shows up
    as pixels attributed to a neighbour -- a quantisation cost read as a
    coverage defect.  Off-grid meshes are still worth drawing, but as a
    measurement of that cost and not as a test of the rule.
    """
    V = []
    if ongrid:
        # a pitch that is itself a whole number of 1/256 steps, and small
        # enough that the mesh still fits
        px = F(((W - 40) // cols) * 256, 256)
        py = F(((H - 40) // rows) * 256, 256)
    for r in range(rows + 1):
        for c in range(cols + 1):
            if ongrid:
                x = F(20) + px * c
                y = F(20) + py * r
            else:
                x = F(20) + F((W - 40) * c, cols)
                y = F(20) + F((H - 40) * r, rows)
            if jitter:
                # a different fraction per vertex, none of them a half.
                # 1/32 is 8/256, so this keeps an on-grid mesh on the grid.
                x += F((r * 7 + c * 13) % 11, 32)
                y += F((r * 5 + c * 3) % 13, 32)
            V.append((x, y))
    idx = lambda r, c: r * (cols + 1) + c
    T = []
    for r in range(rows):
        for c in range(cols):
            a, b = idx(r, c), idx(r, c + 1)
            d, e = idx(r + 1, c), idx(r + 1, c + 1)
            T.append((a, b, e))
            T.append((a, e, d))
    # adjacency: share a vertex (stricter than share an edge)
    own = [set(t) for t in T]
    adj = [set() for _ in T]
    for i in range(len(T)):
        for j in range(i + 1, len(T)):
            if own[i] & own[j]:
                adj[i].add(j); adj[j].add(i)
    colour = [-1] * len(T)
    for i in range(len(T)):
        used = {colour[j] for j in adj[i] if colour[j] >= 0}
        k = 0
        while k in used:
            k += 1
        colour[i] = k
    return V, T, colour

def main():
    rows = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    cols = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    out  = sys.argv[3] if len(sys.argv) > 3 else 'scratch-mesh/mesh.txt'
    ongrid = not (len(sys.argv) > 4 and sys.argv[4] == 'offgrid')
    V, T, C = build(rows, cols, 320, 240, ongrid=ongrid)
    with open(out, 'w') as f:
        f.write("# rows %d cols %d tri %d colours %d\n"
                % (rows, cols, len(T), max(C) + 1))
        f.write("V %d\n" % len(V))
        for x, y in V:
            f.write("%.9f %.9f\n" % (float(x), float(y)))
        f.write("T %d\n" % len(T))
        for (a, b, c), k in zip(T, C):
            f.write("%d %d %d %d\n" % (a, b, c, k))
    off = sum(1 for x, y in V if x * 256 != int(x * 256) or y * 256 != int(y * 256))
    print("%s: %d vertices, %d triangles, %d colours, %d off the 1/256 grid"
          % (out, len(V), len(T), max(C) + 1, off))

if __name__ == '__main__':
    main()
