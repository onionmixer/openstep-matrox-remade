# R6 — G450 PLL Byte-Image Policy

기준일: 2026-08-18

## 범위

`profile/OpenStepMGAG450PLLEncoding.{h,c}`는 이미 검토·선택된 단 하나의
`OSMGAG450PLLPlan`을 G450 primary/secondary DAC PLL byte-image로 변환한다.
이 module은 I/O port, DAC index/data access, MMIO, memory mapping, sleep,
poll 또는 candidate retry를 포함하지 않는다.

입력 source audit은 공식 `xf86-video-mga-2.0.0` archive이며, this analysis
copy's SHA-256 is
`15b0f4cf3ee22eaefb45d54d1a0bf67ee710a292479a273fe3fd86f9fa802f41`.
Archive `COPYING`의 XFree86/Open Group permission notice를
`refs/XORG_MGA_LICENSE_NOTICE.md`에 보존했다. 이 project module은 source를
복사하지 않고, audit에서 검증한 data representation을 새 C89 code로 작성했다.

## Fixed 1600×1200@60 input

승인된 16 MiB PCI G450 record의 162,000 kHz plan은 exact arithmetic으로
324,000 kHz VCO, reference divider 1, feedback divider 6, post divider 2를
선택한다. primary-head image는 다음과 같다.

| field | value |
| --- | ---: |
| target | primary pixel-PLL C |
| M | `0x00` |
| N | `0x04` |
| P | `0x00` |

Secondary head를 명시적으로 선택한 **offline review**는 같은 M/N/P encoding을
얻을 수 있지만, image target은 secondary video-PLL로 바뀐다. 현재 recovery
profile은 primary head만 허용한다. Dual-head, automatic head detection,
runtime fallback은 이 정책 밖이다.

## Rejection rules

- no review/plan, zero requested/achieved clock, VCO outside 256–1300 MHz,
  non-byte M/N range, invalid post-divider: reject.
- primary/secondary가 아닌 head: reject.
- live hardware lock failure: this module cannot retry; future writer must
  report the result to the existing bounded transaction and rollback.

## Verification boundary

```text
sh tools/check-g450-pll-encoding-no-hardware.sh
sh test/run-g450-pll-encoding-host.sh
# OPENSTEP: csh -f test/run-mode-transaction-target.csh /ndrv/openstep-matrox-remade
```

The host strict-C89/static checks and the target integrated C89 transaction
test passed on 2026-08-18. No target DAC state was read or written. A byte
image is not a mode transition and does not permit bundle installation,
matching, mapping, or `enterLinearMode` activation.
