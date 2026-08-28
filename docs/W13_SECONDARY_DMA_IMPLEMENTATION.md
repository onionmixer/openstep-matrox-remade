# W13 — W11 착수 조건의 구현 계획 (2026-08-28, 코딩 전)

W11 은 두 번의 교차검토를 거쳐 **NO-GO** 로 끝났고 §8.6 에 착수 조건을 남겼다.
이 문서는 그 조건을 코드로 옮기는 계획이다. **W11 을 다시 논하지 않는다.**

---

## 1. 범위를 먼저 자른다 — **vertex 모드는 이번이 아니다**

W11 §2.2 는 안전 논거를 `secmod=vertex` 에 걸었다. 그런데 **vertex 모드의
페이로드는 WARP 의 WR 뱅크로 간다**(§8.1 확인). **WARP 가 없으면 그 제출은
아무 데도 도달하지 않는다** — 시험할 대상이 없다.

그래서 둘로 자른다:

| | 무엇 | 안전 논거 |
| --- | --- | --- |
| **W13a (이번)** | `secmod=general`, **드라이버가 만든** 페이로드 | 클라이언트가 페이로드에 손대지 못한다 |
| W13b (다음) | `secmod=vertex` + WARP | 위 + 목적지가 `wvrtxsz` 로 고정 |

**W13a 의 논거가 W11 의 것보다 강하다.** W11 은 "클라이언트가 값은 바꿔도
목적지는 못 바꾼다" 였고, W13a 는 **"클라이언트가 페이로드를 아예 못 만든다"**
다. 그리고 §8.2 가 지적한 회귀(검증 후 변조)가 원천적으로 없다.

---

## 2. 링 예산 (python)

```
링              65536 B
클라이언트 매핑   [0, 24576)        24576 B   <- 카드가 2 차로 읽지 않는다
1 차 목록        [24576, 65536)    40960 B = 10240 dword

  블록 = 인덱스 1 + 값 4 = 5 dword = 20 B      1 차 최대 2048 블록
```

**2 차 페이로드는 1 차 목록과 같은 드라이버 전용 영역을 나눠 쓴다.**
`W13a` 는 한 블록짜리 최소 페이로드만 쓰므로 예산 문제가 없다.

---

## 3. **끝점을 넘어 읽는다는 것은 이 프로젝트가 이미 관측했다**

목록 빌더의 주석(`:10105`):

> *"The trap has to be INSIDE what PRIMEND covers, and **the card reads a
> little past PRIMEND**, so a padding block has to follow it. Getting either
> the wrong way round leaves the list running to the end with the trap never
> fired -- which is exactly what the first attempt did, and what STATUS bit 0
> reported."*

**1 차 채널이 끝점 너머를 읽는 것은 추측이 아니라 이 보드에서 잰 것이다.**
2 차가 다르다고 볼 근거가 없다.

> **그러므로 `SECEND` 를 링의 마지막 소유 바이트에 두지 않는다.** 페이로드
> 뒤에 **패딩 블록**을 둔다 — 1 차가 `SOFTRAP` 뒤에 두는 것과 같은 이유로,
> 같은 모양으로.

이것이 sol 이 *"do not place SECEND at the last owned byte without a
documented no-overfetch guarantee"* 라고 한 것에 대한 답이고, 근거는 문서가
아니라 우리 측정이다.

---

## 4. 불변식 — 코드가 강제할 것

```
    /* 주소 필드에 대한 검사. 모드 비트는 검사 뒤에 OR 한다. */
    ringPhys + RING_OFFSET <= secStart
    secStart <  secEnd
    secEnd   <= ringPhys + RING_BYTES - SEC_OVERFETCH_GUARD
    (secStart & 3) == 0 && (secEnd & 3) == 0
    secEnd - secStart >= 4                    /* 4-13: 길이 0 은 불허 */
```

그리고 **레지스터에 쓰는 워드는 따로 만든다**:

```
    secaddressWord = secStart | MGA_SEC_MOD_GENERAL   /* <1:0> = 00 */
    secendWord     = secEnd   | MGA_SEC_PCI           /* SAGPXFER<1> = 0 */
```

> §7.3 의 지적: 검증을 raw 워드에 하면 유효한 제출이 전부 거부되거나, 누군가
> "고친다" 며 검증 뒤 마스킹을 넣어 증명을 깨뜨린다. **검사는 주소에, 모드는
> 그 뒤에.**

`SAGPXFER = 0`(PCI)를 **의도적으로 고르고 그 사실을 주석에 적는다.**
`IOMallocLow` 는 시스템 메모리이지 AGP 창이 아니다.

---

## 5. `PRIMPTR` — 확인이 아니라 **0 을 쓴다**

`primptren0` 이 서 있으면 **`SECEND` 를 쓸 때마다** 카드가 `primptr` 주소로
시스템 메모리에 16 바이트를 쓴다(3-166). 실측 `PRIMPTR = 0xfffffbf0` 은
부팅 잔재이고 드라이버는 이 레지스터를 한 번도 쓰지 않는다.

```
    /* 정지 상태에서, 첫 SECEND 이전에, 한 번. */
    osmgaW32(mmioBase, MGA_PRIMPTR, 0UL);
```

`R/W` 이므로 쓴 뒤 읽어 0 임을 확인할 수 있다. **확인만 하는 것은 안전을
부팅 잔재에 맡기는 것이다**(§8.4).

---

## 6. `SECEND` 의 패킷 위치

2 차가 끝나면 1 차가 재개되고 **general 모드가 재시작**되어 다음 dword 를
*"a set of four register indexes"* 로 읽는다(4-13 단계 9).

> **`SECEND` 는 블록의 마지막 값 슬롯에 둔다.** 그러면 그 다음 dword 가
> 자연히 새 인덱스 워드다. **이 드라이버는 이미 `SOFTRAP` 을 그렇게 둔다**
> (`:10111`) — 같은 자리, 같은 이유.

---

## 7. 타임아웃 격리 — **가장 어려운 조건**

sol: *"treat any post-doorbell timeout as 'hardware and affected surfaces
unusable until reboot', with no snapshot, mode programming, software reuse,
or rearm."*

```
    static int osmgaSecUnknown;     /* 한 번 서면 부팅 전까지 안 내려간다 */
```

이것이 서면:

| 막을 것 | 왜 |
| --- | --- |
| `OSMGARegSnapshot` | `SECADDRESS`/`SECEND` 를 읽는다 — 4-13 단계 8 이 사용 중 접근을 금한다 |
| 추가 2 차 제출 | 채널 상태를 모른다 |
| `OSMGAAccelRearm` | 엔진을 재초기화하지 않는다. **출하 논거에서 이미 뺐다**(§8.5) |
| 소프트웨어 재그리기 | 엔진이 아직 쓰고 있을 수 있다(`:4915` 가 그래서 금한다) |

**모드 변경은 막지 않는다** — 막으면 화면이 영영 안 돌아온다. 대신 **로그로
크게 알린다.** 이것은 절충이고, 그렇게 적는다.

---

## 8. 검증

| # | 시험 | 하드웨어 |
| --- | --- | --- |
| V1 | 불변식이 범위 밖·비정렬·길이 0 을 거부 | **불필요** (호스트 시험 확장) |
| V2 | 워드 생성이 `주소\|모드` 를 옳게 만든다 | **불필요** |
| V3 | `PRIMPTR` 이 0 으로 읽힌다 | 안전 (읽기) |
| V4 | 2 차 general 제출 한 번 — 오프스크린 1 픽셀 | **위험** |
| V5 | 제출 뒤 VRAM 포렌식 0/16384 | 안전 |
| V6 | 격리 래치가 서면 스냅샷이 거부된다 | 안전 (주입으로) |

**V4 가 유일한 새 위험이고, 그 위험은 "엔진이 물릴 수 있다" 이지 "메모리가
깨질 수 있다" 가 아니다** — 주소가 불변식으로 링 안에 갇혀 있고, `PRIMPTR`
쓰기 경로는 닫혀 있고, 페이로드는 드라이버가 만든다.

**V4 전에 W12 처럼 dry 대조군을 둔다**: 같은 경로를 `SECADDRESS`/`SECEND`
쓰기만 빼고 실행한다.

---

## 9. 하지 않는 것

- **vertex 모드** (§1) — WARP 와 함께
- **클라이언트 페이로드** — W13a 는 드라이버만
- **`RST` 를 되살리지 않는다** — 회수는 없다는 것이 실측이다
- **`stormBlitFailed` 재무장을 안전 논거에 넣지 않는다**

---

## 10. codex 에 물을 것

- W13a/W13b 분할이 옳은가 — general 모드 수송 증명이 vertex 모드에 얼마나
  이월되는가
- `SEC_OVERFETCH_GUARD` 를 얼마로 잡아야 하는가. 1 차의 관측(`SOFTRAP` 뒤
  블록 하나 = 20 B)이 2 차에 그대로 적용되는가
- 격리 래치가 모드 변경을 막지 않는 절충이 옳은가
- `PRIMPTR` 을 0 으로 쓰는 것 자체가 위험한가 (드라이버가 여태 안 쓴 레지스터다)
- V4 의 "1 픽셀" 페이로드가 수송을 증명하기에 충분한가

---

## 11. codex 교차검토 판정 — **전부 채택, 기각 없음**

여덟 지적을 하나씩 소스와 사양서로 확인했다. **하나도 기각할 것이 없었다.**

| # | 지적 | 검증 | 판정 |
| --- | --- | --- | --- |
| 1 | 스테이징 구역이 실제로 없다 | `listDwords3` = **영역 전체** (`:5975`) | ✅ |
| 2 | 가드에 근거가 없고 **내용이 0 이면 안 된다** | 인덱스 `00h` = `DWGCTL` (맵 954) | ✅ |
| 3 | `PRIMPTR` 0 쓰기는 옳다 + 조건 | `0x1e50` 은 인코더 인덱스 창 밖 | ✅ |
| 4 | `SECEND` 위치는 옳다 | 4-13 단계 9, 4-15 리셋 목록 | ✅ |
| 5 | **모드 변경 예외가 틀렸다** | `enterLinearMode` 가 시험 8 개를 돈다 | ✅ |
| 6.1 | 스냅샷이 **정상** 전송과도 경합 | 4-13 단계 8 | ✅ |
| 6.3 | 영구 링의 연속성이 미증명 | 할당 시험은 **자기 임시 버퍼**를 순회 | ✅ |
| 6.6 | 격리를 강제할 수단이 없다 | 드라이버 자신의 주석 `:6492` | ✅ |
| 6.7 | 불변식이 20 배수 아닌 길이를 받는다 | 설계 검토 | ✅ |
| 6.8 | 래치는 안 돌아오는 읽기를 못 구한다 | `:2361` | ✅ |

### 11.1 ★ 가드 내용 — **이 세션에서 이미 잡은 함정을 다시 밟았다**

레지스터 맵 954: `DWGCTL  WO  1C00h  —  00h`. **인덱스 0 은 no-op 이 아니라
`DWGCTL` 이다.** 0 으로 채운 가드는 `DWGCTL` 을 네 번 쓴다.

옳은 것은 `DMAPAD`(3-114):

> *"Writes to this register, **which have no effect on the drawing engine**,
> can be used to pad display lists."*

**W8 초안이 정확히 같은 실수를 했고 같은 이유로 고쳐졌다.** 그것을 기록해
두고도 반복했다.

### 11.2 ★ 모드 변경 예외 — 내 절충이 틀렸다

`enterLinearMode`(`:6628`)는 모드를 프로그램하고 **엔진 점유를 놓은 뒤**:

```
runStormLivenessTest   runStormBlitTest      runStormBlitApiTest
runDmaRingAllocTest    runDmaRingBuildTest   runDmaRingStartTest
runWarpConfigTest      runWarpUcodePlacementTest
```

`runDmaRingBuildTest`·`runDmaRingStartTest` 는 **`stormBlitFailed` 를 한 번도
보지 않는다**(각각 0 회). 즉 **2 차 상태를 모르는 채 모드 변경을 허용하면
DMA 링 시험이 돈다.** "화면 가용성을 메모리 무결성보다 앞세운" 절충이었고,
이 위협 모델이 금지하는 바로 그 교환이다. **예외를 없앤다.**

### 11.3 ★ 스테이징 구역이 없다 (계산)

```
드라이버 전용 영역          10,240 dword = 40,960 B
listDwords3 에 주는 것      10,240 dword   <- 전체
최대 배치(180 삼각형)       2,700~5,400 dword 소비
```

**남는 자리에 스테이징을 두면 클라이언트가 삼각형 수로 그 자리를 침범한다.**
고정 구역을 예약하고 `listDwords3` 를 그만큼 줄여야 한다.

### 11.4 ★ 영구 링의 연속성은 증명된 적이 없다

영구 할당은 **베이스 하나**만 `IOPhysicalFromVirtual` 하고 정렬만 본다
(`:3733`). 페이지 순회는 `runDmaRingAllocTest` 안에 있는데, 그것은
**자기만의 `IOMallocLow` 를 새로 하고 그것을 순회한 뒤 해제한다**(`:6767`).

**카드가 실제로 읽는 64 KiB 는 한 번도 순회되지 않았다.** "카드는 우리 링만
읽는다" 는 그 링이 물리적으로 연속이라는 전제 위에 있고, 그 전제의 증거는
다른 할당에 대한 것이다.

### 11.5 고칠 수 없는 것 — 정직하게

| | |
| --- | --- |
| 안 돌아오는 MMIO 읽기 | 유계 폴이 유계가 아니게 된다(`:2361`). **래치 이전에 얼 수 있다** |
| target-abort | *"no way to change the MGA or its programming to prevent"* (4-14) |
| 매핑을 쥔 클라이언트 | *"cannot stop a client that already has the surface mapped"* (`:6492`) |

**그러므로 §8 의 "V4 의 위험은 메모리가 아니라 엔진뿐" 은 철회한다.**
W13a 는 유계 위험 실험이 아니다.
