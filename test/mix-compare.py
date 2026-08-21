#!/usr/bin/env python3
"""
Judge a mixed frame: within one frame, some triangles are drawn by the Storm
engine and some by Mesa's software rasteriser, into the SAME video-memory
surface.

Every mask is OBSERVED, never inferred from the final picture by geometry.
Each triangle is drawn in a colour of its own so a pixel names its writer.

The baseline for a triangle is its own SOLO run ON ITS OWN PATH -- ssoloN for
software, hsoloN for hardware.  The first version of this compared a software
triangle in a mixed frame against an all-software TWO-triangle run and found
5 pixels of difference on one shape.  That was the control being wrong, not
the driver: in a run where the neighbour also draws, the neighbour overwrites
every pixel they both claim, so the earlier triangle's observed mask is
already short by exactly the disputed pixels.  Hence:

    expected mask of triangle t  =  solo(t)  minus  union of solo(u), u > t
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from seam_oracle import load, cover        # noqa

TCOL = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)]
GX, GY = 60.37, 40.11                      # the shared depth plane's gradients


def masks(px, n):
    out = [set() for _ in range(n)]
    for k, c in px.items():
        for t in range(n):
            if c == TCOL[t]:
                out[t].add(k)
    return out


def main(d):
    bad = []
    print("혼합 프레임 -- 한 프레임 안에서 엔진과 소프트웨어가 같은 표면에 그린다\n")

    print("1. 분할이 실제로 일어났는가 (엔진에 닿은 삼각형 수)")
    for sh in 'ABCDE':
        line = "   도형 %s " % sh
        for md, want in (('allsoft', 0), ('mix', 1), ('mixrev', None)):
            p = os.path.join(d, 'mx-%s-%s.txt' % (sh, md))
            if not os.path.exists(p):
                continue
            m, _ = load(p)
            n = len(m['T'])
            exp = (n - 1) if want is None else want
            line += " %s=%d(기대 %d)" % (md, m['drawn'], exp)
            if m['drawn'] != exp:
                bad.append((sh, md, 'drawn', m['drawn'], exp))
        print(line)

    print("\n2. 각 경로가 서로를 건드리는가 (자기 경로의 solo 를 기준으로)")
    for sh in 'ABCDE':
        m0, _ = load(os.path.join(d, 'mx-%s-allsoft.txt' % sh))
        n = len(m0['T'])
        SS = [set(load(os.path.join(d, 'mx-%s-ssolo%d.txt' % (sh, t + 1)))[1])
              for t in range(n)]
        HS = [set(load(os.path.join(d, 'mx-%s-hsolo%d.txt' % (sh, t + 1)))[1])
              for t in range(n)]
        V = m0['V']
        O = [cover([V[i] for i in m0['T'][t]]) for t in range(n)]
        hchk = sum(len(HS[t] ^ O[t]) for t in range(n))
        if hchk:
            bad.append((sh, 'hsolo', 'oracle', hchk))
        for md, issoft in (('allsoft', lambda t: True),
                           ('mix', lambda t: t != 0),
                           ('mixrev', lambda t: t == 0)):
            meta, px = load(os.path.join(d, 'mx-%s-%s.txt' % (sh, md)))
            M = masks(px, n)
            P = [SS[t] if issoft(t) else HS[t] for t in range(n)]
            diffs = []
            for t in range(n):
                later = set().union(*[P[u] for u in range(t + 1, n)]) \
                        if t + 1 < n else set()
                diffs.append(len(M[t] ^ (P[t] - later)))
            gap = len(set().union(*P) - set().union(*M))
            print("   도형 %s %-8s hw solo=오라클 %d | 삼각형별 %s | 틈 %d"
                  % (sh, md, hchk, diffs, gap))
            if any(diffs) or gap:
                bad.append((sh, md, diffs, gap))

    print("\n3. 순서 -- 겹침은 관측된 solo 마스크의 교집합으로만 정의한다")
    for md in ('over', 'overrev'):
        p = os.path.join(d, 'mx-F-%s.txt' % md)
        if not os.path.exists(p):
            continue
        _, px = load(p)
        M = masks(px, 2)
        A = set(load(os.path.join(d, 'mx-F-solo1.txt'))[1])
        B = set(load(os.path.join(d, 'mx-F-solo2.txt'))[1])
        inter = A & B
        lost = len(inter - M[1])            # t1 is always the later one
        print("   도형 F %-8s 겹침 %d  나중 삼각형이 못 가진 화소 %d%s"
              % (md, len(inter), lost, "" if lost == 0 else "  <-- 순서 결함"))
        if lost:
            bad.append(('F', md, 'order', lost))

    print("\n4. 깊이 혼합 -- 하나의 분수 깊이 평면을 두 경로가 나눠 갖는다")
    for sh in 'ABCDE':
        p = os.path.join(d, 'mx-%s-mixz.txt' % sh)
        if not os.path.exists(p):
            continue
        z, clamp = {}, None
        for line in open(p):
            f = line.split()
            if line.startswith('Z '):
                z[(int(f[1]), int(f[2]))] = int(f[3])
            elif line.startswith('# depth starts clamped'):
                clamp = int(f[-1])
        if not z:
            print("   도형 %s (깊이 없음)" % sh)
            continue
        res = [z[k] - (20000.5 + GX * (k[0] + .5) + GY * (k[1] + .5)) for k in z]
        dev = 0.0
        for (x, y), v in z.items():
            for dd, g in (((1, 0), GX), ((0, 1), GY)):
                nb = (x + dd[0], y + dd[1])
                if nb in z:
                    dev = max(dev, abs((z[nb] - v) - g))
        print("   도형 %s 화소 %6d  잔차 %+0.3f  이웃차 이탈 %.3f  클램프 %s"
              % (sh, len(z), max(res, key=abs), dev, clamp))
        # 1 코드 이상 튀는 인접쌍이 있으면 hw/sw 경계에 깊이 불연속이 있다는 뜻
        if dev >= 1.0 or clamp:
            bad.append((sh, 'mixz', dev, clamp))

    print("\n5. 이상(오라클)에 대한 결손 -- 판정이 아니라 알려진 값")
    print("   두 래스터화 규칙이 공유 변의 소유권에서 다르면 실금이 남는다.")
    for sh in 'ABCDE':
        m0, _ = load(os.path.join(d, 'mx-%s-allsoft.txt' % sh))
        n = len(m0['T'])
        V = m0['V']
        U = set().union(*[cover([V[i] for i in m0['T'][t]]) for t in range(n)])
        row = "   도형 %s 이상 %6d |" % (sh, len(U))
        for md in ('allsoft', 'mix', 'mixrev'):
            P = set(load(os.path.join(d, 'mx-%s-%s.txt' % (sh, md)))[1])
            row += "  %s 안칠함 %d 넘게 %d" % (md, len(U - P), len(P - U))
        print(row)

    print()
    if not bad:
        print("=== nothing to report ===")
        return 0
    print("=== 문제 ===")
    for b in bad:
        print("   ", b)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'scratch-seam'))
