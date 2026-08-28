# W10 — WARP 를 위한 실기 시험 설계 (2026-08-28)

> **결정 (사용자, 2026-08-28)**: *"WARP 구현은 반드시 해야하며, 그를 위한
> 최소한의 freeze 가능 테스트는 감안하겠습니다. 단 freeze 하고 재부팅을
> 진행해도 필요한 추적 자료는 남도록 테스트가 신중하게 진행되어야 합니다."*

그러므로 이 문서의 순서는 **시험이 아니라 증거가 먼저**다. §1 이 증거 구조를
정하고, §2 가 그 구조로도 건질 수 없는 것을 적고, §3 이 그 한계에 맞춰 시험을
자른다. §4 가 사다리, §5 가 여전히 유효한 금지다.

---

## 1. 증거 구조 — freeze 를 견디는 것은 이것 하나뿐이다

### 1.1 기존 경로는 이 요구를 만족하지 못한다

```
IOLog → 커널 msgbuf → syslogd → /usr/adm/messages → nxlogd 200 ms 폴 → NFS
```

`nxlogd` 는 잘 만들어져 있고 지금까지 제 몫을 했다. 그러나 **행 직전 마지막
폴 구간(최대 200 ms + syslogd 버퍼)에 기록된 것은 기계와 함께 죽는다.**
사후에 읽는 텔레메트리(`OSMGAHW3DWaits` 등)는 더 나쁘다 — 정의상 살아 있는
기계에서만 읽힌다.

**그리고 일곱 번째 프리즈(2026-08-28) 때 `nx-logcatch` 는 아예 돌고 있지
않았다.** `logs/` 의 마지막 파일은 08-25 다. 도구가 있어도 켜지 않으면 없다.

### 1.2 채택하는 것 — 위험한 단계 **앞에서** 밖으로 내보낸다

`tools/nxbreadcrumb.c`. 유저랜드 시험이 NFS 마운트(`/ndrv2`)에 직접 쓰고
`fsync` 한 뒤에야 위험한 단계로 넘어간다.

```
   기록 → fsync 반환 → (이제 리눅스가 갖고 있다) → 위험한 레지스터 쓰기
```

**`fsync` 의 반환이 곧 보증이다.** 그리고 보증의 강도가 충분한 이유는
따로 있다: **얼어붙는 기계와 기록을 쥔 기계가 다르다.** 리눅스 호스트는
멀쩡하므로, 서버가 디스크에 내렸는지(`sync`/`async` export)는 이 목적에
무관하다 — OPENSTEP 을 떠나 리눅스가 받기만 하면 된다.

### 1.3 **가장 자연스러운 구현이 증거를 지운다** (실측)

단계마다 열고 쓰고 닫는 것 — append 로그의 교과서적 모양 — 이 이 마운트에서
파일을 파괴한다.

| 방식 | 쓴 바이트 | 디스크에 남은 것 |
| --- | --- | --- |
| fd 하나를 열어 두고 record 마다 `write`+`fsync` | 17,100 | **17,100 (정상)** |
| record 마다 `open(O_WRONLY\|O_APPEND\|O_CREAT)`→`write`→`fsync`→`close` | 220 | **11** |

20 개 중 **마지막 하나만** 남는다. 클라이언트가 낡은 오프셋 0 에 append 한다.

**그리고 이 손실은 가장 흔한 점검 방법에 잡히지 않는다.** 처음 실험에서는
`close` 와 다음 `open` 사이에 `stat()` 이 있었고 **모든 경우가 통과했다** —
`stat` 이 클라이언트의 캐시된 크기를 재검증해 버린다. 그래서 이 손실은
문서의 한 문장이 아니라 **도구 안의 회귀 시험**으로 남겼다(`nxbreadcrumb`
가 매번 `ok` 또는 `LOSS` 를 찍는다).

> **규칙**: fd 를 **한 번** 열고 끝까지 유지한다. 절대 재open 하지 않는다.
> **실행마다 새 파일**을 쓴다. 이전 실행의 파일에 덧붙이지 않는다.
> 재부팅 후 시험을 재개할 때 같은 파일을 열면 **프리즈 이전의 증거를 그
> 자리에서 지운다** — 이 요구사항이 막으려는 바로 그 사고다.

### 1.4 비용 (python 분석, n=300)

| | µs |
| --- | --- |
| 중앙값 | **364** |
| p90 | 558 |
| p99 / 최대 | 9,992 / 10,020 |

3 ms 초과는 8.7% 이고 **주기적이 아니라 연속으로 몰려서** 온다. breadcrumb
20 개면 보통 **7 ms**, 전부 꼬리에 걸려도 **200 ms**.

**즉 비용이 설계를 제약하지 않는다. 위험한 쓰기마다 하나씩 넣는다.**

---

## 2. 그래도 건질 수 없는 것 — 먼저 인정한다

**하드 프리즈 중에 커널이 본 것은 회수할 수 없다.** 일곱 번의 프리즈에서
ICMP 가 끊겼다는 것은 스케줄러가 죽었거나 버스가 물렸다는 뜻이고, 그러면
드라이버도, 사용자 공간의 flusher 도, NFS 클라이언트도 돌지 않는다.
VRAM 에 남기는 방법도 같은 이유로 신뢰할 수 없고, 전원 재시작이면 어차피
사라진다.

**그래서 "얼었을 때 무엇을 보고 있었는지" 를 기록으로 남기려 하지 않는다.
대신 "무엇을 하려던 참이었는지" 를 완벽하게 남긴다.**

이것이 §3 의 설계 원칙을 강제한다.

---

## 3. 설계 원칙 — 위험한 쓰기 하나마다 무장 기록 하나

1. **한 ioctl 은 위험한 레지스터 쓰기를 하나만 한다.** 여러 개를 묶으면
   프리즈가 어느 쓰기였는지 영원히 모른다.
2. **각 쓰기 앞에 breadcrumb**: 시험 ID, 단계 번호, 쓸 레지스터와 값,
   그리고 **그 직전에 읽은 관련 레지스터 값들**.
3. **쓰기가 돌아오면 다시 breadcrumb**: `DONE` + 사후 읽기값.
4. 따라서 프리즈 후 리눅스에 남는 마지막 줄이 `ARM t=T1 s=4 RST<-1` 이면
   **정확히 그 쓰기에서 얼었다**는 뜻이고, 그 앞줄들이 그때의 칩 상태다.
5. 커널의 모든 폴은 이미 유계다(`check-bounded-poll-no-hardware.sh`). 새
   코드도 무한 대기를 만들지 않는다 — 행이 아니라 타임아웃이 되게 한다.

---

## 4. 시험 사다리 — 위험한 시험은 **넷**

각 단계는 앞 단계가 통과해야 진행한다. **T1 이 회수 수단이므로 반드시
먼저다** (W9 §10.2.5: 회수를 2 차 DMA 와 함께 처음 쓰지 않는다).

### T0 — 준비 (freeze 위험 없음, 전부 read-only)

- T0.1 레지스터 스냅샷 수집기: `DEVCTRL`, `RST`, `OPTION`, `STATUS`,
  `ENGSTATUS`, CRTC/DAC 계열, `PRIMADDRESS/PRIMEND`, `SECADDRESS/SECEND`,
  `WIADDR*` 를 한 번에 읽어 breadcrumb 파일로 내보낸다.
- T0.2 `DEVCTRL.recmastab<29>` / `rectargab<28>` 의 **현재 값**을 기록한다.
  둘 다 sticky 이므로 시험 전 0 임을 확인해야 T2 의 관측이 의미를 갖는다.
- T0.3 breadcrumb 하네스 왕복 예행: `nxbreadcrumb` 가 `ok` / `LOSS` 를
  올바로 찍는지, 파일이 리눅스에서 완전한지.

### T1 — soft reset 을 **살아 있는 콘솔에서** 한다 ★ 가장 중요

**목적**: 회수 원시연산을 확보한다. 그리고 **사양서가 적지 않은 것을 잰다.**

사양서 3-167 은 softreset 이 *"return some register bits to their soft-reset
values (**see individual registers**)"* 라고 하는데, **그 문구는 문서 전체에
딱 한 번, 이 자리에만 나온다.** 가리키는 개별 레지스터 설명이 존재하지
않는다. **무엇이 되돌아오는지는 실측으로만 알 수 있다.**

근거가 되는 벤더 문장 (4-23):

> *"The soft reset should be interpreted as a **drawing engine reset** more
> than as a general soft reset. The **video circuitry, VGA registers, and
> frame buffer memory accesses**, for example, **are not affected by a soft
> reset**. Only circuitry in the host section which affects the path to the
> drawing engine will be reset."*

**그러나 유일한 참조 구현은 이것을 콘솔 카드에 하지 않는다:**

```c
/* xf86-video-mga-2.0.0/src/mga_driver.c:2165 */
if ( (!pMga->Primary && !pMga->FBDev) )
    MGASoftReset(pScrn);
```

그리고 X.Org 의 절차는 사양서의 최소 절차보다 넓다 — `RST=1` → `usleep(200)`
→ `RST=0` **→ `MACCESS memreset(1<<15)` → `usleep(10)`**. 뒤의 memreset 은
전원투입 시퀀스(4-24)의 SGRAM 초기화 단계이지 abort 회수 절차가 아니다.

> **우리는 사양서의 최소 절차만 한다. `memreset` 은 하지 않는다.**
> 살아 있는 디스플레이에서 메모리를 리셋할 이유가 없고, 참조가 그것을
> 비콘솔 카드에서만 한다는 사실이 그 자체로 경고다.

**단계** (엔진 idle, DMA 미진행, 3D 클라이언트 없음을 확인한 뒤):

| s | breadcrumb | 동작 |
| --- | --- | --- |
| 1 | 전체 스냅샷 `PRE` | 없음 (read-only) |
| 2 | `ARM RST<-1` | `RST = 1` |
| 3 | `ARM delay 200us` | 200 µs 대기 (최소 10 µs 의 20 배) |
| 4 | `ARM RST<-0` | `RST = 0` |
| 5 | 전체 스냅샷 `POST` | 없음 (read-only) |

**통과 조건**: 화면이 살아 있고, telnet 이 살아 있고, `PRE`/`POST` 차이가
드로잉 엔진 경로에 한정된다. **산출물은 그 차이 표 자체다** — 사양서가
적지 않은 "some register bits" 의 실측 목록.

**실패 시**: 화면만 죽고 기계가 살아 있으면 telnet 으로 스냅샷을 마저 받는다
(그 경우가 가장 값진 자료다). 완전 프리즈면 breadcrumb 의 마지막 `ARM` 이
어느 쓰기였는지 말해 준다.

### T2 — master-abort 를 유도하고, 검출하고, T1 로 회수한다

**목적**: abort 검출을 실전에서 처음 쓰지 않기 위해.

근거 (4-14):

> *"a master-abort (…) occurs when an access is attempted and no device
> responds (…) the recmastab bit of the DEVCTRL register will be set (and
> will remain set until a '1' is written to it)."*
> *"The software **must** write to the softreset bit of the RST register when
> either a master-abort or a target-abort occurs (…) to reset the DMA channel
> and the BFIFO."*

**단계**: 1 차 DMA 목록을 응답 없는 물리 주소로 향하게 해 master-abort 를
유도 → `DEVCTRL.recmastab` 확인 → `1` 을 써서 클리어 → T1 절차로 회수 →
정상 제출 한 번이 성공하는지 확인.

**통과 조건**: `recmastab` 가 서고, 클리어되고, 회수 후 렌더가 정상 재개.

> **주의**: master-abort 는 PCI 수준에서 정의된 종료이고 사양서가 회수를
> 규정한다. 그래도 이것은 **의도적으로 버스 오류를 내는 시험**이며 T1 이
> 통과한 뒤에만 한다.

### T3 — 2 차 DMA 최소 수송

**목적**: `SECADDRESS`/`SECEND` 가 우리 드라이버에서 실제로 도는지.

W2 §24 가 **1 차 VERTEX 모드로는 이것을 피할 수 없다**고 판정했다 — 배치당
run 세 번이 되어 이득 1.42 배가 1.12 배로 내려앉고, 전례도 문서도 없다.
그러므로 2 차 DMA 는 선택이 아니다.

사양서가 준 제약 (3-168, 4-12, 4-13):

- 시작 주소를 **`SECEND` 보다 먼저** `SECADDRESS` 에 쓴다.
- `SECEND` 쓰기가 트리거다 (*"Writing to this field will start the secondary
  DMA transfers"*).
- **길이 0 은 허용되지 않는다** — 4-13 은 *"It is not permitted"*.
  (W9 초안이 3-168 의 조건절을 허가로 잘못 읽었다. 반복하지 않는다.)
- `SECADDRESS` 는 **1 차 DMA 쓰기로만** 접근된다 (*"accessible only by a
  primary DMA transfer for writes"*) — MMIO 로는 안 된다.
- 완료는 `ENDPRDMASTS` 가 primary·secondary·setup 을 **모두** 덮는다.

**단계**: 최소 general-mode 페이로드(오프스크린 1 픽셀 등) 한 개를 2 차로
보내고, 1 차의 `SOFTRAP` 으로 완료를 판정하고, 결과를 read-only 로 확인.
DMA 버퍼는 우리가 소유한 `IOMallocLow` 영역이며 클라이언트 매핑 버퍼를
그대로 넘기지 않는다(W2 §6).

**실패 시**: T1 절차로 회수.

### T4 — WARP 마이크로코드와 비상 정지

**목적**: WARP 를 켜고, **끌 수 있음을 같은 시험에서 보인다.**

- 마이크로코드를 VRAM 에 적재 (쓰기이지만 오프스크린)
- `WIADDR2` 로 프로그램 지정, 삼각형 하나
- **`WIADDRNB2 = 0` 으로 정지** — 3-269/3-272 가 Suspend 만 idle 전제가
  없는 모드이고 `WIADDRNB2`(0x1E00)가 FIFO 를 우회하는 MMIO 임을 준다.
- 배치마다 `DWGCTL.clipdis = 0` 을 다시 쓴다 (3-108: 클립은 `xdst`/`ydst`
  하드웨어 비교라 좌표 출처와 무관하지만, `clipdis` 가 서 있으면 무의미해진다).

**통과 조건**: 삼각형이 그려지고, 정지 후 엔진이 idle 로 돌아오고, 클립
경계 밖에 아무것도 쓰이지 않는다.

---

## 5. 여전히 유효한 금지 — 완화하지 않는다

| 금지 | 근거 | 상태 |
| --- | --- | --- |
| **시저 + 커널 계측을 같은 실행에** | `REMAINING_WORK.md` §3-62, 하드 프리즈 **7 회** | **그대로.** 사양서는 이것을 설명하지 못한다 |

시저 단독도, 계측 단독도 얼지 않는다. **둘이 만나야 언다.** 그래서 구현을
읽어서는 위험이 보이지 않는다 — 이 사실은 문서에만 있다. 일곱 번째 프리즈는
내가 이 표를 라벨로만 인용하고 읽지 않아서 일어났다(W2 §21).

**따라서 위 T1–T4 중 어느 것도 커널 내부 타임스탬프를 추가하지 않는다.**
`A−B` 분해에 그것이 필요했지만, 값어치 조사는 이미 끝났고(1.42 배) WARP 착수
결정은 내려졌다. **더 이상 그 측정을 할 이유가 없다.**

---

## 6. 진행 규칙

1. 매 시험 전 **`nx-logcatch start`** — breadcrumb 을 대체하지 않고 보완한다
   (커널 `IOLog` 는 여전히 유용하다, 마지막 200 ms 만 못 믿을 뿐).
2. 매 시험은 **새 breadcrumb 파일**. 이름에 시험 ID 와 타임스탬프.
3. 한 번에 **한 시험만**. 프리즈하면 그 시험의 결과를 문서에 적고, **원인을
   설명하기 전에는 다음 단계로 가지 않는다.**
4. 프리즈 후 재부팅은 사용자가 한다. **재개하는 시험은 반드시 새 파일에
   쓴다** (§1.3).
5. 각 시험이 끝나면 이 문서에 **결과 절**을 붙인다 — 통과든 프리즈든.

---

## 7. 아직 정하지 않은 것

- T2 의 abort 유도에 쓸 물리 주소를 어떻게 고르는가. "응답 없는 주소" 를
  이 보드에서 안전하게 특정하는 방법이 아직 없다.
- T1 의 `PRE`/`POST` 스냅샷에 넣을 레지스터 목록의 최종본.
- WARP 마이크로코드의 출처와 적재 위치(T4).

**이 셋은 codex 교차검토 뒤에 정한다.**
