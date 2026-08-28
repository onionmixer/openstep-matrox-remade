# W9 — G400 사양서를 찾았다 (2026-08-28)

**프로젝트가 내내 "없다" 고 적어 온 매뉴얼이 공개돼 있다.**

```
Matrox MGA-G400 Specification, 1999-06, 690 쪽
bitsavers.org/pdf/matrox/G400SPEC_Jun1999.PDF
```

Matrox 가 1999 년에 사양서를 공개했다. DRM 주석이 인용하는 *"page 4-16 in the
G400 manual"* 이 바로 이 문서이고, **우리는 그 문서를 안 찾아본 채 그 인용만
믿고 있었다.**

**이 문서가 W6·W7·W8 의 여러 결론을 뒤집는다.** 아래는 사양서의 문장이다.

---

## 1. Q1 — `SECEND` 가 방아쇠다. 매뉴얼이 그렇게 적는다.

**SECADDRESS (3-168)**:

> *"The start address value of the secondary DMA channel **must be written to
> this register before SECEND is written to**."*

**SECEND (3-169)**:

> *"**Writing to this field will start the secondary DMA transfers** by the
> Matrox G400 using bus mastering. **The SECEND register must always be written
> to after SECADDRESS.**"*

그리고 페이지 4-16 이 조건까지 적는다:

> *"A reset of the Pseudo-DMA sequence will be generated (…) When the SECEND
> register is written, **assuming SECEND is not equal to SECADDRESS**."*

**Q1 은 답이 나왔고, 실기 시험이 필요 없다.** W7 §1 이 레지스터 레이아웃에서
추론한 것이 정확히 맞았다 — 다만 **추론이 아니라 문서가 됐다.**

### 1.1 정정 — "길이 0 은 안전한 no-op" 은 **틀렸다**

이 절의 초안은 4-16 의 *"assuming SECEND is not equal to SECADDRESS"* 를 읽고
**"같으면 아무것도 시작하지 않으니 무해한 시험 케이스가 있다"** 고 썼다.

**4-13 이 명시적으로 금지한다:**

> *"**Note: It is not permitted to set SECEND to the same value as
> SECADDRESS.**"*

(setup 채널에 대해서도 같은 주가 붙는다.)

**조건절을 허가로 읽었다.** 4-16 은 "그 경우 리셋이 안 난다" 를 기술한 것이지
"그렇게 해도 된다" 가 아니다. **오늘 세 번째로 같은 실수를 했다** — 문서를
자기에게 유리한 쪽으로 읽는 것. 매뉴얼을 손에 넣은 지 한 시간도 안 돼서.

---

## 1.5 Q2 도 답이 나왔다 — 1 차는 2 차를 기다린다

**4.1.9.2 'Using the DMA Channel', 단계 9:**

> *"SECADDRESS and SECEND are compared. If they are different, the secondary
> DMA continues (refer to step 7). **If they are equal, the secondary DMA is
> finished and the primary DMA continues.**"*

**1 차는 2 차가 끝나기 전에 진행하지 않는다.** W6 §9 가 DRM 의 클립 루프에서
역추론한 것이 맞았고, 이제 **문서다.**

그리고 단계 9 가 하나 더 적는다: 1 차가 재개되면 **"the selected Pseudo-DMA
mode restarts (…) the first dword fetch will be interpreted as a set of four
register indexes."** — 목록을 만들 때 지켜야 할 규칙이다.

### 1.6 그리고 **완료 판정 부재가 해소된다**

**STATUS 레지스터 설명(4-13)**:

> *"End of primary DMA channel status bit and soft trap interrupt pending bit.
> **Use of the primary DMA channel is complete when the primary, secondary and
> setup DMA transfers are finished.**"*

**`ENDPRDMASTS` 는 1 차·2 차·setup 이 **모두** 끝났을 때를 뜻한다.**

W2 §6 이 최대 blocker 로 적어 온 *"2 차 DMA 완료 판정 부재 — 1 차가 soft trap
에 닿아 '끝났다' 고 판정하는 동안 2 차가 아직 메모리를 읽고 있을 수 있다"* 는
**문서상 성립하지 않는다.** 지금 우리가 이미 쓰는 완료 예측이 2 차까지 덮는다.

### 1.7 새 제약 하나

**단계 8**:

> *"The SECADDRESS and SECEND registers **cannot be accessed while they are
> being used** by the secondary DMA channel. This will produce unpredictable
> results."*

즉 2 차가 도는 중에 그 레지스터를 건드리면 안 된다. 목록 안에서만 쓰는 구조가
이것을 자동으로 지켜 준다.

---

## 2. 읽기 범위는 **문서로 유계다**

**SECADDRESS (3-168)**:

> *"The field secaddress is increased by one every time the Matrox G400
> terminates a read access at secaddress in the system memory. **If, when
> incremented, secaddress becomes equal to secend, the secondary channel is
> empty. Bus mastering then continues, using the primary channel.**"*

**2 차 전송은 `secend` 에서 정확히 끝난다.** W6·W8 이 "read-ahead 한계를
아무도 안 적었다" 며 세운 64 KiB 여유는 **필요 없었다** — 적어 둔 곳이 있었고
우리가 안 봤다.

> 1 차의 "끝을 조금 넘어 읽는다" 는 별개 사안이고, 4-16 이 그 이유를 적는다
> (§4).

---

## 3. `SECADDRESS`/`SECEND` 는 **MMIO 로 쓸 수 없다** — 관례가 아니라 불가

**4.1.9 (4-12)**:

> *"The secondary DMA registers are **accessible only by a primary DMA transfer
> for writes**, and through the drawing register base addresses for reads. The
> secondary DMA registers **cannot be written directly** through the drawing
> register base addresses or through the DMAWIN base address, **nor can they be
> written to by a secondary DMA transfer**."*

레지스터 설명에도 같은 주가 붙는다:

> *"**Note: It is not possible to write to this register directly. Write access
> must absolutely be performed through mastering mode.** That is, a primary
> display list must be programmed."*

**W7 §1.6 이 "세 구현 중 아무도 MMIO 로 안 쓴다" 를 관측했는데, 이유가
이것이다 — 쓸 수 없다.** 설계 규칙 "1 차 목록 안에서만" 은 이제 규칙이 아니라
**하드웨어 제약**이다.

---

## 4. 1 차의 패딩 규칙, 그리고 그것이 왜 있는지

**4-16**:

> *"There is no reset of the Pseudo-DMA sequence when PRIMEND is written
> (**since PRIMEND starts the primary DMA transfers**); writing can happen more
> than once to extend the list (**even while the list is still being
> transferred**)."*
>
> *"If you intend to write PRIMEND more than once (without re-writing
> PRIMADDRESS), **fill the last set of Pseudo-DMA transfers with no-ops
> (reserved registers)**. Otherwise, the Pseudo-DMA transfers will restart at
> the last Pseudo-DMA location."*

셋이 한꺼번에 확인된다:

- **`PRIMEND` 가 1 차의 방아쇠다** — W7 §1.3 의 추론, 이제 문서.
- **전송 중에도 `PRIMEND` 를 다시 써서 목록을 늘릴 수 있다** — Windows
  드라이버의 단독 `PRIMEND` 쓰기 둘(W7 §1.5)이 정확히 이것이다.
- **패딩은 "no-ops(예약 레지스터)" 여야 한다.** W8 초안이 "0 으로 채운다" 고
  썼다가 스스로 잡은 그 정정이 **문서로 확인됐다** — 0 은 인덱스 0 = `DWGCTL`
  이고 no-op 이 아니다.

---

## 5. **회수 절차가 있다.** W6 의 중심 결론이 뒤집힌다.

**4-15**, 마스터/타깃 abort 처리:

> *"**The software must write to the softreset bit of the RST register when
> either a master-abort or a target-abort occurs** (the RST register will
> indicate this) **to reset the DMA channel and the BFIFO**. This must also be
> done when a warm boot occurs."*

그리고 **RST.softreset (3-167)** 이 무엇을 하는지:

> *"When set to '1', this resets all bits that allow software resets. This has
> the effect of flushing the BFIFO and the direct access read cache, and
> **aborting the current drawing instruction**. **A soft reset will not generate
> invalid memory cycles; memory contents are preserved.** (…) **The reset bit
> must be maintained at '1' for a minimum of 10 µs** (…) After that period, a
> '0' must be programmed to remove the soft reset. This will:*
> - *reset the set-up engine and set-up engine fifo*
> - ***terminate any bus mastering or pseudo-dma transfer***
> - *return some register bits to their soft-reset values*
>
> *WARNING! A soft reset will not re-read the chip strapping."*

**"terminate any bus mastering or pseudo-dma transfer" 가 곧 무장 해제다.**
그리고 **"memory contents are preserved"** 다.

### 5.1 검출도 있다

`DEVCTRL` 의 `recmastab`(마스터 abort 수신)과 `rectargab`(타깃 abort 수신)이
**어떤 abort 가 났는지 알려주고, 1 을 써서 지운다.**

### 5.2 W6 §1 을 정정한다

W6 은 이렇게 썼다: *"회수 절차는 만들 수 없다. 그래서 필요 없게 만든다."*
근거는 "세 참조 구현 중 아무도 리셋을 안 쓴다" 였다(그마저 X.Org 를 놓쳐
틀렸다).

**참조가 안 쓴다는 것과 절차가 없다는 것은 다른 말이었다.** 절차는 벤더가
적어 두었다: **언제 쓰는지(abort 시), 얼마나 유지하는지(최소 10 µs), 무엇을
하는지(bus mastering·pseudo-DMA 종료), 무엇을 보존하는지(메모리 내용).**

### 5.3 그리고 **디스플레이는 살아남는다** — 마지막 반론이 닫혔다

**4.3.1 Reset (4-23)**:

> *"The soft reset should be interpreted as **a drawing engine reset more than
> as a general soft reset**. **The video circuitry, VGA registers, and frame
> buffer memory accesses, for example, are not affected by a soft reset.** Only
> circuitry in the host section which affects the path to the drawing engine
> will be reset. Soft reset has no effect on the EXTRST/ line."*

그리고:

> *"A Soft reset will not re-read the external straps, **nor will it change the
> state of the bits of the OPTION register**."*

`OPTION` 은 클럭 선택을 들고 있다(`sysclksl`, `sysclkdis`). **그것도 안
바뀐다.**

**W6 §1.2 가 "반쯤 성공한 리셋은 디스플레이를 죽이고 그것은 복구 채널까지
죽인다" 고 적은 것은 근거가 없었다.** 매뉴얼은 정반대를 말한다 — 소프트 리셋은
**그리기 엔진 리셋**이고 비디오 회로에 닿지 않는다.

### 5.4 남는 것

- X.Org 는 200 µs 를 쓴다(매뉴얼 최소치 10 µs 의 20 배). 보수적인 값이고,
  우리가 따를 이유가 있다.
- **X.Org 가 주 카드에 쓰지 않는 이유는 여전히 매뉴얼로 설명되지 않는다.**
  다만 그 호출부는 **초기화 경로**(자기가 인수할 카드를 정리하는 것)이므로,
  "주 카드에 위험해서 피한다" 가 아니라 "주 카드는 이미 BIOS 가 세워 놨으니
  건드릴 필요가 없다" 일 가능성이 크다. **추측이므로 그렇게 적어 둔다.**

---

## 5.5 Q3 도 답이 나왔다 — Suspend 는 **도는 중에** 쓰라고 만든 것이다

**WIADDR2 의 `wmode` (3-269)**:

> *"• **Suspend: In this mode, all execution in the WARP is stopped.** Writing
> to the wiaddr and wagp field is ignored.*
> *• Resume: (…) **This mode must be set only when the engine is idle.***
> *• Jump: (…) **This mode must be set only when the engine is idle.***
> *• Start: This mode operates like Jump mode, but it resets the engine."*

**Resume 와 Jump 에는 "엔진이 idle 일 때만" 이 붙고 Suspend 에는 없다.**
그리고 Suspend 의 설명이 "모든 실행이 멈춘다" 이다. **도는 WARP 를 세우는 것이
이 모드의 용도다.**

W7 §3.6 이 *"달리는 WARP 를 MMIO 로 세우는 것은 어느 구현도 하지 않는다"* 며
전례 없음을 위험으로 적었는데, **전례가 없는 것과 용도가 아닌 것은 다르다.**

### 5.6 그리고 FIFO 를 우회하는 비상 정지가 있다

```
WIADDRNB2   0x1E00   WO, DYNAMIC   DMA 인덱스 없음(MMIO 전용)
```

> *"The WIADDRNB2 register is an alternate means of loading the WIADDR2
> register, **bypassing the BFIFO**."*

**FIFO 가 막혀 있어도 닿는다.** 무장 해제에 정확히 필요한 성질이고,
DMA 로는 못 쓰므로 **CPU 만 쓸 수 있다.**

정리하면 **문서화된 무장 해제가 두 층으로 있다**:

| 층 | 무엇 | 성질 |
| --- | --- | --- |
| WARP 만 | `WIADDRNB2 = 0`(Suspend) | MMIO, FIFO 우회, idle 전제 없음 |
| DMA 전체 | `RST.softreset` 10 µs | bus mastering·pseudo-DMA 종료, 메모리·비디오 보존 |

---

## 5.7 Q4 — 클립은 **하드웨어 비교**다. 다만 조건이 하나 있다.

**CXLEFT (3-108)**:

> *"The cxleft field contains an unsigned 12-bit value which is interpreted as a
> positive pixel address and **compared with the current xdst**. The value of
> xdst must be greater than or equal to cxleft to be inside the drawing window.*
> *Note: Since the cxleft value is interpreted as positive, **any negative xdst
> value is automatically outside the clipping window**."*

**YTOP (3-285)** 도 같은 모양으로 `ydst` 와 비교한다.

**비교 대상은 `xdst`/`ydst` — 그리기 연산의 목적지 주소다.** 그 좌표를 누가
만들었는지(WARP 든 우리 셋업이든)와 무관하게 **그리기 단계에서 하드웨어가
비교한다.** W6 §4 가 "D3-2 측정이 WARP 까지 일반화되지 않는다" 고 축소한 것은
**측정 기준으로는 옳았지만, 기전은 좌표의 출처와 무관하다.**

그리고 음수 `xdst` 가 자동으로 밖이라는 것은 **오버플로·NaN 이 만든 이상
좌표에 대한 방어**이기도 하다.

### 5.7.1 조건 — `clipdis`

> *"Note: **Clipping can be disabled by the clipdis bit in DWGCTL** without
> changing cxleft."*

**클립은 `DWGCTL.clipdis` 가 0 일 때만 듣는다.** 잘못된 스트림이 `clipdis` 를
세우면 봉쇄가 사라진다.

**그리고 이것이 DRM 의 그 코드를 설명한다** — `mga_emit_clip_rect` 가 G400 에서
*"Force reset of DWGCTL on G400 (**eliminates clip disable bit**)"* 라며 DWGCTL 을
다시 쓰는 이유가 바로 이것이다(`mga_state.c:58`). W7 §4.1 이 그 줄을 인용하고도
왜인지는 몰랐다.

**설계 규칙**: 정점 경로에서도 **매 배치마다 `clipdis` 를 0 으로 강제**해야
한다. 참조가 그렇게 한다.

### 5.7.2 WARP 의 읽기 — 마이크로코드는 내부 메모리에 가둘 수 있다

**WIADDR2 의 `wiaddr` (3-269)**:

> *"**When caching is disabled, bit 31 to 11 must be set to '0'** (the microcode
> must reside within the **2 kbyte instruction memory**)."*

즉 캐싱을 끄면 마이크로코드는 **칩 내부 2 KB 명령 메모리** 안에 있어야 하고,
그러면 **WARP 가 시스템 메모리를 마이크로코드로 읽지 않는다.** 읽기 경로 하나가
구조적으로 사라진다.

**남는 읽기 경로**는 정점(2 차 DMA, `secend` 로 유계 — §2)과 텍스처
(`TEXORG` 계열, 별도 논증 필요)다.

---

## 6. GENERAL 모드 — W8 의 철회 사유 하나가 약해진다

**4.1.9 (4-12)**:

> *"In order to send 3D commands to the chip, General Purpose Pseudo-DMA should
> be used. (…) For Vertex transfer to the WARP engine, the Vertex write
> Pseudo-DMA mode should be used.*
> ***Note: This is the recommended usage - any Pseudo-DMA mode can actually be
> used for either case.**"*

W8 은 "GENERAL 은 어디에도 전례가 없으니 보수적 선택이 아니다" 를 철회 사유
(3)으로 들었다. **참조에 전례가 없다는 것은 여전히 사실이지만, 벤더가 명시적으로
허용한다.** 사유 (3)은 **약해진다.**

**사유 (1)과 (2)는 그대로 유효하고, 그중 (1)은 이제 무의미하다** — 시험이
답하려던 질문을 매뉴얼이 답했으므로 **시험 자체가 필요 없다.**

---

## 7. 그래서 무엇이 바뀌는가

| 항목 | 전 | 후 |
| --- | --- | --- |
| Q1 SEC 트리거 | 미해결, 실기 필요 | **해결.** `SECEND` 가 시작, `SECADDRESS` 가 먼저 |
| **Q3 WARP 정지** | 전례 없음 = 위험 | **해결.** Suspend 만 "idle 일 때만" 이 없다 = 도는 중에 쓰는 용도. `WIADDRNB2` 는 FIFO 우회 |
| **Q4 봉쇄** | 측정이 WARP 로 일반화 안 됨 | **기전 해결.** 클립은 `xdst`/`ydst` 하드웨어 비교, 좌표 출처 무관. **단 `clipdis` 를 0 으로 강제해야** |
| **softreset 이 화면을 죽이는가** | 미상, 최대 우려 | **아니다.** *"video circuitry (…) not affected"* |
| **Q2 직렬화** | 추론(W6 §9) | **해결.** 2 차가 끝나야 1 차가 재개(단계 9) |
| **2 차 완료 판정 (§6 최대 blocker)** | **부재** | **`ENDPRDMASTS` 가 1·2 차·setup 전부를 뜻한다** |
| 2 차 읽기 범위 | 한계 미상, 64 KiB 여유로 대응 | **`secend` 에서 정확히 끝난다** |
| 길이 0 제출 | (초안이 "안전한 no-op" 이라 잘못 씀) | **금지됨** — `SECEND != SECADDRESS` |
| SEC 를 MMIO 로 | "아무도 안 한다" | **할 수 없다** |
| 1 차 패딩 | 경험칙 | **문서화된 규칙, no-op 레지스터** |
| 회수 절차 | **"만들 수 없다"** | **벤더가 적어 뒀다** (abort 시 softreset, 10 µs) |
| abort 검출 | 없음 | `DEVCTRL.recmastab` / `rectargab` |
| W8 시험 | 철회 | **철회 유지, 그리고 이제 불필요** |

**W2 §7 의 `SECADDRESS` 금지는 재검토 대상이 됐다.** 금지의 근거는 "회수
수단이 없다" 였고, **회수 수단이 문서화돼 있다.** 다만 그 절차를 콘솔
소유 카드에서 쓰는 것이 안전한지는 §5.3 이 남긴 질문이다 — **금지를 지금
푸는 것이 아니라, 푸는 조건이 달라졌다.**

---

## 8. 그리고 이것이 이 세션의 가장 큰 교훈이다

프로젝트는 **여덟 달 동안** "매뉴얼이 없다" 를 전제로 움직였다. W6 §1.2 는
*"매뉴얼이 없다. `SOFTRESET` 이 무엇을 되돌리고 무엇을 안 되돌리는지 모른다"*
고 적었다. **그 문장을 쓰면서 아무도 찾아보지 않았다.**

DRM 주석이 *"page 4-16 in the G400 manual"* 이라고 **문서 이름을 대고
있었는데도** 그랬다. 인용을 읽고 "매뉴얼이 있구나, 우리에겐 없지" 로 넘어간
것이다.

**없다고 단정하기 전에 찾아본다.** 오늘 grep 이 세 번 빗나간 것과 같은
기전이다 — 있을 거라 생각한 자리만 보고, 실재하는 자리를 안 봤다.

## 9. 다음

1. **§5.3 을 닫는다**: softreset 이 어느 레지스터를 되돌리는지 사양서에서
   전수 확인. 디스플레이가 죽는지 아닌지가 콘솔 카드에서 쓸 수 있는지를 정한다.
2. **Q3(WARP 정지)·Q4(봉쇄)를 사양서에서 찾는다** — Q1·Q2 가 여기서 나왔으니
   나머지도 있을 가능성이 높다.
3. W6·W7·W8 의 뒤집힌 결론을 정정한다.
4. 그다음에 W2 §7 의 금지와 §6 의 blocker 목록을 **다시 판정**한다.

> **주의**: 이 문서를 쓰는 동안에만 내가 두 번 유리하게 읽었다(§1.1, 그리고
> W8 §3 의 0 채움). **매뉴얼이 생겼다고 판단이 안전해지지 않는다.** 오히려
> 읽을 것이 늘어 잘못 읽을 기회도 늘었다.

---

## 10. blocker 재판정 — 매뉴얼 기준

W2 §6·§7 이 들고 있던 안전 blocker 를 사양서로 다시 본다.

| blocker | 근거였던 것 | 매뉴얼이 말하는 것 | 재판정 |
| --- | --- | --- | --- |
| **2 차 DMA 완료 판정 부재** | "1 차가 트랩에 닿아도 2 차가 읽는 중일 수 있다" | *"Use of the primary DMA channel is complete when the primary, **secondary** and setup DMA transfers are finished"* (4-13) | **해소.** 지금 예측이 이미 덮는다 |
| **2 차 읽기 범위 미상** | read-ahead 한계를 아무도 안 적음 | `secaddress` 가 `secend` 에 닿으면 채널이 빈다 (3-168) | **해소.** 구조적으로 유계 |
| **회수 절차 없음** | 참조가 리셋을 안 씀 | abort 시 `RST.softreset`, 10 µs, *"terminate any bus mastering or pseudo-dma transfer"*, 메모리 보존 (4-15, 3-167) | **해소.** 벤더가 절차를 적어 뒀다 |
| **리셋이 콘솔을 죽일 것** | 추측 | *"video circuitry, VGA registers, and frame buffer memory accesses (…) **not affected**"*, OPTION 도 불변 (4-23) | **반증됨** |
| **WARP 무장 해제 없음** | 전례 없음 | Suspend 는 idle 전제가 없는 유일한 모드, `WIADDRNB2` 는 FIFO 우회 (3-269, 3-272) | **해소** |
| **격리 증명이 정책으로** | 클립 측정이 직접 경로만 | 클립은 `xdst`/`ydst` 하드웨어 비교, 좌표 출처 무관. **단 `clipdis`=0 강제 필요** | **크게 축소** |
| **미해결 프리즈 (§3-62)** | 실측 7 회 | 매뉴얼은 이것을 설명하지 않는다 | **그대로** |
| **`SECADDRESS` 를 회수 없이 쓰기 금지** | 회수 수단 부재 | 그 전제가 사라졌다 | **재검토 대상** |

### 10.1 그러나 **금지를 지금 푸는 것이 아니다**

매뉴얼이 답한 것은 **하드웨어가 어떻게 동작하도록 설계됐는가**이지,
**우리 드라이버가 그것을 옳게 쓰고 있는가**가 아니다. 그리고 §3-62 의
프리즈 일곱 번은 여전히 설명되지 않는다 — 그것은 시저와 계측의 조합이었고
2 차 DMA 와 무관하다.

**바뀐 것은 이것이다**: W6 이 "회수가 불가능하니 봉쇄로 대신한다" 고 세운
구조가 이제 **"문서화된 회수가 있으니 그것을 구현하고 검증한다"** 로 바뀔 수
있다. 그것이 다음 설계의 출발점이고, **이 문서는 그 설계가 아니다.**

### 10.2 다음 설계가 지어야 할 것

1. **abort 검출**: `DEVCTRL.recmastab`/`rectargab` 을 제출 경로에서 확인.
2. **회수**: abort 또는 타임아웃에서 `RST.softreset` 10 µs 이상(X.Org 처럼
   200 µs 가 안전) → 0, 그리고 그 뒤 엔진 상태 재초기화.
3. **WARP 비상 정지**: `WIADDRNB2 = 0`.
4. **`clipdis` 강제**: 배치마다 `DWGCTL` 을 다시 써서 0 으로.
5. **그리고 이 넷을 2 차 DMA 없이 먼저 실증한다** — 지금 1 차 경로에서
   abort 를 유도할 수 있는지, softreset 뒤에 화면과 엔진이 돌아오는지.
   **`SECADDRESS` 는 그 뒤다.**

**5 번이 핵심이다.** 회수 절차를 2 차 DMA 와 함께 처음 쓰는 것이 아니라,
**이미 검증된 1 차 경로에서 회수를 먼저 검증한다.** 그러면 2 차를 켤 때
회수는 이미 알려진 것이 된다.
