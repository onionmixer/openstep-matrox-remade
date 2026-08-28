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

### 5.3 남는 위험은 좁아졌지만 남는다

- *"return some register bits to their soft-reset values"* — **어느 비트인지
  레지스터별로 확인해야 한다.** 디스플레이 관련 비트가 돌아가면 콘솔이
  꺼지고, 그때는 우리 모드셋을 다시 돌려야 한다.
- X.Org 는 200 µs 를 쓴다(매뉴얼 최소치의 20 배). 보수적인 값이다.
- X.Org 가 주 카드에 쓰지 않는 것은 여전히 사실이고, **왜 그런지는 매뉴얼이
  설명하지 않는다.**

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
