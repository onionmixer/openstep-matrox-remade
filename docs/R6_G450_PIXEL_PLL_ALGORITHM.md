# R6 — G450 픽셀 PLL 탐색 알고리즘 (원본 MatroxMGA 정본)

원본 `MatroxMGA_reloc`(i386 Mach-O, imagebase 0)을 IDA Hex-Rays로 해독하고
codex 교차검토로 검증한 결과. 우리 초기 재구현이 **주파수는 정확하지만 지터에
불안정한 MNP**를 골라 1600×1200에서 모니터 sync를 잃은(signal error) 원인과
그 정정.

## 레지스터 / 인코딩

- 프로그래밍: `outMGAdac` DAC idx **0x4C=M**, **0x4D=N**, **0x4E=P**
- 락 판정: DAC idx **0x4F**, 데이터 비트 **0x40**
- MNP dword = `(M<<16)|(N<<8)|P`
- **P 바이트 = `(band<<3) | postsel`**
  - postsel = 비트 {6,1,0}. bit6=1 → 분주 없음(÷1);
    아니면 분주 = `2 << (postsel & 3)` → postsel 0/1/2/3 = ÷2/÷4/÷8/÷16
  - band = 비트 [5:3] (VCO 대역 선택)
- Fref = 27000 kHz

## 기본 연산 (원본 함수 ↔ 우리 함수)

| 원본 | 수식 | 우리 |
|---|---|---|
| `sub_449C` | `VCO(M,N) = (54000*(N+2) + ((M+1)>>1)) / (M+1)` | `osmgaG450CalcVCO` |
| `sub_4438` | `Fout = (P&0x40) ? VCO : VCO / (2<<(P&3))` | `osmgaG450Fout` |
| `sub_4474` | `vcoTarget = (sel&0x40) ? f : f * (2<<(sel&3))` | `osmgaG450ScaleToVco` |
| `sub_44F8` | `relErr = 1000*|got-tgt| / tgt` (**퍼밀**) | `osmgaG450Err` |
| `G450WriteMNP` | DAC 0x4C/0x4D/0x4E | `osmgaG450WriteMNP` |
| `G450IsPllLocked` | 최초 락까지 ≤999 스핀, 이후 100샘플 중 **≥90** | `osmgaG450IsLocked` |

## 후보 생성 (`sub_4628` 초기화 + `sub_4534` 생성기)

```
init: freq>650000 ? sel=0x40 : { sel=3; while(sel>0 && scaleToVco(sel,freq)>1300000) sel--; }
      (M은 0xFF로 시드되어 첫 next에서 0이 됨)
loop: vcoTarget = scaleToVco(sel, freq)
      if vcoTarget < 256000: 종료
      N    = (vcoTarget*(M+1) + 27000) / 54000 - 2
      band = vcoTarget 기준 사다리: <=550000→0, <=700000→1, <=900000→2,
             <=1100000→3, <=1300000→4, else 5
      emit (M<<16)|(N<<8)|((band<<3)|(sel&0x43))
      M가 9면: sel이 0x40이면 종료, 아니면 sel: 3→2→1→0→0x40, M=0
      아니면 M++
```

**핵심: band는 목표 VCO(vcoTarget)로 계산하며, 실제 VCO(M,N)로 재계산하지 않는다.**
생성은 vcoTarget으로 N을 구하지만, **정렬(오차 비교)은 실제 `VCO(M,N)`** 을 쓴다.

## 정렬 (`G450CompareMNP`, 삽입정렬, best-first)

1차: `relErr` 오름차순.
동률 tie-break: **양쪽 relErr ≤ 5퍼밀일 때만** 더 **작은 M** 우선.
(동일 M이면 생성 순서 유지 → 고분주/고VCO 후보가 앞선다.)

## 선택 (`G450SetPLLFreq`) — 우리가 빠뜨렸던 강건성

```
MiscOut |= 0x0C   (외부 클럭 선택)
fallback = 0
for cand in 정렬된 후보:
    stable = 0
    if ((cand & 0xFF00) - 0x300) <= 0x7700:        # N 필드 창: 3 <= N <= 122
        if lock(cand-0x300) && lock(cand+0x300) &&
           lock(cand-0x200) && lock(cand+0x200) &&
           lock(cand-0x100) && lock(cand+0x100):
              stable = lock(cand)                   # 중심도 락되어야 함
    if stable: 채택하고 종료                        # 이 값이 프로그램된 채 유지
    if fallback 없음: if lock(cand): fallback = cand
끝까지 못 찾으면: fallback(있으면) 또는 후보[0]을 기록
```

주의(교차검토로 확인): **안정 후보는 fallback에 기록되지 않으며**, 최종 fallback
기록은 리스트를 전부 소진했을 때만 일어난다. 또 fallback 프로브는 **최초 1회**만
수행한다(이후 실패 후보는 중심 프로브조차 하지 않음).

`±0x100/0x200/0x300`은 **N 필드 ±1/±2/±3**을 뜻한다(지터 밴드).

## 우리 버그와 정정

초기 재구현은 (a) 단일 "최소 절대오차" 후보만 계산하고 (b) 그 후보의 지터
오프셋들을 순회하며 **첫 락에서 즉시 반환**했다. 원본과 달리 **후보 리스트도,
밴드 전체 안정성 검사도 없었다.**

실측(호스트 검증, 162000 kHz = 1600×1200@60):

| 순위 | MNP | M | N | P | VCO | Fout | 오차 |
|---|---|---|---|---|---|---|---|
| 1 | `001622` | 0 | 22 | 0x22 (band4, sel2 ÷8) | 1296000 | 162000 | 0 |
| 2 | `000a09` | 0 | 10 | 0x09 (band1, sel1 ÷4) | 648000 | 162000 | 0 |
| 3 | `000400` | 0 | 4 | 0x00 (band0, sel0 ÷2) | 324000 | 162000 | 0 |

우리 옛 코드는 postdiv 낮은 쪽부터 탐색해 **`000400`(VCO 324MHz)** 을 골랐다.
주파수는 정확하지만 저VCO·M=0이라 marginal → 락 판정은 통과(`locked=1`)하면서도
실제 sync는 불안정. 실기 로그상 1600×1200 두 번의 부팅 모두 `mnp=000400`이었고,
한 번은 화면이 나왔고(어두움=별개의 팔레트 버그) 한 번은 **signal error**였다.
원본은 정렬상 `001622`(VCO 1296MHz)를 먼저 시도하므로 `000400`에 도달조차 않는다.

1024×768(65000 kHz)의 경우 옛 코드가 고른 `074b02`는 정렬 3순위이며, 새 코드는
1순위 `034b1b`(VCO 1039500)를 먼저 시도한다 — 원본과 동일한 동작.

## 심도 비의존성

`MGASetPCLK:`는 G450(Chipset 0x525, rev>0x7F)에서 픽셀클럭을 **심도와 무관하게
그대로** `G450SetPLLFreq:`에 넘긴다. 15bpp라고 PLL 주파수를 바꾸지 않는다.

## 검증 방법

`osmgaG450BuildCandidates` 산술을 호스트에서 그대로 컴파일해 후보 목록을 덤프,
codex가 독립적으로 낸 표와 1~6순위가 일치함을 확인했다(교차검증). 실기 로그는
`PLL stable cand N/M` 형태로 채택된 후보 순위를 남긴다.

## ✅ 실기 검증(2026-08-19)

1600×1200 RGB:555/16 재부팅: `PLL stable cand 9/30`, `mnp=021000`
(M=2, N=16, P=0x00 band0 sel0 ÷2, VCO=324000, Fout=162000, 오차 0) 채택,
**정상 밝기 + 정상 모니터 sync**로 부팅 완료.

주목할 점: 정렬 1~8위(고VCO 계열 `001622`/`000a09`/`000400`의 M=0,1 변형들)는
지터밴드 안정성 검사에서 **실제로 탈락**했고, 9번째 후보에서 처음 안정 락을
찾았다. 즉 이번 포팅은 특정 "안전한" 값으로 우연히 우회한 게 아니라,
**설계대로 실기 지터 특성에 맞춰 후보를 순차 검증·선별**하는 원본 알고리즘을
충실히 재현한 결과다. 1024×768 등 다른 해상도는 이번 사이클에서 재검토하지
않았다(변경 범위가 PLL 선택 로직 전체에 영향을 주므로, 필요 시 향후 재확인).
