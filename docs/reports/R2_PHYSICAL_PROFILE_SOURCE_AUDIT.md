# R2 — Physical Profile Source Audit

기준일: 2026-08-18

## 결론

새로 확인한 Matrox G450 family 자료는 PCI G450의 **가능한** memory/RAMDAC
envelope을 강화하지만, 현재 target의 physical VRAM total/type을 확정하지
않는다. 따라서 G2는 계속 **미통과**이며 R3 fixed mode, replacement mapping,
P3 admission에는 사용할 수 없다.

## 새 공개 자료

1. [Matrox Millennium G450 User Guide](https://video.matrox.com/en/media/2090/download)
   의 hardware-information table은 Millennium G450 PCI column에 16 MB 또는
   32 MB DDR SDRAM, main 360 MHz RAMDAC, secondary 230 MHz RAMDAC을 제시한다.
   같은 guide는 3D acceleration이 16/32-bit palette에서만 제공된다고 설명한다.
2. [Matrox G450 2002 product sheet](https://www.dosdays.co.uk/media/matrox/g450_chip_spec.pdf)
   는 PCI 16 MB DDR와 PCI 32 MB DDR를 별도의 retail/bulk product로 열거한다.
   이 source는 exact model number가 target board와 일치한다는 증거가 없으므로
   family/product-line cross-check로만 사용한다.

두 자료는 `102b:0d43` catalogue label이나 OPENSTEP `MGA Memory Size=16` profile을
다시 인용한 것이 아니므로, PCI G450 제품군의 **후보 범위**에 대한 독립 공개
근거로는 유효하다.

## Target evidence와의 대조

| field | target에서 확인된 값 | 새 공개 자료가 말하는 범위 | 판정 |
| --- | --- | --- | --- |
| PCI function | `102b:0525`, PCI G450 topology | G450 PCI product line | family-level 일치 |
| subsystem label | `102b:0d43` = 32 Mb Dual Head PCI catalogue label | PCI G450은 16 MB 또는 32 MB DDR variant 존재 | exact model/physical total 미확정 |
| existing config | G400 16 MB-or-greater profile, memory field 16 | PCI G450에도 16 MB/32 MB variant가 모두 존재 | config profile은 physical probe가 아님 |
| VRAM type | target getter가 publish하지 않음 | PCI G450 family table은 DDR SDRAM | current board population에는 적용 확정 불가 |
| main RAMDAC | target getter가 `0` | family table은 360 MHz | family envelope only; R2 board-limit evidence로 미충분 |
| secondary RAMDAC | target getter가 `0` | family table은 230 MHz | target connector/head assignment 미확정 |

## Original bundle software-catalogue cross-check

evidence ID: `R2-20260818-A`  
scope: outside-sandbox telnet, target-native read-only file inventory/table comparison

post-R5 target에서 original `MatroxMGA.config` bundle을 재수집했다. bundle은
G400 8/16 MB와 G450 16 MB catalogue table/mode files를 모두 포함하지만, 이는
installed driver가 지원하는 configuration catalogue일 뿐 card probe result가 아니다.

| observation | result | physical-profile interpretation |
| --- | --- | --- |
| `MatroxMGAG400_16MB.table` | title만 G400, `Auto Detect IDs=0x0525102B`, `MGA Memory Size=16`, `DAC Speed=300MHz` | G400 family/minimum configuration profile |
| `MatroxMGAG450_16MB.table` | title만 G450, 같은 `0x0525102B`, 16, 300 MHz | same device ID cannot distinguish G400/G450 physical board |
| 16 MB table relationship | table files differ (`cmp=1`) | title text difference를 포함한 catalogue metadata 차이일 뿐 physical measurement 아님 |
| 16 MB mode relationship | `MatroxMGAG400_16MB.modes`와 `MatroxMGAG450_16MB.modes`가 byte-for-byte equal (`cmp=0`) | mode list도 physical board/VRAM total discriminator가 아님 |
| G450 32 MB table/mode | bundle inventory에 없음 | absence는 32 MB card 부재의 증거가 아니며, catalog가 physical total probe가 아님을 보임 |

따라서 target의 selected `MatroxMGAG400_16MB` instance, bundle의
`MatroxMGAG450_16MB` alternative, `MGA Memory Size=16`, `DAC Speed=300MHz` 어느
것도 R2 implementation input으로 승격하지 않는다. 이 software route는 G2를
통과시키지 못하며 B2/B4 physical evidence 요구를 대체하지 않는다.

### 2026-08-18 original catalogue recheck

target original bundle을 read-only로 재확인했다. `MatroxMGAG400_8MB.table` 및
matching `.modes` file, `MatroxMGAG400_16MB.table`,
`MatroxMGAG450_16MB.table`가 존재했다. 8 MiB table의 title은
`Matrox MGA G400 (8MB or greater)`이고 explicit `MGA Memory Size` key는 없었다.
G400/G450 16 MiB tables에는 각각 `MGA Memory Size = 16` key가 있었다. G400 16
MiB와 G450 16 MiB `.modes` files는 byte-identical이었다. G450 32 MiB table/mode는
여전히 original bundle에 없었다.

이 결과는 original catalogue와 replacement parser의 separate responsibilities를
설명할 뿐이다. replacement parser의 original-compatible accepted range는 3..63
MiB이며, 8 MiB table title, 16 MiB configuration key, 또는 32 MiB board candidate
중 어느 것도 target physical total/type의 evidence가 아니며 G2를 바꾸지 않는다.

동일 recheck에서 G400 8 MiB `.modes` file은 640×480부터 2304×1778까지의
geometry/refresh/colorspace catalogue를 열거했다. 각 entry에는 pitch, dot clock,
blanking, RAMDAC/head applicability, current scanout allocation이 없으므로 이 list는
R3 timing source나 mapping-length proof가 아니다. G2 PASS 뒤에도 R3는 exact timing
source와 independently reviewed pitch/mapping bound를 별도로 요구한다.

## G2에 충분하지 않은 이유

G2는 **현 card**의 board identity, physical VRAM type/total, RAMDAC limit을
independent read-only evidence로 요구한다. 공개 guide가 PCI G450의 두 memory
variant를 모두 허용하므로, target의 `0525` device ID 및 `0d43` catalogue label과
결합해도 16 MB 또는 32 MB 중 하나를 선택할 수 없다. 또한 target의 existing
driver는 16 MB minimum profile을 사용하지만 runtime `IOGetDisplayMemory`와
`IOGetRAMDACSpeed`가 useful value를 공개하지 않았다.

따라서 아래 추론은 금지한다.

- PCI G450 family DDR 문구만으로 target VRAM type을 `DDR`로 hard-code하는 것
- PCI 32 MB product가 존재한다는 이유로 target mapping length를 32 MiB로 정하는 것
- 360/230 MHz family table만으로 current connector의 clock/mode limit을 선언하는 것

## 다음으로 허용되는 R2 evidence

1. physical board의 part number, memory-device markings, 또는 trusted service
   inventory처럼 현 card를 직접 식별하는 read-only evidence를 확보한다.
2. 가능하면 vendor-documented, replacement-only recovery environment의
   read-only identification path가 target-specific type/total을 반환하는지 확인한다.
3. 각각의 값에 primary source와 서로 독립적인 cross-check를 붙인다. product-line
   brochure와 PCI catalogue는 서로의 cross-check가 될 수 없다.

이 보고서는 hardware access를 새로 수행하지 않았으며, existing `MatroxMGA`의
ownership과 P3 prohibition을 바꾸지 않는다.
