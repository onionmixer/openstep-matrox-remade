# M10 — v10 제출 경로와 그것을 시험할 프로브 (2026-08-29, 코딩 전)

## 0. 이 단계

```
1단계 (끝남)  정책 개방, 공유 규칙 셋 추출, 오프셋 인자, 빌더 높이
2단계 (이것)  커널이 v10 배치를 실행한다 + 훅 없이 시험할 프로브
3단계         훅 분기
```

**처음 도는 커널 경로다.**  그래서 훅과 같은 부팅에 켜지 않는다 — 실패가
어느 쪽인지 못 가린다.

## 1. 시험 수단 — 훅이 필요 없다

`OSMGAMesaProbeBatch()` 가 커맨드 창을 매핑해 돌려준다(`Probe.c:296`).
그것을 `OSMGAHW3DWarpBatch *` 로 보고 채운 뒤 같은 `OSMGA_IOC_SUBMIT` 을
내면, 커널이 `version` 으로 갈라 v10 경로를 탄다.

```
test/openstep-mga-warp-submit-probe.m  (새로)
  - 커맨드 창을 매핑
  - v10 배치를 채운다: 상태 하나, 런 하나, 삼각형 하나
  - 제출하고 verdict 를 읽는다
  - 그린 것을 되읽어 확인한다 (VRAM Mmap 창)
```

**하네스가 이미 증명한 그림을 다시 그린다** — D2-2c 의 1176 화소 삼각형.
그러면 "제출 경로가 맞는가" 와 "WARP 가 그리는가" 가 섞이지 않는다.

## 2. 커널 경로

M6 §4·§11.1 에서 **T9 가 뺀 펜스를 반영**한 순서:

```
런마다   GENERAL 목록 -> 완료 -> ICLEAR
         VERTEX 제출  -> 포인터
배치 끝  DWGSYNC -> WIADDR2=SUSPEND -> WBUSY·WBUSY1·DWGENGSTS
```

### 2.1 조각 넷

```
osmgaHW3DWarpSnapshot     v9 스냅샷과 커널 내부 공용체 (BSS 23 KiB 절약)
osmgaHW3DFillLimits       lim 채우기 -- 지금 v9 안에 인라인, 둘이 같아야 한다
osmgaWarpTexFromState     OSMGAHW3DState -> OSMGAM3Tex
osmgaWarpFenceAndStop     DWGSYNC -> SUSPEND -> 세 비트
```

### 2.2 이미 있는 것을 쓴다

```
osmgaHW3DValidateWarp      구조 + 정점 + 상태 + 정책
osmgaHW3DDestFits          목적지 대 창          (1 단계)
osmgaHW3DClipBox           scissor 교차, 정수    (M6 §11.2)
osmgaHW3DTexClampAxes      repeat 정책           (M6 §11)
osmgaHW3DTexFilter/DualStage  텍스처 레지스터
osmgaDmaBuildTriangleList  상태 목록 (이제 높이 인자를 받는다)
```

## 3. 실패 정책 (M6 §6, codex 채택)

```
검증 실패   제출 전.  아무것도 안 그리고 UNSUPPORTED.
타임아웃    stormBlitFailed = YES (락 안에서) -> 그다음 stormBusy = NO
            레지스터를 더 쓰지 않는다.  아래 층으로 재생하지 않는다.
            링·마이크로코드 유지.
```

## 4. 답이 필요한 것 — v9 과 다른 두 가지

### 4.1 클립과 pitch 를 목록에 넣는가

v9 은 **일부러 목록 밖**에 둔다:

> *"The engine state that bounds a draw -- PITCH, the clip, MACCESS -- is
> set by MMIO before the list is submitted and never appears in the list,
> so a batch cannot move the walls it is drawn inside."*

**하네스의 WARP 목록은 그것들을 목록 안에 넣는다.**  목록을 만드는 것은
커널이므로 안전성은 같지만, v9 이 원칙으로 적어 둔 것과 어긋난다.

### 4.2 완료 판정이 다르다

```
v9      ENGSTATUS & MGA_DMA_DONE_MASK == MGA_DMA_DONE_VALUE
하네스  포인터 도달 + SOFTRAPEN + ENDPRDMASTS
```

하네스 쪽이 **모든 WARP 밴드에서 실증됐다.**  v9 쪽은 사다리꼴에서 실증됐다.

## 5. codex 에 물을 것

1. **§4.1** — WARP 목록이 클립·pitch 를 담는 것이 v9 의 원칙과 어긋나는데,
   생산 경로에서 어느 쪽이어야 하나?  MMIO 로 빼면 하네스가 실증한 순서를
   벗어난다.
2. **§4.2** — 어느 완료 판정을 쓰나?  둘 다 실증됐지만 다른 층에서다.
3. **프로브가 그린 것을 되읽는 방법**.  `VRAM Mmap` 창으로 읽는 것이 맞나,
   아니면 다른 경로가 있나?
4. **스냅샷 공용체** — `osmgaHW3DSnapshot` 은 30 곳쯤에서 쓰인다.  공용체로
   바꾸면서 이름을 유지하는 방법(매크로 별칭)이 받아들일 만한가?
5. **한 재부팅에 넣을 것**.  제출 경로 + 프로브면 충분한가, 아니면 더
   넣어야 하나?
6. 빠진 것.

---

## 6. 자체 확인 (codex 회신 전) — §4 의 두 질문에 답이 나왔다

### 6.1 §4.1 — v9 의 원칙은 여기서 물지 않는다

WARP 목록이 쓰는 레지스터를 전수로 뽑았다.  `MGA_PITCH`, `MGA_CXBNDRY`,
`MGA_YTOP`, `MGA_YBOT`, `MGA_MACCESS` — **v9 이 일부러 목록 밖에 두는 것들이
전부 목록 안에 있다.**  전제는 맞았다.

그런데 v9 의 원칙 *"a batch cannot move the walls it is drawn inside"* 가
막는 것은 **클라이언트 값이 벽에 닿는 것**이다.  두 경로 다 벽은
`state.dstPitch` 와 scissor 에서 오고 **둘 다 클라이언트가 준 값**이며,
**둘 다 검증기를 통과한 뒤 커널이 쓴다.**  차이는 *어디서 쓰느냐* 뿐이다:

```
v9      MMIO 로 먼저 -> 목록 실행
WARP    목록의 앞부분에서 -> 같은 목록의 그리기
```

목록을 만드는 것이 커널이고 목록이 통째로 실행되므로 **두 순서는 같은 것을
보장한다.**  부분 실행이 가능하거나 클라이언트가 목록 내용에 닿을 수 있다면
달랐겠지만, 어느 쪽도 아니다.

→ **하네스의 형태를 유지한다.**  실기가 증명한 순서를 벗어날 이유가 없다.

### 6.2 §4.2 — v9 의 판정은 WARP 에 쓰면 안 된다

두 술어를 펼쳐 보았다:

```
v9      SOFTRAPEN=1  AND  ENDPRDMASTS=1  AND  ** DWGENGSTS=0 **
하네스  포인터 도달  AND  SOFTRAPEN=1    AND  ENDPRDMASTS=1
```

v9 은 **그리기 엔진이 놀고 있을 것**까지 요구한다.  WARP 경로에서 그것을
쓰면 **매 런이 앞 런의 그리기와 직렬화된다** — T9 가 방금 사 준 것을 그대로
반납하는 셈이다.

`osmgaD2eSubmitBatch` 의 `outBusy` 가 바로 그 경우를 잰다: T9 에서 포인터
경계에 `DWGENGSTS=1` 이었고(`drawing IN FLIGHT`), 그 상태로 다음 상태
목록을 내도 **깊이가 새지 않았다.**

→ **하네스의 술어를 쓴다.**  이유는 T9 다.

(참고로 v9 의 술어도 실측으로 다듬어진 것이다 — 그 매크로 위 주석이
`STATUS=80820025`, *"a finished transfer that never satisfied the test"*
를 적어 놨다.  두 술어는 각자 자기 층에서 옳다.)

---

## 7. codex 교차검토 판정 (2026-08-29, 코딩 전)

| codex 주장 | 내 검증 | 결과 |
|---|---|---|
| **§5 의 질문 1·2 는 M6 이 이미 답했다** | `M6 §3` 의 표가 *"클립 -> 목적지 사각형 ∩ scissor"*, *"`PITCH` -> `state`"* 라고 적어 놨고, `M6 §4` 가 런마다 판정을 **포인터 + SOFTRAPEN + ENDPRDMASTS** 로 적어 놨다 | ✅**채택 — 스무 번째.  §6 에서 코드로 "답했다" 고 한 것을 내 문서가 이미 글로 적어 놨다** |
| 목록에 넣되 **값이 커널 유래일 것** — 안전 조건은 MMIO 가 아니라 *"클라이언트가 내보낼 레지스터 값을 고를 수 없다"* 이다 | 내 §6.1 과 같은 결론이고 표현이 더 낫다.  구체 규칙도 붙는다: 클립은 **`osmgaHW3DClipBox` 의 교차**에서, 원시 scissor 필드에서가 아니라 | ✅채택 |
| 런마다는 하네스 술어, **배치 끝에서만** 전체 은퇴 절차 | §6.2 와 같다 | ✅확인 |
| M10 이 끝의 펜스를 *"DWGSYNC"* 로만 적었다 — 실제로는 **FIFO 입장 + 태그 증가 + 폴링**이다 | 계획 글이 얇았다.  **구현한 `osmgaWarpFenceAndStop` 은 이미 태그와 FIFO 입장을 한다** | ⚖️부분채택 — 코드는 맞고 계획 글을 고친다 |
| 공용체는 **이름을 지어라**, 매크로 별칭으로 옛 이름을 유지하지 마라.  그리고 *"30 곳쯤"* 은 과장 — 20 곳이다 | `grep -c` = **20** | ✅**채택 — 내 수가 틀렸다** |
| **한 재부팅에 살아 있는 v10 그리기 하나로는 부족하다** | 아래 §8 | ✅**채택 — 가장 값진 지적** |
| 의도적 **살아 있는 타임아웃 시험을 이 재부팅에 넣지 마라** — 그 결과가 가속을 영구히 래치하고 재부팅을 또 부른다 | M6 §6 이 그 정책을 적어 놨다.  맞다 | ✅채택 |
| VRAM 창 되읽기가 맞다.  단 **0 이 아닌 매핑 오프셋에 기대지 말고** `mmap()` 이 돌려준 주소를 확인하라 | `M1_3J` 가 오프셋 처리를 미해결로 적어 놨다 | ✅채택 |
| *"`OSMGA_IOC_SUBMIT` 이 아직 v9 만 부른다"* | 맞다 — **디스패처는 이 단계가 만드는 것**이다.  계획 문장이 현재형이라 오해를 샀다 | ⚖️부분채택 — 문장을 고친다 |

## 8. 개정 — 한 재부팅의 시험 순서

살아 있는 그리기 하나로는 실패를 못 가른다.  **다섯을 순서대로** 돌린다:

```
1  v9 직접 제출 + 되읽기            대조군 (v10 전)
2  v10 dry 제출, 정상 배치          디스패치·스냅샷·검증·목록 배치를 증명한다.
                                    dwords 가 0 이 아니어야 하고 엔진은 안 건드린다
3  v10 dry 제출, 망가진 배치        거절이 도어벨을 울리지 않음을 증명한다
4  v10 살아 있는 제출               1176 화소 + 클립 밖 sentinel
5  v9 직접 제출 + 되읽기            대조군 (v10 뒤) — 끝의 정지가 v9 을 남겨 뒀는지
```

`OSMGA_IOC_SUBMIT_DRY` 는 **바로 이것을 위해 이미 있다** — 검증하고 인코딩한
뒤 엔진에 닿기 전에 멈춘다.

**의도적 타임아웃 시험은 넣지 않는다.**  그 결과는 가속을 영구히 끄는
래치이고, 그러면 재부팅을 하나 더 쓴다.

## 9. 확정 사항 (질문이 아니다)

M6 이 이미 정한 것을 다시 묻지 않는다:

```
목록이 담는 것   PITCH·클립·MACCESS 를 담는다, 값은 커널 유래 (M6 §3)
런마다 판정      포인터 + SOFTRAPEN + ENDPRDMASTS (M6 §4)
조각 넷          M6 §13.2
정점은 복사한다  클라이언트 매핑에서 DMA 하지 않는다 (M6 §2)
재부팅 배분      제출 경로 + 프로브가 1 번, 훅이 2 번 (M9 §9.4)
```

스냅샷은 **이름 있는 공용체**로 한다:

```c
static union {
    OSMGAHW3DBatch     v9;
    OSMGAHW3DWarpBatch warp;
} osmgaHW3DSnap;
```

기존 20 곳을 `osmgaHW3DSnap.v9` 로 기계적으로 바꾼다.  매크로 별칭은 어느
멤버가 살아 있는지 숨기고 얻는 것이 없다.

---

## 10. 구현 완료 (2026-08-29, 재부팅 대기)

```
공용체        osmgaHW3DSnap.v9 / .warp    (참조 20 곳 기계적 갱신)
디스패처      runHW3DSubmitDry: 가 version 을 한 번 읽고 v10 이면 넘긴다
제출 경로     runHW3DSubmitWarp:  -- dry 팔 포함
조각 넷       FillLimits / TexFromState / FenceAndStop / 공용체
프로브        test/openstep-mga-warp-submit-probe.c  -- 다섯 시험
```

### 10.1 이 빌드는 `dry` 스위치를 켰다

`OSMGA_HW3D_SUBMIT_DRY` 없이는 dry ioctl 이 컴파일되지 않는다 —
`build-matrox-driver.sh` 의 기본은 OFF 이고, *"A driver built with neither
is the one that ships"* 라고 적혀 있다.

**이 재부팅의 시험 2·3 이 그것을 요구한다.**  dry 는 검증하고 인코딩한 뒤
레지스터를 하나도 쓰지 않고 멈추므로, **엔진을 걸지 않고** 디스패치·스냅샷·
검증·목록 배치를 증명한다.  출하 빌드에는 넣지 않는다.

### 10.2 다섯 시험

```
1  v9 빈 배치 제출          옛 경로가 여전히 답하는가
2  v10 dry, 정상            verdict OK 이고 dwords 가 0 이 아니다
3  v10 dry, 망가진 것 넷    정점 수 / rhw 0 / 없는 opcode / 창 밖 원점
                            -- 각자 자기 verdict 로 거절되어야 한다
4  v10 살아 있는 제출       1176 화소 삼각형
5  v9 빈 배치 제출          끝의 정지가 v9 을 남겨 뒀는가
```

**타임아웃 시험은 없다** — 그 결과가 가속을 영구히 래치하고 재부팅을 하나 더
쓴다.

### 10.3 실행

```
cc -O -DOSMGA_HW3D_SUBMIT_DRY -o /tmp/wsp -Imesa -Ihw3d \
   test/openstep-mga-warp-submit-probe.c mesa/OpenStepMGAMesaProbe.c \
   mesa/OpenStepMGAMesaBuffer.c hw3d/OpenStepMGAHW3D.c
/tmp/wsp
```

## 11. 첫 실행 (2026-08-29 09:08) — 행렬이 제 일을 했다

```
1  v9 before   verdict 16 (E_DSTPITCH)     <- 내 픽스처가 상태를 안 채웠다
2  v10 dry     verdict 4294967295          <- NOT_RUN.  검증 전에 반환했다
3  v10 dry x4  전부 4294967295             <- 같은 지점
4  v10 live    전부 4294967295             <- 같은 지점
5  v9 after    verdict 0, dwords 20        <- v9 은 멀쩡하다
```

**`0xFFFFFFFF` 는 `OSMGA_HW3D_NOT_RUN`** 이다 — 판정이 한 번도 쓰이지 않았다.
곧 메서드가 **이른 가드에서 반환**했고, 그 가드는 하나뿐이다:

```c
if (!osmgaWarpUcodeResident)
    return IO_R_UNSUPPORTED;      /* nothing has placed the microcode */
```

### 11.1 원인 — 생산 경로가 시험에 기대고 있었다

`osmgaWarpUcodeResident` 를 세우는 곳은 **하네스 둘뿐**이었다(`:11345`,
`:11940`).  M7 에서 **부팅 자격검증을 껐고**, 그러자 마이크로코드를 놓는
것이 아무것도 없어졌다.

로그가 확증한다: 이 부팅에 `D2-2c` 줄이 하나도 없다.

**생산 경로가 시험이 돌았기를 전제하면 안 된다.**  이제 첫 제출에서 **클레임
아래** 직접 놓는다 — 하네스는 클레임 앞에서 하는데, 이것은 VRAM 과 레지스터를
쓰는 일이라 "아무도 같은 일을 하고 있지 않다" 를 말해 주는 것이 클레임이다.

### 11.2 그리고 행렬이 아니었으면 못 봤다

살아 있는 제출 하나만 돌렸다면 `IO_R_RESOURCE` 하나만 보고 **엔진이
실패했다고 읽었을 것**이다.  dry 시험 넷이 **엔진을 건드리기도 전에** 같은
지점에서 멈춘 것을 보여줬고, v9 대조군이 **경로 자체는 멀쩡함**을 보여줬다.

### 11.3 대조군의 픽스처도 틀렸다

시험 1 은 `magic`·`version`·`triCount` 만 채우고 **상태를 안 채웠다** —
매핑된 버퍼에 있던 값으로 `E_DSTPITCH`.  시험 5 가 통과한 것은 그때는
v10 픽스처가 상태를 채워 뒀기 때문이다.  **같은 버그가 한 번은 실패로,
한 번은 통과로 보였다.**  이제 대조군이 자기 상태를 채운다.

## 12. 두 번째 실행 (2026-08-29 09:15) — 앞의 넷이 통과했다

```
1  v9 before   verdict 0,  dwords 20        ✅
2  v10 dry     verdict 0,  dwords 134       ✅  상태 목록 110 + 정점 24
3  v10 dry x4  verdict 25 / 26 / 7 / 4      ✅  각자 자기 판정, dwords 0
4  v10 live    verdict 0,  dwords 134,  rc 실패
5  v9 after    NOT_RUN                      래치가 걸려 v9 이 거절됐다
```

**디스패치·스냅샷·검증·인코딩이 전부 동작한다.**  거절 넷이 각각 다른
판정으로 갈렸고 인코딩을 하지 않았다 — 거절은 도어벨을 울리지 않는다.

`microcode placed on first submission` 도 로그에 있다 — §11 의 고침이 들었다.

### 12.1 살아 있는 제출만 실패했고, 래치가 정확히 동작했다

시험 5 의 `NOT_RUN` 은 **v9 이 `stormBlitFailed` 에서 거절된 것**이다.
살아 있는 제출이 실패하면서 래치를 걸었고, 그 뒤 v9 은 **엔진 뒤에 줄서지
않고 거절**됐다 — 설계한 대로다.

### 12.2 원인 — `warp_init` 이 없었다

로그에 **펜스 진단이 없다.**  `osmgaWarpFenceAndStop` 은 실패하면 이유를
찍으므로, 실패는 **제출 루프**에서 났다 — 그리고 그 두 타임아웃은 **아무것도
안 찍고 있었다.**

하네스는 첫 상태 목록 전에 이것을 한다:

```c
ICLEAR; WIADDR2=SUSPEND; WGETMSB; WVRTXSZ; WACCEPTSEQ; WMISC=WRITE;
WMISC 를 되읽어 확인
```

**생산 경로는 하지 않았다.**  검증도 인코딩도 멀쩡했는데(판정 0, 134 dword)
엔진이 목록을 실행하지 않은 것이 그 때문이다.

**배치마다** 한다.  MMIO 여섯 번이고, T6 이 *"사다리꼴이 사이에 끼면 상태
전체 재발행이 필요하다"* 를 실측했으므로 이것들이 남의 그리기를 넘어
살아남는다고 가정하는 것은 **T6 이 거짓이라고 잰 것을 가정하는 것**이다.

### 12.3 그리고 진단을 넣었다

제출 루프의 두 타임아웃이 **어느 런의 어느 단계인지, 포인터와 STATUS 가
무엇인지** 찍는다.  첫 실행에서 로그에 있던 것은 *"did not complete"* 한
줄뿐이었고, 그것으로는 `warp_init` 누락이 보이지 않는다.
