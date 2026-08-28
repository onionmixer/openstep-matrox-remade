# W16 — ALPHACTRL 의 교차 필드 제약 (2026-08-28, 코딩 전)

W11 §8 의 검토가 지적했고 그때는 별건으로 미뤘다: **ALPHACTRL 은 각 필드의
값만 검사하고, 필드 **사이**의 제약은 보지 않는다.**

---

## 1. 사양서가 금지하는 네 조합

`ALPHACTRL` (3-32~3-34) 의 주(Note)들:

| # | 제약 | 사양서 |
| --- | --- | --- |
| 1 | `srcblendf = SRC_ALPHA_SATURATE` 이면 `dstblendf ≠ ZERO` | *"When using SRC_ALPHA_SATURATE, dstblendf **must not be programmed to 0**."* |
| 2 | `alphamode = video alpha` 이면 `dstblendf ≠ ZERO` | *"When using video alpha, dstblendf **must not be programmed to zero**."* |
| 3 | `alphamode = video alpha` 이면 `astipple ≠ 1` | *"Video alpha **must not be used when astipple = '1'**."* |
| 4 | `astipple = 1` 이면 **네 조합만** | *"only the following Src and Dst function combinations are supported"* |

4 번의 네 조합:

```
  ZERO                / ONE
  ONE                 / ZERO
  SRC_ALPHA           / ONE_MINUS_SRC_ALPHA
  ONE_MINUS_SRC_ALPHA / SRC_ALPHA
```

## 2. 지금 무엇을 보는가

```c
/* hw3d/OpenStepMGAHW3D.c, E_ALPHA 분기 -- 이것이 전부다 */
if ((ac & 0xFUL) > OSMGA_HW3D_AC_SRC_MAX ||        /* src <= 8   */
    ((ac >> 4) & 0xFUL) > OSMGA_HW3D_AC_DST_MAX || /* dst <= 7   */
    ((ac >> 8) & 0x3UL) == 0x3UL ||                /* amode RSVD */
    ((ac >> 13) & 0x7UL) == 0x1UL)                 /* atmode 1   */
    return OSMGA_HW3D_E_ALPHA;
```

**각 필드가 자기 값 범위 안인지만 본다.** 네 조합 중 어느 것도 안 걸린다.

## 3. 네 조합이 전부 클라이언트로 도달 가능하다 (python)

```
AC_CLIENT = 03fffbff
  srcblendf <3:0>   허용     dstblendf <7:4>   허용
  alphamode <9:8>   허용     astipple  <11>    허용
```

**관련 필드가 모두 클라이언트 설정 가능하다.** 이론적 제약이 아니다.

---

## 4. 고치는 방식 — **거부한다**

W15 는 크기를 거부하고 인코딩을 마스킹했다. **여기는 마스킹할 것이 없다** —
어느 비트도 "틀린 표현" 이 아니고, **조합 자체가 사양서가 금지한 것**이다.
값을 바꾸면 클라이언트가 요청하지 않은 블렌딩을 그린다.

> **`OSMGA_HW3D_E_ALPHA` 를 그대로 쓴다.** 새 코드를 만들지 않는다 — 같은
> 레지스터의 같은 성격("이 ALPHACTRL 은 쓸 수 없다")이고, 어느 제약이었는지는
> 로거가 값을 찍어 좁힌다(W15 가 그 길을 냈다).

### 4.1 위험도

**하드웨어 위험 없음.** 새로 쓰는 레지스터가 없고, 검사는 거부만 늘린다.

**다만 W15 와 달리 회귀가 깨질 수 있다** — Mesa 가 금지 조합을 보내고 있었다면
지금까지는 통과했고 앞으로는 거부된다. **그 경우 되돌리지 않고 조사한다**:
사양서가 금지한 조합을 Mesa 가 보내고 있었다면 그것이 발견이다.

---

## 5. 검증

| # | 시험 | 하드웨어 |
| --- | --- | --- |
| V1 | 네 제약 각각을 위반하는 값이 거부된다 | **불필요** |
| V2 | 각 제약의 **경계 반대편**이 통과한다 | **불필요** |
| V3 | `astipple=1` 의 네 허용 조합이 통과하고 나머지가 거부된다 | **불필요** |
| V4 | 회귀 15/15 · `SCENES_MOVED=0` | 안전 |
| V5 | `E_ALPHA` 거부가 로그에 안 뜬다 (Mesa 가 금지 조합을 안 보낸다) | 안전 |

**V3 이 특히 중요하다** — 네 조합을 전부 통과시키고 나머지 조합 하나하나를
거부해야 한다. "금지된 것을 거부한다" 만 시험하면 **허용된 것까지 거부하는
구현**이 통과한다.

---

## 6. 하지 않는 것

- **`atmode`·`atref`·`aten` 은 건드리지 않는다** — 교차 제약이 없다
- **`OSMGA_HW3D_AC_CLIENT` 마스크를 바꾸지 않는다** — 어느 필드도 새로
  막거나 열지 않는다
- **`alphamode = FCOL/alpha channel` 에는 제약이 없다** — 2·3 번은
  video alpha 에만 걸린다

---

## 7. codex 에 물을 것

- 네 제약을 다 찾았는가 — `ALPHACTRL` 이나 인접 절에 더 있는가
- `astipple` 의 네 조합 표가 **완전한 목록**인가, 아니면 예시인가
- `dstblendf` 에 `SRC_ALPHA_SATURATE`(1000)가 없는 것이 맞는가 (지금
  `AC_DST_MAX = 7` 이 그것을 이미 막는다)
- 거부가 옳은가 — 어떤 조합은 "무의미하지만 무해" 라 통과시켜도 되는가
- Mesa 가 이 조합들을 보낼 현실적 경로가 있는가 (`glBlendFunc` 매핑)

---

## 8. codex 교차검토 판정 — **처음으로 기각이 하나 나왔다**

| # | 지적 | 검증 | 판정 |
| --- | --- | --- | --- |
| 1a | 제약을 하나 놓쳤다: **`TRAP` 은 `aten=0` 이어야 한다** (4.5.5.4) | **실측** | ❌ **기각** |
| 1b | 그 규칙이 표에 실재한다 | 사양서 17918 | ✅ 사실 |
| 2 | `astipple` 표는 예시가 아니라 완전 목록 | *"only the following"* | ✅ |
| 3 | 거부 정책을 명시하지 않았다 | — | ✅ |
| 4 | `E_ALPHA` 재사용을 **로거로 정당화한 것이 거짓** | 내 로거 `:962`·`:978` | ✅ **또 같은 거짓** |
| 5 | **회귀가 깨지는 것이 확실하다 (`tsa`)** | `AC_SAT_ZERO` | ✅ **결정적** |
| 6 | 시험 계획이 부족하다 | — | ✅ |

### 8.1 ❌ 1a 기각 — 사양서가 금지하는 것을 **하드웨어가 한다**

4.5.5.4 (Gouraud Shaded Trapezoids) 의 `ALPHACTRL` 행: **`"must set aten = '0'"`**.
4.5.5.5 (Texture Mapping) 의 같은 행: **`"—"`**. 즉 opcode 별 제약이고 실재한다.

**그러나 실측이 반대다.** `tat` 은 `glDisable(GL_TEXTURE_2D)` 로 텍스처를 끄고
알파 테스트를 **엔진 대 소프트웨어**로 비교한다:

```
GL_EQUAL / GL_NOTEQUAL / GL_ALWAYS ... 7 개 함수 전부
  engine 101  software 101  wanted 101
ok  the green witness shows through the discarded red
=== nothing to report === (0 failing)
```

**이 G450 은 텍스처 없는 Gouraud TRAP 에서 `aten=1` 을 올바르게 처리한다.**
비교 함수 일곱 개와 알파 세 단계에서 엔진이 소프트웨어와 GL 규격에 일치하고,
폐기된 프래그먼트 뒤로 증인이 비친다.

> **그러므로 `TRAP ⇒ aten=0` 을 거부로 넣지 않는다.** 넣으면 **측정으로
> 동작이 확인된 기능을 거부하게 된다.** 사양서와 실측이 어긋나고, 이
> 프로젝트의 규칙은 실측이 이긴다.
>
> **문서화된 것과 측정된 것의 어긋남으로 기록한다** — 정착 읽기(W9 §9.5)와
> 소프트리셋(W10)이 그랬던 것처럼.

### 8.2 ✅ 5 기각 불가 — `tsa` 가 제약 1 을 일부러 보낸다

```c
/* test/openstep-mga-hw3d-blendsat-client.m */
#define AC_SAT_ZERO  (0x8UL | (0x0UL << 4) | 0x100UL)   /* src=8, dst=ZERO */
...
if (*verdict != OSMGA_HW3D_OK) return 0xFFFFFFFFUL;     /* 거부 = 실패 */
```

**제약 1 이 금지하는 조합 그 자체다.** 그리고 `tsa` 는 그것을 **재려고**
존재하며, 안정된 답을 이미 냈다:

```
=== source factor 8 is source alpha on colour and ONE on alpha,
    so it is not GL_SRC_ALPHA_SATURATE ===
```

**나는 Mesa 만 확인하고 진단 스위트를 안 봤다.** "15/15 그대로" 는 불가능했다.

### 8.3 그래서 네 제약을 같게 다루지 않는다

| 제약 | 우리가 아는 것 | 판정 |
| --- | --- | --- |
| 1 `SAT`+`ZERO` | **실측됨**(`tsa`), 안정적, 주소를 옮기지 않는다 | **거부하지 않는다** |
| 2 video alpha + `dst=ZERO` | 미측정, 아무것도 안 만든다 | **거부** |
| 3 video alpha + `astipple` | 미측정 | **거부** |
| 4 `astipple` 의 68 조합 | 미측정 | **거부** |

일관성이 없어 보이지만 원칙은 하나다: **모르는 것을 거부하고, 잰 것은 거부하지
않는다.** 이 검증기에 이미 같은 판단이 있다 — 예약 `zmode` 1 을 통과시키는
이유가 *"no zmode can move a write"* 다.

그리고 넷 다 **주소를 옮기지 않는다** — 블렌딩·스티플 의미론이다. 그러므로
거부의 근거는 봉쇄가 아니라 *"의미가 정의되지 않은 것을 클라이언트에게
넘기지 않는다"* 이고, 그 근거는 **측정된 1 번에는 적용되지 않는다.**

### 8.4 ✅ 4 — 또 같은 거짓말을 했다

§4 가 *"어느 제약이었는지는 로거가 값을 찍어 좁힌다"* 고 썼다. **거짓이다.**
값을 찍는 것은 `E_TRIFIELD` 뿐이고(`:962`), `E_ALPHA` 는 일반 줄만 받는다
(`:978`). **W15 에서 정확히 같은 거짓을 하고 고쳤는데 한 문서 만에 반복했다.**

게다가 로깅은 **판정 번호당 한 번**이라(`:948`), 앞선 `E_ALPHA`(정의되지 않은
인자)가 슬롯을 먹으면 **교차 제약 위반이 영영 안 찍힌다.** 그러므로 V5 의
*"`E_ALPHA` 로그가 없으면 통과"* 는 증거가 아니다.

> **`OSMGA_HW3D_E_ALPHACROSS 24` 를 append 하고 Mesa 판정 목록에 넣는다.**
> W15 가 낸 길 그대로.

### 8.5 착수 조건

1. **제약 1 은 거부하지 않는다** (§8.3). `tsa` 가 그대로 돈다
2. 제약 2·3·4 만 거부, **새 코드 `E_ALPHACROSS`**
3. **`TRAP ⇒ aten=0` 은 넣지 않는다.** 어긋남을 문서에 기록한다 (§8.1)
4. `src=ONE, dst=ZERO` 가 **반드시 통과**해야 한다 — 사양서가 권하는
   블렌딩 끄기 값이고, 제약 1·2 를 `dst==0` 만으로 구현하면 **모든 불투명
   삼각형이 거부된다**
5. `astipple` 시험은 **72 조합 전수** — 네 개 통과, 68 개 거부
6. Mesa 판정 목록 + 로거

---

## 9. 결과 (2026-08-28 실기)

### 9.1 ★ `tsa` 가 그대로 돈다 — 제약 1 판단의 직접 시험

```
=== source factor 8 is source alpha on colour and ONE on alpha,
    so it is not GL_SRC_ALPHA_SATURATE ===
```

**측정된 조합을 거부하지 않기로 한 판단이 옳았다.** 거부했다면 이 줄이 안
나왔을 것이고, 그것을 재려고 만든 시험이 죽었을 것이다.

### 9.2 나머지 셋

| | |
| --- | --- |
| 회귀 | 전 항목 `0 failing`, 실패·미실행 없음 |
| 장면 기준선 | **`SCENES_MOVED=0`** |
| `E_ALPHACROSS`(24) | **로그에 없음** — Mesa 는 금지 조합을 안 보낸다 |

거부 코드는 기존 넷(`17 · 6 · 13 · 5`)뿐이고, 전부 시험이 일부러 유발하는
것이다.

### 9.3 무하드웨어

```
check-hw3d-validate: PASS
  (batch validator + secondary range + field encoding + alpha combinations)
```

스티플 전수 훑기: **72 조합 중 4 통과 · 68 거부**, 그리고 통과 개수가 4 임을
주장한다 — 허용된 것까지 거부하는 구현이 통과하지 못하도록.

### 9.4 남긴 어긋남

**4.5.5.4 의 `"must set aten = '0'"` 은 구현하지 않았다.** `tat` 이 텍스처
없는 알파 테스트가 비교 함수 일곱 개에서 소프트웨어와 일치함을 측정한다.

> **정정 (W17 §9.4).** 여기 *"하드웨어가 문서보다 관대하다"* 고 적은 것은
> **절반만 맞았다.** 사양서를 더 읽으면 알파 테스트가 **두 개**이고
> (3-34), `aten` 은 **첫 번째만** 끈다. W17 이 24 행으로 쟀다:
> **`aten` 은 텍스처 없는 경로에서 결과를 하나도 바꾸지 않는다.**
>
> | | |
> | --- | --- |
> | 사라진 함의 | *"사양서를 지키면 Gouraud 알파 테스트가 불가능하다"* |
> | **남는 사실** | **이 보드는 `must` 0 인 자리에서 `aten=1` 을 견딘다** |
>
> 그러므로 이 항목은 "관대함" 의 기록으로 남되, **"사양서를 지킬 수 없다" 는
> 뜻이 아니다.**

이 저장소가 같은 종류의 어긋남을 이미 셋 갖고 있다:

| 어긋남 | 문서 | 실측 |
| --- | --- | --- |
| 정착 읽기 (W9 §9.5) | 8-dword 캐시 | 64 바이트 경계 |
| soft reset (W10) | *"video circuitry not affected"* | 화면이 깨진다 |
| **`aten` (여기)** | *"must set aten = '0'"* | **견딘다** — 다만 끄면 두 번째 테스트가 같은 일을 한다 (W17) |
