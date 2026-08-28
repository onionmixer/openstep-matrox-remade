# W12 — 원점 검증 수정 (2026-08-28, 코딩 전 계획)

## 1. 결함

두 번째 교차검토가 찾았고 사양서와 소스로 확인했다(W11 §8.1).
**2 차 DMA 나 WARP 와 무관하게 지금 존재한다.**

클라이언트가 주는 원점 셋(`dstorg`·`zorg`·`texorg`)에 대해 검증기가 하는
검사는 **각각 하나, 수치 범위뿐이다**:

```c
/* hw3d/OpenStepMGAHW3D.c:358, 385, 451 */
if (!osmgaHW3DReach(b->state.dstorg, rows, lim->pitchBytes,
                    lim->colourStart, lim->colourEnd)) ...
```

`osmgaHW3DReach` 는 `org >= lo`, `org < hi`, `span <= hi - org` 만 본다.
**정렬도, 마스크도, 맵비트도 보지 않는다.** 인코더는 값을 **그대로** 쓴다
(`:9698`, `:9746`).

그런데 이 레지스터들은 **하위 비트가 주소가 아니다**:

| 레지스터 | 주소 | map | acc | 주소 필드 | **요구 정렬** |
| --- | --- | --- | --- | --- | --- |
| `DSTORG` | 2CB8 | `dstmap<0>` | `dstacc<1>` | `<31:6>` | **64 바이트** |
| `ZORG` | 1C0C | `zorgmap<0>` | `zorgacc<1>` | `<31:2>` | **128 바이트** — *"multiple of 128 (the seven LSBs = 0)"* |
| `TEXORG` | 2C24 | `texorgmap<0>` | `texorgacc<1>` | `<31:5>` | **32 바이트** |

`map = 1` 은 **"the destination/depth/texture is in the system memory"** 다
(3-130, 3-286, 3-221).

**계산 (python, 1600×1200 RGB:888/32, 컬러 표면 7,680,000 B)**:

```
64 바이트 정렬(안전)            120,000 /  7,680,000 =  1.56%
하위 6 비트가 0 이 아님(위험) 7,560,000 /  7,680,000 = 98.44%
홀수 -> dstmap=1              3,840,000 /  7,680,000 = 50.0%
```

**검증을 통과하는 값의 98.4% 가 예약/맵 비트를 오염시킨다.**

> **지금 터지지 않는 이유는 Mesa 가 정렬된 값만 보내기 때문이지 드라이버가
> 막고 있기 때문이 아니다.** 그리고 그것조차 **측정된 사실이 아니라 추론**이다
> — 3D 가 동작하므로 그럴 것이다, 라는.

`srcorg` 는 클라이언트 경로에 **없다**(드라이버가 `osmgaStormInitState` 에서
0 을 쓴다). 전수 grep 으로 확인했다.

---

## 2. 고칠 방식 — **거부한다. 마스킹하지 않는다.**

이 저장소의 규칙이 이미 그렇게 돼 있다:

> *"Refuses rather than truncates or mis-encodes: a rejected register leaves
> `*pos` untouched and returns 0"* (`:6631` 인코더 주석)

**마스킹하면 클라이언트가 요청한 것과 다른 것을 그린다.** 그것은 조용한
오작동이고, 지금의 조용한 위험을 조용한 오작동으로 바꾸는 것일 뿐이다.

```c
if ((b->state.dstorg & 0x3FUL) != 0UL)  return OSMGA_HW3D_E_DSTORG_ALIGN;
if ((b->state.zorg   & 0x7FUL) != 0UL)  return OSMGA_HW3D_E_ZORG_ALIGN;
if ((b->state.texorg & 0x1FUL) != 0UL)  return OSMGA_HW3D_E_TEXORG_ALIGN;
```

**새 오류 코드를 쓴다** — 기존 `E_DSTORG` 를 재사용하면 "범위 밖" 과 "정렬
안 됨" 이 같은 값으로 보고되고, 어느 쪽이 일어났는지 모르게 된다.

정렬 검사는 **범위 검사보다 먼저** 한다. 정렬이 틀린 값은 그 자체로 주소가
아니므로, 주소로 취급해 범위를 재는 것은 의미가 없다.

### 2.1 `PW24` 는 해당 없다

3-129 주: *"Due to a limitation of the CRTC in PW24, the DSTORG register must
be loaded with a multiple of three 64-bytes."* 이 드라이버는 `PW32` 다
(`MGA_MACCESS_PW32`, `osmgaStormInitState:1698`). **적용되지 않으며, 심도가
늘면 다시 봐야 한다는 주석을 남긴다.**

### 2.2 거부할 때 값을 로그에 남긴다

**이것이 이 수정의 절반이다.** 검사가 실제로 발동하면 **Mesa 가 무엇을
보내고 있었는지**를 알아야 한다. 발동하지 않으면 §1 의 추론이 처음으로
측정으로 바뀐다.

---

## 3. 검증 — **하드웨어 없이 대부분 된다**

**정정 — 시험 하네스는 이미 있다.** 처음 이 절을 쓸 때 "검증기의 호스트
시험이 없다" 고 적었는데 틀렸다. `hw3d/test-hw3d-validate.c` 가 있고, 그
머리말이 이 수정에 필요한 규율을 이미 적어 두었다:

> *"Every bound is exercised on BOTH sides: the last accepted value and the
> first refused one. **Checking only that good input passes would have missed
> every off-by-one**, and this project has already had one verdict that only
> looked at the case it expected to fail."*

**그러므로 새 하네스를 만들지 않고 이것을 확장한다.**

**어디서 도는가**: 리눅스에서는 못 돈다 — `unsigned long` 이 8 바이트라
구조체 크기 정적 assert 가 걸리고, `-m32` 는 32 비트 libc 헤더가 없어
실패한다(확인함). **실기의 `cc` 가 32 비트이므로 거기서 돈다.** 카드는
건드리지 않으므로 여전히 무하드웨어 시험이다. 방금 현재 판을 돌려
`all cases behave as specified (0 failing)` 를 확인했다.

| # | 시험 | 하드웨어 |
| --- | --- | --- |
| V1 | 정렬된 값이 통과한다 (회귀) | **불필요** |
| V2 | `dstorg` 하위 6 비트 각각이 거부된다 | **불필요** |
| V3 | `zorg` 하위 7 비트, `texorg` 하위 5 비트 각각 | **불필요** |
| V4 | 경계값: `lo`, `hi-span`, 정렬된 최댓값 | **불필요** |
| V5 | 실기에서 3D 회귀가 그대로 돈다 | 필요 (안전) |

**V5 가 유일한 실기 시험이고 위험하지 않다** — 검증기가 더 거부하게 될 뿐,
새로 무언가를 쓰지 않는다. 만약 V5 가 깨지면 **Mesa 가 정렬 안 된 값을 보내고
있었다는 뜻이고, 그것이야말로 알아야 할 사실이다.**

V1~V4 를 `hw3d/test-hw3d-validate.c` 에 **추가**한다. 그리고 그 시험을
실기에서 도는 스크립트가 없으므로 `tools/check-hw3d-validate-no-hardware.sh`
를 만들어 회귀로 묶는다 — **시험이 있는데 아무도 안 돌리는 것이 지금 상태다.**

---

## 4. 하지 않는 것

- **인코더에서 마스킹하지 않는다** (§2).
- **`osmgaHW3DReach` 를 바꾸지 않는다** — 그 함수는 범위를 재는 것이고 옳게
  하고 있다. 정렬은 별개의 검사다.
- **`srcorg` 를 건드리지 않는다** — 클라이언트 경로에 없다.
- **2 차 DMA·WARP 를 이 변경에 섞지 않는다.** 이 수정은 그 앞이고, 혼자
  성립해야 한다.

---

## 5. codex 에 물을 것

- 정렬 요구가 정확한가 — `DSTORG` 64, `ZORG` 128, `TEXORG` 32 바이트
- `ZORG` 의 필드는 `<31:2>` 인데 주가 "128 배수" 라 한다. 필드 폭과 정렬
  요구가 어긋나는 이 조합을 어떻게 읽어야 하는가
- 클라이언트가 주는 다른 값 중 같은 함정이 있는 것 — `texW/texH/texPitch`,
  `dstPitch` 등이 레지스터에 그대로 가는가
- 거부가 옳은가, 아니면 **드라이버가 정렬해서 그리는 것**이 클라이언트에게
  덜 놀라운가
- V5 가 깨질 가능성 — Mesa 가 정렬 안 된 원점을 보낼 현실적인 경로가 있는가
