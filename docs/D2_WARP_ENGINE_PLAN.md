# D2 — WARP 엔진 관문

기준일: 2026-08-19
상태: 계획. **codex 교차검토 1회 완료(2026-08-19)** 후 개정판.
초판의 안전 논리가 뒤집혔다 — §3-0 참조.

D1으로 "카드가 시스템 메모리를 읽는다"가 성립했다. 다음 관문은 **3D 래스터화를
하는 WARP 엔진이 이 카드에서 설정을 받아들이는가**다. S5 §1의 표에서 "진짜
이득"이라고 적은 항목이 전부 이것에 달려 있다.

## 0. 이 단계가 묻는 것

**D2-0**: WARP 엔진이 존재하고 설정을 받아들이는가. 마이크로코드도, DMA도,
삼각형도 없다. **레지스터 5개를 쓰고 1개를 되읽어 비교한다.**

이것이 3D판 S1이다. S1이 "2D 엔진이 우리 시퀀스에 반응하는가"를 물었듯,
D2-0은 "WARP 파이프가 켜지는가"를 묻는다. 성립하지 않으면 마이크로코드를
올릴 이유가 없다.

## 1. 확인된 사실

### 1-1. 레지스터 (legacy MGA DRM `mga_drv.h`, MIT)

| 이름 | 오프셋 | DMA 그룹 | 비고 |
| --- | --- | --- | --- |
| `WIADDR` | `0x1dc0` | 0 | G200용 명령 주소 |
| `WGETMSB` | `0x1dc8` | 0 | |
| `WVRTXSZ` | `0x1dcc` | 0 | 정점 크기 |
| `WACCEPTSEQ` | `0x1dd4` | 0 | |
| `WIADDR2` | `0x1dd8` | 0 | G400/G550용 명령 주소 |
| `WMISC` | `0x1e70` | **없음** | MMIO 전용 — `OPMODE`와 같은 사정 |

`WMISC` 비트: `WUCODECACHE_ENABLE=(1<<0)`, `WMASTER_ENABLE=(1<<1)`,
`WCACHEFLUSH_ENABLE=(1<<3)`.

**되읽기 기대값은 `0x3`** — DRM의 `WMISC_EXPECTED`가
`WUCODECACHE_ENABLE | WMASTER_ENABLE`이다. 즉 셋 중 `WCACHEFLUSH_ENABLE`은
쓰기 전용이거나 자동 해제되며, **DRM 스스로 이 되읽기를 실패 판정에 쓴다**
(`mga_warp.c:191-197`). 우리가 판정 기준을 발명하는 것이 아니다.

### 1-2. 우리 카드는 G400 경로다

DRM은 `MGA_CARD_TYPE_G400`(=2)과 `G550`을 같은 분기로 처리한다. 우리 카드는
device `0x0525`이고 X.Org에서 `PCI_CHIP_MGAG400`과 같은 상수다(S1에서 확인,
SRCORG/DSTORG 분기 적용의 근거였다). G550은 `0x2527`로 별개다.

G400 초기화 순서(`mga_warp.c:170-183`):

```
WIADDR2    = 0            (WMODE_SUSPEND — 켜기 전에 파이프를 세워 둔다)
WGETMSB    = 0x00000E00
WVRTXSZ    = 0x00001807
WACCEPTSEQ = 0x18000000
WMISC      = 0x0B         (UCODECACHE | WMASTER | WCACHEFLUSH)
읽기: WMISC == 0x03 이어야 한다
```

DRM 주석이 이 값들을 *"FIXME: Get rid of these damned magic numbers"* 라고
부른다. 즉 **출처가 문서화돼 있지 않은 상수**이며, 우리는 그것을 그대로
따르되 그 사실을 알고 따른다.

### 1-3. 마이크로코드는 확보돼 있다 (D2-1 이후용)

`scratch/xf86-video-mga-2.0.0/src/mga_ucode.h`, 11,610줄, MIT 계열
(ⓒ1999 Matrox). G400용 파이프 16종(`tgz`~`t2gzsaf`), 각각 256바이트 정렬.
`scratch/`는 gitignore이므로 사라질 수 있다(S5 §4-4).

### 1-4. DRM의 실제 호출 순서 — 우리는 일부러 다르게 한다

`mga_dma.c:893-903`의 순서는 이렇다:

```
mga_warp_install_microcode()   ← 마이크로코드 먼저
mga_warp_init()                ← 그 다음 설정 + WMISC 되읽기
mga_do_wait_for_idle()
MGA_WRITE(PRIMADDRESS, ...)    ← DMA는 그 뒤
```

**D2-0은 마이크로코드 없이 `warp_init`만 한다.** 의도적 이탈이므로 근거를
적는다: `mga_warp_init`은 `warp_pipe_phys[]`를 전혀 참조하지 않고,
`WIADDR2 = WMODE_SUSPEND(0)`로 파이프를 **세운 채** 캐시만 켠다. 파이프가
시작되지 않으므로 마이크로코드를 가져갈 일이 없다. 되읽기 판정 자체도
마이크로코드와 무관하다.

그럼에도 이것은 **원본과 다른 순서**이므로, `WMISC`가 `0x3`이 아니면
"WARP가 없다"가 아니라 "마이크로코드 없이는 이 판정이 성립하지 않는다"일
가능성을 먼저 의심해야 한다. 그 경우 D2-0을 D2-1 뒤로 미루는 것이 답이지
중단이 아니다.

## 2. 가장 큰 미지수

**이 G450에서 WARP 파이프가 켜지는가.** 부수 미지수:

- WARP 레지스터를 쓰는 것이 **동작 중인 스캔아웃을 건드리는가.** WARP는
  드로잉 파이프의 앞단이고 CRTC/DAC와 별개이므로 영향이 없어야 하지만,
  **확인된 바 없다.**
- G450이 G400과 같은 매직 상수를 받는가. DRM에 `MGA_CARD_TYPE_G450` 상수가
  있기는 하나 **명시적으로 미사용 표시**돼 있고(`mga_drm.h:77`), X.Org는
  `PCI_CHIP_MGAG400` rev ≥ 0x80을 G450으로 부르면서도 DRI에는 chipset **2**
  (= `MGA_CARD_TYPE_G400`)를 넘긴다(`mga_driver.c:285`, `mga_dri.c:570`).
  즉 **당대 스택도 G450을 G400으로 구동했다** — 우리 선택과 같다.

## 3. 단계

### 3-0. 초판의 오류 — stale 복원이 오히려 위험하다

초판은 "성공/실패와 무관하게 5개 레지스터를 **원래 값으로 되돌린다**"고 적었다.
실기 비파괴 원칙을 기계적으로 적용한 것인데, **이 레지스터들에는 정반대로
작용한다.**

`WIADDR2`(0x1dd8)의 **하위 2비트가 모드**다(`mga_drv.h:556-559`):

| 값 | 의미 |
| --- | --- |
| 0 | `WMODE_SUSPEND` |
| 1 | `WMODE_RESUME` |
| 2 | `WMODE_JUMP` |
| 3 | **`WMODE_START`** |

그리고 파이프 실행은 `WIADDR2 = warp_pipe_phys[pipe] | WMODE_START`로
시작된다(`mga_state.c:347`). 즉 **부팅 시점의 `WIADDR2` 값이 우연히 `|3`
이었다면, 그것을 "복원"하는 순간 우리가 알지 못하는 주소에서 WARP 파이프가
시작된다.** 복원이 곧 실행이다.

`WACCEPTSEQ`도 순서를 갖는 레지스터로 보인다 — DRM은 파이프 설정 시 최종 값
앞에 0을 세 번 쓴다(`mga_state.c:293`, `:322`). 게다가 소스 어디에도
`WIADDR2`/`WGETMSB`/`WVRTXSZ`/`WACCEPTSEQ`를 **되읽는 코드가 없다.** 읽기
의미론이 증명되지 않은 레지스터를 "보관 후 복원"하는 것은 근거 없는 조작이다.

→ **끝 상태는 "이전 값"이 아니라 "알려진 값"으로 둔다.** 즉 DRM의 init 상태
그대로 설정하고 파이프는 `WMODE_SUSPEND`로 세워 둔다. 이전 값은 **기록만**
하고 복원 대상으로 삼지 않는다.

### D2-0a — 최소 프로브 (레지스터 2개)

codex 제안을 채택한다. DRM이 실제로 판정에 쓰는 **유일한 신호**만 확인하고,
읽기 의미론이 불분명한 세 레지스터는 건드리지 않는다.

절차:
1. 2D 엔진 idle 대기(S1의 `osmgaStormWaitIdle` 재사용)
2. `WMISC`·`WIADDR2` 현재 값을 **기록만** 한다
3. `WIADDR2 = WMODE_SUSPEND(0)` — 파이프를 세운다
4. `WMISC = 0x0B`(`UCODECACHE|WMASTER|WCACHEFLUSH`)
5. `WMISC` 되읽기
6. `WIADDR2 = WMODE_SUSPEND(0)` 재차 — 끝 상태를 알려진 값으로

**검증 방법**
- V0-1: `WMISC` 되읽기 `== 0x3`이면 PASS. `WCACHEFLUSH`(bit3)는 되읽히지
  않는 것이 정상이며, 이는 DRM 자신의 판정 기준이다.
- V0-2: 끝 상태 확인 — `WIADDR2` 되읽기가 0(SUSPEND)인지. **보관값과의
  일치가 아니라 알려진 값과의 일치**를 본다(§3-0).
- V0-3: 화면 무변화, S1/S2/S3/D1 self-test 전부 회귀 없음.
  **한계**: 이는 프로브 **후** 2D 경로가 멀쩡함을 증명할 뿐,
  MMIO 쓰기 **도중** 스캔아웃이 흔들리지 않았음은 증명하지 못한다.
  operator의 육안 관찰이 그 부분의 유일한 증거다.
- V0-4: `revertToVGAMode` 정상, telnet 생존.

### D2-0b — 전체 G400 시퀀스 (D2-0a PASS일 때만)

`WGETMSB`/`WVRTXSZ`/`WACCEPTSEQ`까지 포함한 `mga_warp_init` 전체를 그대로
수행하고, **설정된 채 suspend된 상태**로 끝낸다. 이것이 D2-1의 전제 상태다.

**검증**: `WMISC` 되읽기 `== 0x3` 유지, 2D 회귀 없음.

### D2-1 — 마이크로코드 업로드 (D2-0b PASS일 때만)

16개 파이프를 오프스크린 VRAM에 256바이트 정렬로 올리고 `warp_pipe_phys[]`에
해당하는 주소표를 만든다. 크기는 `mga_warp_microcode_size`로 계산한다.
**검증**: 올린 뒤 되읽어 바이트 비교(우리가 쓴 것이 그대로 있는가).
아직 실행하지 않는다.

### D2-2 — 파이프 1개 실행

가장 단순한 파이프 하나로 삼각형 하나를 그린다. D1의 DMA 링을 재사용하되
`SECADDRESS`/`SECEND`(2차 DMA = 정점 버퍼)가 필요하므로 별도 계획이 필요하다.

미리 기록해 둘 것: 파이프 시작 블록 앞에 **`DMAPAD = 0xffffffff` 3개**가
붙는다. DRM 주석이 *"Padding required to to hardware bug"* 라고 적어 둔
것이며(`mga_state.c:344`), D1의 tail 패딩과는 **별개의** 회피책이다.

**D2-1과 D2-2는 별도 계획서와 교차검토 대상이다.** 이 문서는 D2-0만
확정한다.

## 4. 중단 조건

- `WMISC` 되읽기가 `0x3`이 아님 → 값을 기록하고 멈춘다. 단 §1-4대로
  "WARP 없음"으로 단정하지 않는다 — 순서 이탈이 원인일 수 있다.
- 끝 상태 `WIADDR2`가 0이 아님 → 파이프를 세우지 못한 것이므로 재실행 금지.
- 화면 이상 또는 2D self-test 회귀.

## 5. 롤백

opt-in 설정을 `No`로 되돌리고 재부팅하면 끝이다. 커널 드라이버의 기존
경로(S1/S2/S3a/S4a/D1)는 건드리지 않는다.

## 6. 이 계획이 결정하지 않은 것

- D2-0이 실패했을 때 G450 전용 시퀀스를 찾을지 여부. DRM에 G450 분기가
  없다는 것이 이 단계의 실질적 위험이며, 실패하면 원본 `MatroxMGA`
  바이너리에 WARP 흔적이 있는지부터 보는 것이 순서다.
- 마이크로코드를 VRAM에 둘지 시스템 메모리에 둘지(D2-1).
