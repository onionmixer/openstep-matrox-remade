# R3 — operator-approved PCI G450 16 MiB deployment mode

기준일: 2026-08-18  
verdict: **PASS — offline deployment record**

## Decision

운영자 결정에 따라 이 프로젝트의 first replacement deployment profile은
**PCI G450, 16 MiB, single-head, 1600x1200@60 RGB:888/32**로 고정한다.
G450 family의 더 큰 32 MiB variant는 이 record의 capacity, mapping length,
offscreen budget 또는 supported mode를 넓히지 않는다.

이것은 physical-chip inventory를 새로 주장하는 문서가 아니다. 16 MiB는 G450의
최소 구성이라는 운영 전제와 current original configuration을 결합한 보수적인
deployment limit이다. physical board P/N 및 실제 최대 capacity 조사는 별도
reference work로 남지만, 이 release의 G3 차단 조건은 아니다.

## Target baseline recheck

2026-08-18 target read-only `collect-r5-original-preflight.csh` 결과:

| field | observed result |
| --- | --- |
| active owner | `MatroxMGA` loaded |
| selected configuration | `MatroxMGAG400_16MB` compatibility table |
| current output | 1600x1200, 60 Hz, RGB:888/32 |
| original fingerprint | four-file comparator pass |
| NFS source | `/ndrv` pass |

The existing table's `G400` label is not used to change the PCI G450
implementation target.  `P0_TARGET_INVENTORY.md` records the G450 topology;
the original bundle also carries a G450 16 MiB catalogue profile.

## Complete one-mode record

| field | accepted value | source / rationale |
| --- | ---: | --- |
| deployment memory limit | 16 MiB / 16,777,216 bytes | operator-confirmed G450 minimum limit |
| RAMDAC ceiling for this first record | 300,000 kHz | original 16 MiB G450 catalogue compatibility value; deliberately below family maximum |
| visible mode | 1600 x 1200 x 32-bit | current original configuration |
| pitch / alignment | 6,400 bytes / 8 bytes | tight 32-bit pitch, reviewed alignment policy |
| visible footprint | 7,680,000 bytes | 6,400 × 1,200 |
| mapping ceiling | 16,777,216 bytes | fixed deployment limit, not a live mapping action |
| pixel clock | 162,000 kHz | selected standard 1600x1200p60 DMT shape |
| h front / sync / back / total | 64 / 192 / 304 / 2,160 | selected DMT shape |
| v front / sync / back / total | 1 / 3 / 46 / 1,250 | selected DMT shape |
| sync polarity | positive / positive | selected DMT shape |
| calculated refresh | 60,000 mHz | exact integer calculation |

The selected 162 MHz pixel clock is below the conservative 300 MHz ceiling.
This record is a fixed replacement design input; it does not claim that the
original binary used identical blanking fields.

## Executable review

`test/openstep-mga-g450-16m-mode-record-test.c` instantiates exactly the above
record and passes it to `OSMGAValidateR3ManualModeReview`.

- host strict-C89: `OPENSTEP_MGA_G450_16M_MODE_RECORD_TEST_STATUS=pass`
- target OPENSTEP C89: same pass marker; `/tmp` binary removed

The regression also proves that changing the profile total to 8 MiB while
leaving its configured 16 MiB declaration fails closed.  It creates no driver
bundle, mapping, display mode, or hardware access.

## Consequence and remaining boundary

G3's offline single-mode review is complete for this conservative deployment
record.  Applying it to a display remains a later recovery-only driver action:
it requires the G1 sole-owner P-original/P-recovery/P-failure configuration
matrix and explicit activation approval.  It never permits an automatic mode
switch while `MatroxMGA` owns the screen.
