# D0 Target Harness Boundary

기준일: 2026-08-18

## 목적

이 문서는 pure-C D0 EDID parser가 OPENSTEP i386 user process로 실행되는지
확인하는 test harness만 다룬다. production D0 module
`edid/OpenStepMGAEDID.c`/`.h`의 기능·link dependency·hardware 권한을 넓히지
않는다.

## 실행 단계

`test/run-edid-policy-target.csh`는 exact `/tmp/OSMGAD0`에서 다음 순서로
build/run/delete한다.

| 단계 | binary | 목적 | 허용 의존성 |
| --- | --- | --- | --- |
| A | plain probe | target의 일반 C executable loader 확인 | stdio만 |
| B | D0-linked probe | parser object link와 target user runtime 확인 | target-only NetName + stdio bootstrap |
| C | full D0 test | parser/policy unit assertions 확인 | B와 동일 |

모든 단계는 build product를 `/tmp/OSMGAD0` 밖에 남기지 않는다. LKS load,
`kl_util`, DriverKit device lookup, display/server mode 변경, PCI/BAR/VRAM/MMIO,
DMA/IRQ/DDC 호출은 없다.

## B/C bootstrap의 제한

OPENSTEP target에서 plain C probe와 여러 runtime variant가 loader에서 시작하지
못한 관찰이 있다. 반면 이 workspace에서 current target 실행이 확인된 P2
`openstep-mga-namecheck` client는 C compiler default, `netname_look_up`, legacy
stdio를 사용하며 Foundation/`-lDriver`를 link하지 않는다. B/C harness는 존재하지
않는 `openstepmga-d0-bootstrap` name만 lookup하는 target-only NetName helper와
stderr `fprintf` marker를 포함해 그 정확한 user-runtime shape를 검증한다.

이 bootstrap은 target test executable의 process startup 진단용이다. 다음 항목은
엄격히 금지한다.

- `OpenStepMGAService` 또는 다른 LKS의 lookup/load/unload
- `MatroxMGA`, `Display0`, I/O device, framebuffer 조회
- PCI, memory mapping, engine, DDC, display mode API
- target configuration 또는 persistent filesystem 수정

## 결과 해석

| marker | 의미 | 다음 조치 |
| --- | --- | --- |
| `PLAIN_LOADER=pass` | generic C loader도 사용 가능 | B/C 결과를 parser link 관점에서 분석 |
| `PLAIN_LOADER=fail`, `LOADER=pass` | Foundation application runtime이 target executable bootstrap에 필요 | C full test 결과 확인 |
| `LOADER=fail` | parser assertion 전의 loader/link 경계 문제 | `nm -u` output만 기록; D0 code나 hardware path 변경 금지 |
| `IMPORT_GUARD=pass`, `TEST=pass` | target-native D0 통과 | D1 ownership 설계로만 진행; DDC hardware read는 여전히 금지 |

2026-08-18에 `cc -O -Wall` Foundation B runner를 한 번 실행했다. A와
B 모두 compiler exit status는 0이었고, B의 undefined imports에는
`_objc_msgSend`, `__objcInit`, `__cthread_init_routine`, `__mach_init_routine`가
있음을 확인했다. 그럼에도 A와 B는 모두 `Command not found`로 `main()` 전에
거부됐다. `-arch i386`, `-m486`, compiler-default 및 Foundation/Mach bootstrap
차이가 결과를 바꾸지 못했으므로, 이 runner를 반복 실행하지 않는다.

이후 target-native D0 parser claim은 보류한다. D0 production source의 host C89
test와 static guard만 통과 상태로 유지하며, target loader diagnosis는 P2에서
이미 실행된 user client artifact와 같은 조건을 독립적으로 재현할 수 있을 때만
별도 작업으로 재개한다.

그 independent control은 이후 exact `/tmp/OSMGALoaderControl`에서 실행했다.
기존 `openstep-mga-namecheck` P2 client는 `OPENSTEP_MGA_NAMECHECK ... result=1001`
을 출력해 정상 실행됐고, import table에 D0 probe에는 없던 `__iob`와 `_fprintf`가
있었다. D0 full test는 failure reporter 때문에 이미 `fprintf`를 포함한다. 따라서
stdio marker를 추가한 Foundation B도 loader에서 실패했고, Mach helper가 추가한
`_task_self_`/`_port_deallocate` import도 P2 namecheck와 달랐다. current revision은
Foundation/`-lDriver`/port API를 모두 제거하고, P2와 같은 `netname_look_up` +
`fprintf`만 남긴다. 이 helper/marker는 driver, display, PCI, DDC, file persistence를
접근하지 않는다.

그 exact-import revision도 probe import table이 P2 namecheck와 같은
`__iob`, `_fprintf`, `_name_server_port`, `_netname_look_up`, `_printf` 계열임을
보였지만, 여전히 `Command not found`로 `main()` 전에 실패했다. 따라서 D0 target
loader diagnosis는 여기서 중단한다. 현재 failure는 generic D0 C code, architecture
flag, Foundation/DriverKit link 여부, 그리고 visible undefined import table만으로
설명되지 않는다. 추가 target run은 같은 evidence를 반복할 뿐이므로 P2 client의
complete Mach-O/load-command layout을 non-destructively 비교할 별도 환경이 있을
때만 재개한다.
