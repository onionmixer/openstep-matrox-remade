# M1-4E3 — 블렌드 인자를 전부 받는다

## 1. 지금은 한 쌍뿐이다

```c
    if (ctx->Color.BlendSrcRGB != GL_SRC_ALPHA)           return NULL;
    if (ctx->Color.BlendDstRGB != GL_ONE_MINUS_SRC_ALPHA) return NULL;
    if (ctx->Color.BlendSrcA   != GL_SRC_ALPHA)           return NULL;
    if (ctx->Color.BlendDstA   != GL_ONE_MINUS_SRC_ALPHA) return NULL;
```

가산 블렌딩(`GL_ONE, GL_ONE`), 곱셈 블렌딩(`GL_DST_COLOR, GL_ZERO`),
사전곱 합성(`GL_ONE, GL_ONE_MINUS_SRC_ALPHA`) — 전부 여기서 걸린다.

## 2. 엔진의 집합이 GL 1.1 의 집합과 같다

`mgareg_flags.h:41-59`:

```
   원천 (비트 0-3)                    목적지 (비트 4-7)
   0 ZERO                             0 ZERO
   1 ONE                              1 ONE
   2 DST_COLOR                        2 SRC_COLOR
   3 ONE_MINUS_DST_COLOR              3 ONE_MINUS_SRC_COLOR
   4 SRC_ALPHA                        4 SRC_ALPHA
   5 ONE_MINUS_SRC_ALPHA              5 ONE_MINUS_SRC_ALPHA
   6 DST_ALPHA                        6 DST_ALPHA
   7 ONE_MINUS_DST_ALPHA              7 ONE_MINUS_DST_ALPHA
   8 SRC_ALPHA_SATURATE
```

**GL 1.1 이 원천에 허용하는 아홉 개, 목적지에 허용하는 여덟 개가, 그 순서
그대로다.**  역할까지 맞는다 — 원천에는 `DST_COLOR`, 목적지에는 `SRC_COLOR`.
이 하드웨어는 그 표를 보고 만들어졌다.

## 3. 커널은 이미 열려 있다

```c
    #define OSMGA_HW3D_AC_SRC_MAX   8UL
    #define OSMGA_HW3D_AC_DST_MAX   7UL
```

검증기는 그 범위만 확인한다.  **Mesa 만 바뀌고 재부팅이 필요 없다.**

## 4. 그런데 인자는 **하나의 쌍**이다

엔진의 `ALPHACTRL` 은 원천 인자 하나와 목적지 인자 하나를 갖는다 — 색과
알파에 **따로** 줄 수 없다.  GL 은 `glBlendFuncSeparateEXT` 로 따로 줄 수
있고 이 Mesa 에 그 함수가 있다.

그러니 관문은 **네 값이 두 쌍으로 같을 것**을 계속 요구해야 한다:

```
   BlendSrcRGB == BlendSrcA   그리고   BlendDstRGB == BlendDstA
```

지금은 넷을 각각 한 값으로 못 박아서 이 조건이 우연히 성립했다.  넓히면
**명시적으로** 요구해야 하고, 그러지 않으면 알파 인자가 조용히 색 인자로
바뀐다.

## 5. `GL_SRC_ALPHA_SATURATE` 는 따로 본다

GL 의 정의는 색에 `min(As, 1−Ad)` 이고 **알파에는 1** 이다 — 즉 채널마다
다른 인자다.  엔진의 인자는 하나뿐이므로 그 특별 규칙이 있는지 알 수 없다.

`§4` 의 규칙과도 충돌한다: `SRC_ALPHA_SATURATE` 를 알파 인자로 쓰는 것은 GL 이
허용하지 않으므로 `BlendSrcA` 가 그 값일 수 없고, 따라서 두 쌍이 같을 수
없다.  **처음에는 거절한다.**  나중에 잰다.

## 6. 어떻게 재나

`blend-compare.py` 와 네 장면의 구조를 그대로 쓴다 — 다만 오라클이 인자에
따라 달라진다:

```
   out = Cs·f(src) + Cd·f(dst)     한 번만 반올림
```

엔진의 반올림은 이미 확정돼 있다(`(x + 127)/255`, 262144 표본).  인자만
바뀌므로 오라클은 인자 함수를 표로 두면 된다.

시험할 쌍:

| 쌍 | 무엇 |
|---|---|
| `ONE, ONE` | 가산.  포화가 있는지 본다 |
| `SRC_ALPHA, ONE` | 알파 있는 가산 |
| `DST_COLOR, ZERO` | 곱셈 |
| `ZERO, SRC_COLOR` | 그 반대 |
| `ONE, ONE_MINUS_SRC_ALPHA` | 사전곱 |
| `DST_ALPHA, ONE_MINUS_DST_ALPHA` | 목적지 알파를 읽는 쌍 |
| `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` | 지금 것 — 움직이면 안 된다 |

## 7. 공허하지 않게

- **포화**를 반드시 시험한다.  `ONE, ONE` 은 255 를 넘길 수 있고, 엔진이
  자르는지 감싸는지는 잰 적이 없다.  감싸면 그림이 완전히 다르다
- 목적지 알파를 읽는 쌍은 목적지 알파가 **0 도 255 도 아니어야** 한다
- 각 쌍마다 하드웨어와 소프트웨어를 **각자의 산술로** 채점한다
- 지금 쌍(`SRC_ALPHA, ONE_MINUS_SRC_ALPHA`)의 네 장면 기준값이 **안 움직여야**
  한다 — 움직이면 매핑이 아니라 다른 것을 건드린 것이다

## 8. 교차검토에 물을 것

- §2 의 "순서까지 같다"가 정말 맞나.  하나라도 어긋나면 조용히 틀린 그림이다
- §4 의 동등 요구가 충분한가.  `glBlendFuncSeparateEXT` 말고 다른 길로
  갈릴 수 있나
- 포화가 어디서 일어나나 — 곱한 뒤인가 더한 뒤인가.  어떻게 가르나
- Mesa 소프트웨어의 인자 계산과 엔진의 것이 갈릴 만한 쌍
- `SRC_ALPHA_SATURATE` 를 거절하는 것이 맞나

## 9. codex 교차검토 판정 (11차)

| codex 주장 | 검증 방법 | 결과 |
|---|---|---|
| 방향별 매핑은 의미상 일대일이다 — 원천 아홉, 목적지 여덟이 GL 1.1 과 정확히 맞는다 | `mgareg_flags.h:41` 와 `blend.c:63, 91` 을 열어 확인 | ✅사실 |
| **"순서까지 같다"는 GLenum 숫자 순서를 뜻하면 오해다** (`SRC_ALPHA=0x302`, `DST_COLOR=0x306`).  명시적 `switch` 두 개를 쓰고 열거값 순서에서 유도하지 마라 | 맞다.  애초에 그럴 생각은 없었으나 문구가 위험하다 | ✅채택 |
| **`GL_SRC_ALPHA_SATURATE` 는 이 Mesa 에서 알파 인자로 합법이다** — 내 거절 근거가 틀렸다 | `blend.c:71, 205` 열어 확인.  Mesa 가 RGB 에 `min(As, 1−Ad)`, 알파에 정확히 1 을 쓴다(`blend.c:550, 616`) | ✅**사실 — 근거를 고친다.** 거절은 유지하되 이유는 "엔진이 그 특별 규칙을 갖는지 안 쟀다" |
| 네 인자 동등 요구로 충분하다.  이 Mesa 는 방정식이 하나뿐이고 `glBlendFunc` 는 넷을 함께 세운다 | `types.h:361`, `blend.c:76` | ✅사실 |
| **`GL_CONSTANT_*` 를 명시적으로 거절해야 한다** — `EXT_blend_color` 가 기본으로 켜져 있다 | `extensions.c:58`, `blend.c:72` | ✅채택 — 화이트리스트로 |
| Mesa 의 `DST_COLOR,ZERO` 와 `ZERO,SRC_COLOR` 는 **`>>8`(÷256) 절단** 경로로 간다 — `/255` 엔진과 갈릴 가능성이 가장 높다 | `blend.c:849, 469` | ✅채택 — 그 쌍을 반드시 채점 |
| "곱마다 자르기 대 합친 뒤 자르기"는 **구별 불가**다 (인자가 1 이하라 개별 곱이 255 를 못 넘는다).  의미 있는 것은 **곱마다 반올림**하는지다 | 맞다.  `SRC_ALPHA,DST_ALPHA` 에 1 over 1, 알파 둘 다 128 → 한 번이면 1, 곱마다면 2 | ✅**채택 — 내 시험보다 낫다** |
| 시험 쌍이 인코딩을 다 안 덮는다 (`1−DST_COLOR`, `1−DST_ALPHA`, `SATURATE`, `1−SRC_COLOR`, `DST_ALPHA` 누락) | 맞다 | ✅채택 |
| **인자만 쓰면 비트 8-9(ALPHACHANNEL)와 24-25(선택자)를 지워 텍스처 알파가 조용히 망가진다** | `Hook.c:372` 가 선택자를 세운다 | ✅**채택 — 중요** |

## 10. 쟀다 (§86) — 인자는 레지스터에서 python 그대로다

```
   ONE       ONE       -> ffc060   python ffc060
   ONE       ZERO      -> c08040            c08040
   ZERO      ONE       -> 804020            804020
   DST_COLOR ZERO      -> 602008            602008
   ZERO      SRC_COLOR -> 602008            602008
   SRC_ALPHA 1-SRC_A   -> a06030            a06030
```

`ONE/ONE` 의 빨강은 일부러 넘치게 했다 — `0x140` 이 될 자리에서 **`ff`** 가
나왔다.  **자른다, 감싸지 않는다.**

그리고 반올림 자리:

```
   SRC_ALPHA DST_ALPHA, 1 over 1, 두 알파 128 -> 1
```

**끝에서 한 번**이다.  262144 표본으로 맞춘 그 산술이 다른 쌍에도 그대로 간다.

### 10-1 제 기대값 둘이 틀렸다

표에 `DST_COLOR ZERO` 를 `604010`, `SRC_ALPHA 1-SRC_A` 를 `A26030` 으로 적었다.
python 은 `602008` 과 `A06030` 이라 한다 — 손으로 어림한 값을 적은 것이고,
계산은 python 으로 한다는 규칙을 어긴 자리다.  고치고 나니 여섯이 전부 맞았다.
