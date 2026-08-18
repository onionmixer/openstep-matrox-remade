# R6 — G400/G450 register divergence (X.Org 원본 검증)

기준일: 2026-08-18
출처: `xf86-video-mga` `src/mga_reg.h` (openbsd/xenocara 미러, master).
로컬 사본: `scratch/mga_reg.h` (검증용, 저장소 커밋 대상 아님).

이 문서는 드라이버가 **G400과 G450을 모두 지원**해야 하므로, 두 칩에서 오프셋이
같은 영역과 다른 영역을 실제 X.Org 정의로 확정한다. 기존 `R6_STORM_2D_SOURCE_
AUDIT.md`의 "around 0x1c00 / around 0x1e10" 표기는 근사치였고 VCOUNT를 누락했다.
이 문서의 값이 정본이다.

## PCI 식별 (공통)

- G400과 G450은 **동일한 PCI Device ID `0x0525`** (vendor `0x102b`)를 쓴다.
  G200=`0x0521`, G550=`0x2527`. → 우리 probe의 `MGA_G400_G450_ID 0x0525` 매칭이
  두 칩을 모두 포함한다.
- 칩 구분은 PCI **revision**으로 한다(실측 카드 rev=`0x85` → G450 계열). G400은
  낮은 revision. (DAC 기반 정밀 구분은 indexed DAC read=write가 필요하므로 R6에서
  확정.)

## 공통 코어 레지스터 (G200/G400/G450/G550 동일 — 칩 조건부 없음)

BAR1 MMIO(`0xe8200000`, 16 KiB) 내부. **H1 S2가 읽는 대상은 전부 여기 → 두 칩
공통이므로 S2는 그대로 양쪽에 유효.**

| offset | 이름 | 성격 |
| --- | --- | --- |
| `0x0100` | EXEC | write=실행 트리거 (probe 접근 금지) |
| `0x1c00` | DWGCTL | draw control (write reg) |
| `0x1e10` | FIFOSTATUS | read-only |
| `0x1e14` | Status | read-only |
| `0x1e20` | VCOUNT | read-only 자유진행 수직 카운터 |
| `0x1e40` | Reset | write=엔진 리셋 (probe 접근 금지) |
| `0x1e54` | OPMODE | operation mode |
| `0x2e08` | MEMCTL | memory control |

## 분기 영역 — pixel PLL 알고리즘 (인덱스는 공통, 처리 방식이 다름)

> **정정 2026-08-18** (X.Org `mga_g450pll.c`/`mga_dacG.c` 실검증): 이 문서의
> 이전 판은 "G450 PLL = DAC 0xb6–0xb8"라고 적었으나 **틀렸다**. `0xb6–0xb8`
> (EH/EV/WB/ER)는 **G200 계열** 전용이다. **G450과 G400 모두 pixel PLL M/N/P를
> 동일한 DAC 인덱스 `0x4c/0x4d/0x4e`(`MGA1064_PIX_PLLC_M/N/P`, STAT `0x4f`)에
> 쓴다.** 진짜 분기는 인덱스가 아니라 **프로그래밍/lock 알고리즘**이다.

RAMDAC는 `RAMDAC_OFFSET = 0x3c00`, indirect 접근: index를 `0x3c00`에 쓰고
data를 `0x3c0a`에서 read/write (`outMGAdac`/`inMGAdac`).

| 항목 | G400 | G450 (Gx50) |
| --- | --- | --- |
| pixel PLL M/N/P DAC index | `0x4c/0x4d/0x4e` | **동일 `0x4c/0x4d/0x4e`** |
| M/N/P 계산 | `MGAGCalcClock`: ref/feed/post divisor 탐색 (in_div_max=31, feed_div_min=7) | frequency→MNP, VCO=27000 기준, P bit6=no-divide, P bit3-5=S 필터 |
| 쓰기/lock | **1회 write**, lock poll 없음(`PIX_CLK_CTL`가 SEL_PLL로 미리 설정) | **`MGAG450SetPLLFreq`: jitter-search** — 후보마다 ±0x300/0x200/0x100로 흔들며 여러 번 write하고 매번 lock 확인 |
| lock 판정 | 없음(하드웨어가 알아서) | `G450IsPllLocked`: `PIX_PLL_STAT(0x4f)` bit `0x40`, 초기 ≤1000 spin 후 100샘플 중 **≥90** locked여야 통과 |
| clock source | `PIX_CLK_CTL(0x1a)` SEL_PLL | **MiscOutput(`0x1fc2`) `|= 0x0c`(CLKSEL_MGA)를 MNP write 전에** |
| 부가 | — | PLL 직후 `PAN_CTL` loop-filter(밴드별 0x00..0x38) write 필수 |

- 공통 참조: `SYS_PLL_M/N/P 0x2c/0x2d/0x2e`, `PIX_PLL_STAT 0x4f`.
- **주의**: G200 EV/WB/EH의 `PIX_CLK_CTL` 전원 시퀀싱(disable→powerdown→write→
  50us→powerup→500us→select→lock)은 **G450에 적용되지 않는다**. G450은 위
  jitter-search가 lock 메커니즘이다.

### 프로젝트 기존 PLL 접근과의 불일치 (step2에서 해결 필요)

프로젝트의 `OpenStepMGAG450PLLEncoding`은 **단일 M/N/P byte image**를 만들고,
mode-transaction은 그 뒤 bounded-poll로 lock을 기다린다. 그러나 실제 G450은
`MGAG450SetPLLFreq`처럼 **여러 번 write + jitter + 통계적 lock**을 한다. 단일
write가 첫 시도에 lock되지 않을 수 있다. step2 선택지: (a) 보수적 — 계산된 M/N/P
1회 write 후 통계적 bounded-poll, 미lock 시 rollback→VGA(안전); (b) 완전 —
jitter-search 이식. 우선 (a)로 시작하고 미lock이면 (b) 도입. 어느 쪽이든 미lock은
VGA fallback으로 안전하다.

## 드라이버 설계 함의

1. 매칭은 DID `0x0525` 하나로 두 칩 수용, 진입 후 revision(및 R6에서 DAC ID)로
   G400/G450 분기.
2. 코어 2D/status/mapping 경로는 공통 코드.
3. **PLL/DAC/mode 프로그래밍(R6)만 칩별 분기**: G400=`PIX_PLLC 0x4c-0x4e` 표준
   계산, G450=`0xb6-0xb8` 전용 알고리즘 + 기존 `00/04/00` 이미지.
4. dual-head(REMHEADCTL/C2)는 현재 primary-head 범위 밖.
