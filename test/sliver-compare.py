#!/usr/bin/env python3
"""
Where do the two paths disagree on near-degenerate geometry?

This maps a disagreement; it does not propose a threshold.  The review of
M17 was explicit that near a raster boundary the outcome is discrete and
phase-sensitive, so the bad set may be disconnected islands rather than
"some quantity below T" -- and a script written to find a threshold would
report one whether or not it generalises.  So this prints the map and, at
the end, says whether any single quantity in the family separates the two
sets at all.  "Nothing here separates them" is a result, and it is printed
as one.

Only triangles the WARP arm actually took are judged.  One it declined went
down another path and is evidence about that path, not about WARP.
"""
import os
import sys
import collections


def load(p):
    px = collections.defaultdict(set)
    who = {}
    for L in open(p):
        if L.startswith('P '):
            f = L.split()
            px[int(f[1])].add((int(f[2]), int(f[3])))
        elif L.startswith('T '):
            f = L.split()
            who[int(f[1])] = (int(f[3]), int(f[5]), int(f[7]), int(f[9]))
    return px, who


def family(p):
    out = {}
    for L in open(p):
        if L.startswith('F '):
            f = L.split()
            out[int(f[1])] = dict(alt=float(f[2]), length=float(f[3]),
                                  angle=float(f[4]), phase=float(f[5]))
    return out


def main(d):
    fam = family(os.path.join(d, 'family.txt'))
    pw, ww = load(os.path.join(d, 'fam-warp.txt'))
    ps, _ = load(os.path.join(d, 'fam-soft.txt'))

    judged, agree, differ = [], [], []
    declined = 0
    for i in sorted(fam):
        drawn, warp, traps, soft = ww.get(i, (0, 0, 0, 0))
        if warp == 0:
            declined += 1
            continue
        judged.append(i)
        (agree if pw[i] == ps[i] else differ).append(i)

    print("the family: %d triangles, %d taken by WARP, %d declined by it"
          % (len(fam), len(judged), declined))
    print("  of those taken: %d agree with software, %d differ"
          % (len(agree), len(differ)))
    if not judged:
        print("\nNOTHING JUDGED -- the WARP arm took none of the family")
        return 1
    print()

    # -- the map, by each axis on its own
    for key in ('alt', 'length', 'angle', 'phase'):
        tally = collections.defaultdict(lambda: [0, 0])
        for i in judged:
            tally[fam[i][key]][0 if i in set(agree) else 1] += 1
        print("  by %-7s %s" % (key, "  ".join(
            "%g:%d/%d" % (v, t[1], t[0] + t[1])
            for v, t in sorted(tally.items()))))
    print("      (value: differing / judged)")
    print()

    # -- does any single axis separate them?
    sep = []
    for key in ('alt', 'length', 'angle', 'phase'):
        good = set(fam[i][key] for i in agree)
        bad = set(fam[i][key] for i in differ)
        if good and bad and not (good & bad):
            sep.append(key)
    if sep:
        print("  a single axis separates the two sets: %s" % ", ".join(sep))
    else:
        print("  NO single axis separates them: every value that appears in")
        print("  the differing set also appears in the agreeing one.")
        print("  A threshold on any one of these would refuse triangles the")
        print("  tier draws correctly and admit ones it does not.")

    # -- and the shape of the difference, which is not the same question
    tot_miss = tot_extra = 0
    for i in differ:
        tot_miss += len(ps[i] - pw[i])
        tot_extra += len(pw[i] - ps[i])
    if differ:
        print()
        print("  over the %d differing: %d pixels software drew and WARP did"
              " not, %d the other way" % (len(differ), tot_miss, tot_extra))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'scratch-sliver'))
