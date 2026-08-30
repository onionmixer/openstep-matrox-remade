# T5 — 클립된 삼각형의 배치 병합 (계획, 코드 없음)

## 1. 문제 (전부 실측)

T4 이후 레벨 프레임 190 ms 중 **제출이 99.9 ms (53%)**: 542회/프레임 × 184 µs.
flush 분해(100프레임 총계): bracket 6,353 · clip 13,515 · full/기타 소수.
클립 강제 제출이 프레임당 ~386회 — 절두체에 걸린 삼각형마다 배치 하나.

즉시 제출의 이유는 재생 가능성이다: Mesa 절두체 클리퍼는 클립된 프리미티브마다
임시 정점 슬롯을 `VB->FirstFree` 부터 **재사용**하므로(`clip_funcs.h:49, 144`;
유저클립 단계만 같은 프리미티브 안에서 `VB->Free` 를 이어 쓴다), 미뤄 둔
클립 소스의 소프트웨어 재생은 이미 덮인 정점을 읽게 된다.

하드웨어 경로는 무관하다 — WARP 은 `OSMGAMesaWarpAdd` 가 정점 **값**을 배치
버퍼에 복사하는 순간 VB 와 무관해진다. 문제는 오직 **실패 후 재생** 경로다.

## 2. 실패 경로의 현재 크기 (실측)

재생이 필요해지는 경우와 빈도:
- 커널 거절: **0** (T4 이후 100프레임 × 여러 실행에서 `declined 0`)
- WARP 배치의 제출 자체 실패(ioctl 오류): 관측 0
- 시험 훅 주입(`OSMGA_ARM`, inject): 시험 전용
- 창 소멸/컨텍스트 해제 중 flush: `hookRescued/hookDropped` 경로, 관측 0

## 3. 안 (a) — 미루되 재생 불가로 표시 [제안]

`pendSrc[]` 에 `replayable` 플래그를 더한다. 클립 임시 정점을 쓰는 소스는
`replayable = 0` 으로 **미루고**, 재생이 필요해진 시점에 `replayable == 0` 인
소스는 그리지 않고 `hookDroppedClipped`(신설) 로 센다. `batchable` 조건에서
클립 항을 제거한다(멀티패스 게이트는 유지).

- 효과: 클립 flush ~386회/프레임 소멸 → 예상 **~70 ms/프레임** 절감
  (5.6 → ~9 fps). 목표치로만 두고 실측으로 확정.
- 정직한 비용: 실측 0 인 실패 경로가 실제로 발생하면 그 배치의 클립 삼각형이
  **그 프레임에서 사라진다**(다음 프레임에 정상). 카운터가 남는다.
- 이 코드베이스의 선례: 표면이 언바인드된 채 flush 되면 이미 `hookDropped` 로
  드랍한다. "그릴 수 없게 된 것을 세고 드랍" 은 새 원칙이 아니다.

## 4. 안 (b) — VB 필드 스태시 [기각 사유와 함께 기록]

재생에 필요한 VB 벡터(Win·Color·TexCoord·Specular·Index·EdgeFlag…)를 클립
소스마다 스태시하고, 재생 직전 임시 슬롯에 되쓰고 그린 뒤 복원한다.
- Mesa 소프트웨어 래스터라이저가 읽는 필드 집합에 결합된다 — 상태(조명·안개·
  스텐실…)에 따라 달라 전수 열거가 취약하다.
- 재생은 나중 프리미티브의 콜백 **안에서** 일어나므로, 되쓰기·복원을 정확히
  묶지 않으면 진행 중인 클립 프리미티브의 슬롯을 오염시킨다.
- 실측 0 인 경로를 위해 항상 지불하는 스태시 복사 비용(클립 삼각형마다)이
  절감분의 일부를 도로 먹는다.

## 5. 구현 (안 (a), 유저랜드만)

1. `pendSrc[]` 에 `int replayable;` — append 시 `v0<FirstFree && … && pv<FirstFree`.
2. WARP 티어(`osmgaMesaWarpTriangle`)와 사다리꼴 티어의 `batchable` 에서 클립
   항 제거. 멀티패스 항 유지(그건 VB 재실행 문제라 별개).
3. `osmgaMesaReplaySource()` 가 `replayable == 0` 이면 그리지 않고 0 반환 +
   `hookDroppedClipped++`. 호출부들은 반환 0 을 이미 처리한다(확인 필요:
   narrowing 의 `redrew` 분기와 rescue 루프).
4. `mgastats` 에 `dropped-clipped` 표시.
5. 검증: 회귀 3종, 100프레임 자기 종료 실행(WARP=1) — `clip flush → ~0`,
   제출/프레임, ms/프레임, 그리고 `declined` 이 여전히 0 인지(0 이면 드랍도 0).

## 6. 검토받고 싶은 위험

1. 재생 불가 드랍의 수용 가능성 — §2 의 빈도 평가가 맞는가, 놓친 재생 경로가
   있는가 (`osmgaMesaReplaySource` 호출부 전수).
2. `FirstFree` 비교가 클립 임시의 정확한 판별인가 — 유저클립(`VB->Free` 이어
   쓰기)까지 포함해 `>= FirstFree` 가 전부인가.
3. WARP full-flush(`OSMGA_MESA_WARP_FULL`) 재시도 경로에서 pendSrc 재기록이
   플래그를 보존하는가.
4. 멀티패스 게이트를 남기는 판단이 맞는가.

## 7. codex 교차검토 판정 (2026-08-31, gpt-5.6-sol)

| codex 주장 | 검증 | 판정 |
|---|---|---|
| "호출부가 반환 0을 처리한다"는 계획 §5.3 은 틀림 — 8곳 중 7곳이 `(void)` 버리고 `hookReplayed/hookRescued` 를 일괄 가산 | 1897·1903·1950·1974·1992·2016 직접 확인, 전부 일괄 가산 | ✅채택 — 성공 수만 세도록 8곳 수정 |
| 첫 드랍 후 sticky fail-safe(클립 지연 중단, 즉시-flush 복귀) 필요 | 드랍은 성능 목적의 선택이므로 unbound 드랍(불가피)과 선례가 다름 — 논거 타당 | ✅채택 — `clipDeferralOff` sticky |
| fail-safe 후 즉시-flush 되는 클립 소스는 `replayable=1` | 1693 계약 주석: 동기 flush 동안 임시 정점 생존 — 현행 정확성 근거와 동일 | ✅채택 — `replayable = stable \|\| !batchable` |
| `hookDropped`(총 손실)도 함께 가산 | 946-952 카운터 분할 주석 확인 | ✅채택 |
| named narrowing 에서 non-replayable 드랍 → 거절 미사면 → backstop 진행은 기존 원칙에 부합, 단 무조건 `hookReplayed++` 는 오류 | 1981-1992 확인: `redrew` 검사 후에도 `hookReplayed++` 무조건 | ✅채택 — redrew 조건부로 |
| `>=FirstFree` 판정은 이 경로에서 정확·완전 | clip_funcs.h 49/61/80/144/207/234/295/347/409, vb.c 56/117 — 계획 주장과 일치 | ✅확인(변경 없음) |
| WARP_FULL·두 append 에서 플래그 유실 없음, 공통 append 에 기록 | 직접 확인(§6.3 자체 검증과 일치) | ✅확인(변경 없음) |
| `hookFlushClip` 은 clip·multipass 혼합 카운터 | 1478-1480 확인. 단 이 백엔드는 MultipassFunc 를 설치하지 않음(전수 grep: 게이트 2곳+주석뿐) → GLQuake 측정치 386회는 전부 clip | ⚖️부분 — 사실이나 측정 해석 불변, 카운터 분리는 생략(기록) |
| 71 ms 는 상한. 모형(85.4µs 고정+132.4ns/dword)으로는 ~33 ms | REMAINING_WORK.md 모형 확인, 실측 재계산: 497 dw/제출, 모형 82 ms vs 실측 99.9 ms. 병합은 state dword 도 줄이므로 실제는 33-71 ms 사이 | ✅채택 — "33~71 ms, 실측으로 확정"으로 정정 |
| 190-70=120 ms 는 8.3 fps, "~9 fps" 는 과함 | 산술 확인 | ✅채택 |
| 멀티패스 게이트 유지 | vbrender.c 721-722 do-while 확인 — 계획도 유지였음 | ✅확인 |
| `declined==0` 으로 드랍 0 추론 불가(창 소실 rescue 는 declined 없이 재생) | 1238 경로 확인 | ✅채택 — 새 카운터 직접 검사 |
| 계약 주석(762·1211·1693·2973)·헤더 getter 갱신 포함 | 확인 | ✅채택 |
| 혼합/주입 회귀 시험 추가 | TESTHOOKS 주입으로 주요 2종(주입 거절+클립 혼재, fail-safe 복귀)을 ≤60초 실기 실행으로 | ⚖️부분채택(규모 조정) |

**확정 설계**: 안 (a) + sticky fail-safe + 정확한 성공/손실 계정.
`stable = 네 인덱스 < FirstFree`;
`batchable = (MultipassFunc==0) && (stable || !clipDeferralOff)`;
append 시 `replayable = stable || !batchable`.
ReplaySource 입구에서 non-replayable 이면 `hookDropped++, hookDroppedClipped++, clipDeferralOff=1, return 0`.
예상 절감 33~71 ms/프레임(6.4~8.4 fps), 실측으로 확정.

## 8. 결과 (2026-08-31, 실기)

정상 경로(100프레임, WARP): **190 → 153 ms/프레임 (6.52 fps)**.
제출 542 → 154.7회/프레임(전부 bracket), 제출 시간 99.9 → 66.5 ms/프레임,
dwords 269k → 228k/프레임. 클립 flush 13,515 → **0**. 손실 0, 거절 0.
모형 예측 ~33 ms 절감과 실측 33.4 ms 가 일치 — 병합 배치의 dword 감소가
고정비 절감을 상쇄하지 않았다.

fail-safe 경로(`OSMGA_INJECT_REFUSAL=1`, 60프레임): 거절 9회 → 드랍 2(전부
clipped, 첫 드랍에서 래치) → 이후 클립 소스 즉시-flush 복귀(clip flush 재발생)
→ 재생 569건 전부 드로우, magic 거절 8회에서 backstop revoke, crash 없음,
자기 종료. 주의: 소프트웨어 폴백 60프레임에 311초 — 주입 시험은 10프레임
이하로 줄일 것.
