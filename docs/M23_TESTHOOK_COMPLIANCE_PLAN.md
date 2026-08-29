# M23 — 이번 세션이 남긴 시험 훅을 규칙 안으로 (2026-08-29, 코딩 전)

`R20` 이 이미 규칙을 세웠다: 시험 훅은 `OSMGA_MESA_TESTHOOKS` 뒤에 두고,
빌드는 `-test` 로만 켜며, **패키징이 그 심볼을 담은 라이브러리를 거부**한다.

`M20`·`M21` 이 넣은 것들은 **그 규칙 밖에 있다.**  새 정책을 만들 일이
아니라 기존 정책을 따라야 한다.

## 1. 이번 세션이 추가한 것과 판정

| 심볼 | 무엇 | 남의 손에 들어가면 | 판정 |
|---|---|---|---|
| `OSMGAMesaHookAreaOmit(mask)` | 더러운 상자에서 출처 하나를 **뺀다** | **그림이 조용히 화소를 잃는다** — `R20 §2` 의 `LegacyTailDrop` 과 같은 등급 | **게이트 + 패키징 거부** |
| `OSMGAMesaBufferMirrorBox(x0,y0,x1,y1)` | 임의의 상자를 복사 | 계약 밖에서 응용 배열에 쓴다.  기능이 아니다 | **게이트 + 패키징 거부** |
| `OSMGAMesaBufferDisagree()` | 표면과 배열이 다른 화소 수 | 읽기 전용.  느릴 뿐 해가 없다 | 남긴다 |
| `OSMGAMesaHookNarrowMirror(mode)` | 좁힌 미러 켜기(2 면 검증까지) | **기능이다** — 옵트인이고 그림은 바이트 동일함을 보였다 | 남긴다 |
| `AreaAll/AreaBox/AreaFullBr/AreaBoxBr/AreaMissed/AreaVerified`, `BufferCopies` | 계수기 | 읽기 전용 | 남긴다 |

`InjectRefusal` 이 목록에 없는 선례와 같은 이유로 `NarrowMirror` 도 남긴다:
그것은 **문서화된 기능**이지 결함 주입이 아니다.

## 2. 그리고 시험 안의 디버그 잔해

`openstep-mga-narrow-mirror-test.c` 가 시험을 고치는 동안 쓴 진단 줄을
아직 인쇄한다:

```
[narrowed %lu, whole-surface %lu]      표에 이미 narr/full 열로 있다 — 중복
[drawn %lu, soft %lu, engine clears %lu, why %d]
```

뒤의 것은 **버리지 않는다**: `soft 1` 이 그 arm 이 소프트웨어 래스터라이저에
정말 닿았다는 증거이고, 그것이 없으면 `5340 화소를 잃었다` 가 무엇 때문인지
말할 수 없다.  다만 표 안으로 넣어 한 줄로 만든다.

앞의 것은 지운다.

## 3. 반증을 먼저 시도했다 — 지우지 **않기로** 한 것들

`R20` 의 규율은 "지워라" 를 받기 전에 반박해 보는 것이다.

* **`MirrorBox` 를 아주 없앨까?**  없애면 `M20 §7` 의 비용 모형(값은
  화소인가 행인가)을 **다시 잴 수 없다**.  그 측정이 좁히기 전체의 근거다.
  → 없애지 않고 게이트한다.
* **지우기 일회권(`areaPendingFull`)을 없앨까?**  `M21 §11` 이 도달하지
  못했고 빈-상자 되돌림이 이미 덮는다.  그러나 그것은 **오늘의 사실**이고,
  지우기와 그리기가 한 브래킷에 들어오는 순간 화소를 잃는다.
  → 남긴다.  `NOT REACHED` 로 인쇄되고 있으므로 숨은 가드가 아니다.
* **`INST_AREA` 를 없애고 `NarrowMirror` 만 둘까?**  없애면 좁히기를 켜지
  **않고** 예산만 재는 일이 불가능해진다 — `M20` 이 한 일이 그것이다.
  → 남긴다.

## 4. 아직 하지 않은 것 — 미커밋 문서

`docs/M22_WHERE_THE_DRAWING_GOES_PLAN.md` 는 전제(`그리기가 7.7 배 느리다`)가
`M21 §13` 에서 반증됐다.  커밋된 적이 없다.  **지운다** — 반증된 전제를 담은
계획서를 남겨 두면 누가 실행한다.  `M21 §13` 이 그 이름을 부르고 있으므로
그 문장도 함께 고친다.

## 5. codex 에 물을 것

1. `AreaOmit` 과 `MirrorBox` 말고 이번 세션 것 중 게이트해야 할 것이 더
   있나?  특히 `NarrowMirror(2)`(검증 모드)는 브래킷마다 표면 전체를 두 번
   읽는다 — 기능인가 시험 훅인가?
2. `Disagree()` 를 읽기 전용이라 남긴다고 했는데, 그것이 노출되면 응용이
   드라이버 내부 상태를 들여다보게 된다.  문제인가?
3. §3 의 세 반증 중 틀린 것이 있나?
4. 시험이 `-test` 빌드에서만 링크되도록 하는 기존 장치가 무엇인지 —
   `build-regression-bins.csh` 는 `build/mesa` 를 쓰는데 그 빌드는 `-test`
   인가?  게이트를 넣으면 시험이 안 링크될 위험이 있다.
5. 빠뜨린 것.

---

## 6. codex 교차검토 판정

지난 회차에 codex 가 **존재하지 않는 파일 경로**를 근거로 댔으므로 이번에도
전부 원본에서 확인했다.

| codex 주장 | 내 검증 | 결과 |
|---|---|---|
| `build/mesa-test` 트리가 이미 있고 `-test` 가 만든다 | `tools/build-matrox-mesa.csh:87,95-103` — `-test` 가 `outleaf=mesa-test`, `out=...-test`, `-DOSMGA_MESA_TESTHOOKS -DOSMGA_HW3D_SUBMIT_DRY` | ✅ **사실** |
| 시험은 release 라이브러리를 링크하므로 게이트하면 안 링크된다 | `test/build-regression-bins.csh:13` — `set L = .../build/mesa/libGL_mga.a` | ✅ **사실.  내가 물은 위험이 실재한다** |
| `openstep-mga-narrow-mirror-test.c` 가 시험 빌드 목록에 없다 | 같은 파일 `:14` 의 `set bins` 에 없다 | ✅ **사실** |
| `NarrowMirror(2)` 는 기능이 아니라 **혼합**이다 — 0/1 은 기능, 2 는 브래킷마다 표면 전체를 두 번 읽는 내부 검사기 | 코드 확인 | ✅ **채택.  모드 2 만 게이트한다** |
| `Disagree()` 는 "읽기 전용이라 안전" 이 위험 전부가 아니다 — 내부 상태 노출이고, 공개로 두면 ABI 로 굳는다 | 논리 | ✅ **채택.  게이트한다** |
| 세 반증(§3)은 틀리지 않았다 | — | ⏭️ 확인 불필요 |
| 선언과 정의를 **양쪽 다** 게이트하라 | — | ✅ 채택 |

### 부분 채택 하나

codex 는 계수기까지 게이트하는 쪽으로 기울었으나 **`AreaMissed`/`AreaVerified`
는 공개로 둔다.**  이유는 링크다: teapot 데모는 release 라이브러리로 빌드되고
그 둘을 부른다.  게이트하면 데모가 안 링크되거나 `#ifdef` 를 데모까지
번지게 해야 한다.  둘은 **읽기 전용 계수기**이고 release 에서는 검증이
돌지 않으므로 **항상 0** 이다 — 노출되는 내부 상태가 없다.

게이트하는 것은 **검사 자체**(`Disagree` 와 미러 안의 검증 블록)이고,
계수기는 그것이 없으면 0 을 읽을 뿐이다.

### 그리고 `areaOmit` 을 어떻게 게이트하나

핫 패스 아홉 군데가 그것을 읽는다.  `#ifdef` 를 아홉 번 두지 않는다:

```c
#ifdef OSMGA_MESA_TESTHOOKS
static unsigned long areaOmit;
#else
#define areaOmit 0UL      /* 컴파일러가 접는다 */
#endif
```

release 에서 아홉 개의 조건이 상수로 접히므로 **비용도 0 이고 심볼도 없다.**

---

## 7. 시행 결과 — 주석이 아니라 심볼로 확인했다

```
release  build/mesa/libGL_mga.a
    T _OSMGAMesaBufferMirrorNarrow      기능
    T _OSMGAMesaHookNarrowMirror        기능
    (AreaOmit / MirrorBox / Disagree 없음)

test     build/mesa-test/libGL_mga.a
    위 둘 + T _OSMGAMesaBufferDisagree
           + T _OSMGAMesaBufferMirrorBox
           + T _OSMGAMesaHookAreaOmit
```

그리고 `OSMGA_TMR_DUMP` — 출하 빌더가 **텍스처 삼각형마다 `getenv`** 를
부르고 내부 상태를 stderr 로 찍고 있었다.  `M1_4D2` 가 쓰고 끝낸 계기이고
시험도 스크립트도 쓰지 않는다.  지우지 않고 게이트했다(다음 같은 조사에
다시 쓸 수 있다) — **release 에서 문자열이 0 개, test 에서 1 개.**

release 판 teapot 이 좁히기로 여전히 0.706 s 이고, **모드 2 는 release 에서
모드 1 로 동작한다**(검증기가 컴파일에서 빠졌으므로 `missed` 줄이 없다).

```
빠른 회귀            전항목 통과
tnm (게이트된 시험)  PASS, 다섯 출처 중 넷이 필요함을 여전히 보인다
mircost              746.7 ns/화소, 값은 화소라는 결론 그대로
```

## 8. 지우지 않은 것과 그 이유

| | 왜 남겼나 |
|---|---|
| `MirrorBox` | 없애면 비용 모형(화소인가 행인가)을 다시 못 잰다.  게이트로 충분하다 |
| 지우기 일회권 | `M21 §11` 이 도달 못 했으나 그것은 오늘의 브래킷 모양에 대한 사실이지 계약이 아니다.  `NOT REACHED` 로 인쇄되므로 숨은 가드가 아니다 |
| `INST_AREA` | 좁히기를 켜지 않고 예산만 재는 유일한 길이고, `M20` 의 증거가 그것으로 나왔다 |
| `AreaMissed`/`AreaVerified` | 읽기 전용 계수기.  release 에서는 검증이 안 도니 항상 0 이라 노출되는 내부 상태가 없다.  게이트하면 teapot 데모가 링크되지 않는다 |
| `Probe.c`·`Buffer.c` 의 `fprintf` | 가속을 철회하거나 소프트웨어 깊이로 물러설 때의 **사용자 경고**다.  디버그가 아니다 |
