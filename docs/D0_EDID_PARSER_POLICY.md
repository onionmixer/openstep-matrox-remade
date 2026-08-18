# D0 — EDID Base-block Parser and Fixed-mode Policy

기준일: 2026-08-18

## 범위와 안전 경계

`edid/OpenStepMGAEDID.c`는 단일 128-byte EDID base block을 해석하고, 이미
검증된 fixed mode table과의 교집합만 선택하는 순수 C module이다. DriverKit,
PCI, BAR, VRAM, GPIO, DDC transaction, display mode programming을 import하거나
호출하지 않는다.

따라서 이것은 현재 `MatroxMGA`와 병렬로 실행되는 기능이 아니다. 향후 clean-room
replacement display driver가 device를 단독 소유한 경우에만 DDC2B reader의 결과를
이 module에 전달할 수 있다. DDC가 없거나 parsing에 실패한 경우에는 caller의
known-good profile/manual fallback을 그대로 유지한다.

## 입력 검증

`OSMGAParseBaseEDID()`는 다음을 확인한다.

| 항목 | 처리 |
| --- | --- |
| base header | 정확한 8-byte EDID header가 아니면 reject |
| checksum | 128 byte sum이 0이 아니면 reject |
| manufacturer | EISA 3-letter encoding이 유효하지 않으면 reject |
| version | EDID major version `1`만 base-layout parser로 수용; revision/extension count는 metadata로 기록 |
| manufacturer/product/serial | read-only metadata로만 decode |
| preferred timing | EDID feature flag가 선언한 경우에만 base block의 **첫** detailed timing descriptor 사용 |
| interlaced preferred timing | metadata로는 보존하지만 자동 선택은 거부 |

후속 descriptor나 extension block은 D0 범위 밖이다. 첫 descriptor가 timing이
아니면, 뒤의 timing을 preferred timing으로 오인하지 않고 fallback한다.
`extension_count`는 진단 metadata로만 보존하며 extension block을 읽거나
extension의 timing을 자동 선택에 사용하지 않는다.

## mode 정책

`OSMGASelectDisplayMode()`의 순서는 고정돼 있다.

1. non-null manual mode는 driver의 검증된 fixed mode table에 exact match할 때만
   선택한다.
2. 유효한 progressive preferred timing이 있더라도 fixed mode table에 같은
   width/height와 1 Hz 이내 refresh가 있어야 자동 선택한다.
3. EDID 없음, header/checksum 오류, first DTD 부재, interlaced timing,
   미지원 timing은 모두 `fallback_reason`으로 반환하며 새 mode를 만들지 않는다.

`OSMGAEDIDReasonString()`은 위 fallback reason의 stable diagnostic text만
제공한다. 로그 표시용이며 mode programming 또는 hardware ownership 판단에는
사용하지 않는다.

모든 public parser/selection entry point는 null input/output pointer를 실패로
처리한다. unknown reason enum은 diagnostic text `unknown`으로만 변환한다.

이 방식은 EDID가 임의 timing 또는 runtime mode switch 권한을 주지 않도록 한다.
G450의 DDC GPIO access와 actual mode programming은 D2/D3의 단독 display-owner
gate 뒤에만 다룬다.

`OSMGAParseManualDisplayMode()`는 existing configuration의
`Height: 1200 Width: 1600 Refresh: 60Hz ...` 형식을 pure C로 해석해 manual mode
input으로 바꾼다. 필드 순서와 `Hz` 단위를 확인하며, `ColorSpace` 뒤의 text는
mode selection에 사용하지 않는다. 단 trailing text는 비어 있거나 non-empty
`ColorSpace:` field여야 한다. 이 parser도 config 문자열만 다루며 hardware mode
switch를 수행하지 않는다.

따라서 사용자가 명시한 mode는 EDID보다 우선하지만, driver가 실제로 지원하지 않는
timing을 새로 만들거나 programming하지 않는다. table에 없는 manual mode는
`invalid-manual-mode` fallback으로 끝난다.

## i386 호환성

refresh는 `unsigned long` 32-bit인 OPENSTEP i386에서도 overflow하지 않도록
100 mHz 단위의 remainder 계산으로 만든다. 비현실적으로 큰 refresh result는
DTD 자체를 reject한다. parser와 test는 C89 경고를 error로 처리하는 host build를
통과해야 한다.

## 검증

다음 명령은 하드웨어 없이 실행한다.

```
sh openstep-matrox-remade/tools/check-d0-no-hardware.sh
sh openstep-matrox-remade/test/run-edid-policy-host.sh
```

2026-08-18 결과:

```
OPENSTEP_MGA_EDID_TEST_STATUS=pass
```

static guard는 D0 production source의 system header import, DriverKit/Mach API,
PCI/MMIO/port-I/O helper를 거부한다. comment는 제외해 검사하므로 documentation
text가 아닌 실제 code/import만 gate 대상이다.

test는 valid 1600x1200@60 preferred timing, manufacturer/product/serial decode,
manual precedence, 1 Hz refresh tolerance, no-EDID/unsupported fixed mode,
invalid header/checksum/manufacturer, interlaced timing, first DTD 부재와 뒤 descriptor의 timing
무시, preferred-timing feature flag 부재, manual mode의 field
order/unit/range/32-bit overflow, unsupported EDID major version을 포함한다.

OPENSTEP i386 compiler 확인은 다음 target-native runner로 별도 실행한다. 이
runner는 LKS를 load하지 않고 exact `/tmp/OSMGAD0`에서 C binary만
build/run/delete한다. P2 target-native client와 같은 target compiler default
user-program convention(`cc -O -Wall`)을 사용한다. 실행 전 `nm -u`로 `_memset`, `_memcpy`, `_strcmp` import가 없는지도
확인한다. 이 세 legacy libc dependency는 target loader issue를 피하기 위해
D0 source/test에서 사용하지 않는다.

```
csh -f /ndrv/openstep-matrox-remade/test/run-edid-policy-target.csh \
    /ndrv/openstep-matrox-remade
```

초기 target run은 compile marker는 통과했지만 test binary가 `Command not found`로
시작되지 않았다. 이미 실행되는 P2 client와 비교해 D0 binary에만 `_memset` import가
있음을 확인했고, 그 뒤 D0 source/test의 `memset`/`memcpy`/`strcmp` 의존성을 작은
C89 helper로 제거했다. host import guard와 unit test는 통과했다.

수정 뒤 target runner는 `OPENSTEP_MGA_D0_TARGET_BUILD=pass`와
`OPENSTEP_MGA_D0_TARGET_IMPORT_GUARD=pass`까지 통과했지만, D0 object를 link한
minimal probe도 `Command not found`로 시작하지 못했다. 그러므로 현 failure는
parser unit assertion이 아니라 target loader/link ABI 경계의 문제로 취급한다.
full parser test의 target-native 통과 주장은 아직 하지 않는다.

target runner는 full unit test 전에 같은 D0 object를 link한 minimal loader probe를
실행한다. runner는 이제 plain C probe → D0-linked probe → full parser test 순으로
실행한다. probe failure와 full-test failure를 구분해 legacy loader/ABI 문제를
parser logic 문제로 오인하지 않기 위해서다. D0-linked probe의 `nm -u` output도
남겨 unsupported import를 확인한다.

현재 target은 plain C probe도 loader 단계에서 시작하지 못한다. `task_self()`/`-lDriver`
variant 및 존재하지 않는 NetName service를 lookup하는 Mach IPC variant도 같은
`Command not found`로 실패했다. 따라서 이 결과는 D0 parser assertion이나 hardware
path가 아니라 target executable loader/link ABI 경계의 문제로 유지한다.

`-arch i386`와 `-m486` revisions 모두 같은 loader failure를 재현했다. P2
target-native client와 같은 compiler-default `cc -O -Wall` Foundation revision도
compile 후 plain/B probe가 모두 `Command not found`로 `main()` 전에 실패했다.
B의 import table에는 Objective-C, cthread, Mach startup symbols가 포함됐으므로
Foundation link 누락도 원인이 아니다. 따라서 target-native parser success는
주장하지 않는다. 이 runner는 반복하지 않고, P2에서 실행된 client artifact와
동일한 loader condition을 독립적으로 재현할 수 있을 때만 별도 진단으로 재개한다.

그 후 독립 P2 `openstep-mga-namecheck` control binary는 target에서 정상 실행됐다.
그 import에는 D0 loader probe에 없던 `__iob`/`_fprintf`가 있고, full D0 test에는
failure reporting을 위해 이미 `fprintf`가 있다. current probe revision은 stderr
marker 하나를 넣어 full-test와 stdio runtime shape를 맞췄지만 Foundation B는 계속
실패했다. Mach helper의 `_task_self_`/`_port_deallocate` import도 P2 namecheck에는
없었다. current revision은 current P2 control과 동일하게 Foundation/`-lDriver`/port
API 없이 NetName lookup + `fprintf`로 build한다. 이는 parser production code나
hardware boundary를 바꾸지 않는 target-loader diagnostic이다.

이 exact-import revision도 `__iob`, `_fprintf`, `_name_server_port`,
`_netname_look_up`, `_printf` table을 출력했지만 `main()` 전에 실패했다. 그러므로
D0 target test는 더 이상 반복하지 않는다. target-native pass는 보류하고, P2
client의 complete Mach-O/load-command layout과 비교할 별도 diagnostic 환경이
준비될 때만 다시 조사한다.

이 workspace의 정상 OPENSTEP application/test link command는 Foundation framework를
일관되게 포함한다. 다음 target-only revision은
`OSMGA_TARGET_FOUNDATION_BOOTSTRAP`일 때 `NSAutoreleasePool` 하나를 생성·해제하고
`-framework Foundation`으로 link한다. 이는 AppKit session을 열지 않으며,
graphics/device/PCI/display/MGA API를 호출하지 않는다. bootstrap은 test executable에만
존재하고 D0 production source에는 포함되지 않는다.

target harness의 단계·허용 API·결과 해석은
`docs/D0_TARGET_HARNESS.md`에 분리해 기록한다.

## D0.1 — offline linear-memory admission

`OSMGAModeFitsLinearMemory`는 mode, byte-addressable 8/16/24/32 bpp, pitch,
그리고 **이미 별도로 입증된** available-byte limit만 받아 `pitch * height`를
32-bit overflow 없이 계산한다. map/DriverKit/PCI/DDC 호출이나 current VRAM 추정은
없다. failure에서는 caller의 `required_bytes` output도 바꾸지 않는다.

1600x1200x32/6400-byte pitch의 required size는 7,680,000 bytes임을 host C89 test로
고정했다. 이 값은 현재 card가 16 MiB라고 증명하거나 offscreen allocation을
허용하는 근거가 아니다. D1 G2/G3에서 independent profile limit을 얻은 뒤 mode
table의 offline admission에만 사용할 수 있다.

## D0.2 — P3 evidence admission

`OSMGACanEnterP3`는 PCI inventory, physical VRAM size/type, existing owner가
남긴 offscreen range, mapping compatibility의 다섯 independent evidence flag를
순서대로 검사한다. 첫 missing gate를 반환할 뿐 device mapping, MMIO, VRAM,
engine, DDC, DriverKit API를 호출하지 않으며 `hardware_ready`를 바꾸지 않는다.

현재 기록은 PCI inventory만 verified이고, 반환 gate는 `VRAM_SIZE`다. 특히
`Instance0.table`의 16 MiB profile 또는 `102b:0d43` catalogue label을 이 구조의
physical VRAM flag로 설정하는 것은 금지한다. 함수의 ready return 자체도 P3
hardware action 권한이 아니라 `P2_RESOURCE_OWNERSHIP.md` review gate 통과를
표현하는 software precondition일 뿐이다.
