# C2 — Configure.app 에서 WARP 을 켠다 (2026-08-29, 코딩 전)

지금 WARP 은 환경변수 `OSMGA_MESA_WARP` 로만 켜진다(`Hook.c:796` 이 유일한
판정, `:1331` 이 유일한 소비처).  Workspace 에서 띄우는 응용에는 사실상
줄 방법이 없다.  Configure.app 인스펙터에 체크박스를 놓는다.

## 1. 이미 있는 통로 — 새로 만들 것이 아니다

`"Mesa Acceleration"` 이 **정확히 같은 모양**으로 이미 돌고 있고, 커널의
주석이 그 설계를 이렇게 적어 두었다(`:4035-4038`):

> M1-3a: read it here but gate nothing here.  Unlike "VRAM Mmap", which
> decides whether a device is published at all, this switch is only reported
> through the capability parameter — **the library is what declines to
> accelerate.**  Keeping the driver's behaviour identical either way means
> the switch cannot break the display.

WARP 도 똑같다: 커널은 두 형식을 다 받고, **고르는 것은 유저랜드**다.
그러므로 커널은 *거절* 하지 않고 *알려* 주기만 한다.

```
체크박스 -> config table "WARP 3D"
        -> 커널이 :4033 자리에서 읽어 osmgaWarpPreferred 에 담고
        -> :5632 자리에서 flags |= OSMGA_HW3D_CAP_WARP
        -> 라이브러리의 osmgaMesaWarpWanted() 가 그것을 본다
```

`caps[CAP_FLAGS]` 는 비트 `1/2/4/8` 만 쓰고 있다 — **`0x10` 이 비어 있으므로
ABI 배열을 늘리지 않는다.**

## 2. 우선순위 — 환경변수가 이긴다

```
OSMGA_MESA_WARP 가 있으면      그 값이 답이다 (0 도 유효한 답: 끈다)
없으면                         caps 의 WARP 비트가 답이다
```

시험과 측정이 환경변수로 돌아가고 있고(`glwin`, teapot, 스윕 전부),
config 가 그것을 덮으면 **측정이 조용히 다른 층을 재게 된다.**
그러므로 환경변수가 이긴다.  단, 지금 `osmgaMesaWarpWanted()` 는 값이
`1/y/t` 가 아니면 전부 0 으로 접으므로, "설정 안 됨" 과 "0 으로 설정됨" 을
가르도록 고쳐야 한다.

## 3. 패널 자리 — 계산해서 정했다

`build-inspector-nib.py` 의 레이아웃은 전부 상수이고, 그 파일의 주석이
**헤드룸 규칙**을 적어 두었다: 위 여백 = `(9 + GROW) - top`, 오늘 23 px.
python 으로 새 행을 넣고 같은 23 이 나오는지 확인했다:

```
오늘   GROW 154, 최상단 140  -> (9+154)-140 = 23
추가   WARP 행을 y=44 에, 그 위 전부 +20, GROW 154 -> 174
       최상단 160           -> (9+174)-160 = 23   같다
행:  caption 6, mmap 24, warp 44, storm 64, gray 86, vram 108,
     status 130 / 146
```

**여백을 지어내지 않고 원래 값을 지킨다.**  `add_switch(label, y, outlet,
action)` 헬퍼가 이미 있으므로 호출 한 줄과 상수 이동이다.

## 4. 건드리는 파일

```
nib-src/build-inspector-nib.py   행 상수 + add_switch 한 줄
English.lproj/.../data.classes    outlet warpSwitch, action toggleWarp:
OSMGADisplayInspector.h/.m        KEY_WARP, outlet, setTable 읽기, 토글
Default.table / Instance0.table   "WARP 3D" = "No"
hw3d/OpenStepMGAHW3D.h            OSMGA_HW3D_CAP_WARP 0x10
...ReplacementDisplay.m           키 읽기(:4033 옆) + flags(:5632 옆)
mesa/OpenStepMGAMesaHook.c        osmgaMesaWarpWanted() 가 caps 도 본다
```

**커널이 들어가므로 재부팅이 한 번 필요하다.**  이 목록에서 그것만이다.

## 5. 기본값과 경고

`Default.table` 의 값은 **`No`** 다 — `M17` 이 근퇴화 기하에서 좋은 것과
나쁜 것을 가르는 양을 찾지 못했고, 그래서 옵트인인 것이 규칙이 아니라
**결정**이기 때문이다.  체크박스는 그 결정을 사용자에게 넘기는 것이지
뒤집는 것이 아니다.

패널의 캡션 행이 이미 "설정은 다음 부팅에 반영된다" 는 취지를 말하고 있으므로,
WARP 행의 레이블 자체에 단서를 넣는다:

```
Use the WARP setup engine for 3D (faster; differs on sliver triangles)
```

## 6. codex 에 물을 것

1. 우선순위(§2)가 옳은가?  환경변수가 config 를 덮는 것이 맞나, 아니면
   config 가 시스템 정책이니 그쪽이 이겨야 하나?
2. `osmgaMesaWarpWanted()` 는 **프로세스마다 한 번** 캐시한다.  caps 는
   프로브가 열릴 때 온다 — 캐시 시점이 프로브보다 **앞설** 수 있나?
   앞선다면 config 값을 영영 못 본다
3. 커널이 이 키를 읽어도 **드라이버 동작은 하나도 안 바뀐다**(보고만 한다).
   그런데도 재부팅이 필요한 이유가 config table 을 부팅 때만 읽기 때문인데,
   맞나?  살아 있는 드라이버에 밀어 넣는 길이 있나(있어도 쓰지 않겠지만)
4. `0x10` 을 새 CAP 비트로 쓰면 **구버전 라이브러리 + 신버전 커널** 조합에서
   무슨 일이 생기나?  구버전 라이브러리는 `CAP_REQUIRED` 만 보므로 무해할
   것 같은데, 반대 조합(신 라이브러리 + 구 커널)은 비트가 0 이라 "끔" 으로
   읽힌다 — 그게 안전한 기본값인가?
5. 인스펙터에 체크박스를 늘리면 `data.classes` 와 nib 이 어긋날 위험이 있다.
   기존 두 스위치는 어떻게 검증됐나 — 실기에서 Configure 가 로드하는 것을
   확인하는 절차가 있나?
6. 빠뜨린 것.

---

## 7. codex 교차검토 판정

| codex 주장 | 내 검증 | 결과 |
|---|---|---|
| **내가 근거로 인용한 주석이 낡았다** — `"Mesa Acceleration"` 이 *"gate nothing here"* 라 하지만 커널은 실제로 게이트한다 | `:6452`, `:11027`, `:15992` 세 자리가 전부 `if (!osmgaMesaAccelEnabled) return IO_R_UNSUPPORTED;` | ✅ **사실.  내 계획의 근거 인용이 틀렸다** — 선례는 여전히 쓸 수 있으나(WARP 비트는 진짜로 보고 전용이다) **낡은 주석을 현재 동작의 증거로 쓰면 안 된다** |
| 환경변수가 이겨야 하고 **`=0` 도 명시적 답**이어야 한다 | 논리 — 지금 chooser 는 `1/y/t` 아니면 전부 0 으로 접어 "안 설정" 과 "0 설정" 을 못 가른다 | ✅ 채택 |
| 캐시를 **프로브 성공 직후** 채워라.  현재 호출 순서상으로는 안전하지만 그 간접 순서에 정확성을 기대지 마라 | 호출 그래프 확인: `osmgaMesaWarpWanted()` 의 유일한 호출자가 `:1331` 이고 그 자리는 `osmgaMesaChooseTriangle()` 의 `OSMGAMesaProbeRun()` 뒤다 | ✅ 채택 |
| 이름을 `CAP_WARP` 말고 **`CAP_WARP_PREFERRED`** 로 — 체크박스가 꺼지면 WARP **지원**이 사라진다는 뜻이 되지 않게 | 논리 | ✅ 채택 |
| `CAP_REQUIRED` 에 넣지 마라 — 선호이지 가용 조건이 아니다 | 논리 | ✅ 채택 |
| **레이블이 안 들어간다** | python 으로 추정: 340 px 컨트롤에서 이미지·간격 20 을 빼면 320 인데 내 문구는 **420 px** | ✅ **사실.**  `Use WARP 3D (may differ on sliver triangles)` = 264 px 로 바꾼다 |
| **`pkg/Instance0.release.table` 도 키가 필요하다** — 패키지는 개발용이 아니라 이것을 싣는다 | 파일 확인, 세 키를 이유와 함께 열거하고 있다 | ✅ **채택.  내 파일 목록에 빠져 있었다** |
| `Hook.c` 의 *"no capability advertises WARP"* 주석이 거짓이 된다 | `:776` 확인.  게다가 그 주석의 **이유 전체가 낡았다** — *"커널이 v10 배치를 거부한다"* 고 쓰여 있는데 지금은 받는다(WARP 이 실기에서 그린다), *"두 층이 같은 그림을 그린다는 근거가 아직 없다"* 도 `M16`·`M17` 이 채웠다 | ✅ **채택.  주석을 다시 쓴다** |
| 재부팅이 필요하다 | config table 을 초기화 때 읽어 전역에 담고, 살아 있는 갱신 경로가 없다.  게다가 Mesa 프로세스가 프로브 결과를 캐시한다 | ✅ 채택 |
| 버전 엇갈림은 안전하다.  `{env 없음, 0, 1} x {cap 0, 1}` 여섯 조합을 시험하라 | 논리 | ✅ 채택 |
| 이 체크박스만으로 3D 가 켜지지 않는다 — `Mesa Acceleration` 과 mmap 이 먼저다 | 커널이 게이트하는 것을 위에서 확인했으므로 사실 | ✅ 채택.  패널이 그 의존을 말하게 한다 |

### 그러므로 §1 의 근거를 고쳐 적는다

`"Mesa Acceleration"` 은 **보고 전용이 아니다** — 커널이 실제로 거절한다.
선례로 삼을 것은 그 스위치의 *현재 동작*이 아니라 **경로의 모양**이다:
config 키를 커널이 읽고, `caps[FLAGS]` 비트로 유저랜드에 알리고, 유저랜드가
쓴다.  WARP 비트는 그 경로를 쓰되 **커널 쪽 게이트는 두지 않는다** —
커널은 두 형식을 다 받으므로 거절할 것이 없기 때문이다.

---

## 8. 시행 — 재부팅 전에 확인한 것

빌드: 라이브러리 PASS, 드라이버 `BUILD_EXIT=0`(537,952 바이트), 설치
`INSTALL_OK`(Active Drivers 손대지 않음, 인스턴스 표 보존).

nib 은 정규 경로(`tools/rebuild-inspector-nib.sh`)로 다시 만들었고
5,082 → **5,341 바이트**, 세 곳이 일치한다:

```
warpSwitch   nib 커넥터 1,  data.classes 1,  소스(.h/.m) 2
toggleWarp   nib 커넥터 1,  data.classes 1,  소스(.h/.m) 2
```

**지금 돌고 있는 커널은 아직 옛 드라이버**이므로, 이것이 codex 가 물은
"신 라이브러리 + 구 커널" 조합 그대로다.  여섯 조합 중 **셋이 여기서
검증된다**:

```
env 없음, cap 없음    submissions 193  -> 사다리꼴   (안전한 기본값)
env=1,    cap 없음    submissions 128  -> WARP       (기존 옵트인 유지)
env=0,    cap 없음    submissions 193  -> 사다리꼴   (명시적 강제, 새 동작)
```

빠른 회귀 `PROBLEM` 0 건.

남은 셋(`cap 있음` 쪽 세 줄)은 새 커널이 실려야 잴 수 있다.

## 9. 도중에 고친 것 — nib 을 지키는 주석

`tools/rebuild-inspector-nib.sh` 가 *"data.classes 와 data.dependency 는
stock nib 에서 그대로 온다"* 고 적고 있었다.  **`data.classes` 는 아니다** —
확인: stock 사본에 `OSMGADisplayInspector` 가 **0 번** 나온다.  손으로
관리되는 파일이고, 그 말을 믿고 stock 에서 다시 복사하면 **모든 outlet
연결이 조용히 끊긴다**(연결 실패한 outlet 은 nil 이고 nil 에 보낸 메시지는
아무 말도 하지 않는다).  주석을 사실로 고쳤다.

## 10. 재부팅 1 회차 — 새 커널이 키를 읽는다

```
Aug 29 17:26:13  OpenStepMGA C2: WARP 3D preference is No
```

새 드라이버가 실렸고 키를 읽는다.  `cap = 0` 세 줄을 **새 커널로 다시**
확인했다(앞서 잰 것은 구 커널이었다):

```
cap 0 · env 없음   193 제출 -> 사다리꼴
cap 0 · env=1      128 제출 -> WARP
cap 0 · env=0      193 제출 -> 사다리꼴
```

그 다음 `set-config-key.sh -a "WARP 3D" Yes` 로 설치된 인스턴스 표를 켰다.
표는 온전하고(다른 키·`Location`·`Version` 그대로) 값이 들어갔다.
**config 는 init 에서 한 번만 읽으므로 반영에 재부팅이 한 번 더 든다.**
