# R1 — Sole-Owner Configuration Review

기준일: 2026-08-18

## 현재 판정

현재 production boot에서 `MatroxMGA`가 display owner인 것은 R0 evidence와
`R1-20260818-C` P-original instance check로 확인됐다. 그러나
replacement-only recovery profile은 아직 실기 Configure로 검토하지 않았으므로
G1 sole-owner configuration은 **미통과**다.

R1의 현재 산출물은 production configuration을 바꾸지 않는 staging isolation
검사와, 이후 recovery profile이 충족해야 할 review 표다. 이 문서나 R4 source는
replacement bundle을 설치하거나 PCI matching을 추가하는 권한이 아니다.

## Source-level staging isolation

`tools/check-r1-staging-isolation.sh`은 다음을 검사한다.

| 영역 | 요구 조건 |
| --- | --- |
| `Default.table` | `Auto Detect IDs`, `Display Mode`, `FB Address`, non-empty `Location` 없음 |
| resources | memory map, I/O port, IRQ, DMA table이 모두 empty |
| Makefile | `/private/Drivers/i386`, `/private/Devices` install target 없음 |
| class | `+probe:`가 항상 `NO`를 반환 |

이 검사는 source가 **그 자체로** production matching candidate가 되지 않음을
보일 뿐이다. existing driver-loader configuration에 남은 과거 artifact나 future
recovery profile의 matching precedence는 판정하지 않는다.

## Target read-only artifact check

`test/check-r1-staging-target.csh`은 아래 두 production directory에 staged
replacement artifact가 있는지만 확인한다.

```text
/private/Drivers/i386/OpenStepMGAReplacementDisplay.config
/private/Devices/OpenStepMGAReplacementDisplay.config
```

이 script는 read-only `-e` 검사만 수행한다. inline csh `foreach` 사용을 피하여
OPENSTEP csh의 continuation prompt 문제를 만들지 않는다.

최신 결과:

```text
OPENSTEP_MGA_R1_TARGET_ARTIFACT=absent:/private/Drivers/i386
OPENSTEP_MGA_R1_TARGET_ARTIFACT=absent:/private/Devices
OPENSTEP_MGA_R1_TARGET_STAGING_STATUS=pass
```

이는 replacement staging bundle이 두 production directory에 없다는 것만
증명한다. existing `MatroxMGA`와 future recovery replacement의 matching
precedence를 증명하지 않으므로 G1은 계속 미통과다.

동일한 target-native collector는 `/private/Devices -> Drivers/i386`, active
`MatroxMGA.config/Instance0.table`, 그리고 `/etc/rc`의 `driverLoader a` startup
entry를 확인했다. 상세 configuration model은
`../R1_DRIVERLOADER_CONFIGURATION_MODEL.md`를 따른다.

## Production snapshot field evidence

evidence ID: `R1-20260818-B`  
scope: target-native read-only `ls`/`sed`; Configure, `driverLoader`, bundle 변경 없음

현재 original bundle의 two-table snapshot을 직접 재수집했다.

| table | relevant observed fields | R1 해석 |
| --- | --- | --- |
| bundle `Default.table` | `Driver/Server Name=MatroxMGA`, `Family=Display`, legacy `Auto Detect IDs=0x0519102B`, 640×480 default mode, empty `Location` | bundle catalogue의 generic/legacy default다. 현재 card의 active match를 이것 하나로 판단할 수 없다. |
| active `Instance0.table` | `Default Table=MatroxMGAG400_16MB`, `Driver/Server Name=MatroxMGA`, `Auto Detect IDs=0x0525102B`, `Location=Dev:0 Func:0 Bus:4`, 1600×1200@60 RGB:888/32 | current production display candidate와 selected mode를 정의하는 configured snapshot이다. |
| directory relationship | `/private/Devices -> Drivers/i386` | 두 pathname은 independent staging location이 아니며 같은 production storage를 가리킨다. |

따라서 future replacement의 `Default.table`만을 원본의 legacy `0x0519` entry와
비교하는 것은 충분하지 않다. Configure가 만든 replacement `InstanceN.table`이
`0x0525102B` / `Dev:0 Func:0 Bus:4` function에 candidate가 되는 순간 original
`Instance0.table`과 **동시에 active일 수 없도록** complete snapshot 단위로 review해야
한다. 이 evidence는 P-original의 현재 상태만 보이며 P-recovery/P-failure snapshot을
만들거나 G1을 통과시키지 않는다.

`test/check-r1-original-snapshot-target.csh`는 이 P-original record의
default-table, driver name, exact PCI ID/location, display mode 및 production
replacement artifact 부재를 one final marker로 확인한다. Latest target result
`R1-20260818-C` is recorded in `R1_ORIGINAL_SNAPSHOT_CHECK.md`.

## Recovery staging artifact

`packaging/openstep/` now creates `OpenStepMGARecoveryStaging.pkg` only under
`/tmp/OpenStepMGARecoveryStage`. Its payload is rooted at
`DriverStaging/OpenStepMGAReplacementDisplay.config`, not a production driver
root; its pre-install hook rejects every `/private...` destination; its table
has no `Auto Detect IDs`; and the R4 class still rejects `+probe:`. The i386
BOM/payload verifier, safe-prefix and rejected-private-prefix hook checks, and
relocatable import allowlist all passed on target on 2026-08-18.

This adds a reviewable package artifact without creating a candidate for the
current PCI function. It does **not** provide P-recovery/P-failure instances,
Installer rollback evidence, or a G1 PASS verdict.

`recovery/OpenStepMGAG450Recovery.table` separately fixes the future
P-recovery input to the observed function, approved 16 MiB limit and only
1600x1200@60 mode.  It is not included in a bundle/package; a static gate
verifies that separation.  Its use awaits a recovery-capable driver, rather
than the current intentional `+probe: NO` skeleton.

## Recovery profile review template

G1을 통과하려면 아래 표를 **original profile과 replacement recovery profile
모두** 채워야 한다.

| boot profile ID | enabled bundle | explicitly excluded bundle | PCI matching scope | expected display owner | reviewer | verdict |
| --- | --- | --- | --- | --- | --- | --- |
| original production | `MatroxMGA` | replacement | current production function | `MatroxMGA` | pending | pending |
| replacement recovery | replacement | `MatroxMGA` | exact same function | replacement only | pending | pending |

두 profile 중 어느 하나라도 old/new 동시 matching candidate를 허용하거나,
matching precedence에 의존하면 G1은 실패다. current production state에서
replacement source가 build되는 사실은 recovery profile의 evidence가 아니다.

OPENSTEP의 profile이 `InstanceN.table`/Configure state를 포함하는 configuration
snapshot이라는 근거와 future review contract는
`../R1_DRIVERLOADER_CONFIGURATION_MODEL.md`에 기록한다.
