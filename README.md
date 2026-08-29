# OpenStep Matrox MGA Remade

OPENSTEP 4.2의 Matrox G450 PCI VGA를 위한 새 DriverKit 디스플레이
드라이버와, 그 위에 얹은 Mesa 3.4.2 하드웨어 3D 가속이다. 기존 바이너리
display driver를 고친 것이 아니라, 공개 하드웨어 자료와 공개 MGA 구현을
근거로 새로 만든 `IOFrameBufferDisplay` 서브클래스다.

**v1.2 릴리스**: [releases/tag/v1.2](https://github.com/onionmixer/openstep-matrox-remade/releases/tag/v1.2)
— 설치용 `.pkg` 세 개와 SHA256SUMS. 설치와 복구 절차는
[release-packaging/INSTALL.md](release-packaging/INSTALL.md)에 있고,
변경 내역은 [RELEASE_NOTES_v1.2.md](RELEASE_NOTES_v1.2.md)에 있다.

## SDL2 가속

[SDL2 openstep.2](https://github.com/onionmixer/openstep-sdl2) 부터, SDL2
프로그램이 이 카드로 그리고 **프레임이 시스템 메모리를 거치지 않은 채**
화면에 올라간다.  응용은 드라이버의 present 함수 셋을 한 번 등록하고
평범한 `SDL_GL_SwapWindow` 루프를 그대로 돈다.

```c
#include <SDL_openstepglpresent.h>

static const SDL_OpenStepGLPresent hooks = {
    SDL_OPENSTEP_GLPRESENT_ABI, sizeof(hooks),
    OSMGAMesaBufferOrigin, OSMGAMesaBufferPresentMode,
    OSMGAMesaBufferPresentRect
};
SDL_SetWindowData(window, SDL_OPENSTEP_GLPRESENT_KEY, (void *)&hooks);
```

800×600 주전자가 **0.54 → 43.9 fps**.  차이는 그리기가 아니라 되읽기다:
가속된 프레임을 평범하게 전달하면 표면을 호출자 배열로 되읽어야 하고,
그것이 프레임이 아니라 **원시 묶음마다** 돈다.  데모는 Demos 패키지의
`Examples/Mesa342/SDLTeapot`에 소스로 들어 있다.

## 현재 상태 (2026-08-29)

**`OSMGADisplay`가 실기에서 동작한다.** 기존 `MatroxMGA`를
대체하는 완전한 `IOFrameBufferDisplay` 서브클래스로, G450을 다음 조합으로
구동한다:

- **해상도** 5종(640×480 ~ 1600×1200, VESA DMT 타이밍)
- **픽셀 포맷** 4종: RGB:888/32, RGB:555/16, RGB:256/8(PseudoColor,
  `setTransferTable:count:`로 윈도서버 컬러맵 반영), BW:8(grayscale)
- **회색 단계**는 포맷이 아니라 별도 키 `Gray Levels` — 256/16/4/2.
  네 값 모두 같은 8bpp 스캔아웃·같은 `IODisplayInfo`이고 DAC 램프만 다르다.
  진짜 2bpp linear 프레임버퍼는 G450 스캔아웃 엔진의 한계로 불가능함을
  원본 바이너리로 확정했다(순정 VGA의 `BW:2`는 `rowBytes = width/4`인
  플래너 VGA 경로이지 리니어 스캔아웃이 아니다)
- Configure.app의 `Display.modes`로 20개 조합(5×4) 선택 가능하고, 회색
  단계는 같은 인스펙터의 라디오 매트릭스로 고른다.  config-table의
  `Display Mode`/`Gray Levels` 문자열로 부팅 시 반영

G450 PLL(픽셀 클럭 M/N/P 후보 탐색)과 15bpp RAMDAC 팔레트 인덱싱은 원본
`MatroxMGA` 바이너리를 IDA로 완전 해독하고 codex와 교차검토해 충실히
포팅했다 — 자세한 내용과 실기 검증 결과는
[docs/R6_G450_PIXEL_PLL_ALGORITHM.md](docs/R6_G450_PIXEL_PLL_ALGORITHM.md)에,
전체 검증 이력은 [docs/TEST_STATUS.md](docs/TEST_STATUS.md)에 있다.

모드셋 이후에는 **2D 엔진과 메모리 경로**를 실기에서 실증했다: Storm 2D
단색채우기·BITBLT(S1/S2), 유저스페이스→커널 RPC로 엔진 구동(S3a, self-test
6/6), 계측 19종 + 커서 오버라이드(S3b-prep), 오프스크린 VRAM의 유저 태스크
매핑(S4a). 그 과정에서 **`IODisplayDoBlit`은 OPENSTEP 4.2에서 사문화됐음**을
측정으로 확정했다(WindowServer가 보내지 않는다).

### 3D — Mesa 3.4.2가 카드에서 돈다

OSMesa 백엔드(`libGL_mga.a`)가 삼각형을 G450 엔진으로 보내고, 표현할 수 없는
것만 Mesa의 래스터라이저로 내려보낸다. 삼각형을 엔진에 넘기는 길은 **두
가지**이고, 둘 다 실기에서 동작한다:

| | 무엇을 보내나 | 상태 |
|---|---|---|
| **사다리꼴** | 유저랜드가 보간 평면을 풀고 변을 걸어 AR/DR 레지스터를 채운다 | **출하 기본값** |
| **WARP** | 32 바이트 float 정점을 그대로 싣고 **카드의 setup 엔진 마이크로코드**가 계산한다 | 옵트인 — Configure.app 체크박스 또는 `OSMGA_MESA_WARP=1` |

실기 측정 — 800×600 창, 회전하는 teapot, 세 팔을 같은 자리에서:

```
                fps      프레임        clear   draw    화면으로
사다리꼴       40.6    24.60 ms        3.48   16.67   3.71 present
WARP           53.7    18.64 ms        3.47   10.85   3.69 present
정품 Mesa      12.3    81.46 ms        2.65    9.05   62.79 server-wait
```

**WARP 의 그리기가 사다리꼴보다 35% 빠르다**(16.67 → 10.85 ms). 이유는
전송량과 대기다:

```
              dword/프레임    엔진 대기(spin)
사다리꼴         50606         1690,  33/33 제출이 대기했다
WARP             28176          674,   1/33 만 대기했다
```

그리고 가속이 정품보다 4.4 배 빠른 이유는 짚어둘 값어치가 있다.
**래스터화가 아니다** — 정품 쪽 draw 가 9.05 ms 로 여전히 가장 짧다. 차이는
전부 **화면 전달**이다: 드라이버의 VRAM→VRAM blit 3.69 ms 대 AppKit 경로
62.79 ms. (같은 사다리꼴 구성을 2026-08-27 에 잰 값은 47.6 fps 였다 —
실행 간 편차이지 회귀가 아니다.)

오프스크린 전체화면도 실증했다: **1600×1200×32** 에서 오프스크린 창
20,037,632 바이트, teapot 삼각형 17,292 개 중 **97% 를 카드가 그리고 커널
거부 0**. 텍스처 아레나 8,511,488 바이트가 남고, 세 값 모두 python 계산과
바이트 단위로 일치한다.

#### 출하 기본값은 꺼짐이고, 이유가 둘이다

성능 때문이 아니다 — WARP 이 더 빠르다(그리기 10.85 ms 대 16.67 ms).

**첫째, 설치된 보드의 VRAM 용량을 예측할 수 없다.** 이 드라이버는 8/16/32
MiB 보드에 다 실리고 용량은 `MGA Memory Size` 로 **선언**된다. 선언이 작으면
오프스크린 창이 좁아지거나 아예 서지 못한다 — 1600×1200×32 에 8 을 선언하면
가시 프레임버퍼만 7,680,000 바이트라 자리가 없고, 드라이버가 그것을 알아채고
이유를 남기며 OpenGL 을 끈다. 패키지는 상대 기계에 무엇이 꽂혀 있는지 알 수
없으므로 3D 층 선택을 켜 둔 채 내보내지 않는다.

**둘째, 근퇴화 기하다.** 정상 기하에서는 두 층이 같은 그림을 그린다 —
커버리지 27/27 run 표가 바이트 동일하고, 이음매는 삼각형마다 정확하며 중복
소유도 틈도 없다. 다만 **선언된 차이 넷**이 있고, 그중 마지막이 결정적이다:

1. 색이 한 레벨 낮다(엔진이 하위 15 비트를 버리는데 WARP 의 시작값에는 반
   레벨을 더할 자리가 없다)
2. 깊이가 한 코드 낮고, 오차가 `(|dz/dx| + |dz/dy|)/16` 이내다
3. 변 근처 소유권이 다르다(커버리지 자체는 정확하다)
4. **근퇴화 기하** — 바늘처럼 얇은 삼각형에서 WARP 이 더 크게 흔들린다.
   `docs/M17` 이 거부 집합을 만들려 했으나 **어느 양도 좋은 것과 나쁜 것을
   가르지 못했다**. 가를 수 없으면 문턱은 옳은 것을 거절하고 틀린 것을
   받는다. 그래서 규칙이 아니라 **결정**으로 옵트인이다.

#### 켜는 법 — 두 가지, 그리고 우선순위

Configure.app 의 디스플레이 인스펙터에 **`Use WARP 3D`** 체크박스가 있다.
config 표의 `"WARP 3D"` 키를 쓰고, 커널이 그것을 읽어 능력 워드의
`CAP_WARP_PREFERRED` 비트로 알리며, 라이브러리가 그것을 보고 고른다.
커널은 두 형식을 다 받으므로 **아무것도 거절하지 않는다** — 나르기만 한다.
config 표는 드라이버 init 에서 한 번 읽히므로 **다음 부팅에 반영된다.**

환경변수 `OSMGA_MESA_WARP` 는 **있으면 언제나 이긴다**(`0` 도 명시적 답이다).
이 저장소의 모든 측정과 스윕이 환경변수로 층을 고르므로, 기계 설정이
조용히 이기면 측정이 다른 층을 재고도 아무 말을 하지 않게 된다.

실기에서 여섯 조합을 다 확인했다(teapot 의 제출 수: WARP 128, 사다리꼴 193):

```
              체크박스 끔        체크박스 켬
env 없음      193  사다리꼴      128  WARP
env=1         128  WARP          128  WARP
env=0         193  사다리꼴      193  사다리꼴
```

체크박스를 켠 채 부팅하면 **스위트 전체가 WARP 을 지나간다.** 그렇게 돌려
빠른 회귀가 전항목 통과했고(그 전까지 이 스위트는 언제나 사다리꼴로 돌았다 —
`-warp` 스윕 변종이 따로 있는 것이 그 증거다), 층을 고정하는 고정물은 하나도
없으므로 그것은 추론이 아니라 측정이다: 커버리지 고정물이 기본에서 `warp=1`,
`OSMGA_MESA_WARP=0` 에서 `warp=0` 을 답한다. **양쪽 팔이 다 깨끗하다.**

#### 오프스크린으로 그리고 되읽는 응용

OSMesa 는 응용이 배열 포인터를 언제든 말없이 읽을 수 있게 하므로, 이
백엔드는 **렌더 브래킷이 닫힐 때마다** 표면 전체를 배열로 되읽어 왔다.
teapot 을 파일로 쓰는 부하에서 그 되읽기가 실행의 **99.2%** 였다.

브래킷이 실제로 그린 사각형만 배달하는 길을 만들었다(`OSMGA_MESA_NARROW=1`,
옵트인). 같은 부하에서 **41.4 배**이고 그림은 **바이트 동일**하다. 사각형이
덮어야 할 쓰는 주체는 `dd.h` 와 OSMesa 의 드라이버 표를 전수해서 정했고,
그 과정에서 **아무것도 알리지 않고 표면에 쓰는 두 경로**를 찾았다: 거절된
삼각형이 가는 OSMesa 자체 래스터라이저와, OSMesa 가 설치하는 자체 라인
함수다. 둘 다 플래그일 때는 무해했고 사각형이 되는 순간 결함이 된다.

### VRAM 선언

보드 메모리를 `MGA Memory Size` 로 **8 / 16 / 32** MiB 중에 고른다. 32 는 세
가지 독립된 방법으로 실증했고(BAR sizing, 브리지 윈도우, 2,048 페이지 증명),
8 도 실기에서 확인했다 — 1600×1200×32 에 8 을 선언하면 가시 프레임버퍼만
7,680,000 바이트라 오프스크린 창이 들어갈 자리가 없고, 드라이버가 그걸
알아채고 이유를 남기며 OpenGL 을 끈다. **8 을 선언하면 8 만 쓴다.**

8 은 아직 없는 G400 지원을 내다본 것이다. 지금 `:3714` 는 G450 이 아니면
모드 프로그래밍을 거부한다.

### 데모

Mesa Demos 변종 패키지가 데모 **두 쌍**을 싣는다. 각각 소스 하나를 두
바이너리로 빌드하고, 소스·빌드 스크립트·자체 README 를 함께 넣어서 패키지만
보고도 어떻게 만들어졌는지 알 수 있게 했다.

| | |
|---|---|
| `Examples/Mesa342/Teapot` | `teapot_sw` / `teapot_hybrid` — 파일로 쓴다 |
| `Examples/Mesa342/GLWindow` | `glwin_sw` / `glwin_hybrid` — 창에서 돌고 제목에 fps 를 찍는다 |

각 쌍의 `_sw` 는 stock Mesa 만 링크해 Matrox 코드가 **하나도** 없고, 포장
단계가 그걸 심볼로 확인한다. 다만 `glwin_hybrid` 는 `teapot_hybrid` 와 달리
그리기가 아니라 **전달**에 드라이버가 필요해서, 드라이버가 없으면 창을 열고
그렇다고 말한 뒤 비어 있는다.

현재 사실의 정본은 [docs/REMAINING_WORK.md](docs/REMAINING_WORK.md)이고,
초기 단계의 검증 이력은 [docs/TEST_STATUS.md](docs/TEST_STATUS.md), 드라이버
사용법은
[OSMGADisplay/README.md](OSMGADisplay/README.md)다.
3D 착수 전의 실현 가능성 조사는
[docs/S5_HW3D_DMA_FEASIBILITY.md](docs/S5_HW3D_DMA_FEASIBILITY.md)에 남아 있다.

---

## 앞선 설계는 폐기됐다 (2026-08-18)

이 프로젝트는 처음에 **`MatroxMGA` 와 공존하는 3D sidecar 서비스**로 설계됐다
— 기존 디스플레이 드라이버가 화면을 소유한 채로, 별도의 커널 서비스와 MiG
control plane 이 offscreen VRAM 만 빌려 쓰는 모양이었다.  운영자가 target 을
generic SVGA 단독 소유로 재부팅해 MGA 자원을 무소유로 만든 뒤, `MatroxMGA` 를
**완전히 대체하는 디스플레이 드라이버**를 새로 만드는 쪽으로 바뀌었고, 위
"현재 상태" 가 그 결과다.

기억해 둘 것은 셋뿐이다:

* **P1/P2 의 MiG control plane, lease, sidecar service 는 폐기됐고 현재
  드라이버는 그 코드를 링크하지 않는다.**  저장소에 남은 `OSMGAProbe` 와
  `OSMGAService` 가 그 잔재다.
* 그때의 **16 MiB 보수적 cap** 은 지금 정책이 아니다.  VRAM 은 `MGA Memory
  Size` 로 8/16/32 중에 **선언**한다(위 "VRAM 선언").
* 그 시절 문서가 현재형으로 서술하는 값은 **그때의 기록**이지 현재 아키텍처가
  아니다.

근거와 결정 기록은 지우지 않았다 — `ANALYSIS.md`, `PLAN.md`,
`docs/P0_OPENSTEP_DOCUMENTS.md`, `docs/D1_REPLACEMENT_DISPLAY_OWNERSHIP.md`,
`docs/RECOVERY_REPLACEMENT_DRIVER_EXECUTION_PLAN.md` 를 비롯한 40 여 개
문서에 그대로 있다.  README 가 그것을 되풀이하지 않을 뿐이다.
