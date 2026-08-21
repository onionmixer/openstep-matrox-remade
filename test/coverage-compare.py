#!/usr/bin/env python3
"""
Compare what the two rasterisers covered, and judge both against the rule.

openstep-mga-mesa-coverage-test.c prints coverage as per-row runs; run it once
with OSMGA_MESA_ACCEL=0 and once without, for each shape, and point this at
the results.

Two things this deliberately does NOT do.  It does not treat the software
rasteriser as the definition of right: the GL rule is written out here in
integers and both paths are scored against it.  And it does not measure areas
-- a pixel count cannot tell a half-pixel edge rule from a hole in the middle,
which is what started this.

Two mistakes of my own are fixed here and worth naming, because both made a
defect look like a pass:

  * The first version called a difference "on the edge" if it sat within one
    pixel of the software run's end.  That builds a one-pixel tolerance into
    the predicate, so the nine rows where the hardware was two pixels short
    were reported as interior differences -- a wrong answer in both
    directions.  Runs are single and contiguous, so a difference is at an end
    by construction; what is worth reporting is the distribution of how far.

  * It took each path's covered row range from that path's own output, so a
    row the hardware dropped entirely fell outside the range being checked
    and was never counted as missing.  Ranges are now compared across paths.

  * It asked for one run per row on the two-colour shapes, where two runs are
    what correctness looks like -- and duly reported every row of both as
    broken.  What matters on those is that the runs are adjacent, with no
    clear colour between them; that is the crack test.  A single-run rule is
    only meaningful where a single colour was drawn.
"""
import sys, os, re, collections

def load(path):
    meta, rows = {}, {}
    for line in open(path):
        line = line.rstrip('\n')
        if line.startswith('# counters'):
            for k, v in re.findall(r'(\w+)=(\d+)', line): meta[k] = int(v)
        elif line.startswith('# mode'):
            meta['mode'] = line.split()[-1]
        elif line.startswith('R '):
            f = line.split(); y = int(f[1]); runs = []
            for t in f[2:]:
                c, span = t.split(':'); a, b = span.split('-')
                runs.append((c, int(a), int(b)))
            rows[y] = runs
    return meta, rows

def pixels(rows):
    d = {}
    for y, runs in rows.items():
        for c, a, b in runs:
            for x in range(a, b + 1): d[(x, y)] = c
    return d

def cover(verts, W, H):
    """The GL rule in integers: sample the pixel centre, top-left tie-break.
    Doubling the sample point (x+1/2, y+1/2) clears the halves."""
    v = [(int(x), int(y)) for x, y in verts]
    (ax,ay),(bx,by),(cx,cy) = v
    if (bx-ax)*(cy-ay) - (by-ay)*(cx-ax) < 0:
        (bx,by),(cx,cy) = (cx,cy),(bx,by)
    E = [((ax,ay),(bx,by)), ((bx,by),(cx,cy)), ((cx,cy),(ax,ay))]
    out = {}
    for y in range(max(0, min(ay,by,cy)-1), min(H, max(ay,by,cy)+2)):
        for x in range(max(0, min(ax,bx,cx)-1), min(W, max(ax,bx,cx)+2)):
            ok = True
            for (x0,y0),(x1,y1) in E:
                e = (x1-x0)*(2*y+1-2*y0) - (y1-y0)*(2*x+1-2*x0)
                if e < 0: ok = False; break
                if e == 0:
                    dx, dy = x1-x0, y1-y0
                    if not ((dy == 0 and dx < 0) or dy < 0): ok = False; break
            if ok: out.setdefault(y, []).append(x)
    return {y: (min(v), max(v)) for y, v in out.items() if v}

ONE_COLOUR = (1, 2, 3, 4, 5, 8, 9)    # 6 and 7 are drawn in two colours

SHAPES = {
    1: ("flat-bottomed, no split of its own", [(40,40),(200,40),(120,180)]),
    2: ("split at the middle y",              [(40,40),(200,90),(120,180)]),
    3: ("clipped by Mesa",                    None),
    4: ("shape 1, winding reversed",          [(120,180),(200,40),(40,40)]),
    5: ("quad, one colour",                   None),
    6: ("quad, two colours, order A",         None),
    7: ("quad, two colours, order B",         None),
    8: ("shape 1 moved half a pixel",         None),
    9: ("shape 1 moved 37/128 of a pixel",    None),
}
W, H = 320, 240

def main(d, passes=3):
    S = os.path.join(d, '%s-%d-%d.txt')
    fail = []

    print("== the passes agree ==")
    for mode in ('hw', 'sw'):
        for n in SHAPES:
            got = [load(S % (mode, n, p))[1] for p in range(1, passes + 1)]
            if any(g != got[0] for g in got):
                fail.append("%s shape %d differs between passes" % (mode, n))
        print("   %s: %s" % (mode, "all shapes reproduce"
              if not any(f.startswith(mode) for f in fail) else "SOME DIFFER"))

    print("\n== the engine, not the fallback, drew the accelerated runs ==")
    for n in SHAPES:
        m, _ = load(S % ('hw', n, 1))
        ok = (m['software'] == 0 and m['declined'] == 0
              and m['unsupported'] == 0 and m['drawn'] > 0)
        if not ok: fail.append("shape %d was not purely hardware: %r" % (n, m))
        print("   shape %d: %-9s drawn=%d software=%d unsupported=%d declined=%d%s"
              % (n, m['mode'], m['drawn'], m['software'], m['unsupported'],
                 m['declined'], "" if ok else "   <-- NOT PURE"))

    print("\n== each covered row is covered without a gap ==")
    for mode in ('hw', 'sw'):
        for n in SHAPES:
            _, rows = load(S % (mode, n, 1))
            cov = sorted(y for y in rows if rows[y])
            if not cov: continue
            # One colour: one run.  Two colours: two runs are right, and what
            # would be wrong is clear colour showing between them.
            broken = [y for y in cov
                      if (len(rows[y]) != 1) if n in ONE_COLOUR] if n in ONE_COLOUR else [
                      y for y in cov
                      if any(rows[y][i][2] + 1 != rows[y][i+1][1]
                             for i in range(len(rows[y]) - 1))]
            empty = [y for y in range(cov[0], cov[-1] + 1) if not rows.get(y)]
            if broken or empty:
                fail.append("%s shape %d: broken=%s empty-inside=%s"
                            % (mode, n, broken[:6], empty[:6]))
            print("   %s shape %d: rows %d..%d  broken=%d  empty inside=%d%s"
                  % (mode, n, cov[0], cov[-1], len(broken), len(empty),
                     "" if not (broken or empty) else "   <-- DEFECT"))

    print("\n== rows one path covered and the other did not ==")
    for n in SHAPES:
        hw = load(S % ('hw', n, 1))[1]; sw = load(S % ('sw', n, 1))[1]
        h = set(y for y in hw if hw[y]); s = set(y for y in sw if sw[y])
        if h != s:
            fail.append("shape %d: rows only in sw %s, only in hw %s"
                        % (n, sorted(s - h)[:6], sorted(h - s)[:6]))
        print("   shape %d: only software %-12s only hardware %s"
              % (n, sorted(s - h)[:6] or '-', sorted(h - s)[:6] or '-'))

    print("\n== winding must not change coverage, exactly, inside each path ==")
    for mode in ('hw', 'sw'):
        a = load(S % (mode, 1, 1))[1]; b = load(S % (mode, 4, 1))[1]
        if a != b: fail.append("%s: winding changes coverage" % mode)
        print("   %s: %s" % (mode, "identical" if a == b else "DIFFERS"))

    print("\n== the shared diagonal: cracks and overlaps ==")
    for mode in ('hw', 'sw'):
        q = load(S % (mode, 5, 1))[1]
        gaps = [y for y in q if len(q[y]) > 1]
        if gaps: fail.append("%s: crack along the diagonal at %s" % (mode, gaps[:6]))
        p6 = pixels(load(S % (mode, 6, 1))[1]); p7 = pixels(load(S % (mode, 7, 1))[1])
        order = [k for k in set(p6) | set(p7) if p6.get(k) != p7.get(k)]
        if order: fail.append("%s: %d pixels depend on submission order" % (mode, len(order)))
        print("   %s: rows with a gap = %d ; pixels that depend on the order = %d"
              % (mode, len(gaps), len(order)))

    print("\n== how far apart are the two, and on which side ==")
    for n in SHAPES:
        hw = load(S % ('hw', n, 1))[1]; sw = load(S % ('sw', n, 1))[1]
        dl = collections.Counter(); dr = collections.Counter()
        for y in set(hw) & set(sw):
            if not hw[y] or not sw[y]: continue
            # the extent of the whole row, so a two-colour row is not read as
            # its first colour's run
            dl[hw[y][0][1] - sw[y][0][1]] += 1
            dr[hw[y][-1][2] - sw[y][-1][2]] += 1
        print("   shape %d: left  %s" % (n, dict(sorted(dl.items()))))
        print("             right %s" % dict(sorted(dr.items())))

    print("\n== against the rule itself, not against each other ==")
    for n, (what, verts) in sorted(SHAPES.items()):
        if verts is None: continue
        ref = cover(verts, W, H)
        for mode, name in (('sw', 'software'), ('hw', 'hardware')):
            rows = load(S % (mode, n, 1))[1]
            got = {y: (r[0][1], r[0][2]) for y, r in rows.items() if r}
            same = sum(1 for y in ref if got.get(y) == ref[y])
            print("   shape %d %-8s: %3d/%3d rows match the rule exactly%s"
                  % (n, name, same, len(ref), "" if same != len(ref) else "   EXACT"))

    print("\n=== %s ===" % ("nothing to report" if not fail
                            else "%d thing(s) to look at" % len(fail)))
    for f in fail: print("   ! " + f)
    return 1 if fail else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'scratch-cov'))
