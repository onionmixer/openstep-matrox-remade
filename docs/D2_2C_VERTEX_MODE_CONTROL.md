# D2-2c — 1 차 vertex 모드 대조군 (2026-08-28, 코딩 전)

D2-2b 의 계획은 죽었다: 정점은 `WDBR` 로 도착하지 `WR` 로 도착하지 않는다.
그 검토가 남긴 유일한 길이 이것이다:

> *"Primary VERTEX mode is costly as a production design, but the cost is
> irrelevant for a one-shot control."*

**W2 §24 가 1 차 vertex 모드를 기각한 이유는 생산 비용(run 3 개, +3.96 ms)이고,
삼각형 하나짜리 시험에서 그 비용은 0 이다.**

---

## 1. 무엇을 묻는가

**WARP 마이크로코드가 정점을 받아 삼각형을 그리는가.** 그것뿐이다.

수송 방식을 고르는 것이 아니다 — **문서화된 유일한 정점 경로로 한 번
그려 보는 것**이고, 그것이 되면 나중에 어떤 수송을 쓰든 *"마이크로코드와
상태는 옳다"* 를 알고 시작한다.

## 2. 왜 2 차 DMA 가 아닌가

| | 2 차 DMA | **1 차 vertex 모드** |
| --- | --- | --- |
| 새 수송 채널 | **있음** — 네 번 NO-GO | **없음** |
| 회수 필요 | 있음 (그리고 없음) | 1 차와 같음 |
| `SECADDRESS`/`SECEND` | 새 레지스터 | **안 건드림** |
| `PRIMPTR` 상태 쓰기 | `SECEND` 가 유발 | **해당 없음** |
| 생산 비용 | 낮음 | 높음 — **그러나 무관** |

**두 번째 열이 이 문서의 전부다: 새 채널을 열지 않는다.**


---

# 개정 1 판 (2026-08-28) — codex 검토 후

**초판은 기각됐다. 아래가 정본이고, 초판의 §3~§10 은 폐기다.**
기각 사유 네 가지 중 **하나는 이 저장소의 내 문서가 이미 반증하고 있었다.**

## 3. 검토 판정표

| codex 주장 | 검증 방법 | 결과 |
| --- | --- | --- |
| 파이프 7(`tgzsaf`)이어야 하고 계획은 파이프를 안 정했다 | `mga_drm.h:50-62` — `MGA_S\|MGA_F\|MGA_A` = 4\|1\|2 = **7**. `mgastate.c:958` 이 셋을 무조건 OR | ✅**채택 — 그리고 내 W2 §8 이 이미 "파이프 0 이 아니라 파이프 7" 이라 적어 놨다** |
| 기존 빌더가 START 직후 `WIADDR2=SUSPEND` 를 낸다 | `:7789-7793` 을 열었다. `if (startPhys != 0UL)` 안에서 **같은 목록에** SUSPEND 블록을 낸다 | ✅**채택 — 재사용하면 run 2 전에 WARP 가 정지한다** |
| `PRIMADDRESS` 쓰기는 pseudo-DMA 시퀀스를 리셋할 뿐, `ENDPRDMASTS` 나 대기 중 SOFTRAP 을 지우지 않는다 | 사양서 4-15 의 리셋 조건 목록은 **패킷 해독 시퀀스** 이야기다. 3-189: *"Restarting the primary DMA by accessing **PRIMEND** will reset endprdmasts to '0'"* | ✅**채택 — 내 "깨끗한 판정" 논거가 틀렸다** |
| `ENDPRDMASTS` 만으로는 run 2 의 것인지 모른다 → `PRIMADDRESS == 예상 끝` 을 봐라 | 3-189: 이 비트는 *"or when a soft trap interrupt occurs"* — 실행 완료 비트가 아니다. 그리고 `:8043` 의 `primAfter` 가 **이미 그 검사를 하고 있다** | ✅채택 |
| `PRIMEND` 의 bit0/bit1 을 안 정했다 | 3-165: `primnostart<0>`, `pagpxfer<1>`. 저장소에 상수(`:1035-1036`)와 올바른 선례(`:7490` *"PCI: neither PRIMNOSTART nor PAGPXFER"*)가 **이미 있다** | ✅채택 (아래 함정 주의) |
| `PITCH` 가 필수 전역 상태인데 목록에 없다 | 4-30 "Global Initialization (All Operations)": PITCH·DSTORG·MACCESS·CXBNDRY·YTOP·YBOT·PLNWT·ZORG | ✅채택 |
| `TEXCTL2.map1`/`TEXWIDTH.map1`/`TEXHEIGHT.map1` 이 0 이어야 하고 TEXWIDTH/HEIGHT 를 안 낸다 | 3-215: `tmap0dis <= TEXCTL2.map1 or TEXWIDTH.map1 or TEXHEIGHT.map1`. DRM `mga_state.c:180` 이 TEXWIDTH/TEXHEIGHT 를 낸다 | ✅채택 |
| `PRIMPTR` 이 0 으로 안 읽히면 시작을 거부해야 한다 | 3-166: `primptren0` 이면 **SOFTRAP 마다** 시스템 메모리에 double-qword 를 쓴다. run 1 이 **SOFTRAP 으로 끝난다** — 가정이 아니라 직결이다. `:3944` 는 확인만 하고 로그만 남긴다 | ✅채택 |
| `recmastab`/`rectargab` 를 봐야 한다 | 4-15 확인. **그리고 그 다음 문장이 더 중요하다**: abort 시 *"software must write to the softreset bit of the RST register"* — **우리는 그걸 못 쓴다(W10)** | ✅채택 + 강화 |
| 8 점 카나리는 봉쇄를 증명하지 않는다 | 자명하게 참이다. 버퍼 전체 비교로 바꾼다 | ✅채택 |
| `stormLock`/`stormBusy` 를 두 run 에 걸쳐 점유해야 한다 | `:6122` 생산 경로가 정확히 그 형태다 | ✅채택 |
| 기존 헬퍼는 타임아웃에도 `ICLEAR` 를 쓰고(`:8038`) 링을 무조건 해제한다(`:8142`) | 둘 다 소스에서 확인. 초판이 선언한 "전부 보유, 아무것도 안 함" 과 정면 충돌 | ✅채택 — **헬퍼를 재사용하지 않는다** |
| `SRCORG.srcmap` 이 시스템 메모리를 고를 수 있다 | 3-186 확인. 다만 같은 쪽이 *"srcorg field is used during BitBlit operations"* 라 한다 — WARP 삼각형은 blit 이 아니다 | ⚖️**부분** — 1 dword 짜리 보험으로 0 을 박되, **증명된 위험이 아니라 미증명으로 기록** |
| "full DRM context" 라 불렀지만 실은 혼합이다 | `mga_g400_emit_context` 와 `mga_g400_emit_tex0` 는 별개 함수다(`:96`, `:158`). 내가 나열한 내용은 둘의 **합집합**이었다 | ⚖️**부분** — 내용은 맞고 **이름이 틀렸다**. 이름을 고친다 |
| run 이 둘인 것 자체 | codex 도 *"defensible in principle"* 이라 했다 | ✅구조는 생존, **논거만 교체** |
| 결과가 licence 하는 범위가 과장됐다(§1 의 *"마이크로코드와 상태는 옳다"*) | 파이프 7 은 텍스처·알파·스펙큘러·포그를 모두 켠다. 한 삼각형은 그중 무엇도 개별 검증하지 않는다 | ✅채택 |

### 3.1 codex 가 못 본 것 — 내가 찾은 두 가지

**(a) 텍스처를 끄는 것은 파이프 선택이 아니다.** 초판은 *"텍스처 없는 삼각형"* 을
전제했는데, **G400 단일텍스처 파이프 8 개는 전부 `TGZ*` 다**(`mga_drm.h:55-62`) —
텍스처 없는 파이프란 것이 없다. `mgavb.h:100` 이 `MGA_TEX0_BIT` 를 `0x10`,
*"non-warp parameters"* 로 두고 `mgastate.c:957` 이 그 비트를 파이프 index 에서
**지운다**. 즉 정점은 언제나 `tu0`/`tv0` 슬롯을 싣고, 텍스처 적용 여부는
**컴바이너**가 정한다. `mgastate.c:1169` 의 초기값 `tdualstage0 = 0` 이 그것이고,
이 드라이버도 이미 `:9020`·`:9114`·`:9644`·`:9808` 에서 `TDUALSTAGE0 = 0` 을 쓴다.
→ **`TDUALSTAGE0 = TDUALSTAGE1 = 0` 을 명시 상태로 넣는다.**

**(b) run 1 의 완료를 idle 술어로 판정하면 영원히 안 끝난다.** run 1 은 WARP 를
기동해 **정점을 기다리는 상태로 남기는 것이 목적**이다. 3-189 의 `dwgengsts` 는
*"warpfifo is not empty"* 에도 1 이고 `wbusy` 는 WAIT 에도 1 이다. 그러므로:

```
run 1 완료 =  PRIMADDRESS == 목록 끝  &&  ENDPRDMASTS      <- WARP 는 busy 여도 된다
run 2 완료 =  PRIMADDRESS == 정점 끝  &&  ENDPRDMASTS
              && !dwgengsts && !wbusy && !wbusy1            <- 여기서만 idle 을 요구
```

**초판은 두 run 에 같은 술어를 썼다. 그것으로는 run 1 이 통과하지 못한다.**

## 4. `PRIMEND` 함정 — 대칭으로 쓰면 안 된다

```
PRIMADDRESS = phys | 0x3     <- primod<1:0> = 11, DMA Vertex Write   (3-164)
PRIMEND     = phys + 96      <- bit0 primnostart = 0, bit1 pagpxfer = 0 (3-165)
                                 ^^^^ 절대 | 0x3 하지 않는다
```

**두 레지스터의 하위 2 비트는 서로 다른 것이다.** 대칭으로 `| 0x3` 을 쓰면
`pagpxfer=1`(AGP 전송) + `primnostart=1`(시작 안 함)이 된다. 기존 코드가
`| MGA_DMA_GENERAL` 로 무사한 것은 **그 값이 `0x00`(`:1034`)이기 때문일 뿐**이다.

사양서 3-165 의 산문은 *"will start ... (unless primnostart = 0)"* 라고 **두 번**
적었는데 이는 오타다 — 같은 쪽 비트 서술(*"with this bit set to '1' will not
restart"*)이 정본이다. **산문이 아니라 비트 표를 따른다.**

그리고 run 1 이 SOFTRAP 으로 끝나므로 **Softrap Interrupt 가 pending 인 채로**
run 2 의 PRIMEND 를 쓴다. `primnostart=0` 이면 그래도 시작하지만, run 2 전에
`ICLEAR.softrapiclr` 로 **명시적으로 acknowledge 한다.**

## 5. run 1 상태 — 정확한 목록

전역 필수(4-30) + 컨텍스트(`mga_state.c:96`) + 텍스처(`mga_state.c:158`) + 정점 형식.

| 묶음 | 레지스터 |
| --- | --- |
| 전역 필수 (4-30) | `PITCH` `DSTORG` `MACCESS` `CXBNDRY` `YTOP` `YBOT` `PLNWT` `ZORG` |
| 원점·맵 선택 강제 | `YDSTORG=0` `SRCORG=0`(미증명 보험) `PRIMPTR=0`(**읽어서 0 아니면 중단**) |
| 컨텍스트 | `DWGCTL` `ALPHACTRL` `FOGCOL` `WFLAG` `WFLAG1` `TDUALSTAGE0=0` `TDUALSTAGE1=0` `FCOL` `STENCIL` `STENCILCTL` |
| 텍스처 (map1 전부 0) | `TEXCTL` `TEXCTL2` `TEXFILTER` `TEXORG` `TEXWIDTH` `TEXHEIGHT` `TEXBORDERCOL` |
| 정점 형식 | `WVRTXSZ = 0x1807` (wvrtxsz=7 → 8 dword/정점, primsz=0x18=24 → 24 dword/프리미티브, 3-278) |
| 클립 리셋 | G400 `DWGCTL`+`0x80000000` 쌍 |
| 파이프 | `WIADDR2 = pipePhys[7] \| WMODE_START` — **뒤에 SUSPEND 를 붙이지 않는다** |
| 종결 | `SOFTRAP` |

깊이·스텐실은 *"ZORG=0 또는 끔"* 이 아니라 **`DWGCTL.zmode=NOZCMP` + `zdrwen=0`,
`STENCILCTL` 전 비교 off** 로 못 박는다. 수치는 구현 직전에 사양서 쪽수와 함께 확정한다.

## 6. run 2 — 정점 24 dword

`mgavb.h:55` 구조체 순서, `mgavb.h:42` 색 구조체가 **BGRA 바이트 순서**:

```
dword 0..3   x, y, z, rhw            (float)
dword 4      diffuse   B,G,R,A 바이트
dword 5      specular  B,G,R, fog    (alpha 자리가 fog — mgavb.c:57)
dword 6..7   tu0, tv0                (float)
```

×3 = **24 dword = 96 바이트**, `WVRTXSZ` 와 일치.

**해결됨 — §10 참조. 24 번째 dword 가 트리거다.** 별도 트리거는 없다.
단 그것은 `WACCEPTSEQ.seqoff = 1` 을 프로그램했을 때만 성립하고,
**이 드라이버는 이미 그것을 하고 있다.**

## 7. 봉쇄 — 카나리를 버린다

8 점은 8 개 워드를 증명한다. 대신:

- 대상 버퍼 **전체**를 알려진 패턴으로 채우고 **전체를 비교**한다.
- 클립 사각형 밖의 같은 버퍼 영역도 같은 비교에 포함된다.
- 이것은 **손상 탐지**이지 봉쇄 증명이 아니다. **절대 봉쇄는 주장하지 않는다** —
  시험 대상이 그리기 레지스터를 쓸 수 있는 불투명 마이크로코드인 한 불가능하다.

## 8. 실패 정책

- **abort(`recmastab`/`rectargab`) 는 제자리 회복 불능이다.** 4-15 가 요구하는
  `RST.softreset` 을 우리는 못 쓴다(W10 에서 라이브 콘솔 카드에 대해 확정).
  → 전부 보유, 아무것도 더 쓰지 않음, **재부팅 요청**.
- 타임아웃도 같다. `ICLEAR` 를 쓰지 않고 링을 해제하지 않는다.
- **기존 `runWarpPipeOnce` 를 재사용하지 않는다** — `:8038` 과 `:8142` 가 이 정책을
  정면으로 위반한다. D2-2c 는 자기 헬퍼를 쓴다.
- 모든 준비(할당·물리 연속성 확인·정점 구성·버퍼 채움·스냅샷·외부 로깅)는
  **run 1 이 시작되기 전에 끝난다.** WARP 가 정점을 기다리는 동안 실패할 수 있는
  단계가 있어서는 안 된다.
- run 1 직후 · run 2 직전에 **버퍼 체크포인트**를 남긴다 — 파이프 기동이 낸 손상을
  run 2 탓으로 돌리지 않기 위해.

## 9. 결과가 licence 하는 것 — 축소판

삼각형이 나오면 이것만 성립한다:

> **이 카드 이 부팅에서, 1 차 DMA VERTEX 모드가 이 24 dword 를 파이프 7 에
> 전달했고, 파이프 7 이 이 상태에서 이 삼각형을 냈다.**

성립하지 **않는** 것: 다른 파이프, 텍스처·깊이·스텐실·포그·알파·스펙큘러의
개별 정확성, 여덟 필드의 의미 각각, 생산 배치·flush·상태 전이 규칙, 임의
워크로드의 완료 규약, 전역 봉쇄, 2 차 DMA 나 `WDBR` 수송에 대한 무엇이든.

**초판 §1 의 *"마이크로코드와 상태는 옳다"* 는 철회한다.**

안 나오면 원인 후보는 여전히 여럿이다 — 파이프, 상태, 좌표 규약, 컬링·와인딩,
깊이 기각, 정점 가시성, 마이크로코드 불일치, **그리고 수송 자체**. W2 §24.1 이
확인했듯 1 차 VERTEX 모드는 세 참조 구현 어디에도 실행 선례가 없다.
**"실패하면 수송은 용의선상에서 빠진다" 는 초판의 주장은 거짓이다.**

---

## 10. ACCEPT 트리거 — 확정 (2026-08-28)

**질문:** 정점 24 dword 가 도착하면 ACCEPT 시퀀서가 스스로 그리는가,
아니면 별도 트리거가 필요한가.

**답: 스스로 그린다. 24 번째 dword 가 트리거다. 별도 트리거는 없다.**

### 10.1 근거 사슬

**(1) 사양서 3-261, `WACCEPTSEQ.seqoff<28>`:**

> *"When seqoff = 1 the `ACCEPT.seq` instructions are treated like the first
> `ACCEPT.seq` (using the destination of the `ACCEPT.seq` command and
> **the size of `primsz`**)."*

**(2) 사양서 3-278, `WVRTXSZ.primsz<13:8>`** — 그 `primsz` 가 프리미티브의
dword 길이다. `WVRTXSZ = 0x1807` 을 python 으로 풀면:

```
wvrtxsz = 7  -> 8 dword/정점
primsz  = 24 -> 24 dword/프리미티브 = 3 정점        <- 정확히 삼각형 하나
```

즉 `seqoff=1` 이면 ACCEPT 시퀀서는 **길이를 명령이 아니라 레지스터에서** 얻는다.
24 dword 를 세고, 다 차면 실행한다.

**(3) 참조 구현 둘이 독립적으로 `seqoff` 를 켠다** (python 으로 디코드):

| 출처 | 값 | `seqoff` | `wsametag` | `wfirsttag` | `seqlen` |
| --- | --- | --- | --- | --- | --- |
| DRM `mga_warp.c:180` (init, MMIO) | `0x18000000` | **1** | 1 | 0 | 0 |
| DRM `mga_state.c:325` (단일텍스처 pipe) | `0x18000000` | **1** | 1 | 0 | 0 |
| DRM `mga_state.c:296` (이중텍스처 pipe) | `0x1e000000` | **1** | 1 | 1 | 2 |
| Windows `g400dd32.asm:61433` | `0x10000000` | **1** | 0 | 0 | 0 |

네 값이 다르지만 **`seqoff` 만은 넷 다 1 이다.**

**(4) 제출에 전후 장식이 없다.** `mga_state.c:679` 의 정점 dispatch 전부:

```c
DMA_BLOCK(MGA_DMAPAD, 0, MGA_DMAPAD, 0,
          MGA_SECADDRESS, (address | MGA_DMA_VERTEX),
          MGA_SECEND,     ((address + length) | dma_access));
```

**프롤로그도 에필로그도 트리거 레지스터도 없다.** 정점 버퍼가 제출의 전부다.
1 차 VERTEX 모드의 대응물은 `PRIMADDRESS = phys|0x3` / `PRIMEND = phys+96`,
역시 맨몸이다.

### 10.2 그리고 이 드라이버는 이미 그것을 하고 있다

내가 "미해결" 이라고 적은 것이 **두 곳에 이미 구현돼 있었다**:

```
:8093-8097   WARP init (MMIO)      WIADDR2=SUSPEND, WGETMSB, WVRTXSZ, WACCEPTSEQ, WMISC
                                   -> DRM mga_warp.c:177-191 과 레지스터·순서·값이 같다
:7754-7761   osmgaDmaBuildPipeList WVRTXSZ=0x1807, WACCEPTSEQ 4 연속(0,0,0,0x18000000)
                                   -> DRM mga_state.c:317-325 와 같은 모양
:341-342     MGA_WVRTXSZ_G400 / MGA_WACCEPTSEQ_G400 상수
```

**계획서에서 빠졌던 것이지 드라이버에서 빠졌던 것이 아니다.**
§5 의 상태표에 `WACCEPTSEQ = 0x18000000` 을 명시로 올린다.

`WACCEPTSEQ` 를 같은 블록에서 **네 번 연속** 쓰는 이유는 문서화돼 있지 않다
(3-260 은 프로그램할 때마다 `seqptr` 이 `seqdst0` 으로 리셋된다고만 한다).
**이유를 모르므로 그대로 베낀다** — `DMAPAD` 로 바꾸지 않는다.

### 10.3 순서가 뒤집혀 있었다

`mga_g400_emit_state:380-397` 의 순서는 **파이프 → 컨텍스트 → tex0 → tex1** 이다.
초판은 *"컨텍스트 + 클립 + 파이프 기동"* 이라고 **파이프를 마지막에** 두었다.
참조 순서로 뒤집는다. WARP 는 기동 후 정점을 기다리므로, 그 사이에 그리기
상태가 도착하는 것이 정상 순서다.

**그리고 §3 의 SUSPEND 판정이 이것으로 정밀해진다.** `mga_g400_emit_pipe` 는
**SUSPEND 로 시작해서 START 로 끝난다**(`:281`, `:346`). 즉 선행 SUSPEND 는
옳고 필수다. `osmgaDmaBuildPipeList` 의 잘못은 **START 뒤에 하나 더** 붙이는
`:7790-7793` 이다. **앞의 것은 남기고 뒤의 것만 지운다.**

### 10.4 이 사양서로는 더 못 간다 — 챕터 6 이 없다

`WFLAG`(3-263)·`WCODEADDR`(3-262)·`WMISC` 가 *"'Pipeline Operation' on page
**6-6**"*, *"'Cache Operation' on page **6-**"*, *"page **6-16**"* 를 가리킨다.
**이 690 쪽 문서의 목차는 챕터 5(Hardware Designer's Notes)에서 끝난다.**

```
Chapter 1 MGA Overview / 2 Resource Mapping / 3 Register Descriptions
Chapter 4 Programmer's Specification / 5 Hardware Designer's Notes
                                       ^^^^ 여기까지. 6 은 없다.
```

**따라서 `ACCEPT` 명령어 집합과 WARP 파이프라인 동작은 이 문서에 없다.**
`seqoff`/`primsz` 처럼 **레지스터가 노출하는 것**은 답할 수 있지만, 마이크로코드
내부 동작은 참조 구현에서만 온다. 이번 답도 사양서 단독이 아니라
**사양서(무엇을 설정하는가) + 참조 구현(실제로 그렇게 설정한다) + 제출 경로에
트리거가 없다는 전수 확인**의 합이다. 그 이상의 확실성은 이 문서로 못 얻는다.

---

## 11. 구현 (2026-08-28) — 빌드됨, 그러나 **비활성**

`runWarpTriangleVertexTest`. 빌드 통과(`BUILD_EXIT=0`), 새 경고 없음,
문자열이 `_reloc` 안에 있는 것을 확인. **설정 테이블에 플래그가 없으므로
현재 상태로는 실행되지 않는다** — `"WARP Triangle Test" = "Yes"` 를 넣어야 돈다.

`"WARP Test"` 와 **별도 플래그**다. D2-2a 는 파이프를 켜고 멈추지만
이것은 정점을 먹인다. 위험이 다르면 스위치도 달라야 한다.

### 11.1 계획대로 들어간 것

| | 어디 |
| --- | --- |
| 파이프 7, START 뒤 SUSPEND 없음 | `osmgaDmaBuildTriangleList` |
| DRM 순서: 파이프 → 컨텍스트 → 텍스처 → 전역 → 클립 | 같은 함수 |
| `WVRTXSZ`/`WACCEPTSEQ` 4 연속 그대로 복사 | 같은 함수 |
| `PRIMEND` 를 `\| 0x3` 하지 않음 | `MGA_DMA_VERTEX` 주석에 이유 명시 |
| run 1 은 idle 을 요구하지 않음, run 2 만 요구 | 두 폴 루프가 술어가 다름 |
| `PRIMADDRESS == 예상 끝` 을 양쪽 run 에서 검사 | 두 폴 루프 |
| `PRIMPTR != 0` 이면 **시작 거부** | 진입부 |
| `recmastab`/`rectargab` 를 전후로 읽음 | `MGA_CFG_DEVCTRL_ABORTS` |
| `stormLock`/`stormBusy` 를 두 run 에 걸쳐 점유 | 진입부·종료부 |
| 실패 시 `ICLEAR` 안 씀, 링·마이크로코드 보유, `stormBusy` 유지 | 두 타임아웃 분기 |
| 카나리가 아니라 **64×64 전체 비교** | `osmgaD2cReport` |
| run 1 직후 체크포인트 | `osmgaD2cReport(.., "run1")` |
| 마이크로코드·크기·파이프표 정적 보관 | `osmgaWarpUcodeResident` 외 |

### 11.2 구현하며 드러난 것 셋

**(a) 커널은 FPU 를 못 쓴다.** 정점 좌표는 float 인데 커널 드라이버에서
부동소수 연산을 할 수 없다. `osmgaF32FromUInt` 가 정수 연산만으로 IEEE 754
단정도 비트 패턴을 만든다. 호스트의 실제 float 인코딩과 0..4096 및 이 시험이
쓰는 값들에 대해 **전수 대조해 0 불일치**를 확인했다.

**(b) 기존 프로브의 오프스크린 기하는 이 모드에서 성립하지 않는다.**
`OSMGA_S1_VRAM_PROVEN` 은 7 MiB 인데 **1600×1200×32 의 가시 이미지만 7.32 MiB** 다.
D3-2 의 `testY = height + 256` 을 그대로 베꼈으면 `byteEnd` 가 9.27 MiB 가 되어
**아무 말 없이 skip** 됐을 것이다 — 통과로 오독하기 딱 좋은 실패다.
드라이버가 이 부팅에 실제로 증명한 `osmgaMmapWindowStart/End` 를 쓰고,
`DSTORG=0` 에 절대 좌표를 주므로 창 시작을 **행 경계로 올림**한다.

**(c) `(void)ring[i]` 는 배리어가 아니다.** 최적화가 지워도 되는 읽기다.
D2-2a 처럼 합을 누산하고 그 합을 쓰는 형태로 고쳤다.
**컴파일되면 사라지는 배리어는 없는 것보다 나쁘다** — 있는 것처럼 읽히므로.

그리고 이 블록은 클라이언트에게 나눠주는 창 안이므로, 보고가 끝나면
**0 으로 되돌린다**. 우리 sentinel 을 남겨두지 않는다.

### 11.3 실행 전에 남은 것

`"WARP Triangle Test" = "Yes"` 를 설정에 넣고 **재부팅**해야 돈다.
그 순간이 이 프로젝트에서 처음으로 **WARP 에 정점을 먹이는** 시점이고,
freeze 가능 구간이다. §8 의 실패 정책이 그때 작동한다.

---

## 12. 구현 교차검토 판정 (2026-08-28)

codex 판정은 **NO-GO**. 검증 결과 **행동을 바꾸는 지적 다섯 건이 모두 사실**이었다.

| codex 주장 | 검증 | 결과 |
| --- | --- | --- |
| run 2 가 무조건 타임아웃한다(`primod` 마스크) | 3-164 | ✅ — **회신 전에 내가 먼저 찾아 고쳤다** (`5e0daf7`) |
| `TDUALSTAGE0/1` 은 `color0sel/color1sel = '11'` = `0x00600000` 이어야 한다 | 사양서가 **두 곳**(17508, 17978)에서 *"Gouraud shaded trapezoids"* 에 대해 명시. `mgaregs.h:868` 이 `0x600000`. Mesa 가 `tdualstage1 = tdualstage0` 로 값을 복사하므로(`mgatex.c:751`) 레이아웃 동일 | ✅채택 |
| `OPMODE.dmadatasiz` 를 확인해야 한다 | 4-8: *"DMA General Purpose, DMA Vector Write, or **DMA Vertex Write** — should set dmaDataSiz to ... '00' for Little-Endian"* — 우리 두 모드를 **이름으로** 지목 | ✅채택 |
| `IEN` 이 켜져 있으면 핸들러 없이 인터럽트가 뜬다 | 3-150 `softrapien<0>`·`wien<7>`·`wcien<8>`. 드라이버는 IEN 을 **읽기만 하고(`:2532`) 쓰지 않는다** — 즉 리셋/BIOS 가 남긴 값 | ✅채택 |
| run 1 앞의 stale `SOFTRAPEN` 이 완료를 위조할 수 있다 | 3-189: `SOFTRAPEN` 은 `ICLEAR` 까지 남고 `PRIMEND` 는 `ENDPRDMASTS` 만 리셋한다. 시나리오 성립 | ✅채택 |
| WARP 레지스터를 **claim 전에** 쓰고 있다 | 사실이었다. `warp_init` 5 회 쓰기가 `stormBusy` 검사보다 앞에 있었다 | ✅채택 — 구조 재배치 |
| 타임아웃에서 VRAM 4096 워드를 읽는 것은 위험하다 | 물리적으로는 CPU 읽기라 hang 근거가 약하다. 그러나 **순서는 공짜로 고칠 수 있다** | ⚖️부분 — 스캔을 없애지 않고 **판정 로그를 스캔보다 먼저** 내보낸다 |
| 명령 목록·값·정점 페이로드·기하 | 전부 CONFIRMED | ⏭️ 행동 변화 없음 |

### 12.1 검증하다 내가 찾은 것

**`MGA_OPMODE_BYTESWAP`(`0x30000`)은 `dmadatasiz` 가 아니다.** 그것은
`dirdatasiz<17:16>`(직접 프레임버퍼 접근)이고, 문제의 필드는 `dmadatasiz<9:8>`
= `0x300` 이다(`mgaregs.h:559,563`). **기존 상수로 검사했으면 엉뚱한 필드를
보고 통과시켰을 것이다.** 새 상수 `MGA_OPMODE_DMADATASIZ` 를 두고 그 이유를
주석에 적었다.

그리고 `osmgaStormInitState` 가 `opmode & ~MGA_OPMODE_BYTESWAP` 로 지우는 것도
`dirdatasiz` 뿐이다 — **`dmadatasiz` 는 이 드라이버가 한 번도 건드린 적이 없다.**

### 12.2 참조와 갈라지는 한 곳

`TDUALSTAGE` 는 **의도적으로 참조와 다르게** 간다. DRM 은 Mesa 값을 싣고 그것은
흔히 0 이다. 사양서를 택한 이유는 셋이다 — 요구가 **우리가 그릴 바로 그 프리미티브**
(Gouraud shaded trapezoid)에 대해 **두 번** 적혀 있고, 사양서 자신이
*"This has no effect on the result"* 라고 **결과에 영향이 없음을 보증**하며,
따라서 지키면 그림이 달라질 수 없고 안 지키면 첫 실행에서 미문서화 도박이 된다.

### 12.3 최종 제어 흐름

```
읽기만 (PRIMPTR, DEVCTRL, IEN, OPMODE, 엔진 idle)   <- 거부는 여기서
할당·매핑·목록 조립·정점 조립·sentinel 채움          <- 카드 레지스터 쓰기 0
claim (stormLock / stormBusy)
  idle 재확인 · ICLEAR + SOFTRAPEN 확인 · warp_init  <- 첫 쓰기가 여기다
  run 1 -> 체크포인트 -> ICLEAR -> run 2 -> 보고
  성공: 블록 0 복원 -> claim 해제 -> 해제
  타임아웃: 판정 로그 -> REBOOT 로그 -> 스캔 -> **claim 유지, 아무것도 해제 안 함**
  거부(목록 시작 전): claim 반납
```

**claim 이전 첫 카드 레지스터 쓰기는 없다** — 감사로 확인했다.
`osmgaStormWaitIdle` 도 쓰기 0 회다.
