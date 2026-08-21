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

## 9. 고침 설계 (측정 위에서, 아직 코드 아님)

### 고침 1 — **결속**을 명시적으로 기록한다 (경우 1·2·3 의 "여전히 가속")

지금은 "표면이 있는가"(`OSMGAMesaBufferOrigin() != 0`)만 묻는다. 물어야 할
것은 **"지금 이 컨텍스트가 그 표면에 그리고 있는가"** 다.

- `bufBound` — 표면에 결속된 `OSMesaContext`.
- **`OpenStepMesaAccelBuffer` 맨 앞에서**, 부르는 쪽이 결속돼 있으면 푼다:
  `if (bufBound == ctx) bufBound = 0;`
  그러면 **모든 거절 경로가 저절로 푼다** — 그 함수에는 `return 0` 이
  스무 곳쯤 있고, 하나씩 손대면 언젠가 하나를 빠뜨린다.
- 성공하는 두 반환(같은 크기 재바인딩, 새로 만들기)에서만 다시 건다.
- 다른 컨텍스트가 거절당해도 **남의 결속은 안 푼다** (조건이 `== ctx`).
- `OSMGAMesaBufferBoundTo(const void *osmesaCtx)` 를 내보내고,
  `OpenStepMesaAccelUpdateState` 가 **삼각형 함수와 미러 래퍼 둘 다**에
  대해 그것을 묻는다. 지금 래퍼는 결속과 무관하게 달린다 — 경우 3 에서
  B 가 A 의 표면에 그린 것이 그 때문이다.

### 고침 2 — 배치가 안 맞게 되면 **표면에서 물러난다** (경우 1·5 의 어긋난 그림)

하드웨어를 거절하는 것만으로는 부족하다. `ctx->buffer` 가 여전히 VRAM
매핑이라 **소프트웨어가 거기에 새 보폭으로 쓴다.**

`OSMesaPixelStore` 안(훅 `#ifdef` 안)에서, 표면이 이 컨텍스트에 결속돼 있고
새 행 길이/방향이 공유를 깨면 바꿔치기를 되돌린다:

| 바꿔치울 때 | 되돌릴 때 |
| --- | --- |
| `ctx->buffer = accelBuf` | 애플리케이션 버퍼로 (`OSMGAMesaBufferApp()` 을 내보낸다) |
| `ctx->rowlength = accelRow` | 호출자가 방금 요청한 값 그대로 둔다 |
| `DepthBuffer = accelDepth`, `UseSoftwareDepthBuffer = FALSE` | **`DepthBuffer = NULL` 을 먼저** 놓고 `UseSoftwareDepthBuffer = TRUE`, 그 다음 `alloc_depth_buffer` |
| `UseSoftwareAlphaBuffers = FALSE` (알파 있는 visual 일 때만) | 같은 조건으로 `TRUE`, `NewState |= NEW_RASTER_OPS` |
| 매핑 | `OpenStepMesaAccelReleaseBuffer(ctx)` |

**`DepthBuffer = NULL` 을 먼저 놓는 것이 핵심이다.** `_mesa_alloc_depth_buffer`
는 기존 포인터를 `FREE()` 하는데, 우리 것은 `vm_allocate` 로 잡은 것이라
힙이 아니다 — 경우 3 이 죽은 것이 정확히 그 free 다.

그리고 마지막에 `osmesa_update_state( &ctx->gl_ctx )`.

### 고침 3 — 해제가 자기 것만 놓되, **강제 형태를 남긴다** (경우 3, 경우 4 유지)

```c
if (ctx != 0 && bufCtx != 0 && bufCtx != ctx)
    return;                 /* 남의 표면은 안 건드린다 */
```

`ctx == 0` 은 언제나 놓는다 — fork 정리(`OpenStepMGAMesaProbe.c:223`)가 그것에
기대고, 경우 4 가 그것을 지킨다.

### 죽음이 정확히 어디서 났는가 (전수로 확인)

Mesa 에서 깊이 버퍼를 `FREE` 하는 곳은 **딱 둘**이다:

| 곳 | 조건 |
| --- | --- |
| `depth.c:1583` `_mesa_alloc_depth_buffer` | `UseSoftwareDepthBuffer` 일 때만 |
| `context.c:359` `gl_destroy_framebuffer` | **무조건** |

두 번째는 이미 막혀 있다 — `osmesa.c:323` 이 우리 매핑이면 `DepthBuffer` 를
먼저 `NULL` 로 놓고 부른다.

그러면 경우 3 은 어디서 죽었나. 추적하면 **바꿔치기 그 자리**다:

```c
/* osmesa.c:477 */
if (ctx->gl_buffer->DepthBuffer)
    FREE( ctx->gl_buffer->DepthBuffer );      /* <- 여기 */
ctx->gl_buffer->DepthBuffer = accelDepth;
```

B 가 파괴되며 A 의 매핑을 해제한다 → A 를 다시 current 로 만들면 표면을
새로 잡고 깊이도 새로 잡는다 → 그런데 **A 의 `DepthBuffer` 는 아직 해제된
매핑을 가리킨다** → 그것을 `FREE` 한다 → 힙이 아니다.

**고침 3 이 이 경로를 막는다**(B 가 A 의 매핑을 안 놓으므로). 다만 그 줄은
**남은 포인터가 힙에서 왔다고 가정한다.** 그러니 함께 굳힌다:

```c
if (ctx->gl_buffer->DepthBuffer && ctx->gl_buffer->UseSoftwareDepthBuffer)
    FREE( ctx->gl_buffer->DepthBuffer );
```

한 사례가 아니라 **그 부류를 닫는다.** 되돌리기(고침 2)도 같은 이유로
`DepthBuffer = NULL` 을 먼저 놓는다.

### 안 하는 것

- **두 번째 컨텍스트를 가속하지 않는다.** 표면은 하나다. 지금 하는 것은
  거절이 제대로 거절이 되게 하는 것뿐이다.
- **`OSMesaPixelStore` 의 `0 = 이미지 너비` 의미를 이번에 손보지 않는다.**
  헤더와 코드가 어긋나 있지만(`rowlength` 에 0 을 그대로 저장한다) 별개
  문제다. 다만 되돌리기 판단에서 **0 을 너비로 읽어** 잘못된 되돌리기를
  하지 않도록 주의한다.

### 검증

같은 다섯 경우를 다시 돌린다. 전부 통과해야 하고, **경우 4 는 계속 통과해야
한다.** 그리고 `zagree` 와 `zsize` 를 다시 돌려 회귀가 없는지 본다.

## 10. codex 2 차 교차검토 — 설계를 고쳤다 (전부 확인함)

### 놓친 것 중 가장 큰 것: **래퍼는 설치를 막는 것으로 안 없어진다**

`osmesa_update_state` 는 `Clear` 와 삼각형 함수는 다시 놓지만
`RenderStart`/`RenderFinish`/`Finish`/`Flush` 는 **건드리지 않는다.**
그러니 결속을 물어보고 설치를 안 하는 것만으로는 부족하다 — 한 번 달린
래퍼는 그대로 남아, 결속을 잃은 컨텍스트가 **옛 전역 표면을 자기 버퍼로
미러한다.** 애플리케이션 할당이 더 작으면 그 너머로 쓴다.

되돌릴 값은 **NULL** 이다. 확인했다: `osmesa.c` 도 Mesa 도 그 넷을 설정하지
않고 **우리 훅만** 단다. 그리고 네 호출처가 전부 NULL 을 가드한다
(`vbindirect.c:365,393`, `context.c:2021,2033` — 앞의 둘은 주석 처리된 조건
때문에 얼핏 안 보인다). `Clear` 는 저장해 둔 이전 값으로 되돌린다.

### 순서에 관한 것 둘

- **되돌리기는 `compute_row_addresses` 보다 먼저.** 안 그러면 그 함수가
  곧 해제될 VRAM 매핑을 가리키는 행 주소를 만들어 놓고, 다음 소프트웨어
  그리기가 해제된 메모리를 쓴다. `ctx->buffer` 를 되돌린 **뒤에** 행 주소를
  다시 계산한다.
- **놓기 전에 미러한다.** `glFinish` 없이 그린 것이 남아 있을 수 있고,
  지금 해제는 그것을 버린다.

### 깊이는 무조건 되돌리면 안 된다

색은 가속되고 **깊이는 거절될 수 있다**(`bufStride != bufWidth`). 그때는
Mesa 가 힙 깊이 버퍼를 갖고 있으므로, `NULL` 로 놓으면 **샌다.**
`UseSoftwareDepthBuffer == FALSE` 일 때만 되돌린다.

### 크래시 경로 — 내 굳히기가 이미 덮는다

codex 는 "A 의 매핑을 지켜도 free 는 남는다"고 했다. 맞다 — A 를 같은 크기로
다시 바인딩하면 `OpenStepMesaAccelDepthBuffer` 가 **기존 매핑을 그대로**
돌려주고, `osmesa.c:477` 이 그것을 `FREE` 한다. **다만 그 줄에 대한 굳히기를
이미 §9 에 적었다** — `UseSoftwareDepthBuffer` 일 때만 free 한다. 그 시점의
값은 `FALSE` 이므로 free 가 안 일어난다. codex 는 그 문단을 못 본 채 답했다.
결론은 같고, 굳히기가 **필수**라는 것이 다시 확인됐다.

### 그 밖에 받아들인 것

- **되돌리기는 헬퍼 하나**로 만들어 `OSMesaPixelStore` 와 **거절된 재바인딩**
  둘 다에서 쓴다. 경우 2 는 색 버퍼만 돌아오고 깊이·래퍼는 남는다.
- **강제 해제(`ctx == 0`)도 `bufBound` 를 푼다.** 안 그러면 나중 상태 갱신이
  없어진 표면을 결속된 것으로 본다.
- **`OSMESA_ROW_LENGTH = 0`** 은 헤더가 "이미지 너비"라고 하는데 코드가 0 을
  저장해 **모든 행을 같은 주소로** 계산한다. 되돌리기 판단에서만 0 을 너비로
  읽는 것으로는 부족하다 — `compute_row_addresses` 전에 정규화한다.
- **시험 보강**: 경우 5 도 화소를 본다(지금은 `drew` 만 본다).
- **스레드 안전은 이번 범위가 아니다.** `bufCtx`/`bufBound`/저장된 콜백이
  전부 프로세스 전역이고 잠금이 없다. 사실로 적어 두고 손대지 않는다.
