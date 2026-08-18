# R6 — One-Mode Transaction Policy

기준일: 2026-08-18

## 목적

`protocol/OpenStepMGAModeTransaction.{h,c}`는 future replacement-only display
driver가 한 mode transition을 시작하기 전에 통과해야 하는 offline 상태기다.
지금까지 분리되어 있던 R6 mapping review, checked G450 CRTC geometry plan,
reviewed primary CRTC byte image, G450 frequency plan, reviewed PLL byte image,
PLL-lock bounded poll을 연결한다.
이 source는 DriverKit/MMIO/DAC/port API를 포함하지 않으며,
R4 bundle에 compile/link되지 않는다.

## State flow

```text
IDLE
  -> PREFLIGHT_READY
  -> PLL_LOCK_PENDING
  -> PLL_LOCKED
  -> PRIMARY_CRTC_VERIFIED
  -> LINEAR_ACTIVE
  -> ROLLBACK_REQUIRED
  -> ROLLED_BACK
```

PLL timeout, primary CRTC snapshot mismatch 또는 reported linear-mode failure는 retry state가 아니라 즉시
`ROLLBACK_REQUIRED`로 이동한다. `ROLLED_BACK` 이후에는 같은 transaction object를
재사용하지 않는다; 새 offline preflight가 필요하다.

`ROLLBACK_REQUIRED`에서 `ROLLED_BACK`으로 가기 위해서는 future caller가 아래
네 복구 결과를 각각 성공으로 보고해야 한다: captured display state, captured PLL
state, VGA-safe state, superclass revert lifecycle. `OSMGACompleteModeRollback`
is rejected until all four bits are present. A failed report preserves the
rollback-required state; this policy neither captures nor restores hardware
state itself and deliberately fixes no register-write order.

## Preflight invariants

`OSMGABeginModeTransaction`은 아래를 모두 확인한다.

1. R6 mapping review — R3 physical/mode evidence and reviewed range/cache
   policy.
2. **G1 recovery matrix** — complete P-original/P-recovery/P-failure sole-owner
   candidate records, every snapshot's bundle/table/rollback-instruction
   evidence, atomic Installer installation, observed Installer rollback,
   observed P-failure original boot, and independent recovery channel.
   A mapping-review boolean cannot substitute for this matrix.
3. PLL review — same R3 mode record, PLL source evidence, explicit head
   selection.
4. **Record identity** — mapping review와 PLL review의 R2/R3 evidence mask,
   R2 board/cross-check/VRAM/RAMDAC evidence reference, physical profile,
   manual VRAM configuration byte count, geometry, bpp, pitch, pixel clock,
   mapping length와 complete timing의 active/porch/sync/polarity fields가
   byte-field level로 동일해야 한다. 서로 다른 review에서
   각각 pass한 값을 섞을 수 없다.
5. **CRTC geometry** — 동일 mapping-side R3 record로부터 32-bit format 전용
   display/sync/total/pitch/scanout plan을 다시 구성한다. checked plan을 만들 수
   없으면 PLL 단계 전에 거부한다.
6. **Primary CRTC byte image** — checked geometry와 동일 R3 record로부터
   primary-head VGA/extended image를 생성한다. low byte만으로는 표현되지 않는
   horizontal/vertical upper bits도 검증하며, 32-bit first mode 이외에는
   preflight에서 거부한다. 이 값은 write request가 아닌 data-only image다.
7. **PLL byte image** — PLL plan과 explicit head를 one-shot M/N/P image로
   encode한다. encoding 실패나 invalid head는 lock 단계 전에 거부한다.
8. **Range publication plan** — reviewed 16 MiB framebuffer range와 two legacy
   VGA ranges의 exact three-entry layout을 derive한다. synthetic/unaligned base,
   wrong range count/index/length는 lock 단계 전에 거부한다.
9. nonzero timeout/stable-ready policy.

After a future caller reports stable PLL lock, it must provide a primary CRTC
snapshot to `OSMGAReportPrimaryCRTCReadback`. The transaction compares that
caller-supplied snapshot against its own approved byte image and moves to
`PRIMARY_CRTC_VERIFIED` only on success. `OSMGAReportLinearModeEntry` rejects
the older `PLL_LOCKED` state; a mismatch terminates in `ROLLBACK_REQUIRED`.
This is an offline state-policy condition, not an implementation of snapshot
acquisition.

The transaction records abstract geometry/frequency plans, an unwritten
primary CRTC image, an unwritten PLL byte image, and an unpublished
three-range plan only. It does not publish or map a range, write a CRTC/PLL,
observe hardware lock, or enter linear mode.
Future hardware code must call the state transitions only *after* it performs
the corresponding separately approved action and obtains a sampled result.

## Current boundary

The unit test uses the approved offline 16 MiB / 1600×1200×32 / 162 MHz
deployment record shape to cover the happy path, exact CRTC plan and primary
byte-image capture, mismatched review/evidence-reference rejection, 16-bit
rejection, dual-owner recovery-matrix rejection, stable lock, CRTC snapshot mismatch rollback, deadline timeout,
linear-active state, incomplete/failed rollback-stage rejection, and rollback
completion. The byte image remains unwritten.
G3 is an offline design pass; this policy does
not pass G1, authorize a replacement installation, or establish a physical
board maximum.

```text
sh tools/check-mode-transaction-no-hardware.sh
sh test/run-mode-transaction-host.sh
# target: csh -f test/run-mode-transaction-target.csh /ndrv/openstep-matrox-remade
```

Both checks are strict C89/source-purity tests. R4's existing source gate
continues to reject every actual mapping, mode-programming, DAC/PLL, and port
operation in the replacement display bundle.
