# M1-4a0 — 못 그리는 삼각형을 잃지 않고 소프트웨어로 넘긴다

아직 코드 없음. `M1_4_TEXTURE_PLAN.md` §4b 가 막는 것으로 지목한 첫 항목이다.

## 1. 지금은 잃는다

훅 자신의 주석이 그렇게 적고 있다:

> *This triangle is already lost -- Mesa dispatched it here and there is no
> returning it to the software path.  Giving up acceleration now at least
> stops the next one being lost the same way.*

그래서 배치가 거절되면 **그 삼각형을 잃고 프로세스의 가속을 통째로
revoke** 한다. 이 세션에서 실제로 두 번 봤다.

그리고 텍스처를 하려면 **삼각형 단위 판단**(아핀인가)이 필요한데, 선택기는
**상태 단위**로만 정한다. 삼각형 단위로 거절할 방법이 없으면 그 계획 자체가
성립하지 않는다.

## 2. Mesa 3.4 가 이 문제를 어떻게 다루는가 (읽어서 확인)

번들된 하드웨어 드라이버(3dfx `FX/`)는 **상태 단위**로 물러난다 —
`ctx->IndirectTriangles |= DD_SW_RASTERIZE`. 삼각형 단위 대체는 안 쓴다.

그런데 `gl_update_state` 의 순서를 읽으면 우리가 쓸 수 있는 것이 나온다
(`state.c:1099~1125`):

```
ctx->Driver.TriangleFunc = NULL;
ctx->Driver.UpdateState(ctx);          /* OSMesa, 그 안에서 우리 훅 */
if (ctx->IndirectTriangles & DD_SW_RASTERIZE) {
    ...
    gl_set_triangle_function(ctx);     /* Mesa 자신의 소프트웨어 선택 */
}
```

그리고 `gl_set_triangle_function` 은 **이미 채워져 있으면 일찍 돌아간다**
(`triangle.c:1532`):

```c
if (ctx->Driver.TriangleFunc) {
    dputs("Driver triangle");
    return;
}
```

**그것이 우리 훅이 살아남는 이유다.** 그리고 동시에 방법이다.

주의: `OSmesa` 자신의 선택기는 거의 언제나 `NULL` 을 돌려준다 — 텍스처가
켜져 있으면 무조건, 그리고 깊이 하나짜리 특수한 경우 말고는 전부
(`osmesa.c`, `choose_triangle_function`). 그러니 **그 포인터를 저장하는 것만
으로는 대체가 안 된다.** 대부분 `NULL` 이다.

## 3. 고침

`OpenStepMesaAccelUpdateState` 가 우리 함수를 설치하기 **직전에**:

1. `gl_set_triangle_function(ctx)` 를 **우리가 부른다.** 그 시점의
   `Driver.TriangleFunc` 은 OSMesa 의 선택(대개 `NULL`)이므로, Mesa 가
   자기 소프트웨어 함수를 고른다 — 텍스처 있는 것까지 포함해서.
2. 그 포인터를 **저장한다**.
3. 그 위에 우리 것을 설치한다.

그러면 `osmgaMesaTriangle` 은 못 하는 삼각형을 만났을 때 저장한 것을
부른다. 잃지 않는다.

나중에 `gl_update_state` 가 `gl_set_triangle_function` 을 다시 불러도 우리
것이 이미 있으므로 일찍 돌아간다 — **순서는 그대로다.**

### 곁따라 오는 것 — 다만 **모든 실패를 다시 그려서는 안 된다**

처음엔 "배치가 거절되면 그 삼각형만 소프트웨어로 넘기면 된다"고 적었다.
**틀렸다.** 실패에는 두 종류가 있고 하나는 다시 그리면 안 된다.

드라이버를 읽어 확인했다:

| 언제 | 무엇이 일어났나 | 다시 그려도 되나 |
| --- | --- | --- |
| 검증이 거절 (`IO_R_INVALID_ARG`) | **인코딩도 DMA 도 시작 전** | **된다** |
| 검증 통과 뒤 실패 | `MGA_PRIMADDRESS`/`PRIMEND` 를 이미 썼다 — *"submission did not complete"* | **안 된다** |
| ioctl 자체가 실패 | 드라이버에 닿지도 않았다 | 된다 (다만 장치가 못 쓰는 상태) |

두 번째에서 **엔진은 명령 목록을 이미 받았고 일부 또는 전부를 그렸을 수
있다.** 그것을 소프트웨어로 다시 그리면 **이중으로 그린다** — 깊이나 블렌딩이
걸려 있으면 눈에 보이게 틀린다.

클라이언트가 이 둘을 구분할 수 있다: 제출 블록의 `verdict` 가 검증 결과다.
검증이 거절했으면 `verdict != OSMGA_HW3D_OK`; DMA 뒤에 실패했으면 검증은
통과했으므로 **`verdict == OSMGA_HW3D_OK` 인데 상태가 실패**다.

**규칙**

- `verdict` 가 검증 거절 → **소프트웨어로 넘긴다.** revoke 하지 않는다.
- `verdict == OK` 인데 실패 → **revoke 한다. 다시 그리지 않는다.** 그
  삼각형은 잃는다 — 잃는 것이 두 번 그리는 것보다 낫다.
- ioctl 이 아예 실패(`OSMGA_HW3D_NOT_RUN`) → 아무것도 안 그렸으니 **넘기고,
  그리고 revoke 한다.**
- 성공 → 연속 거절 수를 0 으로.

**연속 거절**이 이어지면 그때도 포기한다 — 결정적인 인코더 버그를 삼각형마다
한 번씩 시도하는 것은 성능 절벽이다. **숫자는 재서 정한다.**

## 4. 조심할 것

- **저장한 포인터는 컨텍스트마다 다르다.** "결속이 하나뿐이니 괜찮다"는
  **우연히 맞는 것**이지 명시적으로 맞는 것이 아니다. 포인터와 **그 주인을
  짝지어** 저장하고, 쓰기 전에 둘 다 확인한다:

  ```c
  static GLcontext   *savedTriangleCtx;
  static triangle_func savedTriangle;
  ```

  설치할 때 둘 다 세우고, 표면을 놓을 때 둘 다 지운다. `ctx` 가 안 맞으면
  **남의 함수를 부르지 않는다.**
- `gl_set_triangle_function` 은 `RenderMode` 가 `GL_RENDER` 가 아니면
  feedback/select 함수를 넣는다. 우리 선택기는 그 상태를 이미 거절하므로
  도달하지 않지만, **거절이 먼저 오도록 순서를 지킨다.**
- `NoRaster` 면 `null_triangle` 을 넣는다. 역시 이미 거절한다.
- 저장한 것이 **우리 함수 자신이면 안 된다** — 재진입으로 무한히 돈다.
  설치 전에 저장하므로 그럴 수 없지만, 그래도 **검사한다.**

## 4c. 그리고 두 가지가 더 있다 (확인함)

**(1) `OSMGAMesaBuildTriangle` 의 0 은 두 가지를 뜻한다.**

```c
if (a == 0 || b == 0 || c == 0 || out == 0) return 0;
if (!osmgaCoordOK(a) || !osmgaCoordOK(b) || !osmgaCoordOK(c)) return 0;
...
return 0;               /* no rows: nothing to draw, not an error */
```

**좌표가 범위 밖**인 것과 **그릴 것이 없는** 것이 같은 값이다. 지금 훅은
둘을 구분 못 하고, 그래서 **범위 밖 삼각형을 조용히 잃는다.** 빌더가
`EMPTY` 와 `UNSUPPORTED` 를 구분해 돌려주게 하고, 후자는 **제출 전에**
소프트웨어로 넘긴다.

**(2) 넘길 때 표면을 더럽혀야 한다.** 소프트웨어 함수는 치환된 VRAM 표면에
쓰지만 우리에게 알려주지 않는다. `RenderStart` 훅이 없거나 못 미더운
상황이면 미러가 **깨끗한 줄 알고 복사를 건너뛴다.** 넘기는 삼각형마다
`OSMGAMesaBufferSoiled()` 를 부른다.

## 4b. 먼저 확인한 것 둘

**재진입은 불가능하다.** 소프트웨어 삼각형 틀(`tritemp.h`)은
`Driver.TriangleFunc` 을 한 번도 읽지 않는다 — 그 이름이 나오는 곳은 선택기
자신뿐이다(`triangle.c:1532,1540`). 그러니 저장한 것을 부르는 것이 우리에게
되돌아오지 않는다.

**교합 시험(occlusion)은 이미 거절된다.** `ctx->Depth.OcclusionTest` 는
`RasterMask` 의 `OCCLUSION_BIT (0x800)` 로 올라가고(`state.c:832`), 우리
선택기는 허용 목록 밖의 비트를 전부 거절한다. 그러니 코어 선택기가
`occlusion_zless_triangle` — 그리는 대신 세는 함수 — 을 고르는 상태는 우리
쪽에 도달하지 않는다.

## 5. 검증 (재부팅 없음)

1. **잃지 않는가**: 거절될 배치를 일부러 만들고(예: `dstPitch` 를 정렬 안 되게
   하는 것은 이제 선택기가 막으니, 커널이 거절할 다른 조건을 쓴다) 그
   삼각형이 **소프트웨어로 그려졌는지** 화소로 확인한다.
2. **회귀 없는가**: `zagree` 와 `bind` 를 다시 돌린다.
3. **대체가 옳은가**: 같은 장면을 (가) 전부 가속, (나) 전부 소프트웨어,
   (다) 삼각형마다 번갈아 그려 셋을 비교한다. (다)가 (가)·(나) 와 화소로
   일치해야 한다.

3 번이 이 작업의 진짜 시험이다 — 대체가 **그리기는 하는데 다르게** 그리면
잃는 것보다 나을 것이 없다.

## 6. 결과 (2026-08-21)

`test/openstep-mga-mesa-fallback-test.c`. 평범한 삼각형 하나와, 한 꼭짓점이
뷰포트에서 한참 벗어난 삼각형 하나를 같은 프레임에 그린다.

```
the ordinary triangle alone: accelerated 1
   green pixels: 1596
with the out-of-range triangle: accelerated 1, handed to software 2
   (of which 0 this back end could not express), declined 2
   green 1596 (was 1596), red 42472
PASS
```

**고치기 전이라면 그 42472 화소는 없었고**, 첫 거절에서 revoke 가 걸려 남은
프레임 전부가 소프트웨어로 떨어졌을 것이다. 지금은 **거절된 삼각형만**
소프트웨어로 가고, 옆의 삼각형은 **같은 프레임에서 계속 가속된다**.

### 예상과 달랐던 것 — 그리고 시험이 더 좋아졌다

지렛대로 "좌표 범위(16384) 밖"을 쓰려 했는데 **그 경로는 타지 않았다**
(`of which 0`). **Mesa 가 먼저 자른다.** 도착한 좌표는 범위 안이고, 거절한
것은 **커널**이다.

그래서 시험이 더 값어치 있다 — 커널 거절이야말로 **삼각형을 잃고 프로세스의
가속까지 끄던** 경우이고, 그것이 이제 안 그런다는 것을 그대로 보인다.

두 이유를 **따로 센다**. "이 백엔드가 표현 못 함"과 "커널이 거절함"은 고칠
것이 다르기 때문이고, 안 나눴으면 위 문장을 그냥 틀리게 적었을 것이다.

### 남은 의문 (따로 세운다)

**평범한 장면에서 커널이 왜 거절했는가.** 큰 삼각형을 Mesa 가 자른 결과가
거절됐다. 재현이 쉽고(이 시험이 그것이다) 이유는 `verdict` 가 안다.
`REMAINING_WORK` 로 세운다.
