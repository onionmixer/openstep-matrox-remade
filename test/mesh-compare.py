#!/usr/bin/env python3
"""
Judge a long mesh.  One run, every triangle attributed by its colour.
"""
import sys, os
from fractions import Fraction as F
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from seam_oracle import cover   # noqa

PAL = [(255,0,0),(0,255,0),(0,0,255),(255,255,0),
       (255,0,255),(0,255,255),(255,255,255)]

def readmesh(p):
    V, T = [], []
    it = iter(open(p).read().split('\n'))
    for line in it:
        f = line.split()
        if not f or f[0] == '#': continue
        if f[0] == 'V':
            for _ in range(int(f[1])):
                g = next(it).split(); V.append((F(g[0]), F(g[1])))
        elif f[0] == 'T':
            for _ in range(int(f[1])):
                g = next(it).split(); T.append(tuple(int(x) for x in g))
    return V, T

def readpix(p):
    px, meta = {}, {}
    for line in open(p):
        f = line.split()
        if line.startswith('# counters'):
            import re
            for k, v in re.findall(r'(\w+)=(\d+)', line): meta[k] = int(v)
        elif line.startswith('# select'):
            meta['hard'], meta['soft'] = int(f[3]), int(f[5])
        elif line.startswith('P '):
            px[(int(f[1]), int(f[2]))] = (int(f[3]), int(f[4]), int(f[5]))
    return meta, px

def main(d):
    bad = []
    print("긴 메시 -- 한 번 그리고 삼각형마다 색으로 귀속시켜 판정한다\n")
    for name in ('mesh', 'mesh12'):
        mp = os.path.join(d, '%s.txt' % name)
        if not os.path.exists(mp): continue
        V, T = readmesh(mp)
        O = [cover([V[t[0]], V[t[1]], V[t[2]]]) for t in T]
        # 오라클은 규칙상 서로 겹치지 않는다 -- 먼저 그것부터 확인한다,
        # 아니면 아래 판정 전체가 무의미하다
        seen, dbl = set(), 0
        for m in O:
            dbl += len(seen & m); seen |= m
        uni = seen
        print("  %s: 삼각형 %d, 오라클 합집합 %d 화소, 오라클끼리 겹침 %d"
              % (name, len(T), len(uni), dbl))
        if dbl:
            bad.append((name, 'oracle-overlap', dbl)); continue
        for md in ('hw', 'sw', 'mix'):
            p = os.path.join(d, '%s-%s.txt' % (name, md))
            if not os.path.exists(p): continue
            meta, px = readpix(p)
            # 색으로 나눈 관측 마스크
            obs = {}
            for k, c in px.items(): obs.setdefault(c, set()).add(k)
            wrong = 0
            for i, t in enumerate(T):
                want = O[i]
                got = obs.get(PAL[t[3] % len(PAL)], set()) & (want | (uni - want))
                # 이 색을 쓴 삼각형이 여럿이므로, 이 삼각형 몫만 떼어낸다:
                # 같은 색 삼각형끼리는 꼭짓점조차 공유하지 않으므로
                # 자기 오라클 근방 밖은 다른 삼각형의 것이다
                near = set()
                for (x, y) in want: near.add((x, y))
                gotmine = obs.get(PAL[t[3] % len(PAL)], set()) & near
                wrong += len(want - gotmine)
            gap = len(uni - set(px))
            extra = len(set(px) - uni)
            print("     %-4s 엔진 %4d/%d  안칠함 %4d  넘게 %3d  삼각형 몫 부족 %4d"
                  % (md, meta.get('drawn', -1), len(T), gap, extra, wrong))
            if md == 'hw' and (gap or extra or wrong):
                bad.append((name, md, gap, extra, wrong))
    print()
    print("=== nothing to report ===" if not bad else "=== 문제 ===")
    for b in bad: print("   ", b)
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'scratch-mesh'))
