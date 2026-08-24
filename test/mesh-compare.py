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

# mesh16 은 512 삼각형이고 덤프도 있었는데 이 목록이 둘로 박혀 있어 한 번도
# 채점된 적이 없었다.
MESHES = ('mesh', 'mesh12', 'mesh16')

# (안칠함, 넘게, 삼각형 몫 부족)
#
# 영이 아닌 수가 곧 결함은 아니다.  여기 적힌 것은 전부 이미 설명된 값이고,
# 판정은 "영인가" 가 아니라 "움직였는가" 다 -- 장면 기준선이 쓰는 방식과 같다.
#
#   mesh12 의 hw 값은 좌표 양자화다.  그 메시의 꼭짓점이 1/256 격자 밖에 있어
#   신탁과 어긋난다  (REMAINING_WORK §0)
#   sw 와 mix 의 값은 혼합 프레임의 실금이다 -- 하드웨어 삼각형과 소프트웨어
#   삼각형이 변을 공유하면 폭 1의 빈 화소가 남는다  (M1_4B6 §3)
#
# 격자 위에 있는 mesh 와 mesh16 은 hw 가 정확히 0 이다.
BASELINE = {
    ('mesh',   'hw'):  (0,  0,  0),
    ('mesh',   'sw'):  (0,  0,  6),
    ('mesh',   'mix'): (6,  0,  6),
    ('mesh12', 'hw'):  (0,  0, 52),
    ('mesh12', 'sw'):  (0,  0, 12),
    ('mesh12', 'mix'): (35, 0, 58),
    ('mesh16', 'hw'):  (0,  0,  0),
    ('mesh16', 'sw'):  (0,  0,  5),
    ('mesh16', 'mix'): (5,  0,  5),
}



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
    for name in MESHES:
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
            drawn = meta.get('drawn', -1)
            soft = meta.get('software', -1)
            print("     %-4s 엔진 %4d/%d  소프트 %4d  안칠함 %4d  넘게 %3d"
                  "  삼각형 몫 부족 %4d"
                  % (md, drawn, len(T), soft, gap, extra, wrong))

            # 어느 경로가 그렸는지부터.  이것이 없으면 강제가 통째로 고장난
            # 채로도 그림만 맞으면 통과한다 -- 실제로 그런 상태였다.
            if md == 'hw' and soft != 0:
                bad.append((name, md, 'hw 인데 소프트웨어가 %d 개' % soft))
            if md == 'sw' and (drawn != 0 or soft != len(T)):
                bad.append((name, md,
                            'sw 인데 엔진 %d 소프트 %d (0 과 %d 이어야 한다)'
                            % (drawn, soft, len(T))))
            if md == 'mix':
                if drawn + soft != len(T):
                    bad.append((name, md, '엔진+소프트가 %d, 삼각형은 %d'
                                % (drawn + soft, len(T))))
                elif drawn == 0 or soft == 0:
                    bad.append((name, md,
                                '섞이지 않았다 -- 엔진 %d 소프트 %d'
                                % (drawn, soft)))

            # 그리고 그림.  세 모드 전부 -- 예전에는 hw 만 실패시켜서,
            # 강제된 경로가 완전히 망가져도 성공이 나왔다.
            want = BASELINE.get((name, md))
            if want is None:
                print("       (기준값 없음 -- 그림은 판정하지 않는다)")
            elif (gap, extra, wrong) != want:
                bad.append((name, md, '그림이 기준값 %s 에서 %s 로 움직였다'
                            % (want, (gap, extra, wrong))))
    print()
    print("=== nothing to report ===" if not bad else "=== 문제 ===")
    for b in bad: print("    %s %s: %s" % b)
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'scratch-mesh'))
