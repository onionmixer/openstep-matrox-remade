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

### 곁따라 오는 것: revoke 를 그만둘 수 있다

배치가 거절되면 지금은 프로세스의 가속을 끈다. 대체 경로가 생기면 **그
삼각형만 소프트웨어로 넘기면 된다.** 커널이 어쩌다 한 배치를 거절했다고
남은 프레임 전부를 느리게 만들 이유가 없다.

다만 **거절이 반복되는데 계속 시도하는 것도 낭비다.** 몇 번 연속 거절되면
그때 revoke 하는 쪽이 맞다 — 숫자는 재서 정한다.

## 4. 조심할 것

- **저장한 포인터는 컨텍스트마다 다르다.** 지금 `osmgaMesaPrevClear` 가
  전역인데 같은 문제를 안고 있다. 결속된 컨텍스트가 하나뿐이고 설치할 때마다
  다시 저장하므로 실무상 맞지만, **전역이라는 사실을 적어 둔다.**
- `gl_set_triangle_function` 은 `RenderMode` 가 `GL_RENDER` 가 아니면
  feedback/select 함수를 넣는다. 우리 선택기는 그 상태를 이미 거절하므로
  도달하지 않지만, **거절이 먼저 오도록 순서를 지킨다.**
- `NoRaster` 면 `null_triangle` 을 넣는다. 역시 이미 거절한다.
- 저장한 것이 **우리 함수 자신이면 안 된다** — 재진입으로 무한히 돈다.
  설치 전에 저장하므로 그럴 수 없지만, 그래도 **검사한다.**

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
