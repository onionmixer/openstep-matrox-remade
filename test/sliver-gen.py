#!/usr/bin/env python3
"""
Generate a stratified family of near-degenerate triangles, as data, read by
both the drawing probe and the judge.

M16 S4-e found ONE sliver on which WARP disagrees with software, and the
review that followed said the useful thing plainly: near a raster boundary
the outcome is discrete and phase-sensitive, so the set of shapes WARP gets
wrong may be disconnected islands rather than "some quantity below a
threshold".  A family built around one example would find the example.

So the family is stratified over the things that can move the answer, and
the sweep is CHARACTERISATION -- a map of where the two disagree -- and not
a search for a threshold to install.  "No simple rule separates them" is a
valid outcome and the analysis says so when it is the answer.

The four axes:

    altitude    the minimum altitude 2|A|/Lmax, which the review preferred
                to area: it is what makes a long thin sliver dangerous while
                a small compact triangle need not be
    length      the longest edge, so altitude and area vary independently
    angle       the long edge's direction, because a near-vertical sliver
                and a near-horizontal one meet the sample grid differently
    phase       a sub-pixel translation, because the outcome is discrete and
                the same shape can fall either way

The vertices are written in WINDOW coordinates and the probe draws them
through an orthographic projection that maps window units to pixels, so
what is written here is what Mesa is asked for.  What Mesa actually hands
the hook is recorded by the probe, and the judge uses that.
"""
import math
import os
import sys

ALT   = [0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0]   # pixels
LEN   = [10.0, 40.0, 87.0]
ANG   = [0.0, 22.5, 45.0, 67.5, 90.0]                   # degrees
PHASE = [0.0, 0.25, 0.5, 0.75]

# Somewhere with room for the longest edge at any angle, and far from the
# surface edges so nothing is clipped -- a clipped VB is not batched at all
# and would take a different path through the hook.
CX, CY = 160.0, 120.0


def build():
    out = []
    for a in ALT:
        for L in LEN:
            for th in ANG:
                for ph in PHASE:
                    r = math.radians(th)
                    dx, dy = math.cos(r), math.sin(r)
                    # the long edge, centred, then an apex at altitude a from
                    # its midpoint on the perpendicular
                    x0 = CX - dx * L / 2 + ph
                    y0 = CY - dy * L / 2 + ph
                    x1 = CX + dx * L / 2 + ph
                    y1 = CY + dy * L / 2 + ph
                    x2 = CX - dy * a + ph
                    y2 = CY + dx * a + ph
                    out.append((a, L, th, ph, x0, y0, x1, y1, x2, y2))
    return out


def main(d):
    fam = build()
    if not os.path.isdir(d):
        os.makedirs(d)
    p = os.path.join(d, 'family.txt')
    with open(p, 'w') as f:
        f.write("# stratified near-degenerate triangles\n")
        f.write("# idx alt len angle phase x0 y0 x1 y1 x2 y2\n")
        f.write("N %d\n" % len(fam))
        for i, t in enumerate(fam):
            f.write("F %d %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g"
                    " %.17g %.17g\n" % ((i,) + t))
    # The altitudes as built, checked rather than assumed: the construction
    # puts the apex at distance a from the long edge, so 2|A|/L must be a.
    worst = 0.0
    for (a, L, th, ph, x0, y0, x1, y1, x2, y2) in fam:
        A2 = abs((x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0))
        got = A2 / math.hypot(x1 - x0, y1 - y0)
        worst = max(worst, abs(got - a))
    print("wrote %d triangles to %s" % (len(fam), p))
    print("  altitudes %s" % ALT)
    print("  lengths   %s" % LEN)
    print("  angles    %s" % ANG)
    print("  phases    %s" % PHASE)
    print("  worst |2A/L - altitude| over the family: %.3e" % worst)
    return 0 if worst < 1e-9 else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'scratch-sliver'))
