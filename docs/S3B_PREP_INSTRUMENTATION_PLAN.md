# S3b-prep — 계측과 유저스페이스 경로 실증 (락 설계 이전 단계)

기준일: 2026-08-19
상태: ✅ **완료(§9)** + reject-only 측정 완료(§11) — **`IODisplayDoBlit`은 OPENSTEP 4.2에서 사문화된 API임이 확정**되어 S3b는 성립하지 않는다.
선행: [S3a](S3_IODISPLAY_DO_BLIT_PLAN.md) PASS 6/6.

## 0. 왜 이 단계가 필요한가 — 가정 위에 설계하지 않기 위해

S3a 계획서 §10에 "커서 메서드가 `stormLock`을 공유하도록 오버라이드"라고 적었다.
이는 **`IOFrameBufferDisplay`의 커서가 프레임버퍼를 CPU로 저장·복원한다**는 가정에
기댄 설계인데, 그 구현은 바이너리로만 존재해 **확인한 바 없다.** 커서가 이벤트
시스템의 다른 경로로 그려질 수도 있다.

이 프로젝트에서 가정 위에 세운 설계가 원본 대조·교차검토로 뒤집힌 전례가 여럿이다
(트랜잭션 게이팅, 클립 범위, EXEC 후 타임아웃 처리). 따라서 **락을 설계하기 전에
사실을 측정한다.**

동시에, S3b가 전적으로 의존하는데 **아직 한 번도 실증하지 않은 것**이 있다:
지금까지 `IODisplayDoBlit` 핸들러는 드라이버 **내부에서만** 호출했고,
**유저스페이스→커널 RPC 경로는 미검증**이다.

## 1. 이 단계에서 확인할 것

| # | 미지수 | 상태 |
| --- | --- | --- |
| 1 | 유저스페이스→드라이버 RPC 동작 | ✅ **직접 실증**(§9-2) |
| 2 | 커서 메서드가 프레임버퍼를 건드리는가 | ✅ **확정: 건드린다**(소프트웨어 커서) — §8 |
| 3 | 커서가 실제로 호출되는가 | ✅ **호출됨** `cursorMove 103`(§9-3). 문맥 자체는 여전히 미확정이나 커서 코드에 spl/차단 없음(§8) |
| 4 | 좌표 원점 | ✅ **top-left 확정**(§9-5), 변환 불필요. WindowServer 실제 좌표·부하는 §10에서 |

### 미지수 #1은 이미 해결돼 있었다

`docs/P1_DRIVERKIT_DISPLAY_QUERY.md`에 실기 기록이 있다:
```
candidate=MatroxMGA0 result=-704
candidate=MatroxMGA  result=-704
candidate=Display0   result=0 object=20 kind=Linear Framebuffer
OPENSTEP_MGA_DISPLAY_MEMORY result=0 count=1
OPENSTEP_MGA_DISPLAY_CURRENT_MODE result=0 count=1 index=115
```
→ lookup 이름은 **`"Display0"`**(드라이버 이름도, 클래스명도, name+unit도 아님).
`getIntValues`가 실제로 값을 반환했다. 도구는
`test/openstep-mga-display-info-probe.m`이 이미 있고 `-lDriver`로 빌드한다.
열거(`lookUpByObjectNumber:`)는 fallback으로만 둔다 — object number는 부팅마다
달라지므로 안정된 식별자가 아니다.

## 2. 하는 일 (codex 교차검토 반영해 개정)

### 2-1. 통계 카운터 — `getIntValues` `"OSMGAStats"`

```
[0] statsVersion(=1)  [1] blitRequests   [2] blitOk       [3] blitNoop
[4] refusedDisabled   [5] refusedGeometry [6] refusedBusy
[7] refusedPreExecTimeout                [8] postExecTimeout
[9] cursorShow       [10] cursorMove    [11] cursorHide
[12] cursorWhileStormBusy                [13] thin1pxRequests
[14] enterLinearCount [15] revertVGACount
[16] stormBlitReady(0/1)  [17] stormBlitFailed(0/1)
```
**`count` 처리는 `AMD_SCSI.m:498` 선례를 따른다**: `*count`가 정확히 배열 크기와
같지 않으면 `IO_R_INVALID_ARG`. 부분 반환·클램프 금지. 성공 시 전부 채우고
`*count`를 쓴 개수로 설정. `statsVersion`을 넣어 이후 확장에 대비한다.

**거절 사유를 분리**한다(codex 지적) — "왜 거절됐는지" 없이는 진단이 불가능하다.
요청 하나당 결과는 하나만 집계한다. `blitRequests`는 **RPC 경계에서** 세어
외부 호출자만 반영하고, 부팅 self-test는 포함하지 않는다.
`stormBlitReady`/`stormBlitFailed`는 누적 카운터가 아니라 **0/1 상태 샘플**이다.

`cursorWhileStormBusy`가 이 표에서 가장 중요하다 — 커서 진입 시 엔진 트랜잭션이
진행 중이었으면 증가시킨다. **0이 아니면 향후 직렬화가 반드시 이 상호작용을
다뤄야 한다는 강한 증거**가 된다.

### 2-2. 커서 메서드 계측 (동작 변경 없음)

세 메서드를 오버라이드해 **정렬된 `volatile unsigned` 카운터만 증가시키고 즉시
`super` 호출**. 할당·`IOLog`·락·대기·시간함수 **전부 금지** — 호출 문맥이
인터럽트일 수 있다고 가정한다(§2-5). 카운트 유실이나 불일치는 허용한다.

### 2-3. `OSMGAProbeBlit` — 표준 API를 테스트에 쓰지 않는다

`displayDefs.h`는 *"`IO_DISPLAY_CAN_BLIT`이 세워지지 않았으면 `IO_DISPLAY_DO_BLIT`을
쓰지 말라"*고 명시한다. 우리 테스트 클라이언트가 그걸 쓰면 **계약 위반**이다.

→ 테스트 전용 파라미터 **`"OSMGAProbeBlit"`**(같은 6인자, 같은 내부 헬퍼, 같은 검증)를
따로 둔다. config 플래그로 게이팅한다. 표준 요청 경로는 건드리지 않는다.

### 2-4. 좌표 원점 — 2단계 확정 (개정)

원안("우리 도구로 복사해 눈으로 확인")은 **우리 도구가 넣은 좌표만** 증명하지,
**WindowServer가 무엇을 보낼지는 증명하지 못한다.** codex 제안대로 둘로 나눈다.

- **(a) 우리 매핑 확인**: `OSMGAProbeBlit`으로 상/하단이 확연히 다른 영역을
  작게 비겹침 복사 → 우리 드라이버가 API의 `y`를 어느 스캔라인에 매핑하는지 확정.
  복사 후 해당 영역을 다시 그리게 한다.
- **(b) WindowServer 실제 호출 관측 — 위험 0**: 별도 측정 부팅에서
  **`IO_DISPLAY_CAN_BLIT`을 세우되, 모든 표준 요청을 하드웨어 접촉 전에 기록만 하고
  `IO_R_RESOURCE`로 거절**한다. 엔진을 전혀 쓰지 않으므로 화면 훼손 위험이 없고,
  WindowServer가 실제로 보내는 좌표·크기·빈도·겹침 분포를 그대로 얻는다.
  덤으로 **소프트웨어 폴백이 실제로 정상 동작하는지**도 확인된다(헤더는 호출자가
  폴백을 준비해야 한다고 요구할 뿐, 그 구현이 그렇게 한다는 보장은 아니다).

### 2-5. 커서가 프레임버퍼를 건드리는가 — 카운터로는 증명 불가

codex 지적이 옳다. 카운터는 **호출 여부**만 알려줄 뿐, 그 구현이 프레임버퍼를
읽고 쓰는지는 말해주지 않는다. `IOFrameBufferDisplay.h`에 "mapping tables used in
cursor drawing"이라는 private 멤버가 있어 시사적이지만 증명은 아니다.

→ **타깃의 `IOFrameBufferDisplay` 구현을 IDA로 정적분석**해 세 셀렉터가
`IODisplayInfo.frameBuffer`를 역참조하는지, 픽셀 저장/복원을 하는지 추적한다.
이것이 비침습적이면서 결정적인 답이다. (§8)

### 2-6. 게이팅

`IO_DISPLAY_CAN_BLIT`는 **세우지 않는다**(단계 (b) 측정 부팅 제외 — 그때도 엔진은
안 쓴다). 블릿은 `OSMGAProbeBlit`으로 우리가 명시적으로 요청할 때만 일어난다.

## 3. 안전 분석

| 위험 | 완화 |
| --- | --- |
| 커서 오버라이드 | 카운트 후 즉시 `super`. 할당·로깅·락·대기 없음. 인터럽트 문맥 가정 |
| 표준 API 계약 위반 | `OSMGAProbeBlit`을 따로 둬 회피 |
| 유저스페이스 블릿이 화면 훼손 | 좌표를 우리가 고르고 1회씩. WindowServer의 damage 장부에는 반영되지 않으므로 리페인트 전까지 남을 수 있음 — 해당 영역을 다시 그리게 함 |
| 임의 프로세스가 블릿 가능 | config 플래그로만 열림. **API에 소유권 토큰이 없어 인증은 불가**(codex) — 운영상 통제일 뿐임을 인정 |
| 측정 부팅(b)의 위험 | **엔진 미접촉**(기록 후 즉시 거절)이므로 실질 위험 0 |
| 커서 락 데드락 | **이번 단계에서 락을 도입하지 않는다.** codex 경고: 문맥을 알기 전에 엔진 대기 중 스핀락을 잡으면 인터럽트 커서 호출이 선점해 영원히 스핀 |

## 4. 검증 방법

### 4-1. 타깃 빌드
클린 빌드, 경고 0, `nm -u` 미해결 심볼 0.

### 4-2. IDA 정적분석 (하드웨어 불요, §8)
`IOFrameBufferDisplay`의 커서 3종 구현이 프레임버퍼를 역참조하는지 판정.

### 4-3. 실기 측정 부팅 #1 (`OSMGAProbeBlit`)
1. `Display0` lookup 성공 (기실증이므로 회귀 확인)
2. `OSMGAStats` 조회 성공 → 카운터 값 획득
3. **마우스 이동 후** 재조회 → `cursorMove` 증가 여부 (호출 여부 확정)
4. `OSMGAProbeBlit` 요청 → `blitOk` 증가, 화면에서 사각형 확인 → 우리 y 매핑 확정
5. `cursorWhileStormBusy` 값 확인

### 4-4. 실기 측정 부팅 #2 (reject-only, 별도)
`CAN_BLIT` 세우고 표준 요청을 전부 기록 후 거절. WindowServer 실제 호출 관측.
**엔진 미사용**이므로 화면 훼손 없음. 소프트웨어 폴백 동작 확인.

### 4-5. 판정
미지수 #2·#3·#4에 사실 기반 답을 얻으면 성공. "커서가 프레임버퍼를 안 건드린다"도
유효한 성공이며, 그 경우 S3a §10의 커서 락 요건이 사라진다.

### 4-6. 복구
config 플래그 off 또는 재부팅.

## 5. 이 단계가 증명하지 않는 것

- 고빈도 실사용에서의 경합 동작 (측정 부팅은 단발/거절만)
- 엔진을 실제로 WindowServer가 구동했을 때의 정확성·성능

## 6. 이번 단계에서 하지 않는 것 (명시)

- **락 도입 금지** — 커서 호출 문맥이 확정될 때까지.
- `IO_DISPLAY_CAN_BLIT`을 세운 채 **엔진을 실행하는** 부팅 금지.
- 커서 메서드의 동작 변경 금지(카운트+`super`만).

## 7. codex 교차검토 결과 (2026-08-19)

### 7-1. 채택 — 미지수 #1은 우리가 이미 풀어놨다
codex가 우리 자신의 `docs/P1_DRIVERKIT_DISPLAY_QUERY.md`를 인용했고 **확인 결과
정확**했다. `Display0` → object 20, kind `Linear Framebuffer`. 원본 대조 완료.
lookup 이름 추측이 불필요해졌다.

### 7-2. 채택 — `count` in/out 처리
`AMD_SCSI.m:498` 선례 확인: `*count != 예상`이면 `IO_R_INVALID_ARG`, 부분 반환 없음.

### 7-3. 채택 — 표준 API를 테스트에 쓰지 말 것 → `OSMGAProbeBlit`

### 7-4. 채택 — 좌표 원점은 reject-only 측정 부팅으로 (위험 0, 실제 호출자 관측)

### 7-5. 채택 — 카운터로는 커서의 프레임버퍼 접근을 증명 못 함 → IDA 정적분석

### 7-6. 채택 — 커서 문맥 확정 전 스핀락 금지
`eventProtocols.h:164`가 "Event Driver가 WindowServer와 포인터 관리 소프트웨어의
명령으로 호출한다"고만 하고 **스레드 문맥·spl을 보장하지 않는다.** 인터럽트 문맥일
수 있다고 가정해야 하며, 그렇다면 엔진 대기 중 스핀락 보유는 **영구 스핀 위험**이다.
→ S3a §10의 "커서가 `stormLock` 공유" 설계는 **위험할 수 있음**을 기록한다.
현재 구현(대기하지 않고 거절)은 이 위험이 없다.

### 7-7. 채택 — 추가 계측 항목
`cursorWhileStormBusy`, 거절 사유 분리, 1픽셀 폭/높이 카운터,
`enterLinearMode`/`revertToVGAMode` 카운터, `statsVersion`.

### 7-8. 인정 — 권한/소유권
`IODeviceMaster` API에는 **WindowServer 소유권 토큰이 없다.** 다른 프로세스가 마스터
포트를 얻으면 API 수준에서 막을 방법이 없다. config 플래그는 **운영상 통제일 뿐
인증이 아니다.** 이 사실을 인정하고 기록한다.

## 8. IDA 정적분석 결과 — 미지수 #2 확정 (2026-08-19)

**대상**: 타깃의 `/mach_kernel`(1,117,920 B, i386 Mach-O, imagebase `0x100000`).
`sum` 체크섬 `44051 1092`로 타깃↔로컬 일치 확인. 분석 사본은
`reference/original-binaries/`에 두며 `.gitignore`로 커밋에서 제외한다.

`IOFrameBufferDisplay`는 커널에 구현돼 있다(문자열 `Kernel/IOFrameBufferDisplay.m`).
세 커서 메서드를 모두 찾았다:
`-[IOFrameBufferDisplay hideCursor:]` `0x1c2e1c`,
`moveCursor:frame:token:` `0x1c30bc`, `showCursor:frame:token:` `0x1c3800`.

### 8-1. ✅ 커서는 **소프트웨어 커서**다 — CPU로 프레임버퍼를 읽고 쓴다

`hideCursor:` 디컴파일 요지:
```c
if ( !ev_try_lock((char *)self->priv + 4) )   /* try-lock, 실패시 즉시 포기 */
    return self;
...
v6 = *((_DWORD *)displayInfo + 6);            /* 오프셋 24 = bitsPerPixel */
if ( v6 == 4 ) {                              /* IO_24BitsPerPixel = 32bpp */
    v24 = (char *)v22->var5                   /* var5(오프셋 20) = frameBuffer */
        + 4 * var2 * (dstRow - savedRow)      /* var2(오프셋 8) = totalWidth  */
        + 4 * (dstCol - savedCol);
    ...
    *v24++ = *v26++;                          /* priv 백업버퍼 → 프레임버퍼 */
}
```
- bpp 분기가 `IOBitsPerPixel` enum과 정확히 일치한다(1=8bpp, 4=32bpp/24used,
  그 외=16bpp 2바이트 경로) → 구조체 해석이 옳음을 교차 확인.
- **`frameBuffer` 포인터를 목적지로 하는 CPU 복사 루프**가 존재한다.
- `showCursor:`도 동일 구조이며, 32bpp에서 `sub_1C2B20`(커서 그리기)을 호출한다.

→ **가정이 아니라 사실로 확정**: 커서 그리기/지우기는 프레임버퍼 픽셀을 CPU로
저장·복원한다. 따라서 우리 블릿과 **영역이 겹치면 실제로 훼손이 가능**하다.
구체적 시나리오: `show`가 프레임버퍼를 백업한 뒤 우리 블릿이 그 영역을 덮으면,
다음 `hide`가 **낡은 백업을 복원**해 우리 결과를 지운다.

### 8-2. 커서 락은 비차단 try-lock이며, 경합 시 **그냥 건너뛴다**

```c
_BOOL4 ev_try_lock(volatile signed __int32 *a1)
{ return !_interlockedbittestandset(a1, 0); }
```
- `hide`는 `(char*)priv + 4`, `show`는 `(int32*)priv + 1` — **같은 워드**.
- 원자적 bit test-and-set 한 번. **spl 조작도 인터럽트 차단도 없다.**
- 실패하면 `return self` — 커서 연산을 조용히 생략한다.

→ 이 서브시스템 자신이 **"락을 못 얻으면 건너뛴다"를 정상 동작으로 설계**했다.
경합 시 대기하지 않고 `IO_R_RESOURCE`로 거절하는 우리 설계와 철학이 일치한다.

### 8-3. 그 락을 우리가 공유할 수는 없다

`priv`는 `IOFrameBufferDisplay`에서 **`@private`**(`IOFrameBufferDisplay.h:25`)이라
서브클래스에서 정당하게 접근할 수 없다. 알려진 오프셋으로 뚫는 것은 버전 의존적이고
취약하므로 **하지 않는다.**

### 8-4. 그래서 S3b의 커서 요건은 어떻게 되는가 (재평가)

S3a 계획서 §10의 "커서가 `stormLock`을 공유하도록 오버라이드"는 **불가능하고,
codex 경고대로 위험할 수도 있었다**(문맥 미확정 상태에서 엔진 대기 중 스핀락 보유).

대신 사실에 근거한 재평가:

- **WindowServer가 호출자일 때(S3b 본류)**: WindowServer는 자신의 소프트웨어
  드로잉에 대해서도 이미 커서를 가려야 한다(고전적 obscure-cursor 규약). 자기가
  발주한 하드웨어 블릿에도 같은 규율이 적용될 것으로 **기대**되나 **증명되지 않았다.**
  → 측정 부팅에서 `cursorWhileStormBusy`로 **실제 충돌 발생 여부를 관측**한다.
- **우리 프로브 클라이언트가 호출자일 때**: 커서 조정이 전혀 없다. 위험은 커서
  사각형(16×16) 범위의 **미관 훼손**에 국한된다.

→ 락 설계는 측정 결과를 본 뒤 확정한다. 이번 단계에서는 **락을 도입하지 않는다**(§6).

## 9. ✅ 실기 결과 — 미지수 4개 전부 해결 (2026-08-19)

1024×768×32, `"Storm 2D Test" = "Yes"`, 콜드 부팅 1회.
`IO_DISPLAY_CAN_BLIT`은 세우지 않은 상태(WindowServer는 소프트웨어 경로 유지).

### 9-1. 부팅 시 S3a self-test 회귀 없음
`S3: self-test end 6/6 passed` — 계측 추가 후에도 동일.

### 9-2. 유저스페이스 프로브 (미지수 #1 ✅)

```
lookup Display0 -> object=22 kind=Linear Framebuffer
OSMGA_PROBE_STATS result=0 count=18
```
유저스페이스 프로세스가 `IODeviceMaster`로 드라이버를 찾아 커스텀 파라미터를
읽었다. **S3b가 의존하는 경로가 실증됐다.**

`Display0`이 lookup 이름임을 재확인(object number는 22 — 이전 기록의 20과 다르며,
**부팅마다 달라진다**는 codex 지적대로다. 이름이 안정된 식별자다).

### 9-3. 커서 (미지수 #2·#3 ✅)

```
cursorShow    1
cursorMove  103      ← operator가 마우스를 움직인 결과
cursorHide    0
cursorWhileStormBusy 0
```
**커서 메서드는 실제로, 자주 호출된다.** §8의 IDA 결과(소프트웨어 커서가 CPU로
프레임버퍼를 읽고 씀)와 합치면 → **S3b에서 커서·블릿 충돌은 실재하는 위험**이다.

`cursorHide 0`이 흥미롭다: `move`만 103회이고 `hide`는 0. `moveCursor:`가 내부에서
지우기+그리기를 모두 수행하는 구조로 보인다(§8의 `showCursor:` 디컴파일에서
복원 루프와 `sub_1C2B20` 그리기 호출이 한 메서드 안에 있던 것과 정합).

`cursorWhileStormBusy 0`은 **단발 블릿에 한정된 결과**다. 고빈도 호출에서는 다를
수 있으므로 이것으로 안전을 주장하지 않는다.

### 9-4. 유저스페이스 블릿 (미지수 #3-b ✅)

```
/tmp/osmga-stats blit 0 0 128 128 700 300
OSMGA_PROBE_BLIT result=0 SUCCESS
blitRequests 1   blitOk 1
```
RPC 경계 카운팅이 정확하다 — 부팅 self-test 6회는 집계되지 않았고, 외부 호출
1회만 잡혔다(설계 의도대로).

### 9-5. 좌표 원점 = **top-left** (미지수 #4 ✅)

`src=(0,0)` 128×128을 `(700,300)`으로 복사한 결과, 화면에 나타난 것은
**좌상단의 메뉴**였다(operator 육안 확인).

→ API의 `y=0`은 **첫 스캔라인**이며, 우리 엔진 주소 계산(`y*stride`)과 일치한다.
**좌표 변환 불필요.** 단, 이는 우리 클라이언트가 넣은 리터럴 좌표에 대한 증명이며,
WindowServer가 어떤 좌표를 보낼지는 §10에서 별도로 관측한다.

### 9-6. 기타 확인
`enterLinear 1`, `revertVGA 1`(부팅 시 VGA 복귀 후 리니어 진입),
`stormBlitReady 1`, `stormBlitFailed 0`, `postExecTimeout 0`.

## 10. 다음 단계 — reject-only 측정 부팅

미지수는 풀렸으나 **S3b의 핵심 위험은 남아 있다**: 커서는 소프트웨어 커서이고(§8),
자주 호출되며(§9-3), 우리는 그 락에 접근할 수 없다(`@private`). WindowServer가
자기 블릿 발주 전에 커서를 가리는지는 **여전히 미증명**이다.

→ `IO_DISPLAY_CAN_BLIT`을 세우되 **모든 표준 요청을 하드웨어 접촉 전에 기록만 하고
`IO_R_RESOURCE`로 거절**한다. 엔진을 전혀 쓰지 않으므로 화면 훼손 위험이 없으면서:
- WindowServer의 실제 좌표·크기·빈도·겹침 분포를 관측
- 커서 호출과의 시간적 관계를 관측
- **소프트웨어 폴백이 실제로 동작하는지** 확인(헤더는 호출자가 폴백을 준비해야
  한다고 요구할 뿐, 그 구현이 그렇게 한다는 보장은 아니다)

이 관측 결과로 락 설계를 확정한 뒤에야 엔진을 실제로 구동한다.

## 11. ✅ reject-only 측정 결과 — `IODisplayDoBlit`은 사문화된 API (2026-08-19)

`"Storm Blit Observe" = "Yes"`로 `IO_DISPLAY_CAN_BLIT`을 광고하고, 모든 표준 요청을
하드웨어 접촉 전에 기록·거절하는 모드로 부팅. operator가 창을 옮기고 스크롤하는 등
정상 사용.

```
observeOnly            1      ← 모드 활성, CAN_BLIT 광고됨
cursorMove          1784      ← 실제로 활발히 사용됨
blitRequests           0      ← WindowServer가 한 번도 요청하지 않음
obsRequests            0
```
화면 이상 없음(operator 확인) — 당연하다. 하드웨어 경로를 시도조차 하지 않았다.

### 11-1. 원인 규명 — 플래그 문제가 아니라 호출자가 없다

WindowServer와 같은 경로로 되읽기를 시도했으나
`IOGetDisplayInfo` → **`IO_R_UNSUPPORTED(-711)`**. 즉 그 파라미터 자체가 지원되지
않는다. 그래서 **바이너리에서 직접 확인**했다:

```
strings /usr/lib/NextStep/WindowServer | grep DoBlit   → (없음)
strings /mach_kernel                   | grep DoBlit   → (없음)
```

검색 방법이 유효함을 대조로 확인 — WindowServer가 실제로 쓰는 문자열은 존재한다:
```
IOSetTransferTable
IO_Framebuffer_Dimensions / IO_Framebuffer_Map /
IO_Framebuffer_Pixel_Encoding / IO_Framebuffer_Register
IO_BM256_to_BM38_map / IO_BM38_to_BM256_map / IO_4BPS_to_5BPS_map ...
```

→ **WindowServer는 프레임버퍼를 직접 매핑해 CPU로 그린다**(`IO_Framebuffer_Map`).
헤더의 블릿 API(주석 날짜 12/94)는 정의만 남았고, 이 WindowServer(1997-04 빌드)는
**사용하지 않는다.** `IODisplayDoBlit`은 OPENSTEP 4.2에서 **사문화**됐다.

### 11-2. 판정

**S3b는 성립하지 않는다.** `IO_DISPLAY_CAN_BLIT`을 세워도 호출자가 없다.

**reject-only 방식을 택한 것이 결정적으로 옳았다.** 엔진을 켜는 S3b로 곧장 갔다면
"화면은 멀쩡한데 왜 빨라지지 않는가"를 한참 헤맸을 것이다. 위험 0으로 사실을 얻었다.

### 11-3. 잃은 것은 없다 — 자산은 그대로 유효

- `IODisplayDoBlit` 핸들러 + 겹침 처리 + 입력 검증 + 직렬화: 실기 6/6 동작(S3a §9)
- **유저스페이스→커널 RPC 경로 실증**(`Display0` → 카운터 조회·블릿 요청 성공)
- 좌표 원점 top-left 확정
- 커서가 소프트웨어 커서라는 IDA 확정

즉 **"유저스페이스가 커널을 통해 Storm 엔진을 구동하는 길"은 이미 완성돼 있다.**
호출자가 WindowServer가 아닐 뿐이다.

애초 목표에 비추면 문제가 아니다 — 우리가 원한 호출자는 **Mesa**였다.
`OSMGAProbeBlit`(정식 이름을 붙여) Mesa가 직접 호출하면 된다. WindowServer 가속은
부수적 기대였을 뿐이다.

### 11-4. 다음

S4(Mesa 연결)로 간다. 단 Mesa가 오프스크린 VRAM에 **직접 렌더**하려면 여전히
**유저 태스크 VRAM 매핑**(미검증)이 필요하다. 그것을 먼저 조사한다.
`devsw`의 `mmap` 엔트리(`IOAddToCdevsw`)가 헤더에 있으나 우리 미러에 예제가 없고,
`IO_Framebuffer_Map`이라는 WindowServer가 쓰는 파라미터가 **유력한 단서**다 —
WindowServer가 프레임버퍼를 유저스페이스로 매핑받는 바로 그 메커니즘이다.

## 12. 검증 방법의 결함 발견과 재확인 (2026-08-19)

§11의 결론은 **깨진 명령으로 도출됐다.** 타깃의 `grep`은 `"A\|B"` 교체 패턴에서
**조용히 아무것도 반환하지 않는다**(에러도 없음). §11에서 쓴
`grep -i "IODisplayDoBlit\|DoBlit"`이 정확히 그 패턴이었다.

발견 경위: IDA는 `mach_kernel`에 `IO_Framebuffer_Pixel_Encoding` 문자열이 있다고
보여주는데(`0x1d8fdc`), 같은 방식의 타깃 grep은 없다고 답했다. 대조 실험:

```
strings /mach_kernel | grep IO_Framebuffer_Pixel_Encoding   → 검출됨
strings /mach_kernel | grep "IO_Framebuffer\|IO_BM256"      → (없음)   ← 거짓
strings /mach_kernel | grep IO_Framebuffer                  → 6건 검출
```

**plain grep으로 재검증한 결과 §11의 결론은 유지된다**:
```
strings /mach_kernel                   | grep Blit  → (없음)
strings /usr/lib/NextStep/WindowServer | grep Blit  → (없음)
```
대조군(`IO_Framebuffer_Pixel_Encoding`)이 검출되므로 방법이 유효하다.

> 교훈: 이 저장소 메모리에 이미 "타깃 grep은 `-E`/`-iE`를 못 쓴다"가 기록돼 있었는데
> `\|` 교체도 같은 함정이라는 점을 놓쳤다. **타깃에서는 항상 단일 패턴 grep을 쓴다.**

## 13. 디스플레이 파라미터 실제 계약 (IDA 확정)

`-[IOFrameBufferDisplay getIntValues:forParameter:count:]`(`0x1c3cdc`) 정독 결과.
`IODisplayInfo` 필드 번호는 IDA 구조체(`size=136`, `displayModes` stride 136과 일치)로
확정: `var0`=width, `var1`=height, `var2`=totalWidth, `var3`=rowBytes,
`var4`=refreshRate, `var5`=frameBuffer, `var6`=bitsPerPixel, `var7`=colorSpace,
`var8`=pixelEncoding[64], **`var9`@0x60=`flags`**.

| 파라미터 | 종류 | 동작 |
| --- | --- | --- |
| `IO_Framebuffer_Map` | getInt | `revertToVGAMode` → `enterLinearMode` → `[kmId registerDisplay:self]`. **메모리를 호출자에게 매핑하지 않는다.** |
| `IO_Framebuffer_Dimensions` | getInt | `[width, height, rowBytes, **flags**, bpp숫자]` |
| `IO_Framebuffer_Register` | getInt | `_registerWithED` → 이벤트드라이버 등록, token 반환 |
| `IO_Framebuffer_Pixel_Encoding` | **getChar** | `pixelEncoding` 64바이트 |
| `IOGetDisplayInfo` | **getInt**(count 5 또는 7) | `[width, height, refreshRate, bitsPerPixel, colorSpace, (totalWidth, rowBytes)]` — **flags 없음** |
| `IOGetDisplayMemory` / `IOGetRAMDACSpeed` / `IOGetDisplayModeNum` / `IOGetDisplayModeInfo:` / `IOGetCurrentDisplayMode` / `IOGetPendingDisplayMode` | getInt | 각 게터 |

### 13-1. 우리 프로브의 오류 정정
`IOGetDisplayInfo`를 **getCharValues**로 호출해 `IO_R_UNSUPPORTED(-711)`를 받았는데,
실제로는 **getIntValues**이고 count는 5 또는 7이어야 한다. 프로브가 틀렸던 것이다.

### 13-2. `flags`는 실제로 WindowServer에 노출된다
`IO_Framebuffer_Dimensions`의 [3]번 원소가 `flags`다. 즉 우리가 세운
`IO_DISPLAY_CAN_BLIT`을 WindowServer가 **볼 수는 있다.** 그럼에도 블릿 요청이 0인
이유는 §11대로 **그 요청을 보내는 코드 자체가 없기** 때문이다. 결론은 더 정확한
근거 위에서 유지된다.

### 13-3. 우리 카운터가 설명된다
`enterLinear 1`, `revertVGA 1`은 WindowServer가 기동 시 `IO_Framebuffer_Map`을
호출한 결과다(그 안에서 revert→enter를 연달아 수행). 우연이 아니라 이 계약의 결과다.

### 13-4. S4의 핵심 질문은 아직 열려 있다
`IO_Framebuffer_Map`은 **매핑을 하지 않는다** — 모드 전환과 `kmId` 등록만 한다.
그렇다면 WindowServer는 프레임버퍼를 **어떻게** 자기 주소공간에 넣는가?
후보: `IODisplay`의 `- (port_t)devicePort`로 얻은 디바이스 포트 + Mach VM,
또는 `[kmId registerDisplay:]` 경로. **이것이 S4의 다음 조사 대상이다.**

## 14. S4 관문 조사 — 유저 태스크 VRAM 매핑 (2026-08-19)

### 14-1. WindowServer는 어떻게 프레임버퍼를 얻는가

`nm -u /usr/lib/NextStep/WindowServer`에 `IO*` 심볼이 **하나도 없다.** 이유는
`libDriver.a`가 **정적** 아카이브라 `IODeviceMaster` 코드가 통째로 링크되기 때문이다
(우리 프로브도 `-lDriver`로 같은 방식). 그래서 `IO_Framebuffer_Map` 등의 문자열은
WindowServer 바이너리 안에 있다.

임포트 중 유의미한 것: **`_mmap`**, `_map_fd`, `__NXEvMapEventShmem`.
→ WindowServer는 **`mmap`으로 프레임버퍼를 얻는다.**

`IO_Framebuffer_Map` 자체는 §13대로 매핑을 하지 않고 모드 전환+등록만 한다.
즉 "모드를 켜라"는 신호이고, 실제 메모리 획득은 `mmap`이 담당한다.

### 14-2. 드라이버가 유저 태스크에 매핑할 수 있는가 — **가능**

커널 심볼 확인(둘 다 전역 `T`):
```
001a9d30 T _IOAddToCdevsw               ← 공개 헤더 driverkit/devsw.h에 선언
001a9c6c T _IOAddToCdevswAt
001a9dec T _IORemoveFromCdevsw
0017f74c T __KernBusMemoryCreateMapping ← task 인자를 받음(내부 API)
```

`IOAddToCdevsw(open, close, read, write, ioctl, stop, reset, select,
**mmap**, getc, putc)` — **공개·문서화된 경로**다. 드라이버가 문자 디바이스를
등록하고 mmap 엔트리를 제공하면, 유저 프로세스가 `open()`+`mmap()`으로 VRAM을
자기 주소공간에 넣을 수 있다.

부수 확인: `-[IOFrameBufferDisplay mapFrameBufferAtPhysicalAddress:length:]`는
`_KernBusMemoryCreateMapping(phys, len, &virt, current_task_EXTERNAL(), 1, cache)`를
호출하며, `cache` 인자는 `displayInfo->flags & 0xC`(=`IO_DISPLAY_CACHE_MASK`)에서
유도된다(writethrough→1, copyback→2, off→0). 우리가 쓰는 API의 내부가 확인됐다.

### 14-3. S4에 이것이 필요한 이유 (설계 판단)

**하드웨어 엔진은 VRAM만 조작할 수 있다.** 따라서:
- Mesa가 일반 RAM에 렌더하면(현재 OSMesa 방식) 엔진이 그 버퍼를 건드릴 수 없다.
  `Clear`를 하드웨어로 돌릴 수도, 화면으로 blit할 수도 없다(엔진은 RAM을 못 읽는다).
- 의미 있는 가속은 **Mesa의 렌더 타깃이 VRAM에 있을 때만** 성립한다.

→ 유저 태스크 VRAM 매핑은 S4의 **선택이 아니라 전제**다. 그 관문이 열려 있음이
확인됐다.

### 14-4. S4 착수 시 확정해야 할 것 (다음 계획서에서)

- `cdevsw` mmap 엔트리의 **정확한 시그니처와 반환 규약**(고전 BSD는
  `int mmap(dev_t, off_t, int prot)`로 페이지 프레임 번호 반환이나, 이 커널의
  디스패치를 직접 확인해야 한다)
- 오프셋 검증: 클라이언트가 요구한 오프셋이 **우리가 할당한 오프스크린 범위 안**인지
  강제. 가시 스캔아웃이나 VRAM 밖으로 나가면 거부
- 디바이스 노드 생성·권한
- 오프스크린 할당자(어느 클라이언트가 어느 범위를 갖는가) — `protocol/`의
  `OpenStepMGAOffscreenAllocator`가 이 목적으로 이미 설계돼 있다(현재 미링크)
