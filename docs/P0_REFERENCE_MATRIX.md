# P0.3 — Reference and License Matrix

기준일: 2026-08-18

## 사용 원칙

공개 자료는 두 종류로 분리한다.

1. **구조/동작 참고**: 다른 OS의 API와 결합된 구현은 OPENSTEP 설계 참고로만
   사용한다.
2. **코드 후보**: 개별 파일의 license, attribution, 의존성을 확인한 뒤에만
   별도 clean-room source로 옮긴다.

GPL 자료는 register 의미와 failure mode 이해용으로만 사용하고 복사하지
않는다. binary-only MatroxMGA는 동작 관찰용이며 코드 후보가 아니다.

| 자료 | 확인한 내용 | license 판정 | 이 프로젝트에서의 사용 |
| --- | --- | --- | --- |
| NeXT DriverKit Reference | LKS, `IOFrameBufferDisplay`, MMIO, IRQ, DMA, MiG | NeXT 문서, code source 아님 | API 사용의 1차 문서 |
| 실기 DriverKit examples | OPENSTEP build/driver skeleton | 제3자 개발 kit 자료 | target에서만 참고, 저장소 복사 금지 |
| MatroxMGA 1.0 | OPENSTEP mode/2D/display 동작 | binary distribution, source 없음 | 분석 only; 코드/데이터 재배포 금지 |
| Xorg xf86-video-mga 1.6.5 | G450 PLL, mode/2D, DRI/DDX 분리 | archive `COPYING`은 MIT/X11 계열 허용 조건 | 파일별 header 확인 후 코드 후보 |
| FreeBSD stable/12 legacy MGA DRM | PCI G450 topology, DRM responsibilities | `mga_drv.c` header는 MIT-style 허용 조건 | 구조·PCI 판정 참고; FreeBSD API 직접 포트 금지 |
| Linux MGA DRM/fbdev | hardware sequence, errata, device behaviour | Linux source license는 GPL 계열 | 동작 참고 only; 코드 복사 금지 |

## Xorg MGA

검사한 공개 archive: `xf86-video-mga-1.6.5.tar.gz` from x.org.

- `COPYING`에는 XFree86/Open Group/VA Linux/Matrox 등의 MIT/X11 계열 허용
  조건이 포함된다.
- archive에는 mode, 2D, G450 PLL, DRI/DDX, microcode 관련 source가 분리돼 있다.
- 그러나 Xorg server API와 Linux/DRM interfaces에 결합돼 있으므로, 그대로
  OPENSTEP에 build하지 않는다.
- 실제 source를 반영할 경우에는 **사용 파일 자체의 header**와 `COPYING`을
  모두 기록하고 attribution notice를 유지한다.

## FreeBSD legacy MGA DRM

검사한 source: `stable/12/sys/dev/drm/mga_drv.c`.

- file header는 Precision Insight/VA Linux의 MIT-style permission이다.
- `mga`는 generic DRM core에 의존하며 DMA, IRQ, vblank, ioctl, AGP/MTRR
  feature를 configuration한다.
- 이 file은 `0525`가 HiNT `3388:0021` bridge 뒤에 있을 때 PCI G450로
  판단한다. legacy implementation은 AGP 요구 구조라 이 PCI G450을 지원하지
  않는다고 명시한다.
- 그러므로 본 프로젝트는 해당 source의 FreeBSD driver model을 옮기지 않고,
  PCI-only OpenStepMGA protocol을 별도로 만든다.

## 검증 전제

새 source file을 추가하기 전에 다음 항목을 commit message와 file header에
기록한다.

1. 참조한 공개 file/version/URL.
2. 해당 file의 license와 필요한 notice.
3. OPENSTEP 독립 구현인지, 허용된 source adaptation인지.
4. Linux/FreeBSD/Xorg API에 대한 남은 의존성이 없는지.
