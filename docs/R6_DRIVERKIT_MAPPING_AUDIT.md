# R6 — DriverKit Framebuffer Mapping API Audit

기준일: 2026-08-18  
상태: mapping API audit 및 offline three-range publication plan 완료. target
load·range publication·mapping은 아직 시작하지 않음.

## 질문

future `OpenStepMGAReplacementDisplay`가 sole-owner recovery boot에서 frame
buffer를 mapping해야 할 때, 어떤 OPENSTEP DriverKit path를 사용해야 하며 어떤
근거가 아직 부족한가를 local OPENSTEP 4.2 header와 공개 sample source로
검토한다.

## Local primary references

| source | confirmed fact | Matrox decision |
| --- | --- | --- |
| `ref/openstep/headers/NextDeveloper/Headers/driverkit/IOFrameBufferDisplay.h` | subclass는 `enterLinearMode`, `revertToVGAMode`, `displayMemorySize`, `ramdacSpeed`를 제공하며, legacy `mapFrameBufferAtPhysicalAddress:length:`도 선언함 | lifecycle method signatures는 이 header를 기준으로 유지 |
| `ref/openstep/headers/NextDeveloper/Headers/driverkit/IODirectDevice.h` | `mapMemoryRange:to:findSpace:cache:` / `unmapMemoryRange:from:`가 configured device memory range를 map/unmap하는 API임 | R6 candidate mapping API는 이 pair이며 cleanup도 대응 unmap으로 고정 |
| `ref/openstep/headers/NextDeveloper/Headers/driverkit/IODeviceDescription.h` | `memoryRangeList`, `numMemoryRanges`, `setMemoryRangeList:num:`가 configuration의 memory ranges를 보유 | mapping index/length은 driver-local hardcode가 아니라 reviewed recovery configuration에서 파생되어야 함 |
| `ref/openstep/examples/QVision/QVision_reloc.tproj/QVision.m` | init에서 configuration/mode 검증 뒤 range 0을 `mapMemoryRange`로 map하고, `free`에서 같은 range를 unmap; source comment는 legacy framebuffer-map API를 limited/obsolete 방향으로 설명 | QVision은 lifecycle reference일 뿐 Matrox BAR/range index/cache policy의 증거는 아님 |
| `ref/openstep/examples/S3/S3_reloc.tproj/S3.m` | legacy framebuffer-map API를 실제로 사용 | older example로만 보존하며 새 Matrox code의 mapping template으로 채택하지 않음 |

## Fixed R6 implementation contract

G1~G4가 통과하고 별도 R6 run approval이 주어진 경우에도, implementation은 아래
순서를 벗어나지 않는다.

1. recovery-only configuration이 exact MGA PCI function 하나만 claim하는지
   G1 matrix로 확인한다.
2. R2 physical profile과 R3 one-mode record를 source-linked data로 load한다.
   `MGA Memory Size` operator setting은 R2 evidence와 일치하는 input일 뿐,
   range length를 독자적으로 결정하지 않는다.
3. `deviceDescription`의 memory-range count와 selected range index/length가
   reviewed R6 run sheet와 정확히 일치하는지 검사한다. count, index, length,
   cache policy 중 하나라도 missing/ambiguous이면 fail closed한다.
4. selected mode validation과 documented VGA-safe transition이 성공한 뒤에만
   `mapMemoryRange`를 한 번 호출한다. result와 resulting virtual address를
   검증한다.
5. error path는 mapping이 존재할 때만 정확히 같은 index/address를 unmap하고,
   `revertToVGAMode` 및 superclass lifecycle로 복귀한다. repeated `free`와
   repeated revert가 idempotent인지 host/unit review와 controlled recovery boot에서
   각각 확인한다.

No R6 code may use the S3-style `mapFrameBufferAtPhysicalAddress:length:` path
unless a separate API compatibility reason is documented and reviewed. Neither
API may be called while original `MatroxMGA` owns the display.

## Offline admission implementation

`profile/OpenStepMGAMappingReview.{h,c}` enforces the configuration part of
this contract before any future DriverKit code is allowed to consume it. A
review must contain a passing R3 one-mode record, sole-owner snapshot evidence,
recovery-path evidence, reviewed range-list evidence, reviewed cache-policy
evidence, a bounded range index, and a range length at least as large as the
R3 visible footprint. It accepts no physical or virtual address.

Its 16 MiB/1600×1200×32 fixture is synthetic test data only. Passing the
validator does not pass G1/G2, does not record actual range values, and does
not permit a `mapMemoryRange` call.

## Deliberately unresolved inputs

이 audit은 API 방향만 정한다. 아래 값은 현재 확정하지 않으며 source constant나
`Default.table`에 넣지 않는다.

- Matrox framebuffer BAR index, physical base, map length
- MMIO range index/length and whether it must be separately mapped
- `IOCache` policy (QVision의 write-through choice를 Matrox에 전용하지 않음)
- scanout origin, pitch expansion, cursor/hidden allocation
- mode-programming register sequence, DAC/PLL values
- display-memory total/RAMDAC limit (R2)와 specific fixed mode (R3)

`R6_G450_RANGE_PUBLICATION_PLAN.md`는 original binary의 behavior-level static
evidence를 independently-written three-range data plan으로만 옮긴다. 따라서 R4
static gate는 `mapMemoryRange`, `unmapMemoryRange`, legacy framebuffer map API
모두를 계속 거부한다. 이 문서는 그 gate를 완화하지 않는다.

## Effect on the next implementation

R2와 R3가 PASS가 된 뒤 가장 먼저 만들 artifact는 hardware code가 아니라
`R6_MAPPING_CONFIGURATION_REVIEW.md`다. 그 record는 exact recovery
configuration, range count/index/length, cache policy reason, selected R3
mode evidence ID, map/unmap ownership/error matrix를 담아야 한다. 그 review와
operator 승인 전에는 replacement bundle의 matching table이나 mapping source를
변경하지 않는다.
