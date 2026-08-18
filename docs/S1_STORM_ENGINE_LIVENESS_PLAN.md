# S1 — Storm 2D 엔진 생존 확인 계획 (오프스크린 단색채우기 + CPU readback)

기준일: 2026-08-19
상태: ✅ **완료 — 실기 PASS(§9).** Storm 2D 엔진이 우리가 유도한 시퀀스에
반응함을 실증했다.

## 0. 목적과 범위

단 하나의 미지수를 제거한다: **우리가 해독한 Storm 시퀀스에 G450 엔진이 실제로
반응하는가.**

- 하는 것: 엔진 idle/FIFO 관측 → 최소 상태 초기화 → **오프스크린** 64×64 단색
  채우기 1회 → CPU readback 검증.
- 하지 않는 것: 가시 스캔아웃 접근, BITBLT/copy, DMA, 인터럽트, WARP/3D,
  텍스처, 다중 클라이언트, MiG 노출, Mesa 연결. 전부 후속 단계.

이 단계는 OpenGL 가속 4단계 로드맵의 1단계다(2=프레젠테이션 BITBLT,
3=`OpenStepMGAService` 하드웨어 부착, 4=`mesa-matrox` 드라이버).

## 1. 이미 확보된 전제 (재확인 불요)

| 항목 | 근거 |
| --- | --- |
| BAR1 MMIO 16KiB를 드라이버가 매핑·소유 | `initFromDeviceDescription:`, `mmioBase` |
| 프레임버퍼 16MiB 가상 매핑 보유 | `mapFrameBufferAtPhysicalAddress:length:`, `displayInfo->frameBuffer` |
| MMIO read가 정상 동작 | H1 S2: `VCOUNT` 단조증가, `Status=0x80820000`, `FIFOSTATUS=0x210` |
| 디스플레이 완전 동작·복구 절차 검증 | R6-WORKING, R5-VGA |

H1 S2에서 읽은 값이 X.Org 정의와 정합함에 유의: `Status(0x1e14)` byte2 = `0x82`,
bit0=0 → **idle**. `FIFOSTATUS(0x1e10)` 하위바이트 = `0x10` = 16슬롯.

## 2. 레지스터 정본 (X.Org 2.0.0 동작 관찰, 소스 미복사)

오프셋은 `mmioBase` 기준. 모두 32bit write(명시된 곳만 8bit read).

| 이름 | 오프셋 | 용도 |
| --- | --- | --- |
| DWGCTL | `0x1c00` | 드로잉 제어(오퍼코드/atype/bop) |
| MACCESS | `0x1c04` | 픽셀 폭(PW) |
| PLNWT | `0x1c1c` | plane write mask |
| BCOL / FCOL | `0x1c20` / `0x1c24` | 배경/전경색 |
| CXBNDRY | `0x1c80` | X 클립 `(maxX<<16)|minX` |
| FXBNDRY | `0x1c84` | 채우기 X 경계 `((x+w)<<16)|x` |
| YDSTLEN | `0x1c88` | `(y<<16)|h` |
| PITCH | `0x1c8c` | **픽셀 단위** 스트라이드 |
| YDSTORG | `0x1c94` | 목적지 원점(픽셀) |
| YTOP / YBOT | `0x1c98` / `0x1c9c` | **목적지 픽셀 포인터 min/max 클립** |
| FIFOSTATUS | `0x1e10` | 하위바이트 = 여유 슬롯 |
| Status | `0x1e14` | `+2` 바이트 bit0 = busy |
| OPMODE | `0x1e54` | 동작 모드 |
| SRCORG / DSTORG | `0x2cb4` / `0x2cb8` | G400/G450 계열에서 기록 필요 |
| EXEC | `+0x0100` | 레지스터 주소에 더하면 실행 트리거 |

칩 식별: X.Org `PCI_CHIP_MGAG400 = 0x0525` — **우리 카드와 동일**하므로
SRCORG/DSTORG 기록 분기가 적용된다.

### 유도 상수

- `MACCESS`: `{8bpp:0, 16bpp:1, 24bpp:3, 32bpp:2}`, **depth 15이면 `|= (1<<31)`**
  (RGB:555/16 모드가 여기 해당).
- `OPMODE`: 리틀엔디안 x86에서 `OPMODE = MGAOPM_DMA_BLIT(0x04) | (read(OPMODE) & ~0x30000)`.
- `FCOL`: 32bpp는 색상 그대로(REPLICATE_32=항등). 16bpp는 상하위 16bit 복제,
  8bpp는 4바이트 복제. **첫 테스트는 32bpp만 수행**.
- `DWGCTL` (단색 사각 채우기, ROP=copy, block 모드 미사용):
  `TRAP(0x04) | SOLID(1<<11) | ARZERO(1<<12) | SGNZERO(1<<13) | SHIFTZERO(1<<14)
  | BMONOLEF(0) | RPL(0x00<<4) | bop_copy(0x000C0000)` = **`0x000C7804`**
  - block 모드(`BLK`, atype `0x04<<4`)는 제약이 많아 1단계에서 **의도적으로 배제**.

## 3. 실행 시퀀스

```
[G] config-table "Storm 2D Test" != "Yes" 이면 아무것도 하지 않고 종료
[G] 32bpp 포맷이 아니면 종료 (첫 테스트는 32bpp 한정)
[G] 블록 끝 > 7 MiB 이면 종료 (실증된 VRAM 하한 안쪽만 허용 — §4 참조)
[G] 오프스크린 범위가 매핑 밖이면 종료

0. IOMapPhysicalIntoIOTask로 테스트 블록의 **uncached 별칭 매핑** 확보 (§3-1)
1. bounded wait: engine idle          (Status+2 bit0 == 0)
2. fifoDepth = read8(FIFOSTATUS)      (관측·로깅용)
3. uncached 별칭으로 sentinel 0x5A5A5A5A 기록 + 즉시 read 확인
   → "메모리가 원래 그 값이었다"는 가짜 성공 배제
4. bounded wait: FIFO 여유 >= 13
5. 엔진 최소 상태 초기화 (13 writes, 아래 목록)
6. bounded wait: FIFO 여유 >= 3
7. FCOL   = 0xDEADBEEF
   FXBNDRY = ((x+w)<<16) | x            (오른쪽 경계 exclusive)
   YDSTLEN+EXEC(0x1d88) = (y<<16) | h   ← 실행 트리거, 반드시 마지막
8. bounded wait: engine idle
9. uncached 별칭으로 readback 4096픽셀 전수 비교 + checksum
10. 별칭 매핑 해제, 결과 IOLog
```

5단계 초기화 항목(13 writes): `PITCH=stridePixels`, `YDSTORG=0`, `MACCESS=2`,
`PLNWT=0xffffffff`, `FCOL=0`, `BCOL=0`, `OPMODE=DMA_BLIT|(read&~0x30000)`,
**`CXBNDRY=((testX+testW-1)<<16)|testX`** (inclusive),
**`YTOP=testY*stridePixels`**, **`YBOT=(testY+testH-1)*stridePixels`** (행 시작
포인터, X.Org `MGASetClippingRectangle` 형식), `SRCORG=0`, `DSTORG=0`,
`DWGCTL=0x000C7804`.

**타임아웃 시**: 로그만 남기고 즉시 중단한다. wedge 가능성이 있는 FIFO에
"정리" write를 시도하지 않는다.

### 3-1. 캐시 일관성 — readback은 uncached 별칭으로만

프레임버퍼는 `mapFrameBufferAtPhysicalAddress:length:`로 매핑돼 있으나 **그 매핑의
읽기 캐시 속성은 어디에서도 증명된 바 없다.** CPU write가 스캔아웃에 반영되는 것은
FB clear로 실증됐지만(≥write-through), 문제는 반대 방향이다: **엔진이 VRAM에 쓴 값을
CPU가 읽을 때** 캐시된 sentinel이 반환되면 거짓 실패(또는 거짓 성공)가 된다.
`volatile`은 캐시 일관성을 만들지 않는다.

해결: 테스트 블록에 대해서만 `IOMapPhysicalIntoIOTask`로 **별도 uncached 별칭**을
만들어 sentinel 기록과 readback을 전부 그 포인터로 수행한다. 이 API가 uncached
매핑을 준다는 것은 **우리 자신의 H1 S2에서 실증**됐다 — BAR1을 이 API로 매핑해
`VCOUNT`를 읽었을 때 단조증가했고, 캐시된 매핑이었다면 상수가 반환됐을 것이다.

매핑 범위: `[startPixel*4, (endPixel+1)*4)`를 페이지 정렬로 확장. 1024×768×32에서
물리 `frameBufferPhysical + 4 MiB`부터 256 KiB.

## 4. 주소 산술과 경계 증명

목적지 픽셀 주소 = `YDSTORG + y*PITCH + x` (`YDSTORG=0`, `PITCH=stridePixels`).

### 4-0. `stridePixels == width` 증명 (가정 아님)

`PITCH`와 CPU 행 주소지정은 **실제 프로그램된 CRTC 스트라이드** 하나를 써야 하며,
가시 `width`로 대체하려면 둘이 같음을 증명해야 한다. 우리 드라이버의 경우:

`osmgaComputeCRTC`가 `wd = width >> (4 - bppShift)`를 계산해 CRTC offset(`crtc[19]`)과
CRTCEXT0 상위비트(`ext[0] = (wd & 0x300) >> 4`)에 넣는다. MGA power-graphics 모드에서
offset 단위는 16바이트이므로 스캔아웃 행 바이트 = `wd * 16`.

1024×768×32(`bppShift=2`): `wd = 1024>>2 = 256` → 행 바이트 = `256*16 = 4096`
= `1024 픽셀 × 4바이트`. **패딩 없음, 스트라이드 = width.** ✅

스캔아웃 시작주소도 0이다: `crtc[12]`/`crtc[13]`(start address high/low)은 배열
0-초기화 후 설정하지 않으므로 0이고, `ext[0]`의 start-address 비트도 0이다.
→ 가시영역은 물리 0부터 `width*height*4`까지 연속. §4 표의 전제가 성립한다.

테스트 블록을 **가시영역 아래 256행 가드** 뒤에 둔다:
```
testY = height + 256
testX = 0,  testW = 64,  testH = 64
startPixel = testY * width
endPixel   = (testY + testH - 1) * width + (testW - 1)
byteEnd    = (endPixel + 1) * 4
런타임 가드: byteEnd <= 16 MiB 가 아니면 테스트 생략
```

| 모드 | 가시 끝 | 블록 시작 | 블록 끝 | 8MiB 이내 | 16MiB 이내 |
| --- | --- | --- | --- | --- | --- |
| 1024×768×32 | 3.00 MiB | 4.00 MiB | 4.25 MiB | ✅ | ✅ |
| 1280×1024×32 | 5.00 MiB | 6.25 MiB | 6.56 MiB | ✅ | ✅ |
| 1600×1200×32 | 7.32 MiB | 8.89 MiB | 9.27 MiB | ❌ | ✅ |

가시영역과 테스트 블록 사이 간격은 항상 256행(1024×768에서 1 MiB).

### ⚠ 탑재 VRAM 미측정 — 첫 테스트는 1024×768×32 한정

**우리는 실제 탑재 VRAM을 한 번도 측정한 적이 없다.** BAR0 aperture는 32 MiB로
실측됐지만(H1 S1) 이는 주소창 크기일 뿐이고, populated VRAM 측정(S3)은 보류
상태다. 16 MiB는 operator가 확정한 보수적 배치값일 뿐 측정값이 아니다.

실증된 하한은 하나뿐이다: **1600×1200×32 FB clear가 0~7.32 MiB를 기록하고 화면이
정상이므로 최소 7.32 MiB는 실재한다.**

따라서 1600×1200 모드에서 테스트 블록(8.89~9.27 MiB)은 **미실증 영역**이다.
카드가 8 MiB 탑재라면 그 주소는 미탑재 영역이고, **주소가 wrap되면 0.89 MiB —
가시 스캔아웃 한복판**에 떨어진다.

**`YTOP`/`YBOT` 클립은 이걸 막지 못한다.** 클립은 *픽셀 포인터 값*(2,329,600)을
검사하는데 그 값 자체는 창 안에 있고, 물리적 aliasing은 메모리 계층에서 일어나기
때문이다. §5의 "하드웨어 봉쇄"는 **산술 실수에는 유효하나 VRAM aliasing에는
무효**다.

→ **첫 실기 테스트는 반드시 `1024×768×32`에서 수행한다.** 블록이 4.00~4.25 MiB로
실증된 7.32 MiB 안쪽에 안전히 들어간다. 다른 해상도로의 확대는 populated VRAM을
별도로 측정한 뒤에만 허용한다.

**하드웨어 강제 봉쇄(한정적)**: 클립 레지스터를 테스트 블록으로 좁힌다 —
X.Org의 개방값(`CXBNDRY=0xFFFF0000`, `YTOP=0`, `YBOT=0x7FFFFF`) 대신:
```
CXBNDRY = ((testX + testW - 1) << 16) | testX     (오른쪽 inclusive)
YTOP    = testY * stridePixels                     (행 시작 포인터)
YBOT    = (testY + testH - 1) * stridePixels       (행 시작 포인터)
```
Y는 행 포인터, X는 CXBNDRY가 각각 봉쇄한다. 위 `endPixel`(행 끝까지 포함)은
**메모리 span/매핑 범위 계산용**이지 YBOT 값이 아니다 — 혼동 주의.

이는 **좌표/산술 실수**를 봉쇄하지만, 위에 적었듯 VRAM aliasing은 봉쇄하지 못한다.

## 5. 안전 분석

| 위험 | 완화 |
| --- | --- |
| 무한 대기 → 머신 행 | 모든 대기에 스핀 상한(PLL lock search와 동일 방식). 타임아웃 시 **EXEC 미실행**하고 로그 남기고 중단 |
| 잘못된 주소 → 스캔아웃 훼손 | §4 산술 + 런타임 가드 + **YTOP/YBOT 하드웨어 클립** 3중 |
| 잘못된 DWGCTL → 엔진 폭주 | X.Org 유도값 그대로, block 모드 배제. YTOP/YBOT가 여전히 봉쇄 |
| 평상시 부팅 영향 | config 플래그 기본 **OFF** — 평시엔 엔진 레지스터를 한 번도 안 건드림 |
| 실패 시 복구 불가 | `enterLinearMode` **끝**(화면 기동·네트워크 up 이후)에서 실행 → 최악에도 "화면은 죽어도 telnet 생존"(기검증). 그 위는 R5-VGA 리부트 |
| 엔진 상태 잔류 | 드라이버 평상 동작은 엔진을 쓰지 않음(WindowServer는 CPU로 FB 기록). 드로잉 레지스터는 스캔아웃 레지스터와 별개. 단 **다음 엔진 사용자는 자기 상태를 전부 초기화해야 한다**(좁혀둔 YTOP/YBOT 포함) |
| FIFO 오버런 | 초기화 13 writes 앞에 여유 ≥13, 채우기 3 writes 앞에 ≥3 확인. idle 상태만으로는 FIFO 수용을 보장하지 않음 |
| 캐시된 readback → 거짓 판정 | sentinel/readback을 **uncached 별칭 매핑**으로만 수행 (§3-1) |

## 6. 검증 방법

### 6-1. 하드웨어 이전 (호스트) — ✅ 완료
산술을 호스트에서 그대로 컴파일해 덤프·확인했다(PLL 때와 동일 방식).
- `DWGCTL` 계산값 `0x000C7804` — 의도값과 **일치**
- `MACCESS`: 8bpp=0, 16bpp=1, 32bpp=2, depth15+16bpp=`0x80000001` — 표와 일치
- 5개 실모드 전부 가시영역 미침범, 가드 간격 ≥0.62 MiB, 16 MiB 이내
- **이 과정에서 §4의 VRAM 미측정 위험을 발견**해 첫 테스트를 1024×768×32로 한정

미검증으로 남긴 것(정직 기록): 16 MiB 초과 가드의 **거부 경로는 실제로 발동시켜
보지 못했다** — 우리 모드 테이블 최대치(1600×1200)에서도 9.27 MiB라 가드에 걸리지
않기 때문이다. 이 가드는 순수 방어용이다.

### 6-2. 타깃 빌드
클린 빌드, 경고 0, `nm -u | grep -i osmga` 미해결 심볼 0.

### 6-3. 실기 1회 부팅 (플래그 ON)
성공 판정은 **전부** 충족해야 한다:
1. `engine idle` 대기가 타임아웃 없이 통과
2. `fifoDepth`가 0이 아닌 합리적 값
3. sentinel 기록·확인 통과 (readback 경로 자체 건전성)
4. EXEC 후 `engine idle` 복귀가 타임아웃 없이 통과
5. **4096픽셀 전부 `0xDEADBEEF`**, checksum 일치
6. **가시화면 무변화** (operator 육안 확인) — 변하면 즉시 실패
7. telnet 생존, 로그 회수 가능

실패 시: 불일치 픽셀 앞 N개의 실제 값을 로그로 남겨 진단에 쓴다.

### 6-4. 복구
플래그를 되돌리거나(`"Storm 2D Test" = "No"`), 부팅 불가 시 R5-VGA(Active
Drivers에서 드라이버 제거 후 콜드 리부트).

## 7. 이 단계가 증명하지 않는 것

- 성능, DMA, 인터럽트, 다중 클라이언트 안전성
- 가시 스캔아웃으로의 BITBLT 안전성(2단계에서 별도 검증)
- 3D/WARP 경로의 존재 여부
- 다른 심도(8/16bpp)에서의 엔진 동작 — 첫 테스트는 32bpp 한정


## 8. codex 교차검토 결과 (2026-08-19)

codex 답변을 **그대로 채택하지 않고** X.Org 원본과 하나씩 대조했다.

### 채택한 정정 (원본으로 확인됨)

| 지적 | 원본 대조 | 조치 |
| --- | --- | --- |
| `YBOT`은 행 시작 포인터(`y2*stride`)이며 내 `+testW-1`은 모델 이탈 | ✅ `MGASetClippingRectangle`이 `y2*displayWidth + YDstOrg` 사용 | §3/§4 수식 정정 |
| `CXBNDRY`는 inclusive, `FXBNDRY`는 exclusive | ✅ `(x2<<16)|x1` vs `((x+w)<<16)|x` | 문서화 |
| `CXBNDRY=0xFFFF0000`은 X 봉쇄가 전무 | ✅ 개방값 그대로였음 — **내 계획의 실제 구멍** | 테스트 블록으로 좁힘 |
| 초기화 13 writes도 FIFO 게이팅 필요 | ✅ X.Org는 모든 배치 앞에 `WAITFIFO(n)` | 여유 ≥13 대기 추가 |
| 타임아웃 시 정리 write 금지(FIFO wedge 가능) | 합리적 | §3에 명시 |
| readback 캐시 일관성 — `volatile`로는 불충분 | 헤더에 캐시 모드 미문서화, **우리 H1 S2가 `IOMapPhysicalIntoIOTask`=uncached를 실증** | §3-1 uncached 별칭 매핑 도입 |
| `PITCH`를 실제 CRTC 스트라이드에서 유도할 것 | 우리 코드로 `stride==width` 증명 가능 | §4-0 증명 추가 |

### 확인된 항목 (변경 없음)

- `DWGCTL = 0x000C7804` — codex가 비트별로 재계산해 일치 확인. `ARZERO`/`SGNZERO`
  덕분에 AR/SGN 레지스터 프로그래밍 불요. `TRANSC` 추가 금지.
- `MGAOPM_DMA_BLIT`는 DMA를 **시작하지 않는다** — DMA 사용 시의 해석 타입 선택일
  뿐이고 직접 MMIO write는 PIO로 남는다. `DMA_GENERAL`이 더 안전하지 않다.
- `SRCORG=DSTORG=0` — 이 단색 채우기에는 정확. 단 일반화 금지(0이 아닌 원점은
  정렬 제약 있음).
- `MACCESS=2` 단독으로 32bpp에 충분. dither/fog/TLUT/Z 상태를 상속하지 않고
  명시적으로 0으로 만드는 것이 오히려 바람직.
- `YDSTLEN+0x100` 단일 write가 YDSTLEN 기록과 EXEC를 동시에 수행. 순서는
  `FCOL` → `FXBNDRY` → `YDSTLEN+EXEC` 마지막.
- 소프트 리셋(`MGAREG_Reset`) 불필요 — idle 엔진 + 전체 상태 명시 기록으로 충분.
  `DWGSYNC`도 이 PIO 단발 테스트에는 전제조건 아님.
- `FXBNDRY + YDSTLEN`만으로 TRAP 채우기 충분(`CXLEFT/CXRIGHT/FXLEFT/FXRIGHT/YDST`는
  다른 프리미티브용). block 모드 미사용이므로 타일/뱅크 정렬 제약 없음.
- 엔진 레지스터는 CRTC 스캔아웃 상태와 완전 분리 — 잔류 상태가 화면에 영향 없음.

### 검증 실패 — 채택하지 않음

- **"클리핑 disable은 `DWGCTL.CLIPDIS`(bit 31)"**: X.Org `mga_reg.h`에 `CLIPDIS`
  정의가 **없다**. `0x80000000`은 `MGAMAC_DIT555`(MACCESS의 dither-555 비트)로만
  존재한다. Matrox 하드웨어 문서상 사실일 수는 있으나 **우리는 확인할 수 없었다.**
  결론에는 영향 없음 — 우리 `DWGCTL`은 어느 해석이든 bit31이 0이다.
  (부수 소득: `MGAMAC_DIT555= 0x80000000` 확인으로 depth 15에서 `MACCESS |= 1<<31`이
  dither-555 설정임이 확인돼 §2 유도가 의미상 옳음이 뒷받침됐다.)

### 우리가 독자적으로 발견한 것 (codex 미지적)

- **탑재 VRAM 미측정 위험** — §4 참조. codex는 "physically present VRAM 내부인지
  증명하라"고 일반론으로 언급했으나, 1600×1200 모드에서 블록이 실증 하한(7.32 MiB)을
  넘는다는 구체적 사실과 wrap 시 가시화면 침범 가능성은 우리 호스트 검증에서
  발견했다. → 첫 테스트를 1024×768×32로 한정.

## 9. ✅ 실기 결과 — PASS (2026-08-19)

1024×768×32, `"Storm 2D Test" = "Yes"`, 콜드 부팅 1회.

```
S1 Storm 2D test ENABLED by configuration
S1: begin 1024x768 stride=1024 block y=1024 px 1048576..1113151 bytes 4194304..4452607
S1: engine idle, fifo depth=16
S1: sentinel ok
S1: engine state set (dwgctl=c7804 maccess=02 opmode=4)
S1: fill issued
S1: PASS -- engine filled 4096 px with deadbeef, checksum dbeef000
S1: end
```

### 판정 기준 대조 (§6-3의 7개 전부 충족)

| # | 기준 | 결과 |
| --- | --- | --- |
| 1 | engine idle 대기 타임아웃 없음 | ✅ `engine idle` |
| 2 | fifoDepth 합리적 | ✅ `16` — H1 S2의 `FIFOSTATUS=0x210`(하위바이트 16)과 **정확히 일치** |
| 3 | sentinel 기록·확인 통과 | ✅ `sentinel ok` |
| 4 | EXEC 후 idle 복귀 타임아웃 없음 | ✅ (실패 시 로그가 남는데 없음) |
| 5 | 4096픽셀 전부 `0xDEADBEEF` + checksum 일치 | ✅ 아래 독립 검증 |
| 6 | 가시화면 무변화 | ✅ operator 육안 확인 — 깨진 부분 없음 |
| 7 | telnet 생존·로그 회수 | ✅ |

**체크섬 독립 검증**: 로그값을 그대로 믿지 않고 재계산했다.
`0xDEADBEEF × 4096 mod 2³² = 0xDBEEF000` — 로그와 일치.
PASS 조건은 checksum 일치 **와** 불일치 픽셀 0을 모두 요구하므로, 4096픽셀
전부가 엔진이 기록한 값이다.

계획 값과의 대조: `dwgctl=c7804` = `0x000C7804`(IOLog가 선행 0 생략),
`maccess=02` — §2 유도값 그대로. `opmode=4`는 읽은 값의 `~0x30000` 잔여가 0이고
`DMA_BLIT(0x04)`만 남은 결과.

### 이로써 확정된 것

**우리가 X.Org에서 유도한 Storm 시퀀스에 G450 엔진이 실제로 반응한다.**
그 전까지는 전부 "문서상 그럴 것"이었다. §7의 미증명 항목(성능, DMA, 가시
스캔아웃 BITBLT, 3D, 다른 심도)은 여전히 미증명이며 각 후속 단계에서 다룬다.

### 구현 위치

`OpenStepMGAReplacementDisplay.m`의 `-runStormLivenessTest`
(레지스터 정의는 `MGA_DWGCTL` 이하, bounded wait은 `osmgaStormWaitIdle` /
`osmgaStormWaitFifo`). `enterLinearMode` 말미에서 opt-in 호출.
