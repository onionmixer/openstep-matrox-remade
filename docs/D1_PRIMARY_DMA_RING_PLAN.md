# D1 — primary DMA 링 실증 계획

기준일: 2026-08-19
상태: 계획. **codex 교차검토 1회 완료(2026-08-19)** 후 개정판.
초판의 결함 5건을 §1-6에 적고 본문에 반영했다.

`S5_HW3D_DMA_FEASIBILITY.md` §6이 권고한 첫 단계다. **"카드가 시스템 메모리를
버스마스터로 읽는가"** 하나만 묻는다. 성립하지 않으면 3D 스택을 만들 이유가
없으므로 **거기서 멈춘다.**

## 0. 이 단계가 묻는 것과 묻지 않는 것

**묻는 것**: G450이 우리가 시스템 RAM에 만든 명령 목록을 PCI 버스마스터로
읽어, **이미 실기에서 검증된 2D 명령**(오프스크린 단색채우기, S1)을 실행하는가.

**묻지 않는 것**: 3D, WARP, 성능, 다중 클라이언트, 인터럽트 기반 완료 통지.
전부 이후 단계다. 여기서는 MMIO로 직접 쓰던 것을 DMA로 바꿔도 **같은 결과가
나오는가**만 본다 — 정답을 이미 아는 문제를 새 경로로 푸는 것이다.

## 1. 확인된 사실

### 1-1. 메모리 (S5 §3-2에서 실기 확인)

| 항목 | 값 |
| --- | --- |
| `_IOMallocLow` | 0x1c87e0, 공식 API(`driverkit/i386/kernelDriver.h:21`), 타깃 설치됨 |
| 1회 상한 | **64 KiB** (`dma_buf_alloc` 0x18980c가 `> 0x10000` 거부) |
| 연속성 | `alloc_cnvmem` 0x18ad9c = 고정 아레나 범프 할당자 → **구조적으로 연속** |
| 아레나 | conventional memory(<640 KiB), ISA DMA 사용자와 공유 |
| 물리주소 | `IOPhysicalFromVirtual(IOVmTaskSelf(), virt, &phys)` — 공개 API(0x1a9334) |
| PCI 버스마스터 | H1 S0 실측 `command=0x0007`(bit2 = bus master **이미 켜져 있음**) |

### 1-2. 레지스터 (legacy MGA DRM `mga_drv.h`, MIT 라이선스)

출처는 `scratch/mga-drm/`(Precision Insight/VA Linux, **MIT** — GPL 아님).
값은 사실이므로 인용하되 코드는 복사하지 않는다.

| 이름 | 오프셋 | 역할 |
| --- | --- | --- |
| `PRIMADDRESS` | `0x1e58` | 링 시작 물리주소 \| 모드 |
| `PRIMEND` | `0x1e5c` | 링 끝 물리주소 \| 하위 플래그. **여기 쓰면 DMA가 시작된다** |
| `PRIMPTR` | `0x1e50` | 포인터 **writeback 활성화**(`PRIMPTREN0/1`) — 읽기 포인터가 아니다 |
| `SOFTRAP` | `0x2c48` | 완료 표식용 소프트 트랩 |
| `STATUS` | `0x1e14` | `SOFTRAPEN`/`DWGENGSTS`/`ENDPRDMASTS` |
| `DMAPAD` | `0x1c54` | 블록의 빈 슬롯 채우기(부작용 없는 쓰기) |

모드 비트(`PRIMADDRESS` 하위): `GENERAL=0`, `BLIT=1`, `VECTOR=2`, `VERTEX=3`.
`PRIMEND` 하위 2비트: **`PRIMNOSTART=(1<<0)`**, `PAGPXFER=(1<<1)`
(`mga_drv.h:490-491`). PCI에서는 둘 다 0이므로 쓰는 즉시 시작된다.

**진행 상황은 `PRIMADDRESS`를 읽어서 본다** — DRM이 그렇게 한다
(`mga_dma.c:146`, `:185`). 초판이 `PRIMPTR`을 "현재 읽기 포인터"라고 쓴 것은
근거 없는 서술이었다. `PRIMPTR`은 포인터를 메모리로 되쓰게 하는 활성화
레지스터다.

### 1-3. **PCI 전용 DMA 경로는 실재한다** — S5 §4-1 정정

S5는 "X.Org의 3D 경로는 전부 AGP 전제"라고 적었다. **DDX에 대해서는 맞지만
DRM에 대해서는 틀렸다.** legacy DRM `mga_dma.c`에는 `mga_do_pci_dma_bootstrap`
경로가 따로 있고, 끝에서 `dev_priv->dma_access = 0`으로 두고
`"Initialized card for PCI DMA."`를 찍는다(`mga_dma.c:699` 부근).

즉 **AGP가 아닐 때 `PRIMEND`에 access 비트 없이 물리주소만 쓰면 되고**, 그것이
당시 출하된 구성이다. 우리가 없는 길을 내는 게 아니다.

### 1-4. 명령 인코딩 — 5 dword 블록

`DMA_BLOCK_SIZE = 5 * sizeof(u32)`. 한 블록은 레지스터 4개를 쓴다.

```
dword 0 : 인덱스 헤더 = idx(reg0)<<0 | idx(reg1)<<8 | idx(reg2)<<16 | idx(reg3)<<24
dword 1 : reg0 값
dword 2 : reg1 값
dword 3 : reg2 값
dword 4 : reg3 값
```

인덱스 산출(`mga_drv.h:226-234`):
- 그룹0 `0x1c00..0x1dff` → `(reg - 0x1c00) >> 2`
- 그룹1 `0x2c00..0x2dff` → `((reg - 0x2c00) >> 2) | 0x80`
- 그 밖의 레지스터는 **DMA로 못 쓴다**(예: `MACCESS 0x1c04`는 그룹0라 가능,
  `PRIMADDRESS 0x1e58`은 어느 그룹도 아님 → MMIO 전용)

쓸 게 4개가 안 되면 `DMAPAD`로 채운다.

### 1-5. S1이 쓰는 레지스터의 DMA 주소지정 가능 여부 (직접 대조)

| 레지스터 | 오프셋 | 그룹 | 인덱스 |
| --- | --- | --- | --- |
| `DWGCTL` | 0x1c00 | 0 | 0x00 |
| `MACCESS` | 0x1c04 | 0 | 0x01 |
| `PLNWT` | 0x1c1c | 0 | 0x07 |
| `FCOL` | 0x1c24 | 0 | 0x09 |
| `CXBNDRY` | 0x1c80 | 0 | 0x20 |
| `FXBNDRY` | 0x1c84 | 0 | 0x21 |
| `YDSTLEN` | 0x1c88 | 0 | 0x22 |
| `PITCH` | 0x1c8c | 0 | 0x23 |
| `YDSTORG` | 0x1c94 | 0 | 0x25 |
| `YTOP` | 0x1c98 | 0 | 0x26 |
| `YBOT` | 0x1c9c | 0 | 0x27 |
| `YDSTLEN+EXEC` | 0x1d88 | 0 | 0x62 |
| `SRCORG` | 0x2cb4 | 1 | 0xAD |
| `DSTORG` | 0x2cb8 | 1 | 0xAE |
| `SOFTRAP` | 0x2c48 | 1 | 0x92 |
| `DMAPAD` | 0x1c54 | 0 | 0x15 |

**⚠ 예외 — `OPMODE`(0x1e54)는 DMA로 쓸 수 없다.** 0x1dff를 넘고 0x2c00에
못 미쳐 **어느 그룹에도 속하지 않는다.** S1은 이것을 쓰므로, DMA 링을 짜기
전에 **MMIO로 미리** 설정해야 한다. `FIFOSTATUS`(0x1e10)·`STATUS`(0x1e14)도
같은 이유로 DMA 밖이지만 어차피 읽기 전용 상태라 MMIO로만 본다.

이는 초판 계획의 "S1 명령을 그대로 DMA로 옮긴다"는 서술이 **부정확했음**을
뜻한다. 옮길 수 있는 것은 드로잉 레지스터뿐이고, 엔진 모드(`OPMODE`)는
MMIO 선행 설정으로 남는다.

### 1-6. 초판의 결함 (codex 교차검토 → 전부 소스로 재확인)

| # | 결함 | 근거 |
| --- | --- | --- |
| 1 | `OPMODE`(0x1e54)는 DMA 주소지정 불가인데 S1이 쓴다 | §1-5. `osmgaStormInitState`가 `.m:483`에서 씀 |
| 2 | **`PRIMEND` 너머에 패딩 블록이 필요하다** | 아래 |
| 3 | `PRIMPTR`을 읽기 포인터로 오기 | `mga_drv.h:492`는 `PRIMPTREN0/1` 활성화 비트 |
| 4 | `IOFreeLow` 서술이 자기모순(§3은 해제 금지, §6은 해제) | 아래 |
| 5 | 검증이 checksum만 요구 — S1보다 약함 | S1은 픽셀별 불일치 수 **와** checksum 둘 다 봄(`.m:1714`, `.m:1730`) |

**패딩(2번)이 가장 중요하다.** DRM은 논리적 tail을 먼저 계산해 두고
(`mga_dma.c:129`), 그 뒤에 `DMAPAD` 4개짜리 블록을 **하나 더 붙인 다음**
(`:136-140`), `PRIMEND`는 **패딩 전의 tail**로 쓴다(`:159`). 주석이 이유를
명시한다:

> *"We need to pad the stream between flushes, as the card actually
> (partially?) reads the first of these commands. See page 4-16 in the
> G400 manual, middle of the page or so."*

즉 **카드는 `PRIMEND` 너머를 일부 읽는다.** 패딩이 없으면 카드가 링 끝 다음의
쓰레기를 명령으로 해석할 수 있다 — 이는 안전 문제다. D1은 `SOFTRAP` 뒤에
`DMAPAD` 블록을 **1개 이상** 두고, `PRIMEND`는 그 앞을 가리켜야 한다.
버퍼 크기 계산에도 이 여유가 들어가야 한다.

### 1-7. `DWGCTL.CLIPDIS`는 bit31이다 — S1의 미해결 항목 해소

S1 문서는 codex가 주장한 `DWGCTL.CLIPDIS` bit31을 "X.Org 헤더에 정의가 없어
**확인 불가**"로 기록했다(당시 `0x80000000`은 `MACCESS`의 `DIT555`로만
존재했다). DRM 헤더에 정의가 있다: `MGA_CLIPDIS (1 << 31)`
(`mga_drv.h:461`). **codex의 당시 주장이 옳았고 근거가 없었던 것뿐이다.**

우리가 쓰는 값은 둘 다 bit31이 0이라 클리핑이 살아 있다
(`0x000C7804`, `0x040C4008`). 다만 이는 **링이 정상일 때의 이야기**다 —
§5의 안전 논의에 반영한다.

## 2. 가장 큰 미지수

**카드가 conventional memory(<640 KiB) 영역을 버스마스터로 읽는가.**

부수 미지수:
- `IOMallocLow`가 커널 로더블에서 실제로 호출 가능한가(심볼은 `T`로 export돼
  있으나 `kern_loader`가 해석해 주는지는 별개)
- 캐시 일관성: CPU가 쓴 명령을 카드가 보는가. x86은 PCI 읽기에 대해 캐시
  일관성이 있지만 **이 커널의 매핑 속성은 확인된 바 없다**

## 3. 단계

### D1-0 — 메모리만 확보 (하드웨어 접촉 0)

`IOMallocLow(0x10000)` + `IOPhysicalFromVirtual`만 하고 결과를 로그로 남긴다.
엔진 레지스터를 **한 개도 쓰지 않는다.**

**검증 방법**
- V0-1: 할당 성공 여부, 반환 가상주소, 물리주소, 64 KiB 정렬 여부를 로그.
- V0-2: 물리주소가 `< 0xA0000`(conventional memory) 범위인지 — §1-1의
  아레나 분석과 실제가 일치하는지 확인. **어긋나면 §1-1 분석이 틀린 것이므로
  거기서 멈추고 재분석한다.**
- V0-3: 페이지 경계마다 `IOPhysicalFromVirtual`을 다시 호출해 **물리주소가
  실제로 연속인지** 직접 확인한다. 범프 할당자 분석을 믿지 않고 측정한다.
  (`PAGE_SIZE`는 이 커널에서 **8192**임에 주의 — S4a에서 확인됨)
- V0-4: 실패 시(0 반환) 그대로 기록하고 D1-1로 가지 않는다.

실패해도 화면·부팅에 영향이 없다. 할당만 한다.

### D1-1 — 링 내용 작성, 기동은 하지 않음

D1-0의 버퍼에 S1과 **동일한 결과**를 내는 명령 블록을 조립한다.
`PRIMADDRESS`/`PRIMEND`는 **쓰지 않는다.**

링 구성(§1-6 2번 반영):

```
  [ 드로잉 블록들 ]        DWGCTL/FCOL/FXBNDRY/YDSTLEN+EXEC 등
  [ SOFTRAP 블록 ]         완료 표식
  ← PRIMEND 는 여기를 가리킨다
  [ DMAPAD 블록 ×1 이상 ]  카드가 PRIMEND 너머를 읽으므로 반드시 둔다
```

정렬·경계 조건(`mga_dma.c:649,656` — 하위 2비트가 모드/access이므로):
- `phys & 3 == 0`, `tail & 3 == 0`
- `tail <= alloc_size - DMA_BLOCK_SIZE` (패딩 자리 확보)

S1의 단색채우기(검증된 값): `DWGCTL=0x000C7804`, `MACCESS=2`,
`FCOL=0xDEADBEEF`, `PLNWT=~0`, `CXBNDRY`/`YTOP`/`YBOT` 클립,
`FXBNDRY`, `YDSTLEN`(+0x100이 실행 트리거).

**검증 방법**
- V1-1: 조립한 dword 배열 전체를 로그로 덤프하고, **호스트에서 손으로 디코딩**해
  각 블록이 의도한 레지스터/값 쌍인지 대조한다.
- V1-2: 인덱스 산출 함수를 단위 검증 — `DWGCTL(0x1c00)→0x00`,
  `MACCESS(0x1c04)→0x01`, `DMAPAD(0x1c54)→0x15`, `SOFTRAP(0x2c48)→0x92`를
  손계산과 대조.
- V1-3: 그룹0/1 밖의 레지스터를 넣으려 하면 조립 함수가 **거부**하는지.
  특히 `OPMODE`(0x1e54)를 넣으려는 시도가 거부되어야 한다(§1-5).
  **DRM 매크로는 거부하지 않는다** — 그룹0이 아니면 무조건 그룹1 공식을
  적용한다(`mga_drv.h:234`). 우리 조립기는 그러면 안 된다.
- V1-4: `PRIMEND`가 가리킬 오프셋 뒤에 `DMAPAD` 블록이 실제로 있는지,
  그리고 `tail & 3 == 0`인지 로그로 확인.

### D1-2 — 기동 (첫 하드웨어 접촉)

`Storm 2D Test`처럼 **opt-in 설정**(`"DMA Ring Test" = "Yes"`)으로만 실행하고,
`enterLinearMode` 말미 = 화면이 서고 네트워크가 살아난 뒤에 돈다.

순서:
1. 엔진 idle 확인(S1의 `osmgaStormWaitIdle` 재사용)
1b. **`OPMODE`를 MMIO로 설정**(§1-5 — DMA 주소지정 불가)
2. 목적지 블록을 CPU로 sentinel 값으로 채우고 uncached 별칭으로 되읽어 확인
   (S1과 동일 — 엔진 결과와 구분하기 위해)
2b. **stale `SOFTRAP` 상태 정리** — 완료 신호로 쓰려면 이전 상태가 남아
   있으면 안 된다
3. `PRIMADDRESS = phys | 0`(GENERAL)
3b. **쓰기 배리어** — CPU가 링에 쓴 것이 카드가 읽기 전에 메모리에 도달해야
   한다. DRM도 `PRIMEND` 직전에 배리어를 넣는다(`mga_dma.c:158`,
   `mga_drv.h:200`)
4. `PRIMEND = phys + tail | 0`(PCI이므로 `PRIMNOSTART`·`PAGPXFER` 둘 다 0)
   ← **여기서 시작.** `tail`은 `SOFTRAP` 블록의 끝이며 그 뒤 `DMAPAD`는
   `PRIMEND` 밖에 있다(§1-6 2번)
5. bounded 폴링: `STATUS`의 `ENDPRDMASTS`/`SOFTRAPEN`, 그리고 `PRIMADDRESS`가
   tail까지 전진하는지. **무한 대기 금지** — S1/S2와 같은 유한 루프.
6. uncached 별칭으로 목적지 readback + checksum

**안전 경계** (S1에서 확립된 것을 그대로 승계)
- 대상은 **오프스크린만**: 실증 VRAM 하한 7 MiB 안, 가시 스캔아웃 위 가드 밖.
- 32bpp 모드에서만, 1024×768에서만.
- 클립(`CXBNDRY`/`YTOP`/`YBOT`)을 블록 크기로 좁혀 둔다. **단 이것은 링이
  정상일 때만 봉쇄다** — 링이 망가지면 `DWGCTL.CLIPDIS`(bit31, §1-7)를 켜는
  값이 흘러들 수 있고, 클립은 애초에 물리 VRAM aliasing을 막지 못한다
  (S1 문서에 기록된 한계). 그래서 조립기의 레지스터 화이트리스트와
  `DMAPAD` 패딩이 실질적 봉쇄 수단이다.
- 타임아웃 시 **재시도하지 않고** 영구 비활성화(`stormBlitFailed`와 같은 방식).
- **버퍼 해제 조건(§1-6 4번 — 초판이 자기모순이었다)**: `IOFreeLow`는
  다음이 **전부** 성립할 때만 한다 — `PRIMADDRESS & ~3 == PRIMEND & ~3`,
  `STATUS`가 DMA·드로잉 모두 idle, `SOFTRAP` 상태가 설명됨. 하나라도
  불확실하면 **부팅이 끝날 때까지 들고 있는다.** 헤더는 `IOFreeLow`의
  장치 동기화 의미를 아무것도 약속하지 않는다
  (`driverkit/i386/kernelDriver.h:19`). 첫 실증에서는 성공해도 해제하지
  않는 쪽을 기본으로 한다.

**검증 방법**
- V2-1: **S1과 같은 강도로** 판정한다 — 픽셀별 불일치 수 `== 0` **그리고**
  checksum `== 0xDBEEF000`. checksum만 보는 것은 S1보다 약하다
  (S1은 `.m:1714`/`.m:1730`에서 둘 다 본다). checksum 값은 로그를 믿지 않고
  `0xDEADBEEF * 4096 mod 2^32`로 독립 재계산해 대조한다.
- V2-2: **음성 대조** — 링 내용을 그대로 두고 `PRIMEND`를 쓰지 않은 채
  같은 폴링을 돌리면 목적지가 sentinel 그대로여야 한다. 이것이 없으면
  "MMIO로 이미 써둔 것을 보고 있다"를 배제할 수 없다.
  **성립 조건**: 양성/음성 두 경로가 **같은 조립기·같은 설정 코드**를 쓰고
  마지막 `PRIMEND` 쓰기만 달라야 한다. 아니면 양성 경로에 우발적 MMIO EXEC가
  없다는 것을 증명하지 못한다.
- V2-3: `PRIMADDRESS`가 실제로 전진했는지(기동 전/후 값 비교) — 카드가
  링 주소를 **소비했다**는 직접 증거. 단 이것은 스트림이 **올바르게
  디코딩됐다**거나 모든 쓰기가 봉쇄됐다는 증명은 아니다. 그건 V2-1이 한다.
- V2-4: 가시 화면 무변화. S1/S2/S3 self-test 회귀 없음.
- V2-5: 실패 시 `revertToVGAMode`로 복구되고 telnet이 살아 있는지.

## 4. 중단 조건

다음 중 하나면 **즉시 멈추고 기록만 한다.**
- `IOMallocLow`가 0을 반환하거나 커널 로더블에서 해석되지 않음
- 물리주소가 §1-1 분석과 다름 (분석이 틀렸다는 뜻)
- 물리 연속성이 실측에서 깨짐
- `PRIMADDRESS`가 전진하지 않음 → 카드가 시스템 메모리를 못 읽는 것
- 폴링 타임아웃 1회

## 5. 롤백

D1-2는 opt-in이므로 설정을 `No`로 되돌리고 재부팅하면 끝이다(C1의 Configure
스위치와 같은 방식으로 노출할 수 있으나 이번 단계에서는 config-table만).
커널 드라이버의 기존 경로는 건드리지 않는다 — S1/S2/S3a/S4a 코드는 그대로다.

## 6. 이 계획이 결정하지 않은 것

- 완료 통지를 인터럽트로 바꿀지(지금은 bounded 폴링)
- 링 버퍼 재사용·wrap 처리(지금은 1회용 단발)
- 64 KiB로 충분한지(WARP 단계에서 다시 볼 문제)
- conventional memory 아레나를 영구 점유하는 것이 다른 ISA DMA 사용자에게
  주는 영향 — **측정한 바 없다.** 그럼에도 §3의 해제 조건이 까다로우므로
  첫 실증에서는 부팅 동안 점유한 채 둔다. 64 KiB가 아레나에서 차지하는
  비중은 D1-0의 로그로 확인한다.
