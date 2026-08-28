# W14 — 2 차 DMA 수송, 다시 쓴 계획 (2026-08-28)

W13 은 **NO-GO** 였고 여덟 지적이 **전부 검증을 통과했다**(W13 §11).
이 문서는 살아남은 것 위에 다시 세운다. **W13 을 변호하지 않는다.**

---

## 1. 페이로드를 `DMAPAD` 만으로 만든다 — 한 수로 여러 반대가 사라진다

W13 은 "오프스크린 1 픽셀" 을 그리려 했다. **general 모드의 바이트는
명령이므로**, 그 페이로드는 목적지·시스템메모리 선택자·텍스처·깊이를
프로그램할 수 있다. 그래서 W13 의 *"메모리는 못 깨뜨린다"* 가 성립하지 않았다.

> **2 차 페이로드를 `DMAPAD` 패킷으로만 채운다.**
>
> `DMAPAD` (3-114): *"Writes to this register, **which have no effect on the
> drawing engine**, can be used to pad display lists."*

그러면:

| W13 의 반대 | `DMAPAD` 페이로드에서 |
| --- | --- |
| 페이로드가 그리기 상태를 프로그램할 수 있다 | **불가능** — 네 인덱스가 전부 `DMAPAD` |
| 상속된 `WO` 상태를 증명해야 한다 | **무관** — 아무 상태도 안 건드린다 |
| 목적지/맵 선택자를 강제해야 한다 | **무관** |
| 어느 네 레지스터를 쓰는지 적어야 한다 | **적혀 있다: 넷 다 `DMAPAD`** |

**이것이 증명하는 것은 정확히 수송이다** — 1 차가 `SECADDRESS`/`SECEND` 를
쓸 수 있는가, 2 차 읽기가 일어나는가, 끝점 판정이 도는가, 1 차 파서가 옳은
경계에서 재개하는가. **그리기는 증명하지 않고, 증명할 필요도 없다.**

### 1.1 그리고 `vertex` 모드는 이번이 아니다

vertex 페이로드는 WARP 의 WR 뱅크로 간다. WARP 가 없으면 도달할 곳이 없다.
**W14 는 제어 평면만 증명하고, 데이터 평면은 WARP 와 함께 온다.** W13 이
"W11 보다 강한 논거" 라고 쓴 것은 근거 없는 자기변호였고 철회한다.

---

## 2. 고정 구역 — "남는 자리" 는 클라이언트가 닿는다

```
링 [24576, 65536)  = 40,960 B = 10,240 dword   (드라이버 전용)

  현재:  listDwords3 = 10,240        <- 인코더에 전체를 준다
  최대 배치(180 삼각형): 2,700~5,400 dword 소비
```

**클라이언트가 삼각형 수로 목록 길이를 좌우한다.** 그러므로:

```
  1 차 목록   [24576, 24576 + PRIMARY_BYTES)      listDwords3 를 여기에 맞춰 줄인다
  2 차 페이로드 [SEC_OFF, SEC_OFF + SEC_BYTES)
  가드        [그 뒤, RING_BYTES)                  DMAPAD 패킷으로 채운다
```

**`listDwords3` 를 줄이는 것이 이 절의 핵심이다.** 구역을 나눠 놓고 인코더에
전체를 계속 주면 아무것도 나눈 것이 아니다.

---

## 3. 가드 — 크기는 모르고, 내용은 안다

**크기에 근거가 없다.** 사양서는 2 차 overfetch 한계를 주지 않고, 1 차의
관측(`:10106`)을 2 차로 옮기는 것은 입증 책임을 뒤집는 것이다. 4-16 의
*"fill the last set of Pseudo-DMA transfers with no-ops"* 는 **`PRIMEND` 를
다시 쓸 때 파서 위치**에 관한 것이지 끝 주소 초과 인출의 진술이 아니다.

> **그래서 가드는 "증명" 이 아니라 "여유" 로 둔다.** 페이로드 뒤 **링의 끝까지
> 전부** `DMAPAD` 패킷으로 채운다. 20 바이트를 고르고 근거인 척하지 않는다.
> 링의 남는 곳을 전부 무해한 패킷으로 두는 것은 공짜다.

**내용은 확정이다**: 인덱스 dword 가 네 슬롯 모두 `DMAPAD` 를 지목해야 한다.
**0 으로 채우면 인덱스 0 = `DWGCTL` 이다**(레지스터 맵 954).

---

## 4. 불변식

```
    /* 주소에 대한 검사. 모드 비트는 그 뒤에 OR 한다. */
    secStart >= ringPhys + SEC_OFF
    secEnd   <= ringPhys + SEC_OFF + SEC_BYTES        /* 뺄셈으로 쓴다 */
    secEnd   >  secStart
    (secEnd - secStart) % 20 == 0                     /* general 패킷 = 5 dword */
    (secEnd - secStart) >= 20                         /* 4-13: 길이 0 불허 */
    (secStart % 4) == 0 && (secEnd % 4) == 0
```

**모든 경계를 뺄셈으로 쓴다** — `ringPhys + RING_BYTES` 는 32 비트에서 그
자체로 증명이 아니다.

레지스터 워드는 검증 **뒤에** 만든다:

```
    secaddressWord = secStart | 0x0   /* secmod = general */
    secendWord     = secEnd   | 0x0   /* SAGPXFER = PCI, 의도적으로 */
```

---

## 5. `PRIMPTR` = 0

`primptren0` 이 서 있으면 **`SECEND` 쓸 때마다** 시스템 메모리에 16 바이트를
쓴다(3-166). 정지 상태에서 0 을 쓰고 0 으로 읽히는지 확인한다. `R/W` 이므로
가능하다. 인코더는 이 레지스터에 닿을 수 없다(`0x1e50` 은 인덱스 창 밖).

**"확인만" 이 아니라 "쓴다"** — 확인은 안전을 부팅 잔재에 맡기는 것이다.

---

## 6. `SECEND` 는 블록의 **마지막 값 슬롯**

2 차가 끝나면 general 모드가 재시작하고 다음 dword 를 네 인덱스로 읽는다
(4-13 단계 9). 4-15 의 리셋 목록이 **두 번의 리셋**(SECEND 쓸 때, 2 차가
끝날 때)을 확인해 준다.

```
    블록:  [SECADDRESS] [DMAPAD] [DMAPAD] [SECEND]
```

앞에 두면 남은 값들이 리셋된 파서 상태에서 해석된다. **이 드라이버는 이미
`SOFTRAP` 을 같은 자리에 둔다.**

---

## 7. `secActive` — 타임아웃 뒤가 아니라 **전송 내내**

W13 은 타임아웃 뒤에만 스냅샷을 막았다. **4-13 단계 8 은 사용 중 접근 자체를
금한다** — 정상 전송 중에도.

```
    secActive : 도어벨 직전에 세우고, 완료가 증명된 뒤에 내린다
    secUnknown: 타임아웃이면 세우고 부팅 전까지 안 내린다
```

`secActive || secUnknown` 이면 `OSMGARegSnapshot` 이 `SECADDRESS`/`SECEND`
슬롯을 **읽지 않고** 표시값을 넣는다(전체 거부보다 낫다 — 나머지는 여전히
유용하다).

**`secUnknown` 이면 모드 변경도 막는다.** W13 의 예외는 철회한다(§11.2).
막으면 화면이 안 돌아올 수 있고, **그것이 이 위협 모델이 고른 쪽이다.**

---

## 8. 영구 링의 연속성을 먼저 증명한다

지금 증거는 **다른 할당**에 대한 것이다(`runDmaRingAllocTest` 가 자기
버퍼를 만들어 순회하고 해제한다). **카드가 읽는 링을 순회한다** — 초기화에서
한 번, 페이지마다 `IOPhysicalFromVirtual`, 불연속이면 **명령 창을 아예
제공하지 않는다.**

이것은 2 차 DMA 와 무관하게 옳고, **먼저 한다.**

---

## 9. 검증

| # | 시험 | 하드웨어 |
| --- | --- | --- |
| V1 | 불변식(범위·20 배수·정렬·뺄셈 경계) | **불필요** |
| V2 | 가드가 `DMAPAD` 패킷으로 채워진다 | **불필요** |
| V3 | 영구 링 연속성 순회가 PASS | 안전 (읽기) |
| V4 | `PRIMPTR` 이 0 으로 읽힌다 | 안전 |
| V5 | `secActive` 중 스냅샷이 SEC 슬롯을 안 읽는다 | 안전 (주입) |
| V6 | **dry**: `SECADDRESS`/`SECEND` 쓰기만 뺀 같은 경로 | 위험하지 않음 |
| V7 | **real**: `DMAPAD` 페이로드 2 차 제출 한 번 | **위험** |
| V8 | 제출 뒤 VRAM 포렌식 0/16384 | 안전 |

---

## 10. V7 의 위험 — 유계가 아니다

W13 은 *"위험은 엔진이 물리는 것이지 메모리가 아니다"* 라고 썼다. **철회한다.**

| | |
| --- | --- |
| 안 돌아오는 MMIO 읽기 | 유계 폴이 유계가 아니게 된다. **`secUnknown` 이 서기 전에 얼 수 있다** |
| target-abort | 프로그래밍으로 못 막는다 (4-14). 회수도 없다 |
| 매핑을 쥔 클라이언트 | 드라이버가 못 막는다고 스스로 적는다 (`:6492`) |

**페이로드가 `DMAPAD` 뿐이라 그리기 상태는 못 건드리지만, 그것이 "기계가
무사하다" 를 뜻하지는 않는다.** 사용자는 freeze 가능 시험을 이미 수용했고,
그 수용 아래에서 하는 시험이다. **안전하다고 말하지 않는다.**

---

## 11. 순서

1. **§8 링 연속성** — 2 차와 무관하게 옳고, 위험 없음
2. **§5 `PRIMPTR` = 0** — 위험 없음
3. **§7 `secActive` 게이트** — 위험 없음
4. **§2 고정 구역 + `listDwords3` 축소** — 위험 없음
5. **§3·§4 가드와 불변식** + V1·V2 호스트 시험
6. **V6 dry**
7. **V7 real** ← 유일한 위험 단계

**1~5 는 전부 하드웨어 위험이 없고, 그것만으로도 드라이버가 나아진다.**

---

## 12. 네 번째 교차검토 판정 — **다시 전부 채택**

| # | 지적 | 검증 | 판정 |
| --- | --- | --- | --- |
| 1 | **성공이 흔적을 안 남기는 시험을 만들고, 흔적 없음을 증거라 불렀다** | `ENDPRDMASTS` 는 softrap 만으로도 선다 (4-17) | ✅ |
| 2 | 용량 계산이 틀렸다 | **헤더가 명시**(`ENC_TRI_BLK 9`) | ✅ |
| 3 | `secActive` 만으로는 경합이 안 닫힌다 + ABI | 스냅샷에 배제 없음 (`:5263`) | ✅ |
| 4 | claim 실패가 모드 쓰기를 막지 않는다 | `programLinearMode` 무조건 호출 (`:6637`) | ✅ |
| 5 | `revertToVGAMode` 가 먼저다 | 드라이버 주석 `:1468` | ✅ |
| 6 | "뺄셈으로 쓴다" 면서 식은 덧셈 | 내 §4 본문 | ✅ |
| 7 | 1~5 단계가 무위험이 아니다 | `PRIMPTR` 은 MMIO 쓰기다 | ✅ |
| 8 | W13 의 다른 fail-stop 게이트를 흘렸다 | 내 §7 이 스냅샷·모드만 | ✅ |

### 12.1 ★ 근본 결함 — 증거가 없는 시험

> *"It has designed a test whose intended success leaves no payload-level
> trace, then declares the absence of a trace to be proof of transport."*

**맞다.** 그리고 `ENDPRDMASTS` 는 증인이 못 된다 (4-17):

> *"It is set to '1' **when a soft trap interrupt occurs**."*

즉 2 차가 한 바이트도 안 읽어도 `SOFTRAP` 만으로 선다. **V6(dry)과 V7(real)이
내가 적은 관측으로는 구별되지 않는다.**

**관측치를 찾았다** (검토를 기다리는 동안):

| 관측 | 근거 | 증명하는 것 |
| --- | --- | --- |
| `SECADDRESS == SECEND` (완료 후) | 4-17: *"The DMA current pointer is readable by the CPU through (…) SECADDRESS"* | 채널이 끝까지 **전진**했다 |
| **`DWGSYNC` 에 매직값** | 3-... : *"serves as a **synchronisation pointer**"*, `R/W`, 그리기 효과 없음 | **우리가 쓴 바이트가 명령으로 소비됐다** |

`DWGSYNC` 가 훨씬 강하다 — 포인터 전진이 아니라 **페이로드 소비**의 증거다.
그러므로 페이로드는 `DMAPAD·DMAPAD·DMAPAD·DWGSYNC=<매직>` 이 된다.
`DMAPAD` **만**이라는 §1 의 규칙을 한 칸 푼다.

> **주의 둘**: `dwgsyncaddr` 갱신 조건이 *"직전 프리미티브가 완료됐을 때"* 라
> 프리미티브가 없는 페이로드에서 어떻게 동작할지 미확인. 그리고 예약 비트
> `<1:0>` 이 0 이어야 하므로 매직값의 하위 2 비트를 비운다.
>
> **그리고 `primptren1` 이 서 있으면 `DWGSYNC` 갱신이 `PRIMPTR` 주소로 PCI
> 쓰기를 낸다** — `PRIMPTR = 0` 이 이 관측치의 **선행 조건**이 된다.

### 12.2 ★ 용량을 추측했다 — 헤더에 적혀 있었는데

```
OSMGA_HW3D_ENC_TRI_BLK  9    /* per primitive; 8 unconditional + tex */
ENC_DWORDS = ((8 + 4) + 180 x 9) x 5 = 8,160 dword = 32,640 B
```

| | |
| --- | --- |
| 내가 쓴 값 | 2,700~5,400 dword (삼각형당 3~6 블록으로 **추측**) |
| 실제 | **8,160 dword** |
| 과소평가 | **1.5~3.0 배** |

**여유는 40,960 − 32,640 = 8,320 B** (general 패킷 416 개)뿐이다.

그리고 헤더의 주석이 이 실수를 미리 경고하고 있었다:

> *"Nine, not eight. Counted from the encoder rather than from memory (…)
> the bound it produced was therefore an **UNDER-estimate**, which is the one
> direction a bound must not be wrong in."*

**나도 기억에서 셌고, 같은 방향으로 틀렸다.**

### 12.3 ★ 모드 차단이 동작하지 않는다

```c
int claimed = [self claimEngineForMode];   /* :6633 */
...
if (![self programLinearMode]) {           /* :6637 — claimed 를 안 본다 */
```

**`claimEngineForMode` 를 실패시켜도 모드 레지스터는 그대로 쓰인다.**
그리고 `revertToVGAMode` 가 **먼저** 돈다(`:1468` 주석). 게이트는
`enterLinearMode` 가 아니라 **두 진입점 모두**에서, **`secActive || secUnknown`**
으로, **락을 쥐지 않고 즉시 실패**해야 한다.

### 12.4 내 두 가지 자책

- §4 가 *"모든 경계를 뺄셈으로 쓴다"* 라고 적고 바로 아래 식이
  `ringPhys + SEC_OFF + SEC_BYTES` 다. **피하겠다고 쓴 형태를 그대로 썼다.**
- §7 이 W13 §7 표의 게이트 넷 중 **둘만** 옮겼다. 추가 2 차 제출, `AccelRearm`,
  표면 재사용이 빠졌다.
