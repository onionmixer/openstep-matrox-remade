# M1-3N — 표면의 소유와 수명을 상태 갱신 시점에 지킨다 (§3-23)

아직 코드 없음.

## 1. 세 구멍 (전부 소스에서 확인)

정렬 검사를 우회하지는 않는다. **"거절하고 깨끗이 소프트웨어로 물러난다"는
의도된 동작**을 무너뜨린다. pitch 를 한 번 더 재는 것보다 이쪽이 값어치가
크다는 판단에 동의한다.

### (a) `OSMesaPixelStore` 가 상태를 갱신하지 않는다

```c
void GLAPIENTRY OSMesaPixelStore( GLint pname, GLint value )
{
   OSMesaContext ctx = OSMesaGetCurrentContext();
   ... ctx->userRowLength = value; ctx->rowlength = value; ...
   compute_row_addresses( ctx );          /* 그리고 끝 */
}
```

컨텍스트가 current 가 된 뒤 행 길이를 바꾸면 소프트웨어 행 주소는 다시
계산되지만 `osmesa_update_state` 는 안 불린다. **이미 설치된 하드웨어 삼각형
함수가 그대로 남는다.** 그 함수는 `OSMGAMesaBufferStride()`(옛 보폭)로
제출하고 Mesa 는 새 행 길이로 주소를 계산한다.

보폭 불일치를 잡는 검사는 **있다** —
`OpenStepMesaAccelUpdateState` 의 `rowLength != OSMGAMesaBufferStride()` —
불릴 기회가 없을 뿐이다.

### (b) 선택기가 소유를 안 본다

```c
if (OSMGAMesaBufferOrigin() == 0UL) return NULL;
```

표면이 **있는지**만 묻고 **누구 것인지**는 안 묻는다. 그래서 다른 크기로
다시 바인딩했거나 두 번째 컨텍스트라 표면을 **못 받은** 쪽도, 행 길이만
우연히 맞으면 하드웨어 함수를 받는다.

### (c) 해제가 자기 컨텍스트를 무시한다

```c
OpenStepMesaAccelReleaseBuffer(void *ctx)
{
    (void)ctx;
```

두 번째 컨텍스트가 파괴되면서 **첫 번째의 표면을 해제한다.**

## 2. 포인터가 어느 것인지 (확인함 — 여기가 함정이다)

두 종류가 오간다:

| 훅 | 받는 것 |
| --- | --- |
| `OpenStepMesaAccelBuffer` / `DepthBuffer` / `ReleaseBuffer` | **`OSMesaContext`** (`(void *) ctx`) |
| `OpenStepMesaAccelUpdateState`, 선택기, 삼각형 함수 | **`GLcontext *`** |

`bufCtx` 에는 `OSMesaContext` 가 들어 있다.

**처음에 "그러니 선택기의 `ctx` 를 그냥 비교하면 언제나 다르다" 고 적었다.
틀렸다.** 구조체를 보면:

```c
struct osmesa_context {
   GLcontext gl_ctx;        /* 첫 멤버 */
   ...
```

첫 멤버이므로 `OSMesaContext` 포인터와 `&ctx->gl_ctx` 는 **같은 주소**다 —
C 가 보장한다. 그리고 이 파일이 이미 그 사실에 기대고 있다:

```c
OSMesaContext GLAPIENTRY OSMesaGetCurrentContext( void )
{
   GLcontext *ctx = gl_get_current_context();
   if (ctx) return (OSMesaContext) ctx;      /* 그냥 캐스팅한다 */
```

그러니 두 경로 다 맞는다. `ctx->DriverCtx` 를 쓰는 이유는 "다르기 때문"이
아니라 **의도가 드러나고 구조체 배치에 기대지 않기 때문**이다. Mesa 자신이
그 등식을 단언한다:

```c
/* osmesa.c:1737, 186 */
ASSERT((void *) osmesa == (void *) ctx->DriverCtx);
```

**함정이 아니라 우연한 안전이었다.** 그래도 명시적인 쪽을 쓴다 — 그리고
`(void)` 캐스팅으로 형을 뭉개는 비교이므로, 어느 쪽을 쓰는지 주석에 적는다.

## 3. 고침

### 갱신 순서는 이미 맞다 (확인함)

`osmesa_update_state` 는 소프트웨어 선택을 **먼저** 놓고 우리 것을 **나중에**
부른다:

```
203:   ctx->Driver.TriangleFunc = choose_triangle_function( ctx );
219:   OpenStepMesaAccelUpdateState( ctx, ... );
```

그러니 우리가 거절하면 소프트웨어 함수가 그대로 남는다. 따로 되돌릴 것이 없다.

### (a) `OSMesaPixelStore` 끝에서 상태를 갱신한다

`#ifdef OPENSTEP_MESA_ACCEL_HOOK` 안에서 `osmesa_update_state( &ctx->gl_ctx )`
한 줄. 이 파일은 이미 `OSMesaMakeCurrent` 에서 같은 함수를 직접 두 번 부르므로
전례가 있다. `OSMesaPixelStore` 는 **current 컨텍스트**에 작용하므로 대상이
모호하지 않다.

`OSMesaGetCurrentContext()` 는 `NULL` 을 돌려줄 수 있지만, 그 경우 이 함수는
**내 줄에 닿기 전에 이미** `ctx->userRowLength` 에서 죽는다. 즉 이 함수는
current 컨텍스트를 이미 전제하고 있고, 내 한 줄이 그 전제를 새로 만들지는
않는다. 그 사실을 주석에 적되 여기에만 방어를 다는 짓은 하지 않는다 — 함수의
나머지와 어긋난다.

갱신이 돌면 선택기가 다시 판단하고, 보폭이 안 맞으면 하드웨어 함수를 설치하지
않는다 — 그리고 소프트웨어 드라이버가 방금 넣은 함수가 그대로 남는다
(`"Only ever replace with something; never write NULL"`).

### (b) 선택기가 소유를 묻는다

버퍼 모듈에 술어 하나를 더한다:

```c
int OSMGAMesaBufferOwnedBy(const void *osmesaCtx);   /* bufCtx 와 비교 */
```

선택기는 `OSMGAMesaBufferOwnedBy(ctx->DriverCtx)` 로 묻는다.
`OpenStepMesaAccelUpdateState` 도 같은 자리에서 묻는다 — 삼각형 함수를 넣을지
정하는 것이 거기이므로.

### (c) 해제가 자기 것만 놓는다

```c
if (bufCtx != 0 && bufCtx != ctx) return;   /* 남의 표면은 건드리지 않는다 */
```

소유자가 파괴되면 자기 것이므로 놓는다. 소유하지 않은 쪽이 파괴되면 아무 일도
안 일어난다.

## 3b. codex 교차검토 — 계획의 핵심이 틀렸다 (전부 소스에서 확인)

### (1) `OwnedBy` 는 잘못된 술어다 — **소유가 아니라 결속을 물어야 한다**

같은 컨텍스트를 **다른 크기로** 다시 바인딩하면 `OpenStepMesaAccelBuffer` 가
0 을 돌려주는데, 그 경로는 설명을 그대로 둔다:

```c
/* A different size would need a different surface, and there is only
 * one.  Refused rather than resized: the description is left alone ... */
return 0;
```

`bufCtx` 는 여전히 그 컨텍스트다. 그러니 `OwnedBy(ctx->DriverCtx)` 는 **참을
돌려주고**, 선택기는 옛 표면을 상대로 하드웨어 함수를 그대로 설치한다.

**즉 내 계획의 시험 2 는 고친 뒤에도 실패한다.** 계획을 반증하는 시험이었지
검증하는 시험이 아니었다.

필요한 것은 **"이 컨텍스트가 지금 가속 표면에 결속되어 있는가"** 다.
`OpenStepMesaAccelBuffer` 가 실제로 `bufMapped` 를 돌려줄 때만 서고, 거절해서
Mesa 가 애플리케이션 버퍼로 돌아가는 모든 경로에서 풀리는 것.

### (2) `ReleaseBuffer(0)` 를 fork 정리가 쓴다 — 내 가드가 그것을 깬다

```c
/* OpenStepMGAMesaProbe.c:223 */
OpenStepMesaAccelReleaseBuffer(0);
```

자식이 물려받은 매핑을 버리려고 일부러 `0` 을 넘긴다. 내가 쓰려던
`if (bufCtx != 0 && bufCtx != ctx) return;` 은 표면이 있으면 **언제나 일찍
돌아가므로**, 자식이 매핑을 그대로 쥔다 — 그 호출이 없애려던 바로 그 상태다.

`ctx == 0` 은 **강제 정리**로 따로 다뤄야 한다.

### (3) 하드웨어를 거절해도 소프트웨어가 안전해지지 않는다

`OSMesaPixelStore` 가 행 길이를 바꾸면 `compute_row_addresses` 가 **VRAM
매핑을** 새 행 길이로 다시 주소 매긴다. `ctx->buffer` 는 여전히 `bufMapped`
다. 그러니 하드웨어를 거절해도 **소프트웨어가 320 화소짜리 할당에 333 화소
간격으로 쓰고**, 미러는 옛 `bufAppRow` 로 되돌려 쓴다.

제대로 된 고침은 **표면에서 물러나는 것**이다 — `ctx->buffer` 를 애플리케이션
버퍼로 되돌리고, 깊이/알파 상태도 되돌리고, 그 다음 상태를 갱신한다.
삼각형 함수만 다시 고르는 것으로는 부족하다.

### (4) 미러 래퍼는 상태 갱신이 되돌리지 않는다

`osmesa_update_state` 는 `Driver.Clear` 는 다시 놓지만
`RenderStart`/`RenderFinish`/`Finish`/`Flush` 는 건드리지 않는다 —
소스에서 확인했다. 결속을 잃은 컨텍스트가 **옛 전역 표면을 엉뚱한
애플리케이션 버퍼로 미러하는 래퍼를 그대로 쥔다.**

### (5) 작은 사실 오류

- `OSMesaMakeCurrent` 는 `osmesa_update_state` 를 **두 번이 아니라 세 번**
  부른다. "전례가 있다"는 말은 유효하지만 숫자가 틀렸다.
- `OSMesaPixelStore(OSMESA_ROW_LENGTH, 0)` 은 헤더에 "이미지 너비와 같음"으로
  적혀 있는데 코드는 0 을 그대로 저장한다. 이 함수를 건드리는 이상 그 동작을
  보존하든 고치든 **말은 해야 한다.**

## 3c. 그래서 다음은 고침이 아니라 **측정**이다

계획의 고침 설계가 (1) 때문에 무너졌다. 새 설계를 추론으로 다시 세우는 대신,
**시험을 먼저 만들어 무엇이 실제로 깨지는지 본다.** 이 세션 내내 그렇게 해서
얻은 것이 컸다.

시험은 codex 가 요구한 대로 강하게 만든다:

- **화소를 본다**, `hookDrawn` 만 보지 않는다. 요청한 배치대로 색인하고 가드
  화소를 둔다.
- **각 시험이 먼저 정상 가속 그리기를 한 번 성립시킨다.** 증분 0 이
  "하드웨어가 없어서"인지 "막혀서"인지 구분해야 한다.
- **`hookDeclined` 도 본다.** 커널이 거절해도 `hookDrawn` 은 안 는다.
- **네 번째: fork 정리 경로.** 표면을 쥔 채 `ReleaseBuffer(NULL)` 을 부르고
  원점이 0 이 되는지 본다. 내 가드가 이것을 깼을 것이다.
- **다섯 번째: `OSMESA_Y_UP`** — 같은 방아쇠에 다른 결과.

## 4. 안 하는 것
## 4. 안 하는 것

- **표면을 두 개로 만들지 않는다.** 두 번째 컨텍스트를 가속하는 것은 별개
  작업이고, 지금 하는 것은 **거절이 제대로 거절이 되게** 하는 것뿐이다.
- **`userRowLength` 를 건드리지 않는다.** 그건 호출자가 요청한 값이고,
  미러가 그것을 따라 되돌려 쓴다.
- **Mesa 의 dirty-flag 방식으로 바꾸지 않는다.** 이 파일의 기존 방식(직접
  호출)을 따른다. 방식을 섞으면 어느 쪽이 돌았는지 알기 어려워진다.

## 5. 검증 (재부팅 없음 — 전부 유저랜드)

새 시험 하나, 세 경우:

1. **current 뒤에 행 길이 바꾸기** — 정렬된 표면(320)을 잡고, current 뒤에
   `OSMesaPixelStore(OSMESA_ROW_LENGTH, 333)`, 곧바로 그린다.
   → `hookDrawn` 이 늘면 **안 된다**. 지금은 는다(그것이 버그다).
2. **같은 컨텍스트를 다른 크기로 다시 바인딩** (행 길이는 같게) →
   하드웨어 그리기가 없어야 한다.
3. **두 컨텍스트** A, B 를 같은 정렬 크기로. B 로 그리고 B 를 파괴 →
   B 는 한 번도 하드웨어로 그리지 않아야 하고, **A 의 표면이 살아 있어야
   한다**(`OSMGAMesaBufferOrigin()` 이 0 이 아니어야 한다).

셋 다 **고치기 전에 먼저 돌려** 실제로 실패하는 것을 본다. 실패를 못 보는
시험은 고쳤다는 증거가 못 된다.

## 6. 순서

계획 교차검토 → 재검증 → **시험 먼저(실패를 확인)** → 고침 → 다시 시험 →
기록.

## 7. 시험 결과 — 고치기 전 (2026-08-21)

`test/openstep-mga-mesa-binding-test.c`, 경우마다 별도 프로세스.
각 경우가 먼저 **정상 가속 그리기**를 성립시킨다 — 그래야 나중의 `drew=0` 이
"올바로 거절됨"인지 "애초에 하드웨어가 없음"인지 갈린다. 전부 그림을 본다.

| 경우 | 결과 |
| --- | --- |
| 1 current 뒤 행 길이 320→333 | **실패** — `drew=1`(여전히 가속), 그림이 어긋난 자리에 |
| 2 같은 컨텍스트를 320x120 으로 재바인딩 | **실패** — `drew=1`, 표면은 거절됐는데 |
| 3 두 번째 컨텍스트 | **실패 3 가지 + 프로세스 죽음** |
| 4 `ReleaseBuffer(NULL)` | **통과** — 표면이 0 이 된다 |
| 5 current 뒤 `Y_UP` 끄기 | **실패** — `drew=1` |

### 경우 3 이 가장 나쁘다

```
   B WAS ACCELERATED
   the second context's own buffer  ... NOT as drawn
   after destroying the second: surface=0
   IT TOOK THE FIRST CONTEXT'S SURFACE
   ...
   memory allocation error: attempt to free or realloc space not in heap
   IOT trap
```

두 번째 컨텍스트가 표면을 **거절당하고도 하드웨어 함수를 받아**, 자기 버퍼가
아니라 **A 의 VRAM 표면에** 그린다(그래서 B 의 버퍼는 비어 있다). 그리고
B 가 파괴되며 **A 의 매핑을 해제**한다. 그 뒤 **힙이 아닌 포인터를 free** 해서
프로세스가 죽는다 — A 의 `DepthBuffer` 가 `vm_allocate` 로 잡은 매핑을
가리키는데 이미 해제됐기 때문이다.

이 죽음은 codex 도 나도 예측하지 못했다. **시험을 먼저 만들지 않았으면 못
봤다.**

### 경우 2 는 내가 처음에 잘못 만들었다

처음엔 `352x240` 으로 재바인딩했고 **통과**했다. 너비가 달라지면 행 길이도
달라지고, `OpenStepMesaAccelUpdateState` 의 보폭 불일치 검사가 그것을 먼저
막는다 — 즉 소유와 무관한 이유로 막힌 것이다. **같은 너비, 다른 높이**
(`320x120`)로 고치자 실패했다. 구멍은 **보폭이 맞고 결속이 안 맞을 때만**
드러난다.

### 경우 4 가 내 계획을 반증한다

계획에 넣으려던 가드

```c
if (bufCtx != 0 && bufCtx != ctx) return;
```

는 이 경우를 깬다. 지금은 통과하는데, 그 가드를 넣으면 표면이 있는 한
언제나 일찍 돌아가 fork 정리가 무효가 된다. **통과하는 시험도 있어야 하는
이유가 이것이다.**

## 8. 그러므로 고침은 (측정 위에서)

- **결속**을 따로 기록한다. `OpenStepMesaAccelBuffer` 가 실제로 매핑을
  돌려줄 때만 서고, 그 컨텍스트를 거절하는 모든 경로에서 풀린다.
  선택기와 상태 갱신이 그것을 묻는다. → 경우 1·2·3 의 "여전히 가속".
- **표면에서 물러나는 경로**가 필요하다. 삼각형 함수만 다시 고르는 것으로는
  부족하다 — `ctx->buffer`, 깊이, 알파, 그리고 미러 래퍼까지 되돌려야 한다.
  → 경우 1·5 의 어긋난 그림.
- **해제는 `ctx == 0` 을 강제 정리로 다룬다.** → 경우 4 를 유지하면서 경우 3
  의 "남의 표면을 가져감"을 막는다.
- 경우 3 의 죽음은 위 셋의 결과이지 따로 고칠 것이 아니다. 다시 시험해서
  확인한다.
