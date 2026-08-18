# R6 — Target Pure-C Validation Record

기준일: 2026-08-18

## 범위

R6의 새 policy source가 OPENSTEP i386 `cc`에서도 C89-compatible하게 compile되고
ordinary process memory에서 실행되는지만 확인했다. 이 record는 hardware feature
test나 replacement driver test가 아니다.

`/ndrv/openstep-matrox-remade` NFS source에서 다음 source groups를 temporary
`/tmp` binary로 compile/run했다. 2026-08-18의 latest transaction run includes
the reviewed G450 CRTC plan, primary CRTC byte-image encoder and PLL byte-image
encoder.

| group | result | hardware boundary |
| --- | --- | --- |
| one-mode transaction (`Profile`, `MappingReview`, `RecoveryMatrix`, `G450CRTCPlan`, `G450PrimaryCRTCImage`, `G450PrimaryCRTCReadback`, `G450PLL`, `BoundedPoll`, `ModeTransaction`, `EDID` footprint helper) | `OPENSTEP_MGA_MODE_TRANSACTION_TEST_STATUS=pass` | no mapping/mode/VGA/DAC/PLL/CRTC access |
| primary CRTC readback comparator | `OPENSTEP_MGA_G450_PRIMARY_CRTC_READBACK_TEST_STATUS=pass` | caller-supplied process-memory bytes only; no VGA/DAC/MMIO read/write |
| opaque offscreen allocator (initial policy) | `OPENSTEP_MGA_OFFSCREEN_ALLOCATOR_TEST_STATUS=pass` | no address/offset/MMIO/allocation API access |
| ordinary-memory clear/copy reference oracle | `OPENSTEP_MGA_REFERENCE_TEST_STATUS=pass` | caller-owned process memory only |
| opaque offscreen allocator (live-record verification revision) | `OPENSTEP_MGA_OFFSCREEN_ALLOCATOR_TEST_STATUS=pass` | no address/offset/MMIO/allocation API access |
| allocator-backed offscreen 2D admission | `OPENSTEP_MGA_OFFSCREEN_2D_TEST_STATUS=pass` | no draw submit/address/MMIO/engine access |

각 temporary test binary는 successful run 뒤 `/tmp`에서 삭제했다. `nxrun.sh`가
connection 종료를 담당하도록 하여 command 내부에서 중복 `logout`을 호출하지
않았다.

## 해석 제한

이 결과는 compiler/language portability proof일 뿐이다. G3 offline one-mode
design pass의 16 MiB deployment record를 target process memory에서 다시
검증한 것뿐이며, physical-board maximum, G1 sole owner, mapping range/cache
evidence, target allocator, driver install/load/probe,
VRAM/MMIO/PLL/DAC/CRTC/DDC/2D engine에 관한 어떤 verdict도 바꾸지 않는다.
