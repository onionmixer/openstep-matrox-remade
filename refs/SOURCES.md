# 참고 자료와 사용 경계

## OPENSTEP

- NeXT DriverKit Reference — display driver, PCI, kernel I/O thread, interrupt,
  framebuffer mapping, MiG/LKS의 1차 문서.
- 실기 `/NextDeveloper/Examples/DriverKit` — API 사용 방식의 정본. 제3자
  자료이므로 이 저장소에 복사하지 않는다.

### 로컬 원전과 NFS 동기화

- 조사용 로컬 사본은 `../ref/openstep/`에 둔다. 이것은 프로젝트 코드가 아닌
  원전 reference tree다.
- 실기에만 있는 원전이 필요할 때에는 target source path를 보존하여
  `/ndrv/ref/openstep/` 아래에 복사할 수 있다. target에서 복사 전후 `sync;
  sync`를 실행하고, host에서 목록·크기를 검증한 뒤에만 검색 결과를 근거로
  사용한다.
- 2026-08-18 확인: 실기의 NextDev LKS 문서·DriverKit headers와 S3/QVision/
  CirrusLogicGD542X/AMDPCSCSIDriver/ProAudioSpectrum16 예제는 존재한다.
  NextDev 문서가 언급한 `Examples/MiG`, `Log`, `ServerVsHandler`는 현재
  설치본에서 확인되지 않았다. 없는 자료를 추정하여 code로 재작성하지 않는다.

## MatroxMGA

- Mirko Viviani의 공개 `MatroxMGA-1.0.tgz` — 현재 실기의 display driver와
  같은 크기·시대의 binary distribution. 분석용으로만 사용한다.
- archive에는 source가 없다. binary, disassembly, recovered pseudocode,
  object code는 저장소에 넣지 않는다.
- 새 구현은 이 binary의 코드를 복제하지 않는다. 관찰 결과는 공개 API와
  외부 동작 수준의 명세로만 기록한다.

## 공개 MGA 구현

- Xorg `xf86-video-mga`: mode setting, 2D/DDX 및 historical DRI 연결 구조의
  참고.
- FreeBSD legacy DRM: generic DRM core와 `mga_dma`, `mga_irq`, `mga_state`,
  `mga_warp` 등 kernel 기능 분리의 참고.
- Linux legacy DRM/Matrox framebuffer: register programming과 device behaviour의
  참고.

### R2.1 — 자동 식별/VRAM probe 자료

- FreeBSD ports `x11-drivers/xf86-video-mga` Makefile — FreeBSD의 MGA display
  지원이 independent kernel implementation이 아니라 X.Org MGA DDX port임을
  확인한다. <https://cgit.freebsd.org/ports/tree/x11-drivers/xf86-video-mga/Makefile>
- X.Org `xf86-video-mga-2.0.0` official archive — `mga_driver.c`의 configured
  `VideoRam` precedence 및 map/write/read `MGACountRam()` 동작, `mga_bios.c`의
  G450/G550 PInS parser를 조사했다. VRAM total probe는 passive read가 아니므로
  code를 copy하거나 current target에서 실행하지 않는다.
  <https://xorg.freedesktop.org/archive/individual/driver/xf86-video-mga-2.0.0.tar.gz>
- FreeBSD `stable/7` legacy `mga_drv.c`/`mga_dma.c` — PCI G450 parent bridge
  topology check와 userspace-provided `sgram`/framebuffer layout을 kernel DMA
  init에 전달하는 구조의 조사 자료다. capacity detector로 사용하지 않는다.
  <https://cgit.freebsd.org/src/plain/sys/dev/drm/mga_drv.c?h=stable/7>
  <https://cgit.freebsd.org/src/plain/sys/dev/drm/mga_dma.c?h=stable/7>
- Linux MGA public UAPI — `DRM_MGA_INIT` ABI가 chipset/sgram/offset/pitch를
  userspace caller에게서 받음을 확인하는 cross-check다.
  <https://github.com/torvalds/linux/blob/v6.2/include/uapi/drm/mga_drm.h>
- Linux `mgag200` VRAM probe — map/write/read/restore test라는 safety comparison
  only다. G400/G450 source 혹은 OpenStep implementation source가 아니다.
  <https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/mgag200/mgag200_drv.c>

세부 판정과 OpenStep safety boundary는
`docs/R2_AUTODETECTION_RESEARCH.md`에 기록한다.

각 파일을 코드에 반영하기 전에는 해당 파일의 license와 attribution 조건을
확인한다. GPL 자료는 동작/하드웨어 순서 이해용으로만 사용하며, 이 프로젝트의
배포 코드로 복사하지 않는다.

### R6 — Storm 2D source audit

- local official X.Org `xf86-video-mga-2.0.0` archive — `mga_storm.c`의
  synchronous engine init/FIFO/idle/solid-copy path, `mga_reg.h`의 register
  group, `mga_macros.h`의 DRI-conditional quiescence를 analysis-only로
  대조했다. `COPYING`의 XFree86/Open Group permissive terms는 확인했으나
  source/macro/register sequence를 project code로 복사하지 않는다. 상세
  OpenStep boundary는 `docs/R6_STORM_2D_SOURCE_AUDIT.md`에 기록한다.
- local official X.Org `xf86-video-mga-2.0.0` archive — `mga_g450pll.c`의
  primary/secondary PLL selection, candidate/lock handling과 `mga_dacG.c`의
  DAC clock transaction을 analysis-only로 대조했다. source code/constants를
  project에 복사하지 않으며, board/head/timing evidence 전에는 live candidate
  search를 하지 않는다. 상세 boundary는 `docs/R6_G450_PLL_MODE_SOURCE_AUDIT.md`에
  기록한다.

### PCI subsystem과 VRAM 식별 자료

- pciutils `pci.ids`의 `102b:0d43` catalogue entry — `Millennium G450 32Mb
  Dual Head PCI`라는 **board label**의 공개 근거다. physical memory controller
  probing 결과가 아니므로 target의 VRAM total/type 단독 증거로 사용하지 않는다.
  분석 당시 사용한 immutable mirror는
  <https://fuchsia.googlesource.com/third_party/github.com/pciutils/pciids/%2B/d331f4cd7b37e4c10fec9c410ed85fc27801af93/pci.ids>다.
- Xorg `mga(4)` — G450 family handling과 subsystem ID 기반 SDRAM heuristic의
  공개 참고다. target의 정확한 memory type/size를 확정하는 자료로 사용하지
  않는다. <https://xorg.freedesktop.org/archive/X11R7.5/doc/man/man4/mga.4.html>
- Matrox Millennium G450 User Guide — PCI G450 product family의 16/32 MB DDR
  variant와 360/230 MHz RAMDAC envelope을 제시한다. target board의 exact physical
  profile은 확정하지 않는다. <https://video.matrox.com/en/media/2090/download>
- Matrox G450 2002 product sheet mirror — PCI 16 MB DDR와 PCI 32 MB DDR를
  별 product로 열거하는 family/product-line cross-check다. exact board identity의
  증거가 아니다. <https://www.dosdays.co.uk/media/matrox/g450_chip_spec.pdf>
- PCI hardware archive — `102b:0525` / `102b:0d43` tuple을
  `G45FMDVP32DSF`, 32 MB DDR PCI candidate와 연결한다. target physical board의
  직접 증거는 아니며, R2 candidate comparison에만 사용한다.
  <https://www.pc-schnulli.de/hardw/gkpci/magkpci.html>
- G45FMDVP32DSF part-number product records — PCI/32 MB/G450 cross-check다.
  target board P/N을 대체하지 않는다.
  <https://www.bhphotovideo.com/c/product/819813-REG/Matrox_G45FMDVP32DSF_G450_PCI_PCI_X_4_X.html>
  <https://www.bestbuy.com/site/matrox-g450-graphic-card-32-mb-ddr-sdram-pci/4314186.p?skuId=4314186>
- target P1 PCI header, existing `Instance0.table`, DriverKit getter result의
  대조는 `docs/P1_SUBSYSTEM_MEMORY_RECONCILIATION.md`에 기록한다.

### DDC/EDID 조사 자료

- 로컬 분석용 Xorg `xf86-video-mga-1.6.5.tar.gz` — SHA-256
  `ae1ddf8d4780f6c5313fe0d23826e302fc65556a6ebcaca751c755c9761928ff`.
  `src/mga_dacG.c`/`src/mga_driver.c`의 G-series DDC1/DDC2B I2C path와
  fallback 구조를 조사했다. archive `COPYING`의 XFree86/Open Group 계열
  조건을 확인했지만, 현 단계에서는 code를 복사하지 않는다.
- OPENSTEP `IOFrameBufferDisplay.h` — `Display Mode` 기반의 fixed mode
  selection과 mode lifecycle의 1차 근거다.
- `MatroxMGA-1.0`의 G450 `.table`/`.modes` — 기존 수동 mode fallback의
  외부 configuration behaviour 확인용이다. binary/source code의 구현 근거로
  사용하지 않는다.

### R3 standard timing comparison

- local Linux UAPI `v4l2-dv-timings.h` —
  `V4L2_DV_BT_DMT_1600X1200P60` entry를 current 1600×1200@60 geometry와
  비교할 standard timing shape의 public reference로만 읽었다. project는
  Linux interface/code를 호출하거나 복사하지 않으며, target original mode가
  이 DMT shape라는 결론에도 사용하지 않는다. 상세 경계는
  `docs/R3_DMT_TIMING_CANDIDATE_AUDIT.md`에 기록한다.
