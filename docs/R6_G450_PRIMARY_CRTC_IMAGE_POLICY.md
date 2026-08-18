# R6 — G450 primary CRTC byte-image policy

기준일: 2026-08-18

`profile/OpenStepMGAG450PrimaryCRTCImage.{h,c}`는 승인된 R3 16 MiB /
1600×1200@60 / RGB:888/32 record를 G450 primary-head의 **unwritten** VGA
CRTC 및 extended-CRTC byte image로 변환한다. 1600×1200 DMT의 blanking과
32-bit pitch를 register byte field로 표현할 수 있는지를 C89에서 먼저
확인하는 계층이다.

## Fixed approved image

| byte group | approved values |
| --- | --- |
| standard CRTC notable bytes | `00=09`, `01=c7`, `04=cf`, `06=e0`, `10=b0`, `11=23`, `13=90`, `16=e1` |
| extended bytes | `00=10`, `01=01`, `02=ad`, `03=83`, `04=00`, `05=00` |
| MiscOut external-clock selection bits | OR `0c` |

The image is derived from the checked geometry plan:

```text
H display / sync start / end / total = 1600 / 1664 / 1856 / 2160
V display / sync start / end / total = 1200 / 1201 / 1204 / 1250
pitch = 6400 bytes
```

Horizontal/vertical values above 255 are expected: their upper bits belong in
the VGA overflow and MGA extended fields. The encoder validates the complete
representable field width and does not mistake a low-byte truncation for an
invalid mode. It accepts only the existing 32-bit first-mode record; a
mutated R3 review or any 16-bit replacement is rejected before an image is
returned.

## Deliberate boundary

This module contains no indexed-VGA operation, DAC access, MMIO mapping,
display protection/unprotection, blanking, sequencer reset, PLL write,
readback, or state restore. `misc_output_or` is a requested bit mask, not a
write. It also does not select a physical framebuffer address.

`OSMGABeginModeTransaction` retains this data-only image alongside the CRTC
geometry, PLL byte image and three-range plan. A future recovery-only driver
writer must still establish G1 recovery evidence, map the reviewed resource,
capture restorable state, apply an explicitly reviewed transaction order,
perform bounded hardware readback, and recover the original owner on every
failure. This policy grants none of those permissions.

`OpenStepMGAG450PrimaryCRTCReadback.{h,c}` is the complementary data-only
comparator for that future caller. It accepts a caller-supplied snapshot;
standard/extended bytes must match exactly, while MiscOut is checked only for
the requested `0c` external-clock bits because unrelated preserved bits are
not owned by this policy. The comparator does not obtain a snapshot itself and
does not turn a matching synthetic snapshot into hardware evidence.
`OSMGABeginModeTransaction` now requires that comparator's successful result
after stable PLL lock and before it will accept linear-mode success; a mismatch
has the terminal rollback result.

## Provenance and verification

The behavior-level formula and primary-head clock-selection convention were
audited against the official `xf86-video-mga-2.0.0` archive recorded in
[`refs/XORG_MGA_LICENSE_NOTICE.md`](../refs/XORG_MGA_LICENSE_NOTICE.md).
No source file, macro, or register-write sequence is vendored or invoked.

```text
sh tools/check-g450-primary-crtc-image-no-hardware.sh
sh test/run-g450-primary-crtc-image-host.sh
sh tools/check-g450-primary-crtc-readback-no-hardware.sh
sh test/run-g450-primary-crtc-readback-host.sh
sh test/run-mode-transaction-host.sh
# target: csh -f test/run-mode-transaction-target.csh /ndrv/openstep-matrox-remade
# target: csh -f test/run-g450-primary-crtc-readback-target.csh /ndrv/openstep-matrox-remade
```

The standalone strict-C89 host checks, target integrated transaction test and
target readback-comparator test passed on 2026-08-18. The target binaries
existed only under `/tmp` and were removed; they did not build/load a DriverKit
bundle or access MGA hardware.
