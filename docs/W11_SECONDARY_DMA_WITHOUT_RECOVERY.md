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
아니라 봉쇄다(§6.4). — 그리고 §7.4 가 이 봉쇄마저 과한 주장임을 보인다:
읽기가 안 돌아오면 유계가 아니고, 물린 엔진은 계속 쓸 수 있다.** 화면과 기계가
살아남고, `OSMGAAccelRearm` 으로 재무장할 수 있다(W10 §9.9).

~~**이것은 열등한 회수가 아니라 이 하드웨어에서 유일하게 검증된 회수다.**~~
**철회 (§7.4).** 검증된 것이 아니다.

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

---

## 7. codex 교차검토 판정 (2026-08-28)

`gpt-5.3-codex-spark` 은 사용량 한도였고 기본 모델로 받았다. **여섯 지적 중
넷을 검증해 채택하고 둘을 부분채택했다. 기각은 없다** — 이번 회신은 §6 의
자가검토가 놓친 것을 실제로 찾아냈다.

| # | 지적 | 검증 | 판정 |
| --- | --- | --- | --- |
| 1 | vertex 값은 **프로그래머블 프로세서의 입력**이다. "기하만 틀린다" 는 비약 | 사양서 3-277 | ✅ **채택** |
| 2 | 클립은 주소 방화벽이 아니다 — `dstmap`/`zorgmap`/`texorgmap` | **사양서 3-130·3-221·3-286** | ✅ **채택 (내 주장이 틀렸다)** |
| 3 | `SECADDRESS<1:0>` 은 `secmod` 다 — 정렬 불변식이 모순 | 사양서 3-168 | ✅ **채택** |
| 4 | 봉쇄 주장이 과하다 — 읽기가 안 돌아오면 유계가 아니다 | **드라이버 자신의 주석 `:2318`** | ✅ **채택 (내 주장이 틀렸다)** |
| 5 | `PRIMPTR` 상태 쓰기 — `SECEND` 쓰기가 시스템 메모리 쓰기를 유발 | **사양서 3-166 + 실측** | ✅ **채택 (가장 값진 것)** |
| 6 | 마이크로코드 버퍼는 두 번째 카드 가시 할당이다 | 소스 `:7700`·`:7849` | ✅ **채택** |

### 7.1 ★ `PRIMPTR` — `SECEND` 를 쓰면 카드가 시스템 메모리에 쓴다

사양서 3-166:

> `primptren0 <0>`: *"When set to '1', a double-qword of status data
> information **is written to the system memory** (using PCI cycle) at the
> address corresponding to `primptr` **every time a Softrap or SECEND or
> Setupend register write occurs**."*

**`SECEND` 쓰기가 곧 2 차 DMA 트리거인데, 그 쓰기가 별도 경로로 시스템 메모리
쓰기를 낸다.** "카드는 우리 링만 **읽는다**" 는 논거가 **쓰기**를 고려하지
않았다.

**실측 (T0 스냅샷)**:

```
PRIMPTR = fffffbf0
  primptren0 <0> = 0     <- 다행히 꺼져 있다
  primptren1 <1> = 0
  primptr <31:4>         -> 0xfffffbf0   <- 4 GiB 근처의 쓰레기값
```

**드라이버는 `PRIMPTR` 을 한 번도 쓰지 않는다**(전수 확인). 즉 이 값은 부팅
잔재이고, **누군가 `primptren0` 을 켜는 순간 `SECEND` 쓰기가 0xfffffbf0 으로
PCI 쓰기를 낸다.**

> **불변식 추가**: `SECEND` 를 쓰기 전에 `PRIMPTR<1:0> == 0` 을 **확인한다.**
> `PRIMPTR` 은 `R/W` 라 읽을 수 있고, 이미 스냅샷에 들어 있다.

### 7.2 ✅ 2 번 — **클립은 주소를 묶지 않는다. 내가 틀렸다.**

| 필드 | 0 | **1** |
| --- | --- | --- |
| `DSTORG.dstmap<0>` (3-130) | 프레임버퍼 | **시스템 메모리** |
| `ZORG.zorgmap<0>` (3-286) | 프레임버퍼 | **시스템 메모리** |
| `TEXORG.texorgmap<0>` (3-221) | 프레임버퍼 | **시스템 메모리** |

**좌표가 완벽히 클립돼도 목적지 베이스가 시스템 메모리를 가리킬 수 있다.**
그리고 텍스처 **읽기**는 목적지 클립 사각형에 묶이지도 않는다.

§6.1 의 *"남는 위험은 기하가 틀리는 것이고, 그것은 클립이 막는다"* 는
**철회한다.** 안전 증명에 다음이 들어가야 한다:

```
DSTORG.dstmap  = 0   (드라이버는 이미 osmgaStormInitState 에서 DSTORG=0 을 쓴다)
ZORG.zorgmap   = 0   또는 깊이 비활성
TEXORG.texorgmap = 0 또는 텍스처 비활성
```

셋 다 `WO` 라 읽어서 확인할 수 없다 — **배치마다 우리가 쓰는 수밖에 없다.**

### 7.3 ✅ 3 번 — `SECADDRESS` 의 낮은 두 비트는 주소가 아니다

사양서 3-168: `secmod<1:0>`, `secaddress<31:2>`. vertex 는 `11`.

§2.2 의 `(secStart & 3) == 0` 은 **주소 필드**에 대한 것이고, 레지스터에 실제로
쓰는 워드는 `payload_phys | 0x3` 이다. **검증은 주소에 하고 모드는 검증 뒤에
OR 한다** — 이 순서를 문서에 명시한다. 뒤집으면 유효한 제출이 전부 거부되거나,
누군가 "고친다" 며 검증 뒤 마스킹을 넣어 증명을 깨뜨린다.

`SECEND<1>` 은 `SAGPXFER`(AGP/PCI 선택)다. **PCI 를 의도적으로 고르고 그
사실을 적는다.**

### 7.4 ✅ 4 번 — 봉쇄 주장도 과했다

드라이버 자신의 주석(`:2318`):

> *"What this cannot do, said plainly: **if a read itself never returns, the
> counter never advances and no limit is reached.** A freeze with no give-up
> line afterwards points there rather than here."*

**유계 폴은 MMIO 읽기가 돌아올 때만 유계다.** 그리고 물린 드로잉 엔진은
드라이버가 포기한 뒤에도 프레임버퍼에 **쓸 수** 있다(`:830` 이 그래서 래치를
둔다). **"기계가 계속 쓸 수 있다" 는 보장이 아니라 희망이다.**

### 7.5 ✅ 6 번 — 링이 전부가 아니다

```c
/* :7849 */
/* The ring can go; the microcode cannot -- the card was given its
 * address and nothing proves it has stopped reading. */
```

**마이크로코드 버퍼는 두 번째 카드 가시 할당이고 해제되지 않는다.**
"카드는 링만 읽는다" 는 틀렸다. 안전 증명은 **링 + 마이크로코드 버퍼** 둘을
덮어야 한다.

### 7.6 그래서 방어 가능한 주장은 여기까지다

codex 의 요약을 그대로 받는다:

> *"Vertex secondary DMA prevents a client payload from directly encoding
> arbitrary drawing-register indices, and a correctly encoded logical stream
> is bounded by its programmed endpoint. **It does not yet establish
> non-corruption or continued machine usability.**"*

**§2 의 제목 "abort 를 구조적으로 불가능하게" 는 유지하되, §2.3 의 "기계는
계속 쓸 수 있다" 는 삭제한다.** 이 설계가 주장할 수 있는 것은 **abort 를
막는 것**이지 **엔진이 물렸을 때 기계가 무사한 것**이 아니다.

---

## 8. 두 번째 교차검토 (`gpt-5.6-sol`) — **NO-GO**, 그리고 현재 코드의 구멍

두 번째 회신은 더 가혹하고 **§7 이 놓친 것을 또 찾았다.** 그중 하나는
**W11 의 문제가 아니라 지금 출하 중인 3D 경로의 문제다.**

### 8.1 ★★ 원점 검증 구멍 — **현재 코드의 실재하는 결함**

사양서 `DSTORG` (3-129):

```
dstmap  <0>     1 = 목적지가 시스템 메모리
dstacc  <1>     AGP / PCI
Reserved<5:2>   "must be set to '0'"
dstorg  <31:6>  "corresponds to a 64-byte address in memory"
```

**바이트 오프셋을 그대로 쓰면 하위 6 비트가 맵·접근·예약 비트가 된다.**

검증기가 `dstorg` 에 하는 검사는 **전수로 하나뿐이다**:

```c
/* hw3d/OpenStepMGAHW3D.c:358 -- 이것이 전부다 */
if (!osmgaHW3DReach(b->state.dstorg, rows, lim->pitchBytes,
                    lim->colourStart, lim->colourEnd))
    return OSMGA_HW3D_E_DSTORG;
```

그리고 `osmgaHW3DReach` 는 **수치 범위만** 본다 — 정렬도, 마스크도, 맵비트도
보지 않는다. 인코더는 그 값을 **그대로** 쓴다(`:9698`, `:9746`).

**계산 (python, 1600×1200 RGB:888/32)**:

```
컬러 표면 [0, 7680000)
  64 바이트 정렬(안전)          120,000 / 7,680,000 =  1.56%
  하위 6 비트가 0 이 아님(위험) 7,560,000 / 7,680,000 = 98.44%
  홀수 -> dstmap=1(시스템 메모리) 3,840,000 / 7,680,000 = 50.0%
```

**검증을 통과하는 값의 98.4% 가 예약/맵 비트를 오염시키고, 그 절반은 목적지를
시스템 메모리로 돌린다.** `zorg`·`texorg` 도 같은 구조다(3-286, 3-221).

> **지금 터지지 않는 이유는 Mesa 가 정렬된 값만 보내기 때문이지 드라이버가
> 막고 있기 때문이 아니다.** 잠복 결함이고, **2 차 DMA 보다 먼저 고쳐야
> 한다** — WARP 는 이 원점들을 그대로 쓴다.
>
> **고칠 것**: `(org & 0x3F) == 0` 강제(그리고 `zorg`·`texorg` 는 각자의
> 정렬), 예약 비트 0 확인, `PW24` 면 64 바이트의 3 배수(3-129 주). 범위
> 검사만으로는 부족하다.

### 8.2 ★ 내 계획은 **기존 보호를 버릴 뻔했다**

현재 드라이버는 클라이언트 배치를 **엔진을 잡은 채 복사한 뒤** 검증한다
(`:5838`):

> *"The snapshot is one global. (…) so the batch that was proved and the
> batch that was drawn were not the same batch, **which is the whole thing
> the snapshot was introduced to prevent**."*

**W11 §2.2 는 2 차 페이로드를 클라이언트 매핑에서 카드가 직접 읽게 했다.**
그러면 클라이언트가 **검증 뒤·읽기 중에** 정점을 바꿀 수 있다. 지금 있는
보호를 정확히 되돌리는 것이다.

> **정정**: **2 차 페이로드도 드라이버 전용 영역으로 스테이징한다.** 링의
> `[24576, 65536)` 이 클라이언트 매핑 **밖**이므로 그곳에 복사한다. "값일
> 뿐이다" 는 변경 가능성에 대한 답이 아니다.

### 8.3 ✅ `SECEND` 의 패킷 위치

4-13 단계 9: 2 차가 끝나면 1 차가 재개되고 **general 모드가 재시작**되어
*"the first dword fetch will be interpreted as a set of four register
indexes"*.

**따라서 `SECEND` 를 쓰는 블록 뒤의 1 차 dword 가 의도된 새 인덱스 워드여야
한다.** 아니면 값 하나가 인덱스 블록으로 해석된다. `SECEND` 는 블록의
**마지막 값 슬롯**에 두는 것이 안전하다.

### 8.4 ✅ `PRIMPTR` 은 확인이 아니라 **0 을 쓴다**

§7.1 은 "쓰기 전에 `PRIMPTR<1:0> == 0` 을 확인한다" 였다. sol 이 옳다 —
**`PRIMPTR` 은 `R/W` 이므로 정지 상태에서 0 을 쓴다.** 확인만 하면 안전이
부팅 잔재에 의존한다.

### 8.5 ⚖️ 그 밖에 채택

- **`WVRTXSZ` 는 `WO`** 라 검증기가 살아 있는 값을 알 수 없다. **드라이버가
  쓴 상수와 대조해야 한다** — 현재 `0x1807`(정점당 8 dword, `primsz=24`).
  `wvrtxsz+1` 의 배수는 **부분 정점**을 막지만 **부분 프리미티브**는 못 막는다.
- **target-abort 는 여전히 회수 불가**다 (4-14): *"There is no way to change
  the Matrox G400 or its programming to prevent a target-abort."* **§2 는
  master-abort 만 다룬다.** 정직하게 적는다.
- **`OSMGAAccelRearm` 은 출하 안전 논거에 넣지 않는다.** 엔진을 재초기화하지
  않은 채 래치만 푸는 것은 회수가 아니다. **개발 도구로만 남긴다.**
- **링의 물리 연속성**은 첫 페이지만 확인돼 있다(`:3686`). 전제로 명시하거나
  전 페이지를 확인한다.

### 8.6 판정 — **이 계획으로는 착수하지 않는다**

sol 의 요약을 받는다:

> *"Use a driver-only, immutable, validated secondary vertex buffer;
> explicitly force every bus transaction type and every framebuffer /
> system-memory selector; disable PRIMPTR; prove primary-parser packet
> boundaries; and treat any post-doorbell timeout as **'hardware and
> affected surfaces unusable until reboot'**, with no snapshot, mode
> programming, software reuse, or rearm."*

**§2 의 제목도 고친다**: "abort 를 구조적으로 불가능하게" → **"master-abort
를 구조적으로 불가능하게"**. target-abort 는 막을 수 없다.

### 8.7 그래서 다음 작업은 2 차 DMA 가 아니다

**§8.1 이 먼저다.** 원점 검증 구멍은 2 차 DMA 와 무관하게 **지금 존재하고**,
WARP 는 같은 원점을 쓴다. **구멍을 열어 둔 채 그 위에 WARP 를 얹지 않는다.**
