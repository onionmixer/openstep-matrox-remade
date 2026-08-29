# C7 — SDL2 로 하드웨어 가속 teapot (2026-08-29, 코딩 전)

## 1. 붙는다는 것은 이미 확인했다

SDL2 포트의 GL 백엔드가 우리 드라이버가 붙는 두 자리를 그대로 쓴다:

```
SDL_openstepvideo.m:2246   OSMesaCreateContext(OSMESA_ARGB, share)
SDL_openstepvideo.m:2118   OSMesaMakeCurrent(ctx, pixels, ...)
```

`OSMESA_ARGB` 는 표면 청구가 요구하는 packing(`16/8/0`)이다.  그러므로
**`libGL.a` 대신 `libGL_mga.a` 로 링크하는 것만으로** 카드가 그린다.
SDL2 는 한 줄도 바뀌지 않는다.

## 2. 그러나 전달이 문제다 — 그리고 그것이 이 데모의 요점이다

`OPENSTEP_GL_SwapWindow` 는 OSMesa 버퍼를 **AppKit 으로 올린다**:

```c
data->framebuffer_pixels = context->pixels;
OPENSTEP_UpdateWindowFramebuffer(_this, window, NULL, 0);
```

그러면 프레임마다 **카드 → 되읽기 → AppKit → 화면** 이 된다.  되읽기 비용은
`M1_4EB` 가 800x600 에서 **약 358 ms** 로 기록했다 — 3 fps 도 안 된다.

`glwin_hybrid` 가 52 fps 인 이유는 그리기가 아니라 **present 3.69 ms 대
AppKit 62.79 ms** 였다(`R21 §8`).  같은 결론이 여기서도 나올 것이다.

**그래서 이 데모는 "빠른 SDL2 teapot" 이 아니라 그 질문에 답하는 계측이다.**
추정 358 ms 를 실측으로 바꾸는 것이 1 차 목표다.

## 3. 만드는 것

기존 관례 그대로 **소스 하나, 바이너리 둘**:

```
sdlteapot_sw       stock libGL.a  + libSDL2.a     소프트웨어 기준선
sdlteapot_hybrid   libGL_mga.a    + libSDL2.a     카드가 그린다
```

`teapot_sw`/`teapot_hybrid`, `glwin_sw`/`glwin_hybrid` 와 같은 모양이라
읽는 사람이 새로 배울 것이 없다.

베낄 것:

```
장면·조명·회전   test/openstep-mga-glwin.m       (비교 가능하도록 동일하게)
기하             teapot-geometry.h                (빌드 때 Mesa 에서 잘라낸다)
SDL2 골격        openstep-sdl20 의 gl11-cube-probe (창·문맥·이벤트·swap)
```

## 4. 무엇을 보고하나

fps 만으로는 이 실험이 답을 못 준다.  프레임을 **어디서 잃는지** 갈라야 한다:

```
wall fps
draw    ms   (glFinish 까지)
swap    ms   (SDL_GL_SwapWindow)
가속일 때만: drawn / traps / batches / mirrors
             traps 0 이면 WARP 이 그렸다는 뜻
             mirrors 가 프레임당 1 이면 되읽기가 프레임마다 돈다
```

`glwin` 이 이미 같은 갈래(clear/draw/present)를 찍으므로 숫자가 직접
비교된다.

## 5. 하지 않는 것 (지금은)

**`SDL_GL_SwapWindow` 를 고치지 않는다.**  빠르게 하려면 present 모드로
VRAM→VRAM blit 을 해야 하지만, 그것은 **SDL2 포트의 백엔드 변경**이고 이
데모가 그 근거를 만들어 준 뒤에 할 일이다.  측정 없이 백엔드를 고치는 것은
`M19` 에서 한 번 밟은 순서다.

## 6. 어디에 두나

`openstep-matrox-remade/test/` 에 둔다 — 드라이버를 실증하는 것이 목적이므로.
**데모 패키지에는 아직 싣지 않는다**: 실으면 matrox 데모 패키지가 SDL2 설치를
요구하게 되고, 지금 그 의존을 만들 이유가 없다.

## 7. codex 에 물을 것

1. 정적 링크에서 `libGL_mga.a` 와 `libSDL2.a` 의 순서가 문제되나?  SDL2 가
   OSMesa 심볼을 참조하고 우리 라이브러리가 그것을 대체하므로 순서가
   중요할 것 같다
2. SDL2 의 GL 문맥이 창 크기를 바꾸면 `OSMesaMakeCurrent` 가 다시 불리는데,
   우리 표면 청구는 **크기가 다르면 거절**한다(`Buffer.c`).  창 크기를
   고정해야 하나, 아니면 거절이 곧 소프트웨어 폴백이라 무해한가?
3. `SDL_GL_GetProcAddress` 로 받아야 하나, 직접 호출해도 되나?  기존 큐브
   프로브는 전자를 쓰는데 `glwin` 은 직접 호출한다
4. 되읽기가 프레임마다 도는지 어떻게 확인하나 — `OSMGAMesaHookMirrors()`
   증가분이 프레임 수와 같으면 그것인가?
5. 빠뜨린 것

---

## 8. codex 교차검토 판정

| codex 주장 | 내 검증 | 결과 |
|---|---|---|
| **링크 순서가 중요하다**: 객체 → `libSDL2.a` → **완전한 GL 아카이브 하나**.  `libGL_mga.a` 는 오버레이가 아니라 **정품 Mesa 사본에 `osmesa.o` 를 교체하고 가속 객체를 더한 것**이므로 두 GL 아카이브를 한 줄에 두면 안 된다 | `tools/build-matrox-mesa.csh:127` — `cp $stock $out/libGL_mga.a`, `:169` — `ar r ... osmesa.o osmgaccel.o` | ✅ **사실** |
| **창 크기를 고정하고 리사이즈를 무효 실행으로 취급하라.**  거절은 정확성 면에서 안전하지만 벤치마크에는 아니다 — 한 fps 안에 하드웨어 프레임과 소프트웨어 프레임이 섞인다 | `Buffer.c` 확인: 크기가 다르면 `return 0` 이고 주석이 *"the caller renders in software, which is wrong only in being slow"* | ✅ **채택** |
| GL 함수는 **직접 호출**이면 된다.  `SDL_GL_GetProcAddress` 도 유효하나 가속 스위치가 아니다 | 논리 | ✅ 채택 |
| **"미러 수 = 프레임 수" 는 근거가 못 된다.**  미러 계수기는 **요청/브래킷**을 세고 실제 복사가 아니다.  `OSMGAMesaBufferCopies()` 가 되읽기 신호다 | 내가 오늘 그 둘을 갈라 놓았다(`M20`): 129 호출 중 **128 복사** | ✅ **사실.  내 계획이 틀렸다** |
| `draw ms (to glFinish)` 는 순수 그리기가 아니다 — 미러가 거기서 돈다.  **`render + finish`** 와 **`SDL swap`** 으로 이름을 고쳐라 | 논리 | ✅ 채택 |
| 하드웨어라고 말하려면 **표면 청구 + `drawn > 0` + `batches > 0` + 재생/누락 없음**.  WARP 이라면 **`warp == drawn` 이고 `traps == 0`** — `traps == 0` 만으로는 **전면 소프트웨어 폴백과 구별되지 않는다** | 논리 | ✅ **채택.  가장 중요한 지적** |
| 층을 `OSMGA_MESA_WARP` 로 **명시적으로 고정**하고 인쇄하라 — 환경변수가 Configure 설정을 이기므로 두 실행이 조용히 다른 층을 잴 수 있다 | 오늘 그 우선순위를 내가 만들었다 | ✅ 채택 |
| `SDL_GL_ACCELERATED_VISUAL=1` 을 요청하지 마라 — OPENSTEP 백엔드가 거절한다 | **포트 어디에도 그 처리가 없다**(`accelerated_visual` grep 0 건) | ⚖️ **조언은 채택, 근거는 미확인.**  요청해서 얻을 것이 없으므로 요청하지 않지만, "백엔드가 거절한다" 를 사실로 옮겨 적지는 않는다 |
| GL 문맥은 하나만 — 드라이버는 프로세스당 표면 하나를 소유한다 | `Buffer.c` 가 두 번째 문맥을 거절하는 것을 이미 확인했다 | ✅ 일치 |

## 9. 그러므로 합격 기준이 바뀐다

이 데모의 산출물은 fps 가 아니라 **어느 경로로 돌았는지의 증명**이다:

```
표면이 엔진의 것인가        OSMGAMesaBufferOrigin() != 0
카드가 그렸는가             drawn > 0,  batches > 0
어느 층인가                 warp == drawn 이고 traps == 0  -> WARP
                            (traps == 0 만으로는 전면 폴백과 같다)
소프트웨어로 샌 것          software, unsupported, replayed 를 각각 인쇄
되읽기가 도는가             BufferCopies 증가분 (미러 호출 수가 아니라)
층 고정                     OSMGA_MESA_WARP 를 명시하고 그 값을 인쇄
크기                        고정, 리사이즈 불가.  들어오면 실행 무효
```

## 10. 만들었고, 두 가지가 나왔다

`test/openstep-mga-sdl-teapot.c`, 소스 하나로 두 바이너리.  빌드에 필요한
것 셋을 찾는 데 시간이 걸렸고 적어 둔다:

```
-D__OPENSTEP__            없으면 SDL_config 가 minimal 을 고르고 int8_t 부터 깨진다
                          (PORT_PLAN.md:157 이 이 정의를 쓴다고 적어 두었다)
-framework SoundKit       libSDL2.a 가 _SNDStartPlaying/_SNDWait 를 참조한다
링크 순서                 객체 -> libSDL2.a -> GL 아카이브 하나
```

### 뒤집힌 주전자 — 장면을 베낀 대가

운영자가 화면에서 주전자가 뒤집혀 있다고 알려 줬다.  `glwin` 의 프러스텀을
값 그대로 베꼈는데, **그 위아래 뒤집기는 그 데모의 전달 방식에 딸린 것**이다:
blit 이 행 순서를 못 뒤집으니 투영이 뒤집고, 그래서 컬링도 꺼 둔다.  SDL2 는
백엔드가 `OSMesaPixelStore(OSMESA_Y_UP, 0)` 을 부르므로 이미 top-down 이고,
거기에 한 번 더 뒤집으니 거꾸로 섰다.  정상 프러스텀으로 고쳤다.

**장면을 베끼는 것과 전달을 베끼는 것은 다르다.**

### 그리고 가속이 붙지 않는다 — 붙었다가 놓아진다

계측한 라이브러리로 확인했다:

```
OSMGA-CLAIM entered w=800 h=600 shifts=16/8/0 row=800
OSMGA-CLAIM TOOK IT at body line 220     <- 표면을 넘겨줬다
OSMGA-CLAIM released
OSMGA-CLAIM released                     <- 두 번 반납된다
```

그러므로 **거절이 아니다.**  프로브는 `hardware` 라 답하고, 시프트도
`16/8/0` 으로 맞고, 청구는 성공한다.  그 직후 `SDL_CreateWindow` 안에서
표면이 **두 번 놓아진다.**  fork 는 아니다(그 표식은 안 찍혔다).

배제한 것들:

```
크기        640x480 에서도 같다 (비-SDL teapot 은 640 에서 잘 된다)
실행 경로   gcdsd 로 돌린 비-SDL teapot 도 표면을 받는다
링크        실행 파일에 가속 객체가 들어 있고, libSDL2.a 는 OSMesa 를 정의하지 않는다
포맷        OSMESA_ARGB 는 16/8/0 이고 그것이 요구되는 값이다
```

**다음 단계는 반납을 부르는 자리를 찾는 것**이다 — `osmesa.c` 의
`osmesa_leave_accel` 과 `OSMesaDestroyContext` 가 후보이고, SDL2 가 문맥을
만들었다 버리는 길이 있는지 봐야 한다.

### 지금 시점의 숫자 (전부 소프트웨어)

```
800x600   render+finish 7.75 ms,  SDL swap 7.89 ms,  wall 80.47 ms (12.43 fps)
```

`wall` 에서 둘의 합을 뺀 **약 65 ms** 가 AppKit 경로다 — `glwin_sw` 가 같은
자리에서 62.79 ms 를 쓰고 12.8 fps 인 것과 맞는다.  그러므로 `M19` 의 결론이
여기서도 서 있다: **비용은 그리기가 아니라 전달이다.**
