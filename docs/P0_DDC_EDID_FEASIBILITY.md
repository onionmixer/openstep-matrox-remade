# DDC/EDID 모니터 정보 처리 가능성 및 경계

기준일: 2026-08-18

## 결론

PCI G450 (`102b:0525`)에서 DDC/EDID를 읽어 모니터 정보를 사용하는 것은
기술적으로 가능하다. 그러나 이것은 Mesa 3D sidecar의 기능이 아니라, 해당
카드를 단독 소유하는 **새 OPENSTEP display driver**의 초기화 기능이어야 한다.

현재 적재된 `MatroxMGA`가 scanout, mode, DAC 및 GPIO를 소유하는 상태에서
`OpenStepMGAService`가 DDC를 읽는 것은 허용하지 않는다. DDC2B는 I2C bus를
구동하므로 단순한 read-only probe가 아니며, GPIO 방향과 line state를 바꾸는
write가 필요하다. 이 경계를 지키면 DDC 미응답 시에도 기존과 같은 수동 mode
선택을 완전히 유지할 수 있다.

## 근거

### G400/G450 hardware 경로

로컬 분석용 공개 Xorg `xf86-video-mga-1.6.5` source archive
(`ae1ddf8d4780f6c5313fe0d23826e302fc65556a6ebcaca751c755c9761928ff`)의
`src/mga_dacG.c`는 G-series RAMDAC의 primary DDC P1 및 G400/G450/G550의
secondary DDC P2를 정의한다. P1은 DDC1/2B I2C bus로 만들어지고, DDC EEPROM
주소 `0xa0` probe와 EDID DDC2 read에 사용된다.

그 구현은 `MGA1064_GEN_IO_CTL` 및 `MGA1064_GEN_IO_DATA`를 읽고 쓴다. 특히
open-drain I2C high level은 output driver를 tri-state로 만들어 유지한다.
따라서 EDID 수신 자체도 DAC GPIO ownership과 timing을 요구한다. G400/G450에
DDC 경로가 있다는 증거인 동시에, 현 display driver와 병렬 접근하면 안 된다는
증거다.

`src/mga_driver.c`는 DDC2 실패 뒤 DDC1을 시도하고, 둘 다 실패해도 monitor
configuration을 계속 사용한다. 이는 본 프로젝트의 fallback 정책과도 일치한다.

### OPENSTEP display-driver 경로

실기와 byte-for-byte 대조한 `IOFrameBufferDisplay.h`는 다음을 제공한다.

- `selectMode:count:valid:modeString:` — config table의 `Display Mode` 또는
  명시 mode string을 이용해 mode list에서 선택한다.
- `displayModeCount`/`displayModes` — driver가 제공하는 고정 mode list다.
- `setPendingDisplayMode:`와 `enterLinearMode` — mode transition은 display
  driver lifecycle 안에서 처리된다.

기존 `MatroxMGA.config`의 G450 table에는 기본 `Display Mode`가 있고,
`MatroxMGAG450_16MB.modes`에는 사용자가 고를 수 있는 고정 mode list가 있다.
기존 binary의 공개 selector/문자열 관찰에서는 DDC, EDID, I2C라는 공개 client
interface가 발견되지 않았다. 이는 "절대 없다"는 증명은 아니지만, 현 binary를
안전하게 확장할 공개 경로가 없다는 충분한 근거다. binary patch/link은 프로젝트
금지 사항을 그대로 유지한다.

## 설계 판정

| 항목 | 판정 |
| --- | --- |
| 현재 `OpenStepMGAService`에서 DDC GPIO 접근 | 금지 |
| 현재 `MatroxMGA`와 병렬 DDC reader LKS | 금지 |
| 깨끗이 새로 만든 display driver의 boot-time DDC2B | 가능, 별도 단계 |
| raw EDID parser와 mode policy의 host/unit test | 지금부터 가능 |
| EDID에 없는 임의 timing을 자동 programming | 금지 |
| 사용자가 지정한 수동 `Display Mode` | 항상 가능하고 최우선 |

DDC는 monitor hot-plug notification이나 runtime mode switching의 근거로 쓰지
않는다. 이 세대 VGA connector는 hot-plug 보장이 없고, WindowServer 운용 중
mode를 바꾸는 것은 별도 안정성 작업이다.

## mode 선택 정책

새 display driver가 생기더라도 아래 순서를 고정한다.

1. 사용자가 `Display Mode`를 명시하면 그것이 최우선이다. EDID는 읽더라도
   mode를 덮어쓰지 않는다.
2. 명시값이 없고 유효한 base EDID block을 얻었으면, EDID의 preferred timing과
   driver가 검증한 고정 mode table의 **교집합**만 후보로 삼는다.
3. 일치 후보가 없거나 EDID checksum/header/길이가 불량이면, 해당 card/VRAM
   variant의 보수적인 default table entry로 되돌린다.
4. DDC NACK, timeout, cable/TV-out, DDC1-only monitor도 정상 fallback이다.
   부팅 실패나 blank screen의 이유가 되어서는 안 된다.
5. EDID raw block은 진단용으로만 보관한다. 모든 확장 block, vendor extension,
   hot-plug 동작을 첫 단계의 지원 범위로 주장하지 않는다.

즉 DDC는 선택 폭을 넓히는 입력일 뿐, 기존 수동 설정을 대체하거나 화면을
재설정하는 권한이 아니다.

## 구현 순서와 gate

### D0 — 독립 parser/policy

- 완료: `edid/OpenStepMGAEDID.c`가 kernel/MMIO 없이 base 128-byte EDID
  header/checksum, vendor/product/serial, 첫 preferred detailed timing만
  해석한다.
- 완료: valid EDID, checksum/header 오류, 미지원 preferred timing, interlaced
  timing, 첫 DTD 부재와 manual override를 host unit test한다.
- 결과는 `valid`, `preferred_mode`, `fallback_reason`으로 한정한다. 이 단계는
  screen hardware를 전혀 건드리지 않는다. 상세는
  `docs/D0_EDID_PARSER_POLICY.md`에 있다.
- target-native D0 runner는 minimal loader probe를 full parser test보다 먼저
  실행한다. 이 단계도 pure C process만 build/run하며 DDC transaction이나
  display driver/LKS loading을 수행하지 않는다.

### D1 — display driver ownership 설계

- `MatroxMGA`와 공존하는 sidecar가 아니라, replacement display bundle의
  `IOFrameBufferDisplay` subclass가 PCI device와 DAC/CRTC lifecycle을 소유하는
  구조를 설계한다.
- DDC access는 `enterLinearMode` 이전의 초기 initialization 한 지점으로
  제한하고, I2C line을 release한 뒤에만 mode programming을 시작한다.
- 첫 board test 전에는 current mode save, explicit manual fallback, serial/NFS
  recovery, bounded timeout을 모두 준비한다.
- `IOFrameBufferDisplay` lifecycle, sole-owner boot transition, fail-closed
  teardown, D2/D3 admission gate는
  `D1_REPLACEMENT_DISPLAY_OWNERSHIP.md`에 구체화한다. 이 설계는 sidecar에
  display API를 추가하라는 의미가 아니다.

### D2 — hardware DDC2B bring-up

- 기존 `MatroxMGA`를 사용하는 production screen에서는 실행하지 않는다.
- 새 display driver만 적재된 회복 가능한 시험 환경에서 P1 primary connector를
  대상으로 address ACK, base 128-byte read, checksum만 순차 검증한다.
- 어느 실패도 mode write, reset, PLL write의 trigger가 되어서는 안 된다.

### D3 — mode policy integration

- D0 parser output을 known-good fixed mode table과 교차한다.
- 강제 수동 mode, EDID preferred mode, no-EDID fallback 각각을 cold boot에서
  검증한다.
- runtime hot-plug/dual-head/TV-out/EDID extension은 이 gate 밖의 후속 과제다.

## P2/P3에 미치는 영향

현재 P2.3 capability/acquire/release control plane은 하드웨어에 접근하지 않으므로 그대로
진행할 수 있다. DDC는 P3 offscreen 3D, Mesa backend 또는 SDL2 API의 선행 조건이
아니다. 단, 기존 display driver와 공존하는 동안은 DDC reader를 추가하지 않으며,
DDC 지원은 display replacement track이 승인된 뒤에만 구현한다.

## 라이선스와 출처

- Xorg source는 DDC path의 hardware behaviour와 fallback 구조를 이해하기 위한
  참고다. archive의 `COPYING`에 XFree86/Open Group 등 허용 조건이 있으나,
  실제 code를 이식하기 전에는 해당 source file의 copyright와 attribution을
  재검토하고 본 프로젝트 파일에 명시한다.
- 공개 `MatroxMGA-1.0`은 mode table/configuration behaviour 관찰용이다. binary
  code, disassembly 또는 recovered source를 새 구현에 복사하지 않는다.
- OPENSTEP `IOFrameBufferDisplay.h`와 NextDev DriverKit 문서는 display mode
  ownership과 manual fallback의 1차 근거다.
