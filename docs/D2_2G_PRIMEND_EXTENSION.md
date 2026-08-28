# D2-2g — `PRIMEND` 연장 (2026-08-28, 코딩 전)

## 0. 먼저 — 이것은 blocker 가 아니라 최적화다

D2-2f 로 **생산 가능한 제출 경로는 이미 있다**:

```
배치마다:  정점 채움 -> PRIMADDRESS = X|3 -> 되읽기 확인 -> PRIMEND = X+n
           포인터 도달하면 버퍼는 자유.  펜스는 결과를 읽을 때만.
```

D2-2g 는 그것을 **더 싸게** 만들 수 있는지 묻는다 — `PRIMADDRESS` 쓰기와
되읽기 확인조차 없애고 **`PRIMEND` 만 올리는** 것. 실패해도 잃는 것은 없다.

## 1. 사양서가 허용한다 — 그리고 조건을 붙인다

4-15:

> *"There is no reset of the Pseudo-DMA sequence when PRIMEND is written ...
> writing can happen more than once to **extend the list (even while the list
> is still being transferred)**."*

> *"If commands are added to the primary display list, PRIMEND has to be
> written with its new value to restart the primary DMA transfers."*

그 다음 문장이 **조건**이다:

> *"If you intend to write PRIMEND more than once (without re-writing
> PRIMADDRESS), **fill the last set of Pseudo-DMA transfers with no-ops
> (reserved registers)**. Otherwise, the Pseudo-DMA transfers will restart at
> the last Pseudo-DMA location (either index or data when in General Purpose
> Pseudo-DMA mode)."*

**DRM 이 이것을 지킨다.** `mga_dma.c:132`:

```c
/* We need to pad the stream between flushes, as the card
 * actually (partially?) reads the first of these commands.
 * See page 4-16 in the G400 manual, middle of the page or so. */
BEGIN_DMA(1);
DMA_BLOCK(MGA_DMAPAD, 0, MGA_DMAPAD, 0, MGA_DMAPAD, 0, MGA_DMAPAD, 0);
```

**이 요구가 이 시험의 핵심이다.**

## 2. 핵심 질문 — VERTEX 모드에서 "set" 은 무엇인가

GENERAL 모드의 "set" 은 5 dword 패킷(인덱스 + 값 4)이고 no-op 은 `DMAPAD` 다.
**VERTEX 모드에는 패킷도 인덱스도 없다** — 모든 dword 가 정점 값이고,
"reserved register" 라는 것이 존재하지 않는다.

해석 둘:

| | 뜻 | 예측 |
| --- | --- | --- |
| **(a)** 요구가 GENERAL 전용 | 괄호가 *"when in General Purpose Pseudo-DMA mode"* 만 지목한다. VERTEX 에서는 **온전한 프리미티브 경계로 끝내는 것**이 등가 | 연장이 그냥 된다 |
| **(b)** 모드 무관 | VERTEX 에는 no-op 이 없으므로 **연장을 안전하게 쓸 수 없다** | 기하가 깨진다 |

**관측 형태**: (b)면 *"마지막 Pseudo-DMA 위치에서 재시작"* — VERTEX 에서는 정점/뱅크
포인터가 어긋나 **삼각형이 뒤틀리거나 자리가 밀린다.** hang 이 아니라 그림이 틀린다
**고 보이지만, D2-2f 에서 배운 대로 그것을 전제로 두지 않는다.**

## 3. 설계 — E1 이 E2 를 gate 한다

정점을 **한 영역에 연속으로** 깔고, `PRIMADDRESS` 는 **한 번만** 쓴다.
모든 청크는 24 dword(= 프리미티브) 배수로 끝난다.

```
E1  완료 후 연장 (쉬운 쪽, 4-15 의 두 번째 Note)
    PRIMADDRESS = X|3
    PRIMEND = X+216   폴링 -> 완료
    PRIMEND = X+432   폴링 -> 완료      <- PRIMADDRESS 를 다시 쓰지 않는다
    PRIMEND = X+648   폴링 -> 완료
    펜스 -> 검증(9)

E2  전송 중 연장 (진짜 질문, 첫 번째 Note)
    PRIMADDRESS = Y|3
    PRIMEND = Y+216
    PRIMEND = Y+432        <- 폴링 없이 즉시
    PRIMEND = Y+648
    PRIMEND = Y+864
    폴링(최종 끝) -> 펜스 -> 검증(21)
```

각 청크는 삼각형 3 개 = 72 dword. E1 은 3 청크(삼각형 9), E2 는 4 청크(12).
표는 D2-2e/f 의 21 개를 그대로 쓴다.

## 4. 시험되지 않았으면 PASS 라 하지 않는다 (D2-2f 규율 유지)

E2 의 각 연장 **직전에 `PRIMADDRESS` 를 읽어 기록한다.**
그 값이 직전 `PRIMEND` 보다 **작아야** 전송 중 연장이다.
크거나 같으면 그 연장은 E1 과 같은 것이고 → **`NOT EXERCISED`**.

D2-2f 에서 이 규율이 없었으면 삼각형 1 개짜리 제출로 질문을 건드리지도 못한 채
PASS 를 낼 뻔했다. 같은 함정이 여기에도 있다.

## 5. 무엇이 무엇을 뜻하는가

| 관측 | 결론 |
| --- | --- |
| E1 9 개, E2 12 개 전부 정확 | **(a)가 맞다.** 프리미티브 경계로 끝내면 연장이 된다. 생산은 `PRIMEND` 만 올리면 된다 |
| E1 정확, E2 어긋남 | 전송 중 연장만 문제. 완료 후 연장은 쓸 수 있다 |
| E1 부터 어긋남 | **(b)** — VERTEX 모드에서 연장을 쓸 수 없다. D2-2f 경로가 정본으로 남는다 |
| 삼각형이 **밀려서** 나타남 | *"마지막 Pseudo-DMA 위치에서 재시작"* 의 관측형. 정점 포인터가 어긋난 것 |
| `NOT EXERCISED` | E2 가 질문을 건드리지 못했다 |

## 6. 범위 밖

- **프리미티브 중간에서 끝나는 청크.** 생산은 온전한 프리미티브만 덧붙이므로
  묻지 않는다. 다만 (b)의 진짜 원인일 수 있어 결과 해석에 남긴다.
- 링 wrap. 64 KiB 안에서 선형으로만 쓴다.
- 배치 사이 상태 변경, 인터럽트 완료, Mesa 연결.

## 7. 위험

`PRIMEND` 쓰기는 사양서가 **명시적으로 여러 번 허용**하는 동작이고, D2-2f 가
`PRIMADDRESS` 재기록(선례 없는 동작)도 안전함을 보였다. 그보다 낮다.
**그래도 실패가 그림만 틀리는 것이라고 전제하지 않는다** — D2-2f 계획에서
같은 전제를 근거 없이 썼다가 철회했다. 실패 정책은 두 갈래 그대로.

## 8. codex 에 물을 것

1. **§2 가 핵심이다** — VERTEX 모드에서 "set" 은 무엇이고, no-op 패딩 요구가
   적용되나. (a)와 (b) 중 어느 쪽인가. 사양서·참조·Windows 드라이버에 근거가 있나.
2. Windows 드라이버가 1 차 스트림을 연장하나. 그렇다면 어떻게 패딩하나.
3. E2 의 "전송 중" 판정(`PRIMADDRESS < 직전 PRIMEND`)이 옳은가.
4. `PRIMADDRESS` 를 안 쓰고 `PRIMEND` 만 올릴 때 `ENDPRDMASTS` 가 어떻게 되나 —
   중간 청크가 끝날 때마다 서고, 다음 `PRIMEND` 쓰기가 내리는가.
5. 청크 경계를 프리미티브 배수로 두는 것으로 충분한가, 아니면 정점(8 dword)
   배수여도 되나.
6. 빠진 것.

---

## 9. codex 판정 (2026-08-28) — **NO-GO, 그리고 이번엔 하지 않는 것이 맞다**

| codex 주장 | 검증 | 결과 |
| --- | --- | --- |
| **산술이 틀렸다.** 삼각형 3 개 = 72 dword = **288 바이트**인데 계획서는 `X+216` 이라 썼다 | python: `216 바이트 = 54 dword = 삼각형 2.25 개`. **216 은 E1 전체의 dword 수(9×24)** — 전체를 dword 로 센 값을 첫 청크의 **바이트** 오프셋으로 썼다. **내 계획서가 자기 불변식(프리미티브 경계)을 스스로 어긴다** | ✅**채택 — 구체적 오류** |
| **해석 (a)를 참조가 반박한다** — DRM 은 **광고한 끝 *너머*에** 패딩을 둔다 | `mga_dma.c:130` 이 패딩 **전에** `tail` 을 잡고, `:132` 가 `DMAPAD` 블록을 덧붙이고, `:158` 이 **패딩 전 `tail`** 을 `PRIMEND` 에 쓴다. 즉 **온전한 패킷으로 끝내는 것만으로 불충분하다고 본 것** | ✅**채택 — 결정적** |
| VERTEX 에는 no-op 이 없다 | 광고한 끝 바로 뒤의 dword 가 **다음 청크의 실제 정점 데이터**다. 카드가 끝을 넘어 부분적으로 읽으면(주석이 그렇게 말한다) 그 dword 를 소비하고, 연장이 같은 것을 다시 제시한다 → **중복된 정점** | ✅채택 |
| Windows 도 `PRIMEND` 만 올리지만 **GENERAL 모드**다 | `0x15` = `(0x1c54-0x1c00)>>2` = DMAPAD 인덱스 확인. **인덱스 바이트 패딩은 GENERAL 에만 존재한다** — VERTEX 에는 인덱스가 없다. 선례가 아니다 | ✅채택 |
| **전송 중 판정이 race 다** — 쓰기 *전* 읽기는 쓰기 시점을 말하지 않는다 | 맞다. 포인터가 읽기와 쓰기 사이에 끝에 닿을 수 있다. 옳은 형태는 **쓰기 후** `PRIMEND` 되읽기로 순서를 확정하고 그때 `PRIMADDRESS < oldEnd` 인지 보는 것 | ✅채택 |
| 중간 청크 재사용 술어는 `>= chunkEnd` 이지 `== end && ENDPRDMASTS` 가 아니다 | 3-189: `ENDPRDMASTS` 는 **모든** 채널이 끝에 닿아야 서고 `PRIMEND` 접근이 지운다. 연장 중에는 중간 끝에 안 설 수 있다 | ✅채택 |
| **이번 재부팅을 여기 쓰지 마라** | 아래 §9.1 | ✅**채택** |

### 9.1 그리고 이 권고를 받아들인다

D2-2f 가 남긴 경로는 **이미 생산 가능**하다. D2-2g 가 없애려는 것은 배치당
`PRIMADDRESS` 쓰기 하나와 되읽기 확인 하나다. **그것이 병목이라는 측정이 없다.**

그리고 §7 이 *"실패가 그림만 틀리는 것이라고 전제하지 않는다"* 고 적었는데,
codex 가 실제 wedge 경로를 짚었다 — DMA 시퀀서가 VERTEX 위치를 중복하거나
건너뛰면 `PRIMADDRESS` 는 최종 끝에 닿아도 WARP 에 **불완전한 프리미티브**가
남고, 최종 `DWGSYNC` 가 회수되지 않는다. **재부팅 경로다.**

> **측정하지 않은 병목을 없애려고, 재부팅을 걸고, 사양서가 요구하는 패딩을
> 만족시킬 수 없는 모드에서, 참조 선례 없이.**

넷 중 하나만 참이어도 미룰 이유가 되고, 넷 다 참이다. **D2-2g 는 보류한다.**

### 9.2 다시 볼 때를 위해 남긴다

**VERTEX 모드의 no-op 후보는 퇴화 삼각형이다** — 면적 0 인 24 dword 프리미티브는
그려질 것이 없다. GENERAL 의 `DMAPAD` 블록에 대응하는 유일한 형태로 보인다.
청크당 24 dword 를 더 쓰지만, `PRIMEND` 연장을 살릴 수 있다면 싸다.
**추측이고, 검증한 바 없다.** 되돌아올 때의 출발점으로만 적어 둔다.

재개 조건: **제출 처리량 프로파일이 시작 주소 되읽기를 병목으로 지목할 때.**
그 전까지는 D2-2f 경로가 정본이다.

### 9.3 정정된 값 (되돌아올 때 쓸 것)

```
청크(삼각형 3) = 72 dword = 288 바이트
E1  PRIMEND = X+288, X+576, X+864          (삼각형 9)
E2  PRIMEND = Y+288, Y+576, Y+864, Y+1152  (삼각형 12)
```
