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
