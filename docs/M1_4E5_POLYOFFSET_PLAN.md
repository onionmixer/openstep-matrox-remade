# M1-4E5 — `glPolygonOffset`

## 1. 지금

```c
    if (ctx->Polygon.OffsetFill)      return NULL;
```

데칼, 동일평면 겹치기, 그림자 볼륨 — 전부 소프트웨어로 간다.

## 2. 엔진에는 오프셋 장치가 없다.  필요도 없다

`glPolygonOffset` 은 깊이에 상수를 더하는 것이고, **드라이버가 깊이 평면을
이미 푼다.**  Mesa 소프트웨어도 그렇게 한다 — 하드웨어 기능이 아니라
래스터라이저의 산술이다.

`vbrender.c:101-118`:

```c
   if (c*c > 1e-16) {
      ac = a / c;  bc = b / c;
      if (ac<0) ac = -ac;   if (bc<0) bc = -bc;
      m = MAX2(ac, bc);
      offset = m * OffsetFactor + OffsetUnits;
   }
```

그리고 `tritemp.h:718` 이 그것을 **창 z 에 그대로 더한다**:

```c
   GLfloat z0 = VB->Win.data[vLower][2] + ctx->PolygonZoffset;
```

`a/c` 와 `b/c` 는 창 좌표에서의 `dz/dx`, `dz/dy` 다.  즉 `m` 은 **화소당 깊이
코드의 최대 기울기**이고, `units` 는 **코드 단위**로 더해진다 — 16 비트
버퍼에서 `r` 이 1 코드라는 뜻이다.

## 3. 단위를 맞춘다

드라이버는 `z` 를 `Win[2] × 256` 으로 담는다 (`osmgaFix`, `SUBBITS 8`).
그러니 빌더가 푼 깊이 평면의 `dx` 는 **코드×256 / 화소**다.

```
   m        = max(|zplane.dx|, |zplane.dy|) / 256        코드/화소
   offset   = m·factor + units                           코드
   고정소수 = offset × 256 = max|dz|·factor + units·256
```

그래서 빌더가 할 일은 한 줄이다:

```
   zplane.at_a += max(|zplane.dx|, |zplane.dy|)·factor + units·256
```

**Mesa 와 같은 식을, 같은 단위로.**  재현할 근사가 없다.

## 4. 퇴화 평면

Mesa 는 `c*c > 1e-16` 일 때만 오프셋을 준다 — 모서리로 선 폴리곤은 `c ≈ 0`
이라 `a/c` 가 발산한다.  빌더도 같은 자리에서 같은 판단을 해야 한다.
빌더는 이미 `den`(삼각형 넓이의 두 배)으로 퇴화를 가려내므로 그 자리에 붙인다.

## 5. `OffsetPoint`/`OffsetLine`

이 백엔드는 삼각형만 그린다.  `GL_POLYGON_OFFSET_FILL` 만 보고 나머지 둘은
무시한다 — Mesa 도 `PolygonZoffset` 을 `OffsetFill` 로만 세운다.

## 6. 어떻게 재나

1. **같은 평면 두 장.**  같은 깊이의 사각형을 둘 그리고, 뒤엣것에 오프셋을
   준다.  오프셋이 없으면 `GL_LESS` 에서 뒤엣것이 안 보이고, 있으면 보인다
2. **부호.**  양의 `units` 는 **멀어지는** 방향이다 (GL: 깊이가 커진다).
   음수면 가까워진다.  둘 다 시험한다
3. **기울기 항.**  기울어진 사각형에 `factor` 만 주고 `units = 0` 으로 —
   평평한 것에는 효과가 없고 기울어진 것에는 있어야 한다
4. **소프트웨어와 화소 단위 비교.**  같은 식이므로 **차이가 0 이어야 한다**

## 7. 공허하지 않게

- 오프셋 **없는** 대조군이 반드시 있어야 한다.  "보인다"가 오프셋 덕인지
  애초에 보였는지 갈라야 한다
- `factor` 시험은 **평평한 대조군**이 있어야 한다 — 평평한 것에도 움직이면
  기울기를 안 쓰고 상수를 쓴 것이다
- 깊이 버퍼를 직접 읽어 **얼마나** 움직였는지 본다.  색만 보면 방향만 안다
- 소프트웨어와 0 이 아니면 그건 단위를 틀린 것이다

## 8. 교차검토에 물을 것

- §3 의 단위 환산이 맞나.  `Win[2]` 가 정말 코드 단위인가, `[0,1]` 인가
- Mesa 의 `m` 이 정말 `max(|dz/dx|, |dz/dy|)` 인가 — `a`, `b`, `c` 의 정의를
  확인해 달라
- §4 의 퇴화 조건을 빌더의 어느 값으로 판단해야 하나
- 오프셋이 깊이를 범위 밖으로 밀 수 있나.  그러면 어떻게 되나
- 이 변경이 처음 돌게 하는 조합

## 9. codex 교차검토 판정 (13차)

| codex 주장 | 검증 방법 | 결과 |
|---|---|---|
| `Win[2]` 는 코드 공간이 맞다 | `context.c:219`, `WindowMap.m[Sz] = 0.5·DepthMaxF` | ✅사실 — 내가 먼저 확인 |
| **그러나 빌더가 이미 256 으로 나눠 놓았다.** `d1`,`d2`,`at_a` 전부 `/SUBONE` 이므로 평면은 **코드/화소**다.  `units·256` 은 **256 배 틀렸다** | `Triangle.c:826, 833` 을 열어 확인 | ✅**사실 — 내 §3 이 틀렸다** |
| `m` 은 `max(\|dz/dx\|, \|dz/dy\|)` 가 맞다.  `(a,b,c) = e × f` | `vbrender.c:283, 300` — 내가 먼저 확인 | ✅사실 |
| **슬라이버 평탄화가 나중에 온다.**  평탄화된 `dx` 로 계산하면 factor 항이 0 이 되는데 Mesa 는 원래 평면으로 계산한다 | `Triangle.c:844` 가 `at_a` 설정 뒤에 온다 | ✅**사실 — 원래 기울기로 계산해야 한다** |
| 퇴화 판정이 다르다: Mesa 는 **스냅되지 않은** `Win` 의 `c·c > 1e-16`, 빌더의 `den` 은 스냅된 것 | 맞다.  `Hook.c:245` 가 `Win[2]` 를 1/256 로 스냅한다 | ✅**사실 — 내 §4 가 틀렸다** |
| **범위를 벗어나면 갈린다.** Mesa 는 자르지 않고 32 비트로 비교하는데 빌더는 사다리꼴 씨앗을 **포화**시킨다.  추측하지 말고 **거절하라** | `Triangle.c:692` 의 포화와 `depth.c:180` 을 확인 | ✅채택 |
| "차이가 0 이 아니면 단위가 틀린 것"은 과한 주장이다 | 맞다 — 스냅·슬라이버·반올림도 원인일 수 있다 | ✅채택 |
| 보이게 되는 시험은 **음수** `units` 여야 한다 | 맞다 | ✅채택 |

## 10. 고친 설계 — 훅이 계산하고 빌더는 더하기만 한다

세 문제가 하나의 답으로 모인다.  단위·평탄화·퇴화 판정이 전부 "빌더의 값으로
계산하면 Mesa 와 다르다"이므로, **Mesa 자신의 값으로 계산한다**:

```
   훅에서 (스냅 전 Win 으로):
      ex,ey,ez = win[v1] − win[v0]
      fx,fy,fz = win[v2] − win[v0]
      a = ey·fz − ez·fy,  b = ez·fx − ex·fz,  c = ex·fy − ey·fx
      c·c > 1e-16 이면  offset = max(|a/c|,|b/c|)·factor + units,  아니면 0
   빌더는:
      zplane.at_a += offset          (코드 단위, 그대로)
```

이러면 **식도 값도 Mesa 의 것**이다 — 알파 기준값을 Mesa 의 바이트로 실은
것과 같은 방식이고, 재현할 산술이 없다.

평탄화 문제도 사라진다: `m` 이 빌더의 `zplane.dx` 를 안 보고 원래 평면을 보므로
평탄화 전후가 무관하다.

## 11. 범위

오프셋이 깊이를 `[0,65535]` 밖으로 밀면 Mesa 는 32 비트로 비교하고 빌더는
포화시킨다 — **다른 그림**이다.  그러니 빌더가 오프셋 뒤의 평면이 프리미티브
위에서 표현 가능한지 보고, 아니면 `UNSUPPORTED` 를 돌려 소프트웨어로 보낸다.
포화를 하나 더 넣는 것은 추측이다.

## 12. 진입점은 하나다

병렬 진입점(`...Offset` 을 따로 두고 기존 것은 0 으로 전달)을 먼저 썼다가
버렸다.  **시험이 흔들리는 것을 피하려고 고른 방법이었고, 그건 이유가 못
된다** — 기본값이 있는 편의 함수는 나중 호출자가 오프셋을 **잊게** 만든다.

`OSMGAMesaBuildTriangleTex` 와 `OSMGAMesaBuildTriangle` 이 둘 다
`double zoffset` 을 받는다.  호출부 열한 곳이 전부 자기 값을 말하고, 시험들은
`0.0 /* no polygon offset */` 이라고 적는다.  잊으면 **컴파일이 안 된다.**

## 13. 됐다 — python 이 말한 그대로

```
1. the units term, on a flat polygon
   units  -8.0  engine 32760  software 32760
   units  +0.0  engine 32768  software 32768
   units  +8.0  engine 32776  software 32776
   the spread between -8 and +8 is 16 codes, python says 16
   ok  a positive offset moves AWAY, as GL says

2. the factor term, which must see the slope
   flat   factor 0 -> 32768   factor 4 -> 32768   (moved 0)
   sloped factor 0 -> 41052   factor 4 -> 41797   (moved 745, python says 745)

3. the decal
   ok  without an offset it does not show
   ok  with a negative one it does
```

python 이 먼저 말한 것:

```
   flat quad at z = 0        window depth 32767.5 codes
   slope 0 -> -0.5 over 88px  186.18 codes a pixel
   factor 4                   744.72 codes -> 745
```

**세 숫자가 전부 맞았다.**  단위 환산이 틀렸다면 256 배로 어긋났을 것이고,
기울기를 평탄화 뒤에 읽었다면 2 번이 0 으로 나왔을 것이다.

### 13-1 대조군이 하는 일

- 평평한 폴리곤이 `factor` 에 **안 움직이는** 것이 기울기를 상수와 가른다
- 데칼의 오프셋 **없는** 경우가 "오프셋이 한 일"임을 세운다
- 소프트웨어를 강제한 짝이 각 경우마다 붙어 있다

## 14. 회귀

```
   기준 장면 14      SCENES_MOVED=0
   블렌드 네 장면    BLEND_SCENES_BAD=0
   tpo tat tbf tdf texdraw reach tr tc tv trh trs tbg   전부 0 failing
```
