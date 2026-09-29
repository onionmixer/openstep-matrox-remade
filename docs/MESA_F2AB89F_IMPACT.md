# Mesa `f2ab89f` 가 Matrox 백엔드에 주는 영향 (2026-09-29, 판단만 — 코드 변경 없음)

radeon9250 작업 중 공유 Mesa(`openstep-mesa342`)에 들어간 변경이 Matrox 드라이버에 영향을 주는지 판단한 기록.  실기에 Matrox 가 없어 실기 시험은 하지 않았다 — 판단은 원문 대조와 python 계산으로만.

## 1. 범위 — Matrox v1.3 이후 Mesa 에서 바뀐 것

- v1.3 시점 서브모듈 포인터는 `3271160`(상위 저장소 `05da99c`).  그 뒤 Mesa 커밋은 둘: `ed4a637`(스테이징 스크립트의 디렉터리 이름 오타, radeon 이전)과 **`f2ab89f`**(2026-09-26, radeon 시기).  `git diff 3271160 HEAD` 의 `osmesa.c` 변경은 `@@ -558,13 +558,27 @@` 한 덩어리뿐이고, 작업 트리는 깨끗하다.
- `libGL_mga.a` 는 이 `osmesa.c` 를 hook 매크로로 직접 컴파일한다(`tools/build-matrox-mesa.csh` 64·73·134행) → **Matrox 를 다시 빌드하면 들어간다.  공개된 v1.3 바이너리에는 없다.**

## 2. 변경 내용

`OSMesaMakeCurrent` 가 `OpenStepMesaAccelBuffer` 를 **형식이 OSMESA_RGBA·BGRA·ARGB 일 때만** 부른다.  그 밖(RGB·BGR·COLOR_INDEX)은 부르지 않고 소프트웨어로 둔다.  hook 이 받는 다른 호출(`BoundTo`, `osmesa_update_state` → `OpenStepMesaAccelUpdateState`)은 그대로다.

## 3. 형식별 판정

i386(리틀 엔디언)의 shift(`osmesa.c` 187–257): RGBA (0,8,16) · BGRA (8,16,24) · ARGB (16,8,0) · **RGB·BGR (16,8,0)** · CI (0,0,0).  Matrox 는 (16,8,0) 만 받는다(`OpenStepMGAMesaBuffer.c:1010`).

| 형식 | v1.3 | f2ab89f 뒤 | 판정 |
|---|---|---|---|
| ARGB | 가속 | 가속, 호출 순서 동일 | 변화 없음 |
| RGBA·BGRA | shift 에서 거절 | 같음(여전히 호출) | 변화 없음 |
| RGB·BGR | **가속 — 망가짐** | 소프트웨어 | **버그 수정** |
| COLOR_INDEX | probe 뒤 shift 에서 거절 | 호출 안 함 | 그림 변화 없음 (4 절의 fork 경우만) |

RGB·BGR 이 v1.3 에서 망가졌던 이유(python): 되복사·가져오기는 호출자 배열을 **4 바이트 워드**로 걷는데(`OpenStepMGAMesaBuffer.c` 499·550·579·651·689·734), RGB 배열은 화소당 3 바이트다 — 320×240 76,800 B, 640×480 307,200 B, 800×600 480,000 B 를 배열 끝 너머에 쓴다(배열의 33.3 %).  또 소프트웨어 래스터라이저의 행 간격은 `rowlength*3`(`osmesa.c` 455–460), 엔진은 `stride*4` 라 640 폭에서 1,920 B 대 2,560 B 로 어긋난다.

우리 쪽 소비자는 전부 영향 밖이다: 문맥 생성 전수 grep 에서 SDL2(`SDL_openstepvideo.m:2333`)·GLQuake(SDL2 경유)·teapot·glwin·Matrox 시험 전부 `OSMESA_ARGB`, 나머지는 `GL_RGBA`(= RGBA, 원래 거절).  RGB·BGR·CI 문맥을 만드는 코드는 없다.  → Matrox 가 GLQuake 를 올바르게 구동한 이력과 모순 없음.

## 4. 호출을 건너뛰어 달라지는 Matrox 상태

RGB·BGR·CI 문맥 C 에 대해 v1.3 의 `OpenStepMesaAccelBuffer` 가 하던 일: (a) `bufBound==C` 면 해제(891) — C 는 이제 결코 바인딩되지 않으므로 무의미, (b) `OSMGAMesaProbeRun`(964) — 첫 호출이면 장치를 열어 캐시, **fork 된 자식이면 물려받은 표면·fd 를 버린다**(`OpenStepMGAMesaProbe.c` 212·223), (c) 다른 문맥의 표면이면 거절, (d) shift 판정.  C 에서 Matrox 훅은 바인딩이 없으므로 아무것도 설치하지 않는다(`OpenStepMGAMesaHook.c:4501-4510`).

유일한 상태 차이 — **fork 된 자식이 ARGB 문맥 A 를 쓰던 부모에게서 태어나, 먼저 RGB·BGR·CI 문맥 B 를 current 로 만들 때**: v1.3 은 그 자리에서 물려받은 표면을 풀었고, 이제는 A 를 다시 current 로 만들 때(또는 끝내 안 만들면 종료 때) 푼다.
- 그 사이 B 로는 표면에 아무것도 그려지지 않는다(훅 미설치).  B 의 상태 갱신이 하는 flush·콜백 해제(4476·4502–4503)는 v1.3 에서도 `AccelBuffer` 앞(`osmesa.c:534` 대 옛 `:561`)에서 똑같이 일어난다 — f2ab89f 의 차이가 아니다.
- A 의 `OSMesaMakeCurrent` 에서는 첫 상태 갱신이 물려받은 바인딩을 보고, 이어 `AccelBuffer(A)` 의 probe 가 fork 를 알아채 풀고 새 표면을 잡는다 — **v1.3 에서 자식이 곧바로 A 를 current 로 만들 때와 같은 순서**이고, 끝난 뒤의 상태(새 표면이 A 에 바인딩)는 두 판에서 같다.  (이 마지막 동등성은 원문 추론이다 — 실기 없음.)
- 우리 소비자 중 GL 문맥을 쥔 채 fork 하는 것은 없다.

## 5. 문서 인용 (동작과 무관)

Matrox 트리의 `osmesa.c:N` 인용 29 개를 python 으로 v1.3 판과 HEAD 에 대조: 그대로 10, **+14 밀림 18**, 바뀐 덩어리 안 1(`REMAINING_WORK.md:2068` 의 `561-568`).  밀린 18 개 중 상당수(`1737`·`1895`·`1912`·`1260-1265` 등)는 v1.3 시점에도 이미 문장과 맞지 않았다 — hook 삽입 이전 판에 대고 쓴 것.  소스 안의 것은 주석 하나(`OpenStepMGAMesaHook.c:3821` 의 `osmesa.c:1996` → 지금 2010).  바이너리에는 영향 없음.

## 6. codex 교차검토 (`gpt-6-astra`, 한 주장씩)

| codex 주장 | 내 검증 | 판정 |
|---|---|---|
| 1 차: "C 는 Matrox 상태를 안 바꾼다" 는 거짓 — fork 된 자식에서 B 의 호출이 사라져 물려받은 바인딩이 남는다 | Buffer.c 964·1010·838·227, Probe.c 212·223, Hook.c 4501, osmesa.c 534·573·2027 을 열었다 — 전부 맞다 | ✅ 채택 — 4 절에 fork 경우로 기록 |
| 2 차: "B 를 안 부른 것과 같은 상태" 는 거짓 — B 의 갱신이 pending 을 비우고(1794–1795) `savedTriangle` 을 지운다(4502–4503) | 줄을 열었다 — 맞다.  단 v1.3 에서도 `osmesa.c:534` 가 옛 `:561` 보다 먼저 불려 같은 일이 일어난다 | ⚖️ 부분 채택 — 내 문장이 과했다; f2ab89f 의 차이는 아니다 |

## 7. 결론

- **Matrox 소스 변경 필요 없음.**
- 다시 빌드한 `libGL_mga.a` 는 RGB·BGR 문맥의 메모리 넘침·그림 깨짐을 고친 판이다(우리 소비자는 해당 없음).  v1.3 재릴리스 여부는 별개 결정.
- 문서 인용 +14 밀림은 기록만 한다.
