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
