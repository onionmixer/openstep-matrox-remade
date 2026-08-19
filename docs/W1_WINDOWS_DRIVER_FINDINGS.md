# W1 – Windows G400/G450 driver findings (A-scope)

## 0) Is `%ecx` the MMIO base or a context pointer?

Observation:
- In `0xbaa418f0`, `%ecx` is loaded from `*(arg1 + 0x2a6)` and then used repeatedly for accesses like `1e10(%ecx)`, `1e14(%ecx)`, `1c00/1c04/1c1c/1d00(%ecx)`, `1d5c(%ecx)`, `1c58/1c60/...(%ecx)`.
- It performs hardware-status style polling loops on `1e14(%ecx)` and gating on `1e10(%ecx)` feature bits.
- It programs command/status registers (`1d00(%ecx)=0x840c4008`, `1d5c(%ecx)=1`, `1c5c(%ecx)=1`, `1c1c(%ecx)` etc.) in a clear MMIO command-sequence pattern.

Inference:
- `%ecx` is the **MMIO aperture pointer** (not a plain software context struct).

## 1) Writes at/around `baa41b17..baa41b49` (function `0xbaa418f0`)

Observation:
- `0xbaa41b0b`: `mov 0x40(%esp), %eax` then `mov %eax, 0x1c8c(%ecx)`.
- `0xbaa41b17`: `xor %eax, %eax` then `mov %eax, 0x1c58(%ecx)` (SGN).
- `0xbaa41b1d`: `mov %eax, 0x1c6c(%ecx)` (AR3).
- `0xbaa41b23`: `mov %edi, 0x1c60(%ecx)` (AR0).
- `0xbaa41b29`: `mov 0x18(%esp), %eax` then `mov %eax, 0x1c74(%ecx)` (AR5).
- `0xbaa41b33..0x41b49`: `mov 0x1c(%esp), %eax; add $0xffff,%eax; shl $0x10,%eax; and $0xffff,%esi; or %esi,%eax; mov %eax, 0x1c84(%ecx)` (FXBNDRY).

Inference:
- Store order in the cluster is exactly: `1c8c` (earlier instruction), `1c58`, `1c6c`, `1c60`, `1c74`, `1c84`.
- Sources are stack/register-derived values local to `0xbaa418f0`:
  - `0x1c8c`: stack slot at `0x40(%esp)` (forwarded argument area from caller chain).
  - `1c58`: immediate zero.
  - `1c6c`: cleared to zero here.
  - `1c60`: `%edi` (a value precomputed from `[*(0x3c(%esp)+0x10] - [*(0x3c(%esp)+0x8)]` in the setup sequence).
  - `1c74`: value from `0x18(%esp)`.
  - `1c84`: packed from `0x1c(%esp)-1` in high half-word and low 16 bits from `%esi` (low half-word of `%ebp`).

Observation (continuing in same function):
- If `ebx != 0`, loop updates:
  - `0xbaa41bb2`: `mov %eax, 0x1c6c(%ecx)`
  - `0xbaa41bba`: `add %edi, %eax; mov %eax, 0x1c60(%ecx)`
  - `0xbaa41bce`: `mov %edx, 0x1c84(%ecx)` where `%edx=(esi-0x10000)&0xffff_0000 | (%ebp & 0xffff)`.
  - then decrements counter and repeats.

Inference:
- The loop is a per-scanline/segment update stream; AR3, AR0, and FXBNDRY are repointed every iteration, so these are not one-time constants for the trapezoid set.

## 2) Caller-derived computation and e (Bresenham error term)

Observation:
- I can directly observe `1c58` and `1c6c` being set from `eax = 0` at this entry point and in-loop from `%eax` update logic.
- `1c6c/1c60` are then updated by `%edi` increments per iteration as above.

Inference:
- The function does not expose a separate explicit register write that is obviously an XAA-style `e` accumulator assignment by name.
- `e` as “exact definition and initial value” cannot be proven from this routine alone without identifying the caller stack-frame contract for the `arg` values threaded via `%esp`.
- What is directly provable here:
  - The AR3 write at `baa41b1d` is initialized to 0 in this cluster.
  - In the iteration path, AR3 starts from and then evolves from the same counter used to compute AR0.
- Not determined: whether this 0 corresponds to XAA’s `e`, or whether `e` is encoded elsewhere before entering `0xbaa418f0`.

## 3) Does trapezoid path write AR3 (0x1c6c), and what?

Observation:
- Yes.
  - Initial write: `0xbaa41b1d` writes `0`.
  - Runtime loop write: `0xbaa41bb2` writes `%eax` each iteration.
  - `0xbaa41bb8` follows `AR0 = AR3 + const_delta` and `AR0`/`AR3` remain tightly coupled.

Inference:
- In this path AR3 is definitely used as an active trapezoid/scanline endpoint register, updated together with AR0 and FXBNDRY in the inner submission loop.
- Meaning: it is one of the two edge-x registers for span generation in this hardware program sequence; exact edge direction/sign semantics are not finalised from this cluster alone.

## 4) Triangle split into trapezoids: criterion

Observation:
- `0xbaa418f0` has mode/shape-dependent control flow and multiple write paths (primary path from `0x41b6d`, fallback path `0x41bf0..41de0`), including dispatch based on values derived from `0x3c(%esp)` / `0x34(%esp)` and `0x28c(%edx)`-style fields.

Inference:
- Not enough to assert the full “triangle -> two trapezoids vs many trapezoids” split rule from this cluster alone.
- Not determined: precise triangle split criterion in caller terms.
- Needed to settle: the immediate caller chain before `0xbaa418f0` with argument struct field definitions (especially which struct fields at the relevant `0x2a/0x3c/...(%esp)` offsets are X/Y vertices, signs, and split flags).

## Overall completion status

- Q0: **Concluded MMIO base**.
- Q1: **Established** for this cluster (ordered writes + visible sources).
- Q2: **Not fully determined** (can state what this function writes, but not definitive XAA `e` definition/initial value).
- Q3: **Concluded yes** (AR3 is written here, and its active update pattern is observed).
- Q4: **Not determined from this function alone**.

---

# 검증 (Claude, 2026-08-19) — codex 결과를 소스로 재확인한 결과

이 프로젝트는 codex 결과를 그대로 채택하지 않는다. 위 내용을 디스어셈블로
직접 대조했다.

## V-1. 전사는 정확하다

`baa41b0f`~`baa41b49`의 명령과 저장 순서는 **한 글자도 틀리지 않았다.**
`0x1c8c`(PITCH) → `0x1c58`(SGN=0) → `0x1c6c`(AR3=0) → `0x1c60`(AR0=%edi) →
`0x1c74`(AR5) → `0x1c84`(FXBNDRY).

## V-2. Q0(MMIO인가)은 옳다

`%ecx`가 MMIO 구경이라는 결론을 지지하는 독립 증거: `baa41b9a`에
`mov 0x1e10(%ecx),%edx; and $0x1f,%edx; cmp $0x5,%dl; jb` — 우리 드라이버의
`osmgaStormWaitFifo`와 **같은 형태의 FIFO 대기 루프**다. `0x1e10`은
FIFOSTATUS이고 하위 5비트가 여유 슬롯 수다.

## V-3. **Q3은 틀렸다 — 이 함수는 사다리꼴이 아니라 BITBLT다**

codex는 이 시퀀스를 "사다리꼴 경로"로 부르고 `AR3`를 쓴다고 답했다. 그러나
**바로 다음 세 명령을 결론에 넣지 않았다**:

```
baa41b4f:  movl $0x1,        0x1c5c(%ecx)   ← LEN
baa41b59:  mov  %eax,        0x1c90(%ecx)   ← YDST
baa41b63:  movl $0x840c4008, 0x1d00(%ecx)   ← DWGCTL+EXEC = 실행
```

`0x840c4008`을 풀면 **opcode `0x8` = BITBLT**이고, 우리 S2의 BITBLT 값
`0x040c4008`과 **`0x80000000`(CLIPDIS) 하나만 다르다.**

→ **이 함수는 BITBLT다.** `AR3`는 BITBLT에서 소스 스팬 시작 주소이며, 우리가
이미 아는 역할이다. 따라서 `REMAINING_WORK.md` §3-5의 결론(`AR3`는
BITBLT/ILOAD 계열 전용, 사다리꼴과 무관)은 **뒤집히지 않고 세 번째 출처로
보강된다** — X.Org DDX, 원본 OPENSTEP 드라이버에 이어 Windows HAL도 같다.

**교훈**: 과제 문서가 "사다리꼴 경로"라는 이름표를 미리 붙여서 넘겼고,
분석이 그 이름표를 물려받았다. **검색 기준을 레지스터가 아니라 opcode로
잡았어야 했다.**

## V-4. 옳은 기준으로 다시 — `DWGCTL` opcode 조사

`DWGCTL`(0x1c00)과 `DWGCTL+EXEC`(0x1d00)에 쓰는 **즉시값 13곳, 고유값 7개**:

| 값 | opcode | atype | CLIPDIS | 횟수 |
| --- | --- | --- | --- | --- |
| `0x840c4008` | BITBLT | RPL | **SET** | 4 |
| `0x840c6008` | BITBLT | RPL | **SET** | 3 |
| `0xc40c4008` | BITBLT | RPL | **SET** | 2 |
| `0x48006009` | ILOAD | RPL | - | 1 |
| `0x08086019` | ILOAD | RSTR | - | 1 |
| `0x040c6009` | ILOAD | RPL | - | 1 |
| **`0x000c7076`** | **TEXTURE_TRAP** | **I** | - | 1 |

`baa620fb`의 `0x000c7076`은 **우리가 실기에서 동작시킨 `0x000c7074`
(TRAP\|I)와 opcode 한 비트만 다르다.** ARZERO·SGNZERO·SHIFTZERO·bop이 전부
같다. 우리 D3-0/D3-1 값 선택이 출하 드라이버와 같은 계열임을 뒷받침한다.

**단 이 값도 ARZERO/SGNZERO가 켜져 있다 — 축 정렬이다.** 즉 이 즉시값
경로에는 기울어진 사다리꼴이 없다.

## V-5. 아직 못 찾은 것 — hot path는 레지스터 경유다

`DWGCTL`에 **레지스터 값**을 쓰는 곳이 11군데 있다(`baa156ca`, `baa184db`,
`baa1ab4d`, `baa25f1d`, `baa28836`, `baa41e3a`, `baa42c7c`, `baa4942b`,
`baa49739`, `baa542c4`, `baa559cb`). 정적 즉시값 조사로는 여기서 어떤
opcode가 나가는지 알 수 없다. **기울어진 삼각형이 있다면 이 중에 있다.**

이것이 다음 분석의 대상이며, 검색 기준은 처음부터 opcode였어야 했다.

## V-6. 부수 관측 — HAL은 BITBLT에서 CLIPDIS를 켠다

즉시값 BITBLT 3종이 **전부 CLIPDIS(bit31)가 서 있다.** 출하 드라이버는
하드웨어 클립을 끄고 소프트웨어에서 클리핑을 처리하는 쪽을 택했다는 뜻이다.

우리는 반대로 `CXBNDRY`/`YTOP`/`YBOT` 클립을 **안전 봉쇄 수단으로 의존한다**
(D3-2 §3-1). 설계 선택이 다르다는 것이며 우리 쪽이 틀렸다는 뜻은 아니다 —
우리는 오프스크린 실험 중이고 저쪽은 성능을 쫓는다. 다만 **하드웨어 클립이
성능상 공짜가 아닐 수 있다**는 신호로 기록해 둔다.
