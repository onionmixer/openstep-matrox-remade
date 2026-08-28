# M8 — 로그가 살아남지 않아도 판정은 남는다 (2026-08-29, 코딩 전)

## 0. 무엇이 일어났나

수리된 밴드를 `/me/wq 3`(T7 이후만)으로 돌렸다.  **9 줄만 살아남았다.**

```
2231 -> 2240 줄        45 줄쯤 나왔어야 한다
살아남은 것: T9 의 마지막 다섯 줄과 SUMMARY
잃은 것:     T7a/b/c, T7e, T8a x2, T8b  — 전부
```

**T9 는 답했다**(`T9 PASS -- ... 펜스 불필요`).  마지막이었기 때문이다.

## 1. 원인 — 지연을 늘려도 소용없다

`osmgaD2Settle()` 은 **`IODelay(5000)`**, 곧 5 ms **바쁜 대기**다.  CPU 를
놓지 않으므로 하네스가 도는 동안 **syslogd 는 한 번도 스케줄되지 않는다.**
45 줄이 커널 링에 쌓이고 앞쪽이 밀려 나간다 — 살아남는 것이 **끝의 9 줄**인
것이 그 증거다(FIFO 손실).

그러므로:

```
지연을 늘린다     ✗  더 오래 스핀할 뿐, syslogd 는 여전히 못 돈다
밴드를 쪼갠다     △  듣지만 재부팅마다 한 조각씩
```

## 2. 저장소가 이미 이 문제를 풀어 놨다

`osmgaD2Settle` 정의 바로 위:

```c
/* What the last verify saw, so the summary can restate it without the
 * body of the log having survived. */
static unsigned long osmgaD2LastChanged;
static unsigned long osmgaD2LastWrong;
static unsigned long osmgaD2LastOutClip;
static unsigned long osmgaD2LastFenceTag;
```

**측정값을 static 에 담고 끝에서 한 줄로 다시 말한다.**  꼬리는 언제나
살아남는다 — 이번에도 SUMMARY 는 살아남았다.

## 3. 설계

각 밴드가 판정을 static 에 남기고, **맨 끝에 압축 SUMMARY 두세 줄**을 찍는다.

```c
static unsigned long osmgaM4SumT7[3];    /* 축마다 실패한 위상 수 */
static unsigned long osmgaM4SumT7e[6];   /* u near/far, v near/far, corner */
static unsigned long osmgaM4SumT8a[4];   /* diffuse lo/hi, fromtex lo/hi */
static unsigned long osmgaM4SumT8b;      /* 어긋난 화소 수 */
static unsigned long osmgaM4SumT9[3];    /* busy, leak fenced, leak unfenced */
```

끝에서:

```
M4 SUMMARY/T7: phases failing 0 0 0 | seam 127 128 127 128 127 127
M4 SUMMARY/T8: alpha 0..249 / 51..51 | blend wrong 0
M4 SUMMARY/T9: busy 1, leak 0 0
```

**세 줄이면 다섯 밴드의 판정이 전부 들어간다.**  본문이 사라져도 판정은
남는다.

밴드 선택자는 그대로 둔다 — 본문(진단)을 읽고 싶을 때 여전히 쓸모가 있다.

## 4. 초기화

값을 **실행 시작에 지워야** 한다.  안 그러면 돌지 않은 밴드가 지난번 값을
그대로 다시 찍는다 — v9 제출 경로가 `osmgaHW3DLast[]` 를 매번 지우는 것과
같은 이유이고, 그 주석이 *"leaving them alone would answer this submission
with the last one's verdict"* 라고 적어 놨다.

돌지 않은 밴드는 **`-`(안 돔)로 구별**되어야 한다.  0 은 "통과" 이고
"안 돔" 이 아니다.

## 5. codex 에 물을 것

1. static 에 담아 끝에서 재진술하는 것이 옳은 방향인가, 아니면 커널 링을
   직접 읽는 더 나은 길이 있나?
2. "돌지 않음" 을 어떻게 표현해야 하나 — 별도 플래그 배열인가, 센티넬 값인가?
3. SUMMARY 를 몇 줄까지 믿을 수 있나?  이번에 9 줄이 살아남았는데, 그것이
   보장인가 우연인가?
4. `IODelay` 가 CPU 를 놓지 않는다는 내 진단이 맞나?  놓게 하는 다른 호출이
   DriverKit 에 있나(`IOSleep`)?
5. 빠진 것.


---

## 6. 자체 확인 (codex 회신 전) — **훨씬 작은 고침이 있다**

§5 의 질문 4 를 타깃 헤더로 직접 확인했다.
`/NextDeveloper/Headers/driverkit/generalFuncs.h` 가 둘을 나란히 적어 놨다:

```c
/*
 * Sleep for indicated number of milliseconds.
 */
void IOSleep(unsigned milliseconds);

/*
 * Spin for indicated number of microseconds.
 */
void IODelay(unsigned microseconds);
```

**`IODelay` 는 "spin", `IOSleep` 은 "sleep".**  진단이 맞았고, **DriverKit 에
양보하는 지연이 있다.**

### 6.1 `osmgaD2Settle` 은 하드웨어 타이밍이 아니다

호출처 **86 곳 중 85 곳이 `IOLog` 바로 뒤**다(python 으로 전수 확인).
나머지 하나도 로그 옆이다.  곧 이것은 **로그 속도 조절기**이지 레지스터
정착을 기다리는 물건이 아니다.

### 6.2 그러므로 고침은 한 줄이다

```c
static void
osmgaD2Settle(void)
{
    IODelay((int)OSMGA_D2_SETTLE_US);   ->   IOSleep(1);
}
```

`IODelay` 는 CPU 를 쥔 채 5 ms 를 돈다 — 그동안 `syslogd` 는 **한 번도
스케줄되지 않는다.**  `IOSleep` 은 양보하므로 줄마다 배출된다.

부수 효과도 좋은 쪽이다: 86 곳 × 5 ms **스핀** 430 ms 가 86 ms **수면**으로
바뀐다 — 벽시계로도 더 빠르고 기계에 훨씬 친절하다.

### 6.3 §3 의 SUMMARY 는 어떻게 하나

**여전히 값어치가 있다** — 로그가 어떤 이유로든 잘리면 판정은 남아야 한다.
다만 **우선순위가 뒤집힌다**: `IOSleep` 이 본문을 살리므로, SUMMARY 는
보험이지 주된 수단이 아니다.

먼저 한 줄을 고치고 **재부팅 한 번으로 확인**한다.  본문이 살아남으면 SUMMARY
작업은 하지 않아도 된다.

⚠ 확인할 것: `IOSleep` 은 블록한다.  하네스는 파라미터 경로(스레드 문맥)에서
돌므로 안전해야 하지만, `osmgaD2Settle` 은 `enterLinearMode` 의 다른 시험들도
쓴다.  인터럽트 문맥에서 불리는 곳이 없는지 확인이 필요하다.

---

## 7. codex 교차검토 판정 — **§1 과 §6 이 틀렸다**

| codex 주장 | 내 검증 | 결과 |
|---|---|---|
| *"5 ms 지연 때문에 syslogd 가 못 돈다"* 는 **저장소의 실측과 모순된다** | `M3 §20` 을 열었다: *"부팅 때는 25 줄 중 4 줄만 남았는데, `OSMGAWarpQual` 로 시스템이 완전히 뜬 뒤 다시 돌리자 **25 줄이 전부 살아남았다. 줄 사이 5 ms 지연은 문제가 아니었다**"* | ✅**채택 — 내 진단이 틀렸다. 열여덟 번째** |
| **`IOSleep` 으로 바꾸지 마라.** 문맥에 따라 위험하다 | `doc/driverkit.md` 에 **실측 표**가 있다 — 네트워크 스택 ioctl 문맥에서 `IOSleep` 은 **"불가 — 머신이 멈춘다"**. 2026-07-20 에 실제로 멈췄다. 그리고 설계 원칙이 *"짧은 정착이 꼭 필요하면 `IOSleep` 이 아니라 **`IODelay`**"* 라고 못박아 놨다. 하네스는 `setIntValues:` 로 들어온다 | ✅**채택 — 86 곳을 바꿀 뻔했다** |
| static + 꼬리 재진술이 옳은 패턴이다 | 드라이버가 이미 그렇게 한다 | ✅확인 |
| 커널 링 직접 읽기는 하지 마라 | 경로가 `IOLog → msgbuf → syslogd` 뿐이다 | ✅채택 |
| 센티넬이 아니라 **명시적 상태**를 써라 | *"T0 이 실패해서 막힌 것"* 과 *"운영자가 안 골랐다"* 는 다른 사실이다 | ✅채택 |
| **종단 탈출 경로가 SUMMARY 를 건너뛴다** | `goto unmapM4` 가 SUMMARY 를 지나쳐 뛴다 — 실패하면 판정이 하나도 안 남는다. **가장 아픈 지적** | ✅채택 |
| T7e 는 아무것도 반환하지 않는다 | 로그만 찍는다 | ✅사실 |
| 실행 가드가 없다 | 선택자가 호출 앞뒤로 바뀌는 static 인데 배타 제어가 없다 | ✅채택 |
| 세 줄이 살아남는다는 것은 **관측이지 보장이 아니다** | 맞다. 연속으로, 사이에 settle 없이 | ✅채택 |

### 7.1 원인을 다시 적는다

**지연이 아니라 양이다.** 25 줄은 살아남았고 45 줄은 못 살아남았다.

```
IOSleep 으로 교체    ✗  진단이 틀렸고, 문맥이 위험하다 (머신이 멈춘 전례)
압축 SUMMARY         ✅ 본문이 잘려도 판정은 남는다
밴드 선택자          ✅ 이미 있다. 본문을 읽고 싶을 때 쓴다
```

## 8. 개정된 설계

```
상태     밴드마다 NOT_SELECTED / BLOCKED / RUNNING / COMPLETE / ABORTED
값       밴드마다 몇 개의 unsigned long
초기화   요청을 받아들일 때마다 전부 지운다 (osmgaHW3DLast[] 와 같은 이유)
출력     SUMMARY 를 **정상 끝과 모든 종단 탈출 앞**에서 찍는다.
         연속으로, 사이에 settle 없이.
가드     실행 중이면 두 번째 요청을 거절한다
```

`IODelay` 는 **그대로 둔다.**
