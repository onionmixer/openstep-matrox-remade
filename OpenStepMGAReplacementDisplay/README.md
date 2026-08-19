# OpenStepMGAReplacementDisplay

Matrox MGA G450(PCI `102b:0525`, rev ≥ 0x80) primary head용 OPENSTEP 4.2
DriverKit 디스플레이 드라이버. `IOFrameBufferDisplay` 서브클래스이며 **실기에서
화면을 소유하고 동작 중이다.**

## 무엇을 하는가

| 기능 | 상태 |
| --- | --- |
| BAR0 프레임버퍼 매핑 (`mapFrameBufferAtPhysicalAddress:length:`) | 동작 |
| BAR1 MMIO 매핑 (`IOMapPhysicalIntoIOTask`) | 동작 |
| G450 전체 리니어 모드셋 (VGA + 확장 CRTC + RAMDAC + 픽셀 PLL) | 동작 |
| 해상도 5종 × 픽셀 포맷 5종 (`Display.modes` 25개 항목) | 동작 |
| `setTransferTable:count:` 팔레트 (8bpp PseudoColor, 그레이스케일) | 동작 |
| Storm 2D 엔진 단색채우기 / BITBLT | 실증 완료(기본 비활성) |
| 오프스크린 VRAM의 유저 태스크 매핑(cdevsw `d_mmap`) | 실증 완료(기본 비활성) |
| 3D / WARP / DMA | **미착수** (`../docs/S5_HW3D_DMA_FEASIBILITY.md`) |

지원 해상도는 640×480, 800×600, 1024×768, 1280×1024, 1600×1200 (전부 VESA DMT
60 Hz). 픽셀 포맷은 `RGB:888/32`, `RGB:555/16`, `RGB:256/8`, `BW:8`, `BW:4`.

## 설정 (`Default.table` / `Instance0.table`)

| 키 | 기본값 | 의미 |
| --- | --- | --- |
| `Display Mode` | `Height: 768 Width: 1024 Refresh: 60Hz ColorSpace: RGB:888/32` | 해상도 + 픽셀 포맷 선택 |
| `MGA Memory Size` | `16` | VRAM MiB (보수적 고정값) |
| `Storm 2D Test` | `No` | `Yes`면 `enterLinearMode` 끝에서 엔진 자체시험 실행 |
| `VRAM Mmap` | `No` | `Yes`면 오프스크린 VRAM 창을 캐릭터 디바이스로 공개 |

`Storm 2D Test`가 `No`인 기본 부팅은 **드로잉 엔진 레지스터를 한 번도 쓰지
않는다.** `VRAM Mmap`을 켜면 클라이언트 매핑이 드라이버 수명보다 오래
남으므로 이후 드라이버를 언로드해서는 안 된다
(`../docs/S4A_VRAM_MMAP_PLAN.md`).

`Memory Maps` / `I/O Ports` / `FB Address`는 의도적으로 비어 있다. 부트 콘솔과
충돌할 수 있는 레거시 자원을 예약하지 않고, BAR0/BAR1을 PCI config에서 직접
읽어 매핑한다.

## 계측

`getIntValues:forParameter:"OSMGAStats"`가 19개 카운터를 반환한다(정확 카운트
계약 — `*count`가 정확히 19가 아니면 거부). 유저스페이스 리더는
`../test/openstep-mga-stats-probe.m`이며 `-lDriver`로 빌드한다.

`IO_DISPLAY_CAN_BLIT`은 **일부러 광고하지 않는다.** 실측 결과 WindowServer는
`IODisplayDoBlit`을 보내지 않는다(`../docs/S3B_PREP_INSTRUMENTATION_PLAN.md`).

## 빌드 / 설치

빌드와 설치는 반드시 저장소 루트의 스크립트로만 한다:

```
tools/build-matrox-driver.sh      # 타깃에서 릴로커터블 빌드
tools/install-matrox-driver.sh    # 번들 설치 (0바이트 번들 함정 회피)
```

빌드 자체는 실행 중인 디스플레이를 바꾸지 않는다. 드라이버가 화면을 소유하는
것은 `System.config`의 `Active Drivers`에 선택되고 콜드 부팅한 뒤부터다.

## 설계 문서

`../docs/` 아래. 시작점은 `../docs/TEST_STATUS.md`(현재 상태 원장)이고,
하드웨어 세부는 `R6_G450_FULL_LINEAR_MODESET.md`,
`R6_G450_PIXEL_PLL_ALGORITHM.md`, 엔진 작업은 `S1`~`S4A`, 3D 조사는 `S5`.
