# P2 — Resource Ownership Decision Record

상태: **unresolved; no MGA resource may be acquired**

기준일: 2026-08-18

## 현재 owner

실기의 `MatroxMGA`는 `IOFrameBufferDisplay` 계열 display driver로 loaded되어
있으며, 1600x1200 RGB:888/32 screen을 제공한다. NextDev header와 S3/QVision
공식 예제는 이 class가 mode, linear framebuffer, cursor와 display lifecycle을
담당함을 보인다.

따라서 현재 card의 다음 자원 owner는 `MatroxMGA`다.

| 자원 | owner | OpenStepMGAService P2 권한 |
| --- | --- | --- |
| PCI function claim/device description | MatroxMGA | 없음 |
| CRTC/DAC/PLL/mode | MatroxMGA | 없음 |
| scanout/front buffer/cursor reservation | MatroxMGA/WindowServer | 없음 |
| PCI config write | MatroxMGA 또는 firmware | 없음 |
| BAR0/1/2 MMIO mapping | 미확정 | 없음 |
| offscreen VRAM | 미확정 | 없음 |
| bus mastering/IRQ | 미확정 | 없음 |

P2 control service의 lease token은 **software protocol ownership**일 뿐 card
resource ownership이 아니다.

## 확인된 값과 사용 금지

P1은 read-only PCI config probe로 raw BAR `f8000008 e8200000 e8800000`을
기록했다. 과거 `pcils` 기록의 BAR1은 `e8300000`이었으나, 현재 부팅에서
독립 rebuild한 `pcils/PCIscan`은 P1과 동일한 64-byte header 및 BAR 값을
보고했다. 따라서 P1/P1.1 공통값은 current inventory로 확정했다. 상세 evidence는
`P1_PCIL_RECHECK.md`와 `P0_TARGET_INVENTORY.md`에 있다.

이 확인은 mapping target 또는 device ownership을 부여하지 않는다. BAR 값은
kernel-private inventory로만 취급한다.

기존 OpenStep configuration은 16 MiB를 말하지만 PCI subsystem `102b:0d43`의
공개 board catalogue 표기는 32 Mb다. DriverKit runtime getter도 `0`으로
uninformative하므로 physical VRAM total/type은 미확정이다. 또한 stride, cursor,
hidden allocation, WindowServer state가 확정되지 않았다. 따라서 "남은 VRAM"을
offscreen range라고 계산하는 방식은 금지한다. 대조 기록은
`P1_SUBSYSTEM_MEMORY_RECONCILIATION.md`에 있다.

## P2에서 허용되는 작업

- MiG control protocol build 및 scalar message test.
- `kl_util`/NFS log를 이용한 no-hardware LKS load/unload test.
- P1과 동일한 PCI config **read-only** inventory의 재현.
- `MatroxMGA` status와 device table을 read-only로 비교.
- documented `IODeviceMaster` lookup 및 `getIntValues`로 existing display의
  metadata만 read-only query. framebuffer pointer를 포함하는
  `IOGetDisplayInfo`와 모든 setter는 제외한다.

P2.0 message path, P2.1 반복 software lease, P2.2 client-death cleanup,
P2.3의 명시적 no-hardware capability contract, P2.4 two-client contention
stress, P2.5 raw fixed-size/type negative message handling, P2.6 full
target-native control-plane regression은 이 허용 범위에서 통과했다.
P2.7 host-side static guard는 P2 source/table/load command에 hardware API,
device binding, IRQ/DMA/memory map, `START`/`WIRE`가 추가되는 것을 거부한다.
P2.8 target-native binary import guard는 `nm -u`로 built relocatable에 mapping,
PCI, framebuffer, DMA/direct-I/O helper import가 없는지 service load 전에
확인한다. 이 guard를 포함한 target-native control-plane suite도 통과했고 test
뒤 `OpenStepMGAService`는 deallocated, 기존 `MatroxMGA`는 loaded 상태였다.
strict import allowlist와 P2.9 clean-build reproducibility runner도 target에서
통과했다. 결과는 `P2_CLEAN_REPRODUCIBILITY.md`를 따른다.
자세한 실행 증거는 `P2_P20_PROTOCOL_REPORT.md`, `P2_P21_LEASE_REPORT.md`,
`P2_P22_PORT_DEATH_REPORT.md`, `P2_P23_CAPABILITIES_REPORT.md`,
`P2_P24_MULTI_CLIENT_STRESS_REPORT.md`, `P2_P25_RAW_MIG_NEGATIVE_REPORT.md`에
`P2_P26_CONTROL_REGRESSION_REPORT.md`, `P2_STATIC_SAFETY_GATE.md`,
`P2_BINARY_IMPORT_GATE.md`, `P2_CLEAN_REPRODUCIBILITY.md`를 포함해 기록한다.

## P2에서 금지되는 작업

- MGA BAR mapping 또는 BAR address read/write.
- VRAM, framebuffer, engine, DAC, PLL, CRTC, reset access.
- `IOPCIDirectDevice`를 통한 동일 PCI device 재claim 또는 config write.
- interrupt registration, bus-master DMA, AGP/GART.
- 기존 display driver unload/reload, mode switch, automatic driver registration.

## P3 이전 승인 조건

모두 충족해야 한다.

1. 동일 부팅에서 independent read-only PCI scanner와 P1이 BAR0/1/2 및
   command/IRQ를 일치시킨다. **통과 (P1.1).**
2. physical variant와 VRAM type/size를 read-only evidence로 확정한다.
   **미통과:** 16 MiB configuration과 `102b:0d43` 32 Mb board catalogue
   표기가 충돌하고, runtime getter는 size/type을 publish하지 않는다.
3. 기존 driver가 소유하지 않는 offscreen range를 문서 또는 안전한 관찰로
   식별한다. 식별할 수 없으면 별도 시험 카드 또는 system-memory-only test
   경로를 선택한다.
4. mapping API가 existing display owner와 충돌하지 않는다는 OPENSTEP 문서와
   target-specific evidence를 확보한다.
5. P2 protocol에서 simultaneous client, stale lease, unload를 안전하게
   처리한다.

이 조건 중 하나라도 불명확하면 P3 command submission을 시작하지 않는다.
pure-C `OSMGACanEnterP3`는 이 다섯 조건을 same-order software precondition으로
표현한다. 현재 admission object에는 PCI inventory만 verified로 둘 수 있으며,
`VRAM_SIZE` gate가 first failure다. 이 helper는 MGA device action이나 P2의
`hardware_ready=0` contract를 바꾸지 않는다. 상세 test와 provenance는
`D0_EDID_PARSER_POLICY.md`를 따른다.

P3가 열릴 때에도 user client가 VRAM/BAR address를 전달하는 Submit ABI는 만들지
않는다. future clear/triangle의 geometry-only validation과 kernel-owned allocation
rule은 `P3_COMMAND_ENVELOPE.md`에 고정한다. 그 source는 현 P2 MiG ABI에는
연결되지 않았고, P3 admission proof를 대체하지 않는다.
