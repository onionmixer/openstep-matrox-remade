# W11 — 회수 없이 2 차 DMA 를 안전하게 (2026-08-28, 코딩 전 계획)

## 1. 왜 안전 논거를 새로 써야 하는가

W9 §10 은 2 차 DMA 의 최대 blocker 를 이렇게 닫았다: *"abort 가 나면
`RST.softreset` 으로 회수한다 — 벤더가 절차를 적어 뒀다."*

**실기에서 두 번 시도했고 두 번 다 기계를 못 쓰게 만들었다**(W10 §12·§14).

| 시도 | 결과 |
| --- | --- |
| `softreset` 만 | VRAM 읽기가 +8/+4 dword 어긋남. 화면 파손 |
| `softreset` + `memreset` | 어긋남은 사라지고 **90% 무작위**. 화면 파손 |
| `memreset` 단독으로 복구 | **안 된다** |
| 확인된 복구 | **재부팅뿐** |

`MGASoftReset` 은 X.Org 에서 **초기화 중에만, 비콘솔 카드에만** 불리고 그
뒤에 전체 프로그래밍이 따른다. **그것은 회수가 아니라 재초기화의 앞부분이다.**

> **그러므로 이 문서의 전제**: **회수 수단은 없다.** 있는 척하지 않고, 회수가
> 필요 없는 설계를 만든다.

---

## 2. 새 논거 — abort 를 **구조적으로 불가능**하게

master-abort 는 *"an access is attempted and **no device responds**"* 다
(4-14). 카드가 **응답하는 메모리만** 읽으면 일어날 수 없다.

### 2.1 이미 갖춰진 것 (소스 확인)

| 사실 | 근거 |
| --- | --- |
| DMA 링은 `IOMallocLow` 로 **한 번 할당하고 절대 해제하지 않는다** | `:1975` 주석, `:3686` |
| 물리 주소는 페이지 정렬이 검증되고 실패하면 창을 아예 안 연다 | `:3690`~`:3700` |
| 3D 제출 목록은 **그 링 안**에 있다 | `listPhys3 = osmgaMmapCmdPhysical + OSMGA_HW3D_RING_OFFSET` (`:5909`) |
| 인코더는 `SECADDRESS`/`SECEND`/`SETUPADDRESS`/`SETUPEND` 를 **거부한다** | `:6622` |

`:6622` 의 주석이 이미 이 문서의 논지를 적어 두었다:

> *"the encoder should not be the thing standing between a future
> list-building caller and a register that **makes the card walk memory of
> its choosing**."*

**해제되지 않는 단일 할당 + 알려진 베이스와 크기.** 이것이 논거의 토대다.

### 2.2 더할 것

1. **일반 인코더의 거부는 그대로 둔다.** `SECADDRESS` 를 쓰는 것은 **전용
   함수 하나**이고, 그 함수는 값을 **계산**하지 **전달받지 않는다.**
2. **범위 불변식**을 그 함수가 강제한다:
   ```
   ringPhys <= secStart  <  secEnd <= ringPhys + OSMGA_DMA_RING_BYTES
   secEnd - secStart >= 4              (4-13: 길이 0 은 "It is not permitted")
   (secStart & 3) == 0, (secEnd & 3) == 0
   ```
   하나라도 어긋나면 **제출 자체를 거부한다.** 레지스터에 쓰지 않는다.
3. **`secmod` 는 vertex(`11`)만 쓴다.** 이것이 안전의 핵심이다 —
   **general 모드의 페이로드는 명령이고 vertex 모드의 페이로드는 데이터다.**
   링은 클라이언트에 매핑돼 있으므로, 클라이언트가 내용을 바꿀 수 있다.
   **데이터를 바꾸는 것은 그림을 틀리게 하고, 명령을 바꾸는 것은 카드를
   임의로 조종한다.** 그 차이가 이 설계를 성립시킨다.

### 2.3 그래도 남는 것 — 그리고 그때 무엇을 하는가

abort 가 불가능해져도 **엔진이 물릴 수는 있다**(잘못된 명령, 잘못된 정점
형식). 그때 쓸 회수는 없다. **그러나 이 드라이버에는 이미 답이 있다:**

```
유계 폴이 시간 초과  ->  stormBlitFailed 래치  ->  가속을 그만 쓴다
                    ->  소프트웨어로 그린다  ->  기계는 계속 쓸 수 있다
```

**"엔진을 되살린다" 가 아니라 "엔진을 포기한다" 이고, 정확히는 회수가
아니라 봉쇄다(§6.4).** 화면과 기계가
살아남고, `OSMGAAccelRearm` 으로 재무장할 수 있다(W10 §9.9).

**이것은 열등한 회수가 아니라 이 하드웨어에서 유일하게 검증된 회수다.**

---

## 3. `RST` 를 출하 경로에서 없앤다

`OSMGASoftReset` 은 기계를 두 번 못 쓰게 만들었다. **개발 스위치 뒤로
보내거나 지운다.** 남겨 둘 이유가 없다 — 회수에 쓸 수 없다는 것이 이제
측정된 사실이고, 남아 있으면 언젠가 누군가 부른다.

`OSMGAMemReset` 도 같이 간다. 단독으로는 아무것도 고치지 못한다.

> **W10 §5 의 금지표에 한 줄 추가한다**:
> **`RST.softreset` 을 살아 있는 콘솔 카드에 쓰지 않는다. 회수 불가. 실측 2 회.**

---

## 4. abort 없이 검증하는 방법

**이 설계의 장점은 위험한 시험이 필요 없다는 것이다.**

| # | 시험 | 하드웨어 |
| --- | --- | --- |
| V1 | 범위 검증기가 잘못된 값을 거부하는지 — 호스트 단위 시험 | **불필요** |
| V2 | 드라이버가 범위 밖 제출을 **레지스터를 건드리기 전에** 거부하는지 | 안전 (거부 경로) |
| V3 | 링 안의 최소 vertex 페이로드로 2 차 제출 한 번 | 위험하지만 abort 불가 |
| V4 | 제출 뒤 VRAM 포렌식 0/16384 | 안전 |

**V3 이 유일하게 새로운 위험이고, 그 위험은 "엔진이 물릴 수 있다" 이지
"메모리가 깨질 수 있다" 가 아니다.** 물리면 §2.3 의 경로로 떨어진다.

---

## 5. 주장하지 않는 것

- **물린 엔진이 돌아온다고 주장하지 않는다.** 아무도 못 봤다(W10 §11.4).
- **`SECEND` 트리거가 우리 드라이버에서 돈다고 주장하지 않는다.** V3 이 그것을
  처음 본다.
- **클라이언트가 정점 데이터를 바꿔도 안전하다고 주장하지 않는다** — 그림이
  틀릴 뿐 카드가 임의 메모리를 읽지는 않는다는 것까지만 주장한다.

---

## 6. 검토 — codex 가 한도에 걸려 직접 했다

> codex 는 사용량 한도로 응답하지 못했다(15:12 까지). **검증은 어차피 내가
> 해야 하는 일이므로** 같은 여섯 질문을 사양서와 소스에 직접 물었다.
> **codex 를 못 썼다는 사실 자체를 기록해 둔다** — 이 절은 교차검토를 받은
> 것이 아니라 자가검토다.

### 6.1 ✅ `secmod=vertex` 는 목적지를 페이로드가 정하지 않는다

**이것이 계획의 하중을 다 받는 주장이었고, 사양서가 답한다.**

general 모드 (4-13, 단계 9):

> *"The General Purpose Pseudo-DMA mode is selected, **the first dword fetch
> will be interpreted as a set of four register indexes**."*

**general 모드는 페이로드가 곧 레지스터 인덱스다 — 클라이언트가 쓰는 버퍼를
그 모드로 읽히면 클라이언트가 카드를 조종한다.**

vertex 모드 (`WVRTXSZ`, 3-277):

> *"`wvrtxsz`: This is **the number of registers (minus one) to be written
> sequentially in the WR register** before switching to the next bank. This
> information is used to align the vertices in the register bank
> boundaries."*

**목적지는 `wvrtxsz` 와 순서가 정하고, 페이로드는 값만 싣는다.** 클라이언트는
*무엇이 쓰이는가*를 바꿀 수 있고 *어디에 쓰이는가*는 바꿀 수 없다.

**남는 위험은 기하가 틀리는 것이고, 그것은 클립이 막는다** — W9 §7: 클립은
`xdst`/`ydst` **하드웨어 비교**라 좌표 출처와 무관하다. **단 배치마다
`clipdis = 0` 을 강제해야 한다**(W9 §10.2).

### 6.2 ✅ 읽기 범위는 구조적으로 유계다

4-13 단계 7~9:

> *"The secondary current address will be **advanced to point to the next
> dword**. (…) SECADDRESS and SECEND are compared. If they are different,
> the secondary DMA continues. **If they are equal, the secondary DMA is
> finished**."*

dword 단위 전진 + 비교다. **read-ahead 나 버스트 오버런을 사양서가 기술하지
않는다.** 그리고 범위 불변식(§2.2)이 그 구간을 링 안에 가둔다.

### 6.3 ✅ **1 차 목록은 클라이언트가 못 쓴다 — 이미 그렇게 돼 있다**

이것이 물어야 했던 것 중 가장 위험한 질문이었다. 답은 **이미 막혀 있다**:

```
링 전체            0x10000 = 65536 B  (IOMallocLow, 한 번, 해제 안 함)
클라이언트 매핑     [0, 0x6000)  = 24576 B     <- 페이로드/배치만
1 차 목록          0x6000 부터                 <- 매핑 밖
```

`osmgaMmapCmdBytes = OSMGA_HW3D_RING_OFFSET` (`:3753`) 이고 매핑 검사가
`rel >= OSMGA_HW3D_RING_OFFSET` 을 거부한다(`:2775`). 그 주석이 이렇게 적어
두었다: *"A test that always fails is a test nobody reads, and this one was
**standing in front of a real hole**."*

**그러므로 2 차 페이로드는 클라이언트 매핑 안에 두어도 되고, 1 차 명령은
애초에 클라이언트가 닿지 못한다.**

### 6.4 ⚖️ "가속을 그만 쓴다" 는 정직한가 — **부분적으로**

카드를 **멈출** 수단은 없다(`RST` 이 빠졌으므로). 그러나 **멈출 필요가 없다**:
DMA 주소가 구조적으로 링 안에 갇혀 있으므로, 물린 채로 계속 마스터링해도
**우리가 소유한 64 KiB 를 읽을 뿐**이다. 밖으로 나가지 못한다.

**그래서 "회수" 라고 부르는 것은 과하다. "봉쇄" 가 맞다.** 문서를 그렇게
고친다 — 엔진은 되살아나지 않고, 다만 **아무것도 망가뜨리지 못한다.**

### 6.5 ⚖️ `RST` 는 지우지 말고 **개발 스위치 뒤로**

지우면 훗날 "전체 재초기화 회수" 를 시험할 때 다시 써야 한다. **기본 빌드에서
컴파일되지 않게 하되 소스에는 남긴다** — `OSMGA_HW3D_FENCE_OBSERVE` 등이
쓰는 기존 게이팅과 같은 방식.

### 6.6 남은 미확인

- `wvrtxsz` 와 페이로드 길이가 안 맞으면 정점 스트림이 어긋난다. **기하가
  틀릴 뿐 범위는 유계**지만, 제출 시 길이를 `(wvrtxsz+1)*4` 의 배수로
  검증해야 한다.
- 4-13 단계 8: *"SECADDRESS and SECEND **cannot be accessed while they are
  being used** by the secondary DMA channel. This will produce unpredictable
  results."* — **제출 중 이 레지스터를 읽지 않도록** 해야 한다. 지금
  `OSMGARegSnapshot` 이 그것을 읽는다(§4 T0.1). **제출 중에는 스냅샷을
  찍지 않는다**를 규칙으로 넣는다.
