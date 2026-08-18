# R2 Physical-Profile Admission Policy

기준일: 2026-08-18

## 목적

`profile/OpenStepMGAProfile.{h,c}`는 future R3 manual-mode review가 사용할
physical board profile을 **증거가 완전할 때만** 받아들이는 pure-C policy다.
PCI catalog label, existing `Instance0.table`, family brochure, 16/32 MiB candidate는
이 type을 valid state로 만들 수 없다.

이 module은 DriverKit, PCI config, BAR/VRAM mapping, DAC/mode I/O, DDC, DMA,
interrupt, kernel-server API를 포함하지 않는다. hardware를 discover하거나
program하지 않으며 R6/P3 권한을 주지 않는다.

## Required profile evidence

`OSMGAR2PhysicalProfile`이 pass하려면 다음 다섯 evidence bit와 nonzero value가
모두 필요하다. 또한 board/cross-check/VRAM/RAMDAC 각각에 nonempty evidence
reference string이 있어야 한다. reference는 B1--B6 ID 또는 redacted report-safe
artifact ID여야 하며, PCI ID나 configuration value를 값처럼 다시 적는 shortcut은
아니다.

| evidence bit | required value | acceptable evidence class |
| --- | --- | --- |
| `BOARD_ID` | physical board identity | readable target sticker P/N 또는 memory/GPU marking과 exact board correlation |
| `INDEPENDENT_CROSSCHECK` | independent review present | B5/B6가 같은 assertion을 재인용하지 않는 documentary cross-check |
| `VRAM_TYPE` | SDR/DDR type enum | physical part marking + exact datasource |
| `VRAM_SIZE` | physical byte total | exact board/memory population evidence |
| `RAMDAC_LIMIT` | applicable kHz limit | exact board/head applicability가 있는 source |

`vram_type`, `physical_vram_bytes`, `applicable_ramdac_khz` 중 어느 하나라도
unknown/zero면 reason과 함께 fail한다. 값의 크기나 memory family는 module이
추측하지 않는다. bit만 설정하고 reference를 비워도 fail한다.

## Current target interpretation

현재 `102b:0d43` → `G45FMDVP32DSF` / 32 MB DDR PCI candidate와 existing 16 MiB
configuration profile은 `BOARD_ID`/`VRAM_TYPE`/`VRAM_SIZE` evidence bit를 설정할
근거가 아니다. R2 physical evidence record가 `PASS`가 될 때까지 production code는
valid `OSMGAR2PhysicalProfile` instance를 만들지 않는다.

R2-20260818-A의 target bundle sweep도 같은 결론이다. G400/G450 16 MB table은
동일 `0x0525102B` ID 및 16/300 MHz field를 사용하고, 두 mode catalogue도 identical이다.
catalogue에 G450 32 MB profile이 없다는 사실 역시 physical card total을 부정하는
evidence가 아니다.

이 policy의 pass도 다음을 증명하지 않는다.

- original owner의 scanout/cursor/hidden offscreen allocation range
- existing driver와 replacement mapping compatibility
- sole-owner recovery profile(G1), cold-boot recovery(G4)
- P3/Mesa command submission

따라서 `OSMGACanEnterP3`의 별도 five-gate policy는 그대로 유지된다.

## Verification

`test/openstep-mga-profile-test.c`는 missing board ID/cross-check, missing
board/cross-check/VRAM/RAMDAC reference, missing/unknown VRAM type, zero VRAM
size, zero RAMDAC를 각각 reject하고, synthetic complete-evidence fixture만
accept한다. fixture의 16 MiB/300 MHz 숫자와 B2/B6 reference는 target assertion이
아니라 admission-path test data다.

```text
sh tools/check-profile-no-hardware.sh
sh test/run-profile-host.sh
```

두 check는 C89 host build와 source purity만 검증한다. target driver를 build/load하거나
hardware에 접근하지 않는다.
