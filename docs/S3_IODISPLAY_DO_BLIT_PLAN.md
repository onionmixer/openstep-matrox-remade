# S3 — `IODisplayDoBlit` 구현 (WindowServer가 Storm 엔진을 쓰게 하기)

기준일: 2026-08-19
상태: ✅ **S3a 완료 — 실기 PASS 6/6(§9).** S3b(`IO_DISPLAY_CAN_BLIT` 광고)는 §10의 선행작업 후.
선행: [S1](S1_STORM_ENGINE_LIVENESS_PLAN.md) PASS(엔진 생존),
[S2](S2_STORM_BITBLT_PLAN.md) PASS(BITBLT·오프스크린→가시).

## 0. 방향 전환 — 발명하지 않고 표준 API를 구현한다

원래 로드맵의 3단계는 "`OpenStepMGAService`(MiG)에 하드웨어를 붙여 유저스페이스에
VRAM과 엔진을 노출"이었다. 조사 결과 **더 확실한 길이 이미 존재**한다.

`driverkit/displayDefs.h`가 디스플레이 드라이버용 **블릿 표준 API**를 정의한다:

```c
#define IO_DISPLAY_CAN_BLIT     0x00000020   /* IODisplayInfo.flags */
#define IO_DISPLAY_DO_BLIT      "IODisplayDoBlit"
#define IO_DISPLAY_BLIT_SIZE    6
/* setIntValues 파라미터: [0]=src_x [1]=src_y [2]=width [3]=height
                          [4]=dst_x [5]=dst_y */
```
헤더 주석 원문 요지: *"Drivers can return IO_R_RESOURCE if the blit is not
available or fails. Users should be prepared to do the functional equivalent of
the blit in software."*

즉 **실패를 반환하면 호출자가 소프트웨어로 처리하는 것이 계약에 명시**돼 있다.

### 이 경로가 "가장 확실한" 이유

| | MiG 서비스 확장(원안) | `IODisplayDoBlit`(본안) |
| --- | --- | --- |
| 메커니즘 | 새로 설계 | **문서화된 표준** |
| 유저스페이스의 MMIO 접근 | 필요 | **불필요 — 엔진은 커널에만** |
| 새 커널 로더블 | 필요 | 불필요 |
| 엔진 소유자 | 드라이버와 서비스가 **경쟁**(FIFO wedge 위험) | 드라이버 단독 유지 |
| 기존 P2 정적 가드 | **폐기 필요**(`check-p2-no-hardware.sh`가 빌드 거부) | 건드리지 않음 |
| 유저 태스크 VRAM 매핑 | 필요(규약 미검증, 예제 없음) | 불필요 |
| 첫 소비자 | 우리가 만들 테스트 클라이언트 | **WindowServer 자신** |
| 실패 시 대비책 | 없음 | **계약상 소프트웨어 폴백** |

유저스페이스→드라이버 RPC는 `IODeviceMaster`의
`setIntValues:forParameter:objectNumber:count:`이며, WindowServer가 이미 밝기·감마
설정에 쓰고 있는 검증된 경로다. 우리는 수신 측만 구현한다.

> `OpenStepMGAService`(MiG lease)는 이번 단계에서 **쓰지 않는다.** 다중 클라이언트
> 직렬화가 실제로 필요해질 때 다시 꺼내되, 그때도 엔진은 드라이버가 소유한 채
> 서비스는 중재만 맡는 게 옳다.

## 1. 위험 분리 — S3a / S3b

`IO_DISPLAY_CAN_BLIT`를 세우는 순간 **WindowServer가 상시 호출**한다. 오류가 있으면
64×64 테스트 사각형이 아니라 **화면 전체**가 망가진다. S2와 같은 방식으로 쪼갠다.

| 단계 | 내용 | 호출자 | 실패 시 |
| --- | --- | --- | --- |
| **S3a** | `setIntValues:` 핸들러 + 검증 + 겹침 방향 처리 구현. **`IO_DISPLAY_CAN_BLIT`는 세우지 않음.** 드라이버 자체 self-test가 통제된 입력(겹침 포함)으로 호출 | 우리 self-test | 화면에 국소 오류(테스트 사각형 범위) |
| **S3b** | `IO_DISPLAY_CAN_BLIT`를 세워 WindowServer에 넘김 | **WindowServer** | 화면 전체 훼손 가능 |

S3b는 S3a PASS 후 **별도 부팅**에서만 진행한다.

## 2. S2 대비 새로 필요한 것

### 2-1. 겹침 처리 (핵심 신규 작업)

WindowServer의 스크롤·창 이동은 **겹치는 영역을 복사**한다. S2는 비겹침 top-down만
검증했다. memmove 규칙을 하드웨어 방향 비트로 구현해야 한다.

```
ydir = (srcY < dstY) ? -1 : +1
xdir = (srcX < dstX) ? -1 : +1

SGN  = (ydir<0 ? BLIT_UP(4) : 0) | (xdir<0 ? BLIT_LEFT(1) : 0)
AR5  = ydir * stridePixels

w = w - 1
if (ydir < 0) { srcY += h-1 ; dstY += h-1 }        ← 아래 행부터
start = end = srcY*stride + srcX
if (xdir < 0) start += w   else   end += w         ← 오른쪽부터면 start 이동
AR3 = start ; AR0 = end
FXBNDRY = ((dstX + w) << 16) | dstX                (BITBLT는 inclusive)
YDSTLEN + EXEC = (dstY << 16) | h                  (h는 감소시키지 않음)
```
방향 선택은 비겹침일 때도 **항상 정확**하므로 조건 없이 위 규칙을 적용한다.

### 2-2. 요청 검증 (신규)

호출자는 WindowServer이며 우리는 그 입력을 신뢰하지 않는다. 하나라도 어긋나면
**엔진을 건드리지 않고 `IO_R_RESOURCE` 반환** → 계약대로 호출자가 소프트웨어 처리.

- `count == IO_DISPLAY_BLIT_SIZE(6)`
- `width >= 1`, `height >= 1`
- `srcX, srcX+width <= displayInfo->width`; `srcY, srcY+height <= height`
- `dstX, dstX+width <= width`; `dstY, dstY+height <= height`
- 32bpp이고 `linearModeActive`이며 `mmioMapped`
- 산술 오버플로 없음(모두 부호 없는 비교로)
- 파라미터는 `unsigned`로 전달되므로 **최상위 비트가 선 값(음수로 의도된 값)을 거부**한다
- `src == dst`(같은 좌표·같은 크기)는 **no-op으로 성공 반환**(엔진 미접촉)

### 2-3. 동시성 — 동기식 RPC만으로는 불충분 (codex 지적으로 정정)

당초 이 절은 "우리 블릿은 동기식이고 호출자는 RPC에 블록돼 있으니 조건 충족"이라고
적었다. **틀렸다.** 동기식 RPC는 *호출 스레드만* 직렬화한다. 아래 2-4~2-6이 실제로
필요한 조치다.

### 2-4. 엔진 트랜잭션 직렬화 (codex 지적, 신규 필수)

`setIntValues:`는 **동시 호출될 수 있다.** Storm 레지스터는 전역 상태이므로 두
스레드가 끼어들면 서로의 상태를 덮어써 결과가 깨지거나 엔진이 wedge된다.
S1/S2는 `enterLinearMode` 말미 1회 실행이라 이 문제가 없었다.

→ **idle 확인부터 상태 설정·EXEC·완료 대기까지 하나의 락으로 감싼다.**

독립 확인: 저장소 안의 다른 Matrox 구현
`openstep-sdl12/.../SDL_fbmatrox.c`도 목적지가 화면일 때 블릿 전체를
`SDL_mutexP(hw_lock)`/`SDL_mutexV(hw_lock)`로 감싼다.

### 2-5. EXEC 이후 타임아웃은 "안전한 실패"가 아니다 (codex 지적, 신규)

지금까지의 규칙은 "타임아웃이면 `IO_R_RESOURCE` 반환 → 호출자가 소프트웨어 처리"
였다. **EXEC 이전 타임아웃에는 맞지만, EXEC 이후에는 틀리다.**

EXEC를 낸 뒤 idle 대기가 타임아웃하면 **엔진이 아직 쓰고 있을 수 있다.** 이때
`IO_R_RESOURCE`를 반환하면 호출자가 같은 영역을 소프트웨어로 복사하기 시작하고,
뒤늦은 엔진 write가 그 위에 떨어져 **지속적 훼손**이 된다. bounded wait은 CPU 무한
스핀만 막을 뿐 이 경합은 막지 못한다.

→ EXEC 이후 타임아웃 시:
1. `stormBlitFailed = YES`로 **가속을 영구 비활성화**(이후 모든 요청 즉시 거절)
2. 검증된 리셋/복구 수단이 없으므로 엔진 복구를 시도하지 않는다
3. 로그로 명확히 남긴다

### 2-6. 커서·teardown 동시성 (codex 지적, 신규)

동기식 RPC는 **호출 스레드만** 직렬화한다. 다음은 막지 못한다:
- `IOFrameBufferDisplay`에서 상속한 **프레임버퍼 기반 커서**(`hideCursor:`/
  `moveCursor:`/`showCursor:`)의 저장·복원
- `revertToVGAMode`/teardown이 진행 중인 블릿과 경합
- 프레임버퍼를 매핑한 다른 프로세스의 쓰기

→ 커서 메서드와 모드 teardown이 **같은 락을 공유**하도록 한다. 프레임버퍼 매핑은
write-through를 유지한다(우리 `IODisplayInfo.flags = 0` = `IO_DISPLAY_CACHE_WRITETHROUGH`
확인함; copyback이면 CPU가 쓴 소스 픽셀을 엔진이 못 볼 수 있다).

## 3. 구현 개요

```
- (IOReturn)setIntValues:(unsigned *)p forParameter:(IOParameterName)n count:(unsigned)c
{
    if (strcmp(n, IO_DISPLAY_DO_BLIT) != 0)
        return [super setIntValues:p forParameter:n count:c];

    if (stormBlitFailed)  return IO_R_RESOURCE;   /* 영구 비활성 (2-5) */
    if (!stormBlitReady)  return IO_R_RESOURCE;   /* 게이팅: 명확히 거절 */
    if (!validate(p, c))  return IO_R_RESOURCE;   /* SW 폴백 */
    if (src == dst)       return IO_R_SUCCESS;    /* no-op */

    lock(engineLock);                             /* 2-4: 트랜잭션 전체 */
      if (!waitIdle())      { unlock; return IO_R_RESOURCE; }   /* EXEC 전 */
      program state + direction + EXEC;
      if (!waitIdle()) {                          /* EXEC 후 — 다르다 */
          stormBlitFailed = YES;                  /* 2-5: 영구 비활성 */
          unlock; return IO_R_RESOURCE;
      }
    unlock(engineLock);
    return IO_R_SUCCESS;
}
```
S2의 `osmgaStormInitState`/bounded wait를 재사용하고, 복사 실행부만 방향 처리를
포함하도록 일반화한다(`osmgaStormBlit(...)`). 이 내부 헬퍼는 **`IO_DISPLAY_CAN_BLIT`
플래그와 무관하게** 호출 가능해야 한다(self-test가 직접 쓴다).

커서 메서드(`hideCursor:`/`moveCursor:`/`showCursor:`)와 `revertToVGAMode`도
`engineLock`을 공유한다(2-6).

## 4. S3a self-test 설계

`enterLinearMode` 말미에서, S1/S2와 같은 opt-in 플래그로, **통제된 입력**으로 핸들러를
직접 호출한다. 화면 우하단 근처의 좁은 영역만 사용한다.

| 케이스 | 목적 | src → dst | 경로 |
| --- | --- | --- | --- |
| A. 비겹침(가시) | 기본 경로 | (64,64) → (704,576) 64×64 | 공개 API |
| B. 겹침·아래로 | `BLIT_UP` | (896,640) → (896,672) | 공개 API |
| C. 겹침·오른쪽 | `BLIT_LEFT` | (896,576) → (928,576) | 공개 API |
| D. 겹침·대각(둘 다) | `BLIT_UP\|BLIT_LEFT` 조합 | (880,432) → (912,464) | 공개 API |
| E. src==dst | no-op 성공 | (704,576) → (704,576) | 공개 API |
| F. 잘못된 입력 | 검증 경로 | w=0 / 화면 밖 / count≠6 / 최상위비트 | 공개 API, 전부 `IO_R_RESOURCE` |
| G. 오프스크린 소스 | S2 회귀 | (0,1024)오프스크린 → 오프스크린 | **내부 헬퍼 직접 호출** |

**케이스 G가 내부 헬퍼인 이유(codex 지적으로 정정)**: 공개 API 검증은 좌표를
`displayInfo->width/height` 안으로 제한하므로 오프스크린 소스(`y=1024`)는 **반드시
거부돼야 한다.** 원래 계획의 케이스 A는 이 검증과 자기모순이었다. 오프스크린 관련
검증은 공개 API가 아니라 내부 블릿 헬퍼를 직접 호출해 수행한다. API 계약이
"on the screen"이라고 명시하고 있으므로 이것이 옳다.

B·C·D 검증법: 소스에 위치 인코딩 패턴을 깔고, 복사 후 목적지를 읽어 **겹침에도
불구하고 원본 패턴이 온전히** 나타나는지 확인한다(방향이 틀리면 줄무늬처럼
번지므로 즉시 검출된다).

## 5. 안전 분석

| 위험 | 완화 |
| --- | --- |
| WindowServer 상시 호출로 화면 전체 훼손 | **S3a에서는 `IO_DISPLAY_CAN_BLIT`를 세우지 않는다.** S3b는 별도 부팅 |
| 잘못된 입력 | 전수 검증 후 `IO_R_RESOURCE` — 계약상 호출자가 소프트웨어 처리 |
| 겹침 방향 오류 | S3a 케이스 B·C·D가 패턴 검증으로 잡는다(대각 포함) |
| 동시 호출로 레지스터 인터리브 | `engineLock`이 트랜잭션 전체를 감싼다 (2-4) |
| EXEC 후 타임아웃 → 지연 write가 SW 복사를 덮음 | **가속 영구 비활성화**, 복구 시도 안 함 (2-5) |
| 커서/teardown 경합 | 커서 메서드·`revertToVGAMode`가 같은 락 공유 (2-6) |
| 좌표 원점(top-left/bottom-left) 미확정 | 헤더에 명시 없음. S3a는 대칭적이지 않은 패턴으로 확인하고, **S3b에서 WindowServer 실사용으로 최종 확정** |
| 엔진 wedge | bounded wait, 정리 write 금지. EXEC 전후 처리는 2-5 참조 |
| 화면 훼손 지속 | 프레임버퍼 훼손은 리페인트·재부팅으로 소거. PLL/DAC/타이밍 미접촉 |
| VRAM aliasing | 좌표를 전부 가시영역 안으로 검증하므로 실증 VRAM 안쪽 보장 |

## 6. 검증 방법

### 6-1. 하드웨어 이전 (호스트) — ✅ 방향 로직 완료

방향 규칙을 **시뮬레이션으로 검증**했다: 공유 버퍼 위에서 하드웨어가 복사할
순서(행/열 방향)대로 픽셀을 옮긴 결과가, 올바른 memmove 결과와 일치하는지 전수 비교.

```
case               ydir xdir SGN    AR5       AR3       AR0     FXBNDRY    YDSTLEN  memmove
A non-overlap         1   -1   1   1024   1048639   1048576  0x03FF03C0 0x02C00040  n/a(src offscreen)
B overlap down       -1    1   4  -1024    720768    720831  0x03BF0380 0x02DF0040  True
C overlap right       1   -1   1   1024    590783    590720  0x03DF03A0 0x02400040  True
D overlap up          1    1   0   1024    689024    689087  0x03BF0380 0x02800040  True
E overlap left        1    1   0   1024    590752    590815  0x03BF0380 0x02400040  True
F overlap both       -1   -1   5  -1024    638895    638832  0x03CF0390 0x028F0040  True
```
겹침 5개 케이스(B~F) 전부 **memmove 의미론과 일치**. 아래로/오른쪽으로 이동할 때만
역방향이 선택되고(B: `BLIT_UP`, C: `BLIT_LEFT`, F: 둘 다), 위로/왼쪽으로 이동할 때는
정방향이 유지된다(D, E) — memmove 규칙 그대로다.

**관찰(기록해 둠)**: 케이스 A는 `srcX=0 < dstX=960`이라 새 무조건 규칙에서
`BLIT_LEFT`가 선택된다. S2에서 같은 복사를 `SGN=0`으로 수행해 PASS했는데, 비겹침이라
어느 방향이든 정확하므로 둘 다 옳다. S2의 `SGN=0` 경로에 대한 회귀 검증은 **S2 테스트
자체가 같은 부팅에서 계속 실행되므로** 유지되고, 케이스 A는 추가로 `BLIT_LEFT`
경로를 덮는다.

남은 호스트 검증(구현과 함께): 검증 로직이 잘못된 입력(경계값 포함)을 전부 거부하는지.

### 6-2. 타깃 빌드
클린 빌드, 경고 0, `nm -u` 미해결 심볼 0.

### 6-3. 실기 1회 부팅 (S3a)
1. 케이스 A/B/C의 목적지 픽셀이 소스 패턴과 정확히 일치
2. 케이스 D가 `IO_R_RESOURCE`를 반환(엔진 미접촉)
3. bounded wait 타임아웃 없음
4. 화면: 테스트 영역 외 이상 없음. **`IO_DISPLAY_CAN_BLIT`를 안 세웠으므로
   WindowServer는 여전히 소프트웨어 경로** — 평소와 동일해야 정상
5. telnet 생존

### 6-4. 복구
플래그 off 또는 재부팅. 최악은 R5-VGA.

## 7. 이 단계가 증명하지 않는 것

- WindowServer 실사용 시의 정확성·성능(S3b)
- Mesa 연결(S4) — 이 단계는 **화면 내 복사**만이며, Mesa가 오프스크린 VRAM에
  직접 렌더하려면 여전히 유저 태스크 매핑(미검증)이 필요하다
- DMA, 인터럽트, 다중 클라이언트 직렬화
- 다른 심도(32bpp 한정)

## 8. codex 교차검토 결과 (2026-08-19)

이번 검토는 **지적의 질이 특히 높았다.** 대부분 채택했고, 그 과정에서 저장소 안의
제3의 Matrox 구현을 발견해 여러 사실을 독립 확인했다.

### 8-1. 채택 — 엔진 트랜잭션 직렬화 (누락돼 있던 필수 요건)

`setIntValues:`는 동시 호출될 수 있는데 내 계획에 락이 없었다. S1/S2는 부팅 시 1회
실행이라 문제가 없었으나 S3는 다르다. → §2-4.

**독립 확인**: `openstep-sdl12/upstream/SDL-1.2.15/src/video/fbcon/SDL_fbmatrox.c`가
목적지가 화면일 때 블릿 전체를 `SDL_mutexP/V(hw_lock)`로 감싼다.

### 8-2. 채택 — EXEC 이후 타임아웃은 소프트웨어 폴백으로 넘기면 안 된다

내 계획은 모든 타임아웃에 `IO_R_RESOURCE`를 반환하려 했다. EXEC 이후에는 엔진이
아직 쓰고 있을 수 있어, 호출자의 소프트웨어 복사 위에 지연 write가 떨어져 **지속적
훼손**이 된다. bounded wait은 CPU 스핀만 막고 이 경합은 못 막는다. → §2-5,
가속 영구 비활성화.

### 8-3. 채택 — 커서·teardown 동시성

동기식 RPC는 호출 스레드만 직렬화한다. `IOFrameBufferDisplay`에서 상속한
프레임버퍼 기반 커서 저장·복원과 `revertToVGAMode`가 실제 경합 대상이다. → §2-6.
(우리 `IODisplayInfo.flags = 0` = write-through 확인 — copyback이었다면 CPU가 쓴
소스를 엔진이 못 볼 수 있었다.)

### 8-4. 채택 — S3a self-test의 자기모순

원안 케이스 A는 오프스크린 소스(`y=1024`)를 **공개 API**로 넣는데, 같은 계획의 검증
규칙은 좌표를 화면 안으로 제한하므로 반드시 거부된다. API 계약도 "on the screen"이다.
→ 공개 API 케이스는 전부 가시 좌표로 바꾸고, 오프스크린 검증은 **내부 헬퍼 직접
호출**로 분리했다. 대각(양방향) 겹침 케이스 D도 추가했다(원안에 없었음).

### 8-5. 채택 — 입력 검증 보강

파라미터가 `unsigned`로 전달되므로 **최상위 비트가 선 값을 거부**한다.
`src == dst`는 no-op 성공. 1픽셀 폭·높이는 정상 입력이므로 거부하지 않는다.

### 8-6. 확인 — 방향 규칙이 제3의 출처로 검증됨

`SDL_fbmatrox.c:155-180`이 내 규칙과 **글자 그대로 동일**하다:
```c
if (srcX < dstX) sign |= 1;                                  /* BLIT_LEFT */
if (srcY < dstY) { sign |= 4; srcY += h-1; dstY += h-1; }    /* BLIT_UP  */
stop = start = (srcY * pitch) + srcX;
if (srcX < dstX) start += w-1;  else  stop += w-1;
skip = (srcY < dstY) ? -pitch : pitch;                       /* AR5 */
```
`FXBNDRY`도 `(dstX | ((dstX + w-1) << 16))`로 inclusive 형식이 같다.

### 8-7. 미해결로 남김 — 좌표 원점

헤더는 top-left/bottom-left를 명시하지 않는다. DPS의 사용자 좌표가 bottom-left라는
사실이 이 API의 래스터 관례를 증명하지는 않는다. 디스플레이 로컬 프레임버퍼 픽셀
좌표(top-left)일 가능성이 높지만 **가정하지 않는다.** S3a는 비대칭 패턴으로 확인하고,
최종 확정은 S3b의 WindowServer 실사용에서 한다.

### 8-8. S3b용 메모 (지금 구현하지 않음)

- `IO_DISPLAY_CAN_BLIT`는 **디스플레이 등록/능력 탐색 이전**에 세워야 한다
  (WindowServer가 `IODisplayInfo.flags`를 캐시한 뒤면 늦다).
- 실사용 부하: 큰 노출영역 복사, 잦은 겹침 스크롤, 얇은 damage strip, 1픽셀 폭/높이.
  **정확성 이유로 작은 블릿을 거부하지 말 것.**
- 로깅은 **호출당 로그가 아니라 카운터/버킷**으로(치수, 방향, 지연, 거부, 타임아웃 상태).
- "실패를 반환하면 호출자가 소프트웨어로 처리한다"는 헤더의 요구사항이지 실행 보장이
  아니다. S3b에서 실제 폴백 동작을 확인해야 한다.

## 9. ✅ S3a 실기 결과 — PASS 6/6 (2026-08-19)

1024×768×32, `"Storm 2D Test" = "Yes"`, 콜드 부팅 1회.
`IO_DISPLAY_CAN_BLIT`는 세우지 않았으므로 WindowServer는 소프트웨어 경로 유지.

```
S3: self-test begin (CAN_BLIT not advertised)
S3/a-nonoverlap:    PASS (64,64)->(704,576) 64x64
S3/b-overlap-down:  PASS (896,640)->(896,672) 64x64     ← BLIT_UP
S3/c-overlap-right: PASS (896,576)->(928,576) 64x64     ← BLIT_LEFT
S3/d-overlap-diag:  PASS (880,432)->(912,464) 64x64     ← 둘 다
S3/e-noop:          PASS
S3/f-invalid:       PASS (4/4 refused)
S3: self-test end 6/6 passed
```

### 판정

| 기준 | 결과 |
| --- | --- |
| 비겹침 복사 | ✅ |
| `BLIT_UP` 겹침 | ✅ |
| `BLIT_LEFT` 겹침 | ✅ |
| **대각(양방향) 겹침** | ✅ — codex 지적으로 추가한 케이스 |
| `src==dst` no-op 성공 | ✅ 엔진 미접촉 |
| 잘못된 입력 4종 거부 | ✅ 전부 `IO_R_RESOURCE` |
| 화면 이상 없음 | ✅ operator 확인 |
| telnet 생존 | ✅ |

위치 인코딩 패턴을 썼으므로 **방향이 틀렸다면 겹친 영역이 번져 즉시 검출**됐을
것이다. 대각 케이스까지 통과한 것이 겹침 로직 검증의 핵심이다.

### 부수 확인 — 직렬화가 실제로 유효함

`mach/i386/simple_lock.h`를 확인한 결과 `simple_lock`은 no-op이 아니라 `xchgl`
기반의 실제 스핀락이다. 따라서 `stormBusy` test-and-set이 원자적으로 보호된다.
설계상 스핀락은 플래그 조작 몇 명령어만 잡고, 경합한 호출자는 **대기하지 않고**
`IO_R_RESOURCE`를 받아 소프트웨어로 처리한다.

### 구현 위치

`-doDisplayBlitSrcX:srcY:width:height:dstX:dstY:`(검증·직렬화·영구실패 처리),
`-setIntValues:forParameter:count:`(정확 이름 매칭 후 위임),
`osmgaStormBlit()`(방향 처리 포함 일반 복사), `osmgaTextEquals()`.
self-test는 `-runStormBlitApiTest` / `-stormBlitCheckSrcX:...`.

## 10. S3b 착수 전 필수 선행 작업

S3a는 커서가 없는 부팅 초기에만 실행됐으므로 §2-6의 동시성 문제가 잠재해 있었다.
`IO_DISPLAY_CAN_BLIT`를 세우기 **전에** 다음이 필요하다.

1. **커서·teardown 락 공유** — `hideCursor:`/`moveCursor:`/`showCursor:`와
   `revertToVGAMode`가 `stormBusy`/`stormLock`을 공유하도록 오버라이드.
   커서는 프레임버퍼를 CPU로 저장·복원하므로 진행 중인 블릿과 영역이 겹치면 훼손된다.
2. **좌표 원점 확정** — top-left 가정이 맞는지 WindowServer 실사용으로 확인.
   틀리면 화면이 상하 반전된 위치로 복사된다.
3. **카운터 기반 통계** — 호출당 로그는 금지(고빈도). 치수·방향·거부·타임아웃을
   카운터로 모으고 별도 파라미터로 조회.
4. **플래그 설정 시점** — `IO_DISPLAY_CAN_BLIT`는 `IODisplayInfo` 발행 시점
   (init)에 세워야 한다. WindowServer가 flags를 캐시한 뒤면 늦다.
