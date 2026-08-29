# M21 — 좁힌 미러의 설계 (2026-08-29, 코딩 전)

`M20` 이 (B) 를 정당화했다 — 화소 예산 **113 배**, 시간으로 **37~44 배**,
그리고 (A) 와 달리 **OSMesa 응용이 볼 수 있는 것을 바꾸지 않는다**.
`M19 §8` 은 뒤집혔다.

`M20 §9` 가 남긴 것은 하나다: **쓰는 주체의 전수가 계약이어야 한다.**
"이 부하에서 다 보였다" 는 부하의 사실이지 계약이 아니다.

## 1. 전수 — 추정이 아니라 `dd.h` 와 `osmesa.c` 를 읽었다

Mesa 3.4.2 `src/dd.h` 에서 **색 버퍼**를 쓰는 것 전부:

| 항목 | 지금 | 근거 |
|---|---|---|
| `WriteRGBASpan` `WriteRGBSpan` `WriteMonoRGBASpan` `WriteRGBAPixels` `WriteMonoRGBAPixels` | ✅ **다섯 개 다 감싸고 있다** | `Hook.c` 의 `OSMGA_WRAP(...)` 다섯 줄 |
| `WriteCI32Span` `WriteCI8Span` `WriteMonoCISpan` `WriteCI32Pixels` `WriteMonoCIPixels` | ✅ **VRAM 표면에 닿을 수 없다** | `osmesa.c:182` — `OSMESA_COLOR_INDEX` 는 `rshift=gshift=bshift=0`; `Buffer.c` 의 청구가 `rshift!=16 \|\| gshift!=8 \|\| bshift!=0` 을 **거절**한다.  컬러 인덱스 문맥은 표면을 못 받고, 그러면 미러도 안 돈다 |
| `Clear` | ✅ 이어 붙여져 있다 | `osmgaMesaPrevClear` |
| `DrawPixels` `CopyPixels` `Bitmap` | ✅ **OSMesa 가 아예 설치하지 않는다** | `osmesa.c` 에 `Driver.DrawPixels`/`CopyPixels`/`Bitmap` 대입이 **없다** → Mesa 의 일반 경로 → 위의 RGBA 스팬으로 내려온다 |
| `WriteDepthSpan` `WriteDepthPixels` | — | 깊이 버퍼는 미러가 옮기지 않는다 |
| 엔진 삼각형 | ✅ `OSMGA_LOAD` | 가속된 꼭짓점이 전부 지나는 한 곳 |

codex 가 (B) 에 없다고 한 "완전한 쓰기 주체 모형" 이 **거의 다 이미 있었다.**

## 2. 그러나 구멍이 하나 있고, 코드가 이미 그것을 알고 있다

`osmesa.c:610-630` — `osmesa_choose_triangle_function` 이 상태가 맞으면
`smooth_rgba_z_triangle` / `flat_rgba_z_triangle` 을 돌려준다.  **OSMesa 제
최적화 래스터라이저**이고 **버퍼에 직접 쓴다** — 드라이버 스팬 콜백을
거치지 않는다.  이것이 codex 가 말한 "OSMesa 의 빠른 경로" 다.

그리고 훅이 거절한 삼각형은 바로 그리로 간다:

```c
osmgaMesaFlushPending();
(*savedTriangle)(ctx, v0, v1, v2, pv);
/*
 * It drew into the substituted surface and did not tell us.
 */
OSMGAMesaBufferSoiled();
```

**주석이 이미 "우리에게 말해 주지 않았다" 고 적고 있다** — 그래서 거기서
표면을 더럽다고 표시한다.  플래그로는 충분했지만 **사각형으로는 부족하다.**

teapot 에서 이것이 안 터진 이유는 단순하다: **소프트웨어로 간 삼각형이
0 개**였다.  `M20` 의 "좁히지 못한 브래킷 0" 은 그래서 나온 수이고,
codex 가 경고한 *"결정적으로 보이지만 아닌 수"* 가 될 뻔했다.

## 3. 설계

```
브래킷의 더러운 상자 = 네 출처의 합집합

  1  가속 꼭짓점        OSMGA_LOAD          (있다)
  2  스팬 쓰기 5 종      래퍼               (있다)
  3  소프트웨어 삼각형   osmgaMesaSoftly    ** 새로 **
  4  가속 지우기        제 사각형으로       ** 지금은 표면 전체 **

그리고 시저와 표면에 자른다.
```

3 번은 꼭짓점이 손에 있다(`VB->Win.data[v0/v1/v2]`).  4 번은 지우기가 제
사각형을 아는데 지금은 전체로 청구한다 — **프레임마다 지우는 응용에서는
그 한 브래킷이 곧 전부**이므로 이것이 실제 이득을 정한다.

## 4. 그리고 놓친 주체를 **잡는 장치**를 같이 짓는다

전수를 읽어 확인했다는 것으로는 부족하다 — 새 경로가 생기면 조용히
깨진다.  그래서 검증 모드를 함께 만든다:

```
OSMGA_MESA_MIRROR_VERIFY=1
  좁힌 미러를 돌리고, 그 다음 전체 미러를 돌려,
  좁힌 것이 놓친 화소를 센다.  0 이 아니면 이름을 대고 보고한다.
```

이것이 있으면 어떤 부하에서든 "쓰는 주체를 놓쳤나" 가 **논증이 아니라
측정**이 된다.  그리고 teapot 처럼 소프트웨어가 0 인 부하만으로 판단하는
일이 다시 생기지 않는다.

## 5. 안전 — 왜 이것이 재부팅을 요구하지 않나

전부 유저랜드다.  커널 드라이버도, 레지스터도, 모드도 건드리지 않는다.
잘못되면 그림이 틀리지 기계가 멈추지 않는다.  그리고 **기본값은 지금
그대로**로 두고 좁히기를 스위치 뒤에 둔 채 검증 모드로 먼저 증명한다.

## 6. codex 에 물을 것

1. §1 의 전수에 빠진 것이 있나?  특히 `Accum`, `ReadPixels` 가 쓰기로
   되돌아오는 경로, 그리고 `glCopyTexImage` 류가 표면을 **읽는** 것 말고
   **쓰는** 일이 있나?
2. §2 의 구멍을 상자로 메우는 것과 "소프트웨어 삼각형이 하나라도 있으면
   브래킷 전체" 중 어느 쪽인가?  전자가 맞다면 그 삼각형의 **클립된**
   범위를 어디서 얻나 — `VB->Win` 은 클립 전인가 후인가?
3. §4 의 검증 모드가 놓칠 수 있는 것 — 좁힌 미러와 전체 미러를 **연달아**
   돌리면 두 번째가 첫 번째의 결과를 덮으므로 차이가 0 으로 보이지 않나?
   순서를 어떻게 해야 하나?
4. 지우기의 제 사각형(§3 의 4)이 실제로 이득의 대부분인가 — teapot 은
   프레임마다 지우지 않아 이 항이 0 이었다.  재야 하나?
5. 기본값을 언제 바꾸나, 아니면 영영 스위치로 두나?
6. 빠뜨린 것.

---

## 7. codex 교차검토 판정

codex 가 댄 **파일 경로가 이 트리에 없다**(`opennstep-matrox-remade/`,
`opennstep-matrox342/`).  읽지 않고 기억으로 답했다는 뜻이므로 주장을
하나도 그냥 받지 않고 전부 원본에서 확인했다.

| codex 주장 | 내 검증 | 결과 |
|---|---|---|
| `glAccum` 은 스팬을 우회하지 않는다 — `Driver.WriteRGBASpan` 으로 간다 | `accum.c:389`, `:416` 이 `(*ctx->Driver.WriteRGBASpan)` 을 부른다 | ✅ **사실** |
| `glReadPixels` 는 읽기, `glCopyTexImage` 류는 색 버퍼를 **읽는** 것 | `osmesa.c` 의 드라이버 표에 `Read*` 만 있다 | ✅ **사실** |
| **점·선이 드라이버 콜백으로 갈 수 있으니 모형에 넣어라** | `osmesa.c:1995-1996` — `PointsFunc = NULL`(점은 일반 경로→스팬 ✅) 이지만 **`LineFunc = choose_line_function(ctx)`** 이고, 그것이 `OSMESA_ARGB` 에 `flat_rgba_z_line`/`flat_rgba_line` 을 **설치한다**.  훅은 라인을 **하나도 안 감싼다** | ✅ **채택 — 그리고 이것이 두 번째 구멍이다** |
| 거절 자리는 삼각형마다 상자가 맞다; 브래킷 전체는 과하다 | 논리 + 래스터라이저는 세 꼭짓점의 볼록껍질 밖에 못 쓴다 | ✅ **채택**, ±1 여유와 시저·표면 자르기를 함께 |
| 검증 모드를 앱 버퍼에 두 번 쓰면 차이가 0 으로 보인다 — 전체 미러는 **별도 버퍼**로 | 내가 §6-3 에서 물은 바로 그것 | ✅ **채택** |
| 프레임마다 지우는 응용에서는 지우기 브래킷이 전체라 (B) 가 무너진다; 지우기의 **제 사각형**을 추적하라 | 논리 | ✅ **채택** |
| 기본값을 지금 바꾸지 마라; 검증이 깨끗한 뒤에 | 논리 — 그리고 여기서 거짓음성은 성능이 아니라 **화면 손상**이다 | ✅ **채택** |

## 8. 그래서 전수가 이제 **표 하나로 닫힌다**

`osmesa.c` 가 설치하는 드라이버 훅을 **전부** 뽑았다(`ctx->Driver.X =`).
색 버퍼를 쓰는 것은 정확히 여덟 개다:

```
Clear                     이어 붙임                        ✅
TriangleFunc              훅이 대체, 거절은 저장본으로      ← 구멍 1
LineFunc                  OSMesa 고속 라인, 직접 쓴다       ← 구멍 2
WriteRGBASpan  WriteRGBSpan  WriteMonoRGBASpan
WriteRGBAPixels  WriteMonoRGBAPixels                       ✅ 다섯 개 다 감쌈

PointsFunc = NULL         점은 일반 경로 → 스팬            ✅
WriteCI* 다섯 개          컬러 인덱스는 표면을 못 받는다    ✅
Read* / Color / Index / ClearColor / ClearIndex /
SetDrawBuffer / SetReadBuffer / GetBufferSize /
GetString / UpdateState                    쓰기가 아니다   ✅
```

**지금 이 두 구멍이 결함이 아닌 이유**는 `bufDirty` 가 **플래그**이기
때문이다 — 브래킷 시작에서 무조건 더럽히므로 보이지 않는 쓰기도 전체
미러로 배달된다.  **사각형으로 바꾸는 순간 둘 다 결함이 된다.**
그것이 이 설계가 먼저 해야 할 일이다.
