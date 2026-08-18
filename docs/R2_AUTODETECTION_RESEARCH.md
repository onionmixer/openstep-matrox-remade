# R2.1 — BSD/Linux MGA와 기존 OPENSTEP Driver의 자동 식별 조사

기준일: 2026-08-18  
상태: **source/offline binary 조사 완료 — target hardware action 없음**

## 질문과 결론

질문은 기존 `MatroxMGA`가 카드에서 board/VRAM 정보를 자동으로 얻는 경로가
있는지, 그리고 BSD/Linux의 MGA 구현에서 그것을 안전하게 참고할 수 있는지다.

결론은 다음과 같다.

1. **어댑터 식별은 자동이다.** 공개 OPENSTEP 배포본의 Help은 PCI driver가
   target adapter를 자동 검출한다고 명시한다. FreeBSD legacy DRM도 PCI ID로
   MGA를 probe하고, `0525` PCI G450은 parent HiNT `3388:0021` bridge를 보아
   PCI-versus-AGP topology를 구분한다.
2. **정확한 VRAM total의 자동 판별 경로는 존재하지만 passive read가 아니다.**
   X.Org MGA의 `MGACountRam()`은 G400 계열에서 framebuffer를 map하고 각
   candidate boundary에 `0xAA`를 **쓴 뒤** read/compare한다. linear mode와
   CRTC register도 일시 변경한다. 현재 WindowServer를 구동하는 original
   `MatroxMGA`와 병행해 실행할 수 있는 read-only probe가 아니다.
3. **기존 OPENSTEP driver에는 hardware-sensitive detection path가 있다.**
   target-sum-matched binary의 Ghidra static audit은 `MGAProbe` 뒤
   `MGAReadBios`/`MGACountRam:` selector를 참조하는 code와 PCI/MMIO helper call을
   확인했다. Help의 최대 32 MB memory envelope도 보존한다. 그러나 source가
   없고 current boot를 instrument하지 않았으므로, 이 사실만으로 현재
   `Instance0.table` boot에서 정확히 어느 branch가 실행됐는지는 선언하지 않는다.
4. **현재 boot의 `MGA Memory Size=16`은 자동 측정 증거가 아니다.** 공개
   README/release notes는 이 key를 “카드의 메모리 양을 driver에게 알려 주는”
   override로 정의한다. X.Org도 configured `VideoRam` 값이 있으면
   `MGACountRam()`보다 먼저 그것을 사용한다. 따라서 현재 table의 16 MiB를
   physical 16 MiB로 승격할 수 없다.
5. **FreeBSD/Linux legacy DRM은 capacity detector가 아니다.** DRM init ABI는
   userspace DDX가 이미 정한 `sgram`, framebuffer/front/back/depth/texture
   offsets와 pitches를 kernel에 전달한다. kernel은 mapping/DMA/IRQ/engine
   ownership을 수행할 뿐 VRAM capacity/type을 독립적으로 판정하지 않는다.

그러므로 이 조사로 R2/G2가 통과하지는 않는다. 다만 향후 replacement-only
test boot에서 어떤 동작을 절대 현 owner와 겹치면 안 되는지가 명확해졌다.

## 근거별 상세

### 1. 기존 OPENSTEP 배포본

공개 `MatroxMGA-1.0`의 Help은 “This PCI bus device driver automatically
detects the target adapter”라고 하고, 지원 memory envelope을 up to 32 MB로
기록한다. 같은 배포본의 README와 release notes는 Expert setting
`"MGA Memory Size" = "size in MB"`를 card memory amount를 driver에게
알리는 값으로 정의한다.

이는 다음을 분리한다.

| 항목 | 공개 배포본이 말하는 것 | 현재 target에 대한 증명 범위 |
| --- | --- | --- |
| adapter | PCI target adapter auto-detect | MGA function matching 가능 |
| memory key | Configure가 driver에 MB를 전달하는 override | current physical total/type 불명 |
| static binary audit | `MGAProbe` 뒤 config override 및 `MGAReadBios`/`MGACountRam:` selector path | original lifecycle/override behavior; current boot branch 및 physical total은 미확정 |

original binary의 local static-analysis artifact identity, function-level xref,
configuration override predicate와 hardware-sensitive boundary는
`R2_ORIGINAL_BINARY_CONFIGURATION_AUDIT.md`에 제한적으로 기록한다. 이 분석은
새 driver에 address/register sequence를 복사하거나 original hardware path를
replay할 근거가 아니다.

### 2. X.Org MGA DDX의 실제 VRAM count 방식

FreeBSD가 배포하는 `x11-drivers/xf86-video-mga`는 X.Org MGA DDX package다.
그 upstream 2.0.0 source의 precedence는 다음과 같다.

1. config의 `VideoRam`이 non-zero이면 그 값을 사용한다.
2. fbdev일 때만 fbdev가 publish한 size를 사용한다.
3. 그 외에는 `MGACountRam()`을 호출한다.

`MGACountRam()`의 G400/G450 family 경로는 disabled `#if 0` PCI-option
memory-config branch를 남겨 둔다. comment는 BIOS 초기화 뒤 그 값이 arbitrary
해서 8/16 MiB G200에서 같게 보였다고 적는다. 따라서 해당 source는 option
register bitfield를 trustworthy capacity source로 쓰지 않는다.

대신 function은 candidate framebuffer length를 map하고, non-G200SE path에서
2 MiB 간격의 boundary byte에 `0xAA`를 쓴 뒤 read/compare한다. `CRTCEXT3`의
linear-mode bit를 set하고 CRTC index write로 cache flush한 뒤 state를 restore한다.
이 방식은 **VRAM write probe**다. probe bytes를 원복하지 않으므로 existing
scanout/cursor/hidden allocation과 공유한 live card에서 “read-only”라고 취급할
수 없다.

BIOS PInS parser는 G450/G550 version 5를 지원하며 pixel/system/video clock,
memory clock, PLL reference, host interface를 얻는다. examined parser에는
VRAM total을 output하는 field가 없다. 또한 target P1.4 PCI capability walk는
VPD capability absent를 이미 확인했고, option-ROM mapping은 현 gate에서 금지다.

### 3. FreeBSD legacy DRM의 범위

FreeBSD `stable/7` legacy `sys/dev/drm/mga_drv.c`와 `mga_dma.c`를 조사했다.
이는 X.Org DDX가 already-known display layout을 보내면 kernel이 DMA/IRQ/MMIO를
소유하는 DRI kernel side다.

- `mga_probe()`는 generic `drm_probe()`에 MGA PCI ID list를 전달한다.
- G450 PCI/AGP ambiguity는 `0525` function의 parent bridge (`3388:0021`)를
  확인한다. 이 tree는 그런 PCI G450을 AGP requirement와 맞지 않아 support하지
  않는다고 명시한다.
- `DRM_MGA_INIT`의 `drm_mga_init_t`에는 `chipset`, `sgram`, `fb_cpp`,
  front/back/depth offsets/pitches, texture offsets/sizes가 들어 있다.
- `mga_do_init_dma()`는 그 caller-provided values를 `dev_priv`에 복사하고
  map/DMA setup을 시작한다. VRAM size/type discovery routine은 아니다.

따라서 BSD kernel source에서 얻을 수 있는 것은 PCI topology handling과
sole-owner resource sequencing이며, R2 physical profile의 read-only measurement
대체물은 아니다. Linux legacy MGA DRM의 public UAPI도 같은 init structure를
사용한다. modern `mgag200`의 VRAM probe도 map 후 write/read/restore test라서
현재 G400/G450 target의 passive inspection solution이 아니다.

## OpenStep 적용 결정

| 후보 | 얻는 정보 | current original owner와 병행 가능 | R2 evidence로 채택 |
| --- | --- | --- | --- |
| PCI header / bridge topology | vendor/device/revision, PCI topology | 예, bounded config read | family/topology 보조 근거만 |
| existing table / `MGA Memory Size` | configured minimum/override | 예, file read | 불가 |
| option-ROM PInS | clock/interface envelope | 아니오, ROM access 필요 | 현 단계 불가; total도 미제공 |
| X.Org-style VRAM count | usable alias boundary size | 아니오, map + register + VRAM writes | 현 단계 금지 |
| FreeBSD DRM init | userspace-provided layout | 아니오, DMA/MMIO owner path | detector 아님 |
| physical marking + exact source | board/VRAM type/total | shutdown 상태 | R2의 현재 승인 경로 |

## 후속 순서

1. R2 physical marking/documentary evidence를 계속 우선한다. 이것이 target
   board의 total **및** memory type을 non-destructively 확정하는 경로다.
2. 별도 실험 카드 또는 replacement-only isolated boot가 장래에 준비되면,
   R2.2로 VRAM probe를 검토한다. 그 run sheet에는 original owner absence,
   mapped range, touched offsets, restoration semantics, display-off/rollback,
   postflight memory test를 모두 명시해야 한다. 이 문서는 그러한 run을 승인하지
   않는다.
3. R2.2가 성공해도 VRAM type, RAMDAC limit, reserved scanout range는 별도
   evidence가 필요하다. size test 하나만으로 G2/P3를 열지 않는다.

## Sources

- [FreeBSD ports `xf86-video-mga` Makefile](https://cgit.freebsd.org/ports/tree/x11-drivers/xf86-video-mga/Makefile)
  — FreeBSD가 X.Org MGA display driver를 port로 제공함을 확인.
- [X.Org `xf86-video-mga-2.0.0` official archive](https://xorg.freedesktop.org/archive/individual/driver/xf86-video-mga-2.0.0.tar.gz)
  — `mga_driver.c`의 `MGACountRam`/`VideoRam` precedence 및 `mga_bios.c`의
  PInS parser source.
- [FreeBSD stable/7 `mga_drv.c`](https://cgit.freebsd.org/src/plain/sys/dev/drm/mga_drv.c?h=stable/7)
  and [FreeBSD stable/7 `mga_dma.c`](https://cgit.freebsd.org/src/plain/sys/dev/drm/mga_dma.c?h=stable/7)
  — legacy BSD DRM의 PCI topology 및 caller-provided DMA init layout.
- [Linux MGA DRM public UAPI header](https://github.com/torvalds/linux/blob/v6.2/include/uapi/drm/mga_drm.h)
  — `DRM_MGA_INIT` structure and fields.
- [Linux mgag200 VRAM probe](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/mgag200/mgag200_drv.c)
  — contemporary Matrox server chip의 map/write/read probe도 passive read가 아님을
  보이는 comparison only; G400/G450 implementation source로 사용하지 않음.
- local public distribution `/tmp/MatroxMGA-1.0` — README, release notes, Help,
  binary string inventory. Binary-derived details are not redistributed.
