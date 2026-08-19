# C1 — Configure.app 인스펙터로 `Storm 2D Test` / `VRAM Mmap` 노출

기준일: 2026-08-19
상태: **완료(2026-08-19). C1-0 ~ C1-3 전부 실기 검증 통과.** 결과는 §7.
아래 본문은 착수 전 계획(codex 교차검토 1회 완료 후 개정판)을 그대로 둔다.
초판의 오류 3건을 이 개정판이 고쳤다 — §2-1(nib 없는 프로브는 거짓 음성),
§1(스톡 디스플레이 인스펙터가 없다는 주장은 거짓, `MatroxInspector` 존재),
§C1-2(파서는 `osmgaTextEquals` 정확 일치가 아니라 `osmgaTextContains`).

## 0. 목표와 비목표

**목표.** Configure.app에서 이 드라이버를 선택했을 때, 기존 디스플레이 모드
선택 UI를 **그대로 유지한 채** 아래 두 설정을 스위치로 켜고 끌 수 있게 한다.

| 키 | 라벨(안) | 기본 |
| --- | --- | --- |
| `Storm 2D Test` | Run Storm 2D engine self-test at boot | No |
| `VRAM Mmap` | Publish offscreen VRAM as a character device | No |

**비목표.** 실행 중 반영. 두 키는 드라이버 초기화 시점에만 읽히므로
(§2-3) 인스펙터는 **설정 파일만** 바꾸고 적용은 재부팅이다. UI에 그렇게
표시한다.

## 1. 확인된 사실 (근거 있음, 추측 아님)

| 사실 | 근거 |
| --- | --- |
| Display family 전용 인스펙터 `IODisplayInspector : IODeviceInspector`가 존재 | `/NextDeveloper/Headers/driverkit/IODisplayInspector.h` (타깃·미러 양쪽) |
| 확장 지점은 `- setAccessoryView:`. 프레임워크가 명시적으로 권장 | `IODeviceInspector.h` 주석: *"override the init method, load your supplemental nib file, and use the setAccessoryView: method to install it in the UI"* |
| `IODisplayInspector`는 `displayAccessoryHolder` 아웃렛을 따로 가짐 | 헤더 ivar 목록, 그리고 `VBE20DisplayDriver.config/English.lproj/DisplayInspector.nib/data.classes` |
| 드라이버 `.config`는 이미 `bundle.make` 번들 | `OpenStepMGAReplacementDisplay/Makefile` (`BUNDLE_EXTENSION = config`) |
| 현재 번들 실행파일에는 **클래스가 하나도 없다**(텍스트 0바이트) | 타깃 `size` 출력: `__TEXT 0 __DATA 0 __OBJC 0` |
| 드라이버 번들에 인스펙터 클래스를 넣는 것은 스톡 관행 | `ps2/PS2Mouse.config/PS2Mouse`에 `.objc_class_name_PS2MouseInspectorV2` |
| **스톡 디스플레이 드라이버가 `IODisplayInspector`를 서브클래싱한 선례가 있다** | `MatroxMGA2064WDisplayDriver.config/English.lproj/DisplayInspector.nib/data.classes`: `MatroxInspector = { OUTLETS = { locationsField }; SUPERCLASS = IODisplayInspector; }` |
| 다만 대부분의 스톡 디스플레이 드라이버는 넣지 않는다 | `VGA`, `S3`, `CirrusLogicGD542X` 실행파일에 `Inspector` 문자열 0건 |
| **Display family의 인스펙터 nib 이름은 `DisplayInspector.nib`로 고정** | Configure.app 자체(`/NextAdmin/Configure.app/English.lproj/DisplayInspector.nib`), VBE20, MatroxMGA2064W 세 곳 모두 같은 이름 |
| Configure.app에 `principalClass`/`bundleForClass:`/`DriverBundle` 심볼 존재 | 타깃 `strings /NextAdmin/Configure.app/Configure` |
| 이 워크스페이스에 성공 선례가 있다 | `openstep-spacesaver2ps2`의 `SS2MouseInspector`(+`compat/appkit/appkit.h`) |
| nib은 nibmaker로 생성 가능 | `openstep-nibmaker/tools/nibgraft.py`, 선례 `SpaceSaver2Mouse/nib-src/build-inspector-nib.py` |
| 타깃에는 `appkit` 헤더 트리가 없다 | 타깃 `ls /NextDeveloper/Headers/appkit` → not found. spacesaver의 `compat/appkit/appkit.h` 방식으로 우회 |

## 2. 가장 큰 미지수

**Configure.app이 드라이버 번들의 `IODisplayInspector` 서브클래스를 찾아
인스턴스화하는 정확한 규칙은 아직 모른다.**

알아낸 것과 못 알아낸 것을 구분한다.

- 알아낸 것: 이 패턴이 **실제로 출하됐다**(`MatroxInspector`). 그러므로
  "Display family에서는 아예 불가능"이라는 가설은 **기각됐다.**
- 못 알아낸 것: 선택 규칙. `Class Names`는 커널 드라이버 클래스이지
  인스펙터가 아니다(PS2Mouse `Default.table:6`은 `PS2Mouse`만 지정하는데
  실행파일 클래스는 `PS2MouseInspectorV2`다). 테이블에 `Inspector Class`
  같은 키도 없다. `principalClass` 심볼이 Configure에 있긴 하나 그것이
  드라이버 번들에 쓰이는지는 증명되지 않았다.

### 2-1. 여기서 나온 설계상의 함정 (초판 계획의 오류)

Display family의 인스펙터 nib 이름은 **`DisplayInspector.nib`로 고정**이고,
서브클래스를 싣는 드라이버는 **자기 번들에 그 nib을 함께 싣는다**
(`MatroxMGA2064W`, `VBE20` 둘 다). 즉 `IODisplayInspector`의
`loadMainNibFile`이 nib을 찾는 경로가 인스펙터 클래스/피검사 번들에 따라
달라진다.

따라서 **nib 없이 서브클래스만 넣는 초판의 C1-0 프로브는 위험하다.**
메인 nib을 못 찾아 인스펙터가 실패하면, 그것이 "Display family에서
서브클래싱이 안 된다"는 **거짓 음성**으로 읽힌다. C1-0은 nib을 함께
실어야 한다.

## 3. 단계

### C1-0 — 미지수 제거 프로브 (가장 싼 것 먼저)

두 가지를 **동시에** 확인한다: Configure가 우리 서브클래스를 쓰는가, 그리고
우리 번들의 `DisplayInspector.nib`을 찾는가. UI는 한 픽셀도 바꾸지 않는다.

1. `OSMGADisplayInspector : IODisplayInspector` — 메서드는
   `setTable:`(super 호출 + `NXLogError` 한 줄) 하나뿐.
2. Configure.app의 `English.lproj/DisplayInspector.nib`을 **그대로 복사**해
   우리 번들 `English.lproj/`에 넣고, `nibgraft.py`로 File's Owner 클래스명만
   `IODisplayInspector` → `OSMGADisplayInspector`로 바꾼다. 레이아웃·아웃렛·
   액션은 손대지 않는다. (spacesaver가 스톡 PS2 nib에 쓴 것과 같은 기법.)
3. `Makefile`에 `CLASSES = OSMGADisplayInspector.m`,
   `LOCAL_RESOURCES`에 `DisplayInspector.nib`, `OTHER_CFLAGS = -I./compat`.

**모달 패널을 쓰지 않는다.** 초판은 `setTable:`에서 `NXRunAlertPanel`을
띄우려 했으나, 이는 Configure가 테이블을 배정하는 도중 UI를 막고 테이블
배정마다 반복될 수 있다. 스톡 선례도 모달은 **사용자 액션 안에서만** 쓴다
(`SS2MouseInspector.m:244`). 프로브는 로그로 한다.

빌드 경로 확인 완료: `tools/build-matrox-driver.sh`는 번들 디렉터리에서
`make`를 돌리므로 `CLASSES`와 `TOOLS`(커널 reloc)가 함께 빌드된다. 그리고
`common.make`의 재귀 규칙은 상위 `CLASSES`/`OTHER_CFLAGS`를 서브프로젝트로
넘기지 않으므로, 커널 드라이버 빌드는 구조적으로 영향을 받지 않는다.

**검증 방법**
- V0-1 (원격, 무위험): `strings <bundle>/OpenStepMGAReplacementDisplay |
  grep objc_class_name` → `.objc_class_name_OSMGADisplayInspector` 1건.
  이것은 "클래스가 들어 있다"만 증명하며 로드·인스턴스화는 증명하지 않는다.
- V0-2 (원격, 무위험): 커널 드라이버 불변 확인. 설치 **전에** 새로 빌드된
  `*_reloc`을 따로 보관해 두고, 설치 후 `cmp`로 바이트 동일 + 크기 동일을
  본다(`sum`은 약한 체크섬이라 단독으로 쓰지 않는다).
- V0-3 (콘솔, operator): Configure.app → Display → 이 드라이버 선택.
  - 패널이 **스톡과 똑같이** 뜨고 모드 목록이 보이면 → 미지수 해소, C1-1로.
  - 패널이 뜨지 않거나 모드 목록이 비면 → **여기서 멈춘다.**
- V0-4 (원격): V0-3 직후 `NXLogError` 출력이 Configure의 stderr/콘솔 로그에
  남았는지 확인 → 우리 클래스가 **실제로 인스턴스화됐다**는 증거.
  V0-1과 달리 이것이 진짜 런타임 증거다.

이 단계는 커널 코드를 한 줄도 바꾸지 않는다. 실패해도 부팅과 디스플레이에
영향이 없다.

### C1-1 — 스위치 이식

C1-0에서 이미 우리 번들에 들어간 `DisplayInspector.nib`에 스위치 두 개를
`nibgraft.py`로 이식한다. 별도 액세서리 nib을 새로 만들지 않는다 — 스톡
레이아웃을 그대로 두고 컨트롤만 얹는 편이 잃을 것이 적다.

```
  [ ] Run Storm 2D engine self-test at boot
  [ ] Publish offscreen VRAM as a character device
      Both take effect after the next reboot.
```

File's Owner(`OSMGADisplayInspector`)에 아웃렛 `stormSwitch`, `mmapSwitch`,
액션 `toggleStorm:`, `toggleMmap:`을 추가한다.

배치는 두 안 중 하나이며 C1-0의 실제 패널을 보고 정한다:
- (a) 스톡 nib의 `displayAccessoryHolder` 영역에 넣는다 — 프레임워크가
  의도한 자리.
- (b) 그 영역이 스톡 nib에 실체로 없으면, 별도 박스를 만들어 `init`에서
  `[self setAccessoryView:]`로 설치한다(`IODeviceInspector.h` 권장 방식).

**검증 방법**
- V1-1 (호스트): `nibroundtrip`으로 생성 nib이 바이트 동일하게 왕복.
- V1-2 (호스트): `nibdump`/`nibtree`로 `data.classes`에 클래스명·아웃렛·
  액션 이름이 정확히 들어갔는지.
- V1-3 (호스트): 이식 **전후** nib을 `nibtree`로 비교해, 스톡 객체 그래프가
  추가분 외에는 변하지 않았음을 확인. 모드 UI를 건드리지 않았다는 증거.

### C1-2 — 인스펙터 구현

```objc
- init                     // [super init]; 우리 nib 로드; [self setAccessoryView:accessoryView]
- setTable:(NXStringTable *)instance
                           // [super setTable:instance]; 두 키 읽어 스위치 상태 반영
                           // 키가 없으면 "No"로 간주(Default.table과 일치)
- toggleStorm:sender       // table insertKey:"Storm 2D Test" value:"Yes"/"No"
- toggleMmap:sender        // table insertKey:"VRAM Mmap" value:"Yes"/"No"
```

주의점:
- `[super init]`/`[super setTable:]`를 반드시 호출한다. 모드 선택 UI는
  `IODisplayInspector`가 담당하므로 이를 건너뛰면 25개 모드 선택을 잃는다.
- 값 문자열은 반드시 `NXCopyStringBuffer()`로 복사해 넘기고 **이후 free하지
  않는다.** `NXStringTable`은 `HashTable`의 `insertKey:value:`를 그대로
  물려받을 뿐이고(`HashTable.h:47`), 헤더는 값을 복사하는지 포인터로
  들고 있는지 **명시하지 않는다.** 즉 "테이블이 소유권을 가져간다"는
  확정된 사실이 아니다. 스택 버퍼를 넘기면 테이블이 포인터를 보관할 때
  use-after-free가 되고, 반대로 테이블이 복사한다면 사본이 샌다.
  **출하된 스톡 관례(`SS2MouseInspector.m:201`)를 그대로 따르는 것**이
  이 불확실성에 대한 최선의 대응이며, 새로 추론하지 않는다.
- 드라이버 쪽 판정은 `osmgaTextContains(flag, "Yes")`(대소문자 구분 부분문자열)
  이며 `osmgaTextEquals`가 **아니다**(`.m:1187`, `.m:1284`, 함수 `.m:1038`).
  정확 일치 계약이 아니다: `"Yes"`뿐 아니라 `"YesNo"`도 켜지고, 대문자
  `"YES"`는 **꺼진다.** `"No"`가 특별한 것도 아니며 `"Yes"`를 포함하지 않는
  모든 문자열이 끔이다. 키가 없으면 `flag == 0`이므로 꺼짐.
  → 정규 문자열 `"Yes"`/`"No"`만 쓰면 의도대로 동작하지만, 이는 UI 관례이지
  파서가 강제하는 계약이 아니다.
- 값 문자열은 `NXCopyStringBuffer()`로 복사해 넘긴다 — 테이블이 소유권을
  가져간다. 선례: `SS2MouseInspector.m:205`
  (`[table insertKey:KEY_DEBUG value:NXCopyStringBuffer(on ? "Yes" : "No")]`).

**검증 방법**
- V2-1 (빌드): 타깃 `cc` 경고 0.
- V2-2 (콘솔, operator): Configure에서 **모드 선택이 여전히 동작**하는지 —
  해상도/색심도 조합을 하나 고르고 저장.
- V2-3 (원격, 무위험): 저장 후 `Instance0.table`을 telnet으로 읽어
  `"Storm 2D Test"`/`"VRAM Mmap"` 값이 스위치와 일치하는지 확인.
  이것이 **문자열 계약의 실제 증거**다.
- V2-4 (원격): 스위치를 다시 반대로 놓고 저장 → 값이 반대로 바뀌는지.
- V2-5 (콘솔): Configure에서 다른 드라이버를 골랐다가 **이 드라이버로
  돌아온다.** 스위치 상태와 선택된 모드가 모두 복원되는지 확인 —
  `setTable:`이 여러 번 불릴 때의 상태 복원과, 상위 클래스의 레이아웃
  패스가 나중에 우리 액세서리 뷰를 덮어쓰지 않는지를 함께 잡는다.

### C1-3 — 종단 검증 (두 키 **모두**)

여기까지는 파일만 바뀐 것을 확인했다. 이제 Configure에서 바꾼 값이 커널
드라이버 동작을 실제로 바꾸는지 본다. **두 키를 각각, 양방향으로** 한다 —
초판은 `Storm 2D Test`만 검증하려 했는데 그것으로는 `VRAM Mmap`이 증명되지
않는다.

`Storm 2D Test`
- V3-1: 켜고 재부팅 → `/private/adm/messages`에 `OpenStepMGA S1/S2/S3`
  자체시험 로그가 뜨는지.
- V3-2: `test/openstep-mga-stats-probe.m`으로 `stormBlitReady == 1`인지
  (S3 자체시험이 이 플래그를 세운다, `.m:2034`).
- V3-3: 끄고 재부팅 → 자체시험 로그가 **사라지고** `stormBlitReady == 0`인지.

`VRAM Mmap`
- V3-4: 켜고 재부팅 → S4a cdev 등록 로그가 있고
  `test/openstep-mga-vram-mmap-probe.m`이 통과하는지.
- V3-5: 끄고 재부팅 → 등록 로그가 **없고** 프로브가 실패하는지.

V3-5는 특히 중요하다. 드라이버는 켜졌을 때만 cdev를 공개하고(`.m:1274`),
한 번 공개하면 클라이언트 매핑이 드라이버보다 오래 살기 때문에 언로드가
금지된다(`.m:1329`). "끄면 정말 안 열린다"가 그 안전 규칙의 전제다.

## 4. `Default.table`

두 키는 이미 `Default.table`에 있고 기본 `"No"`로 문서화돼 있다(2026-08-19
정리 커밋). 인스펙터 작업에서 추가로 넣을 것은 없다. 다만 인스펙터가 키가
**없는** 인스턴스도 다루게 되므로(구버전에서 올라온 `Instance0.table`),
`setTable:`에서 키 부재를 `"No"`로 해석한다 — 드라이버의 해석과 동일하다.

## 5. 롤백

인스펙터는 전적으로 유저랜드다. 최악의 경우가 "Configure에서 이 드라이버
인스펙터가 뜨지 않거나 Configure가 죽는다"이며, **부팅·디스플레이·커널
드라이버에는 영향이 없다.**

되돌리는 법: `Makefile`에서 `CLASSES` 줄을 빼고 다시 빌드·설치하면 번들
실행파일이 다시 비고 Configure는 내장 `IODisplayInspector`로 돌아간다.
`*_reloc`(커널 드라이버)은 이 작업에서 한 번도 바뀌지 않는다 — V0-2가 그것을
매 단계 `cmp`로 확인하고, `common.make:284`의 재귀 규칙이 상위 `CLASSES`를
서브프로젝트로 넘기지 않는다는 점이 구조적 근거다.

## 6. 이 계획이 결정하지 않은 것

- 인스펙터에 다른 설정(예: `MGA Memory Size`, `Display Mode` 문자열 직접 편집)을
  추가할지. 지금은 두 스위치만.
- 라이브 반영. 불가능하다고 판단했으나(§0), C1-3까지 끝난 뒤 `IODeviceMaster`
  기반 라이브 토글이 의미 있는 키가 생기면 그때 별도로 계획한다.

## 7. 실행 결과 (2026-08-19)

전 단계 통과. 커널 코드는 한 줄도 바뀌지 않았고 `*_reloc`은 매 빌드·설치에서
`cmp` 바이트 동일이었다.

| 검증 | 결과 |
| --- | --- |
| V0-1 클래스 링크 | `nm`에 `.objc_class_name_OSMGADisplayInspector` 정의, `IODisplayInspector`는 U(Configure 안에서 해석) |
| V0-2 커널 불변 | 빌드·설치 전후 `cmp` RC=0 (매번) |
| V0-3 패널 표시 | 스톡과 동일하게 표시 |
| V0-4 런타임 인스턴스화 | `Configure[528]: OSMGADisplayInspector: setTable: ...` |
| V1-1/2/3 nib | `nibroundtrip` 바이트 동일, XML 검증 통과, 이식 전후 객체 그래프 비교로 스톡 구조 보존 확인 |
| V2-1 빌드 | 경고 0 |
| V2-2 모드 선택 | 정상 동작 |
| V2-3 테이블 기록 | `Storm 2D Test` 켬 → `"Yes"` 기록. **Configure가 `Instance0.table`을 통째로 재작성하면서도 우리 키 2개를 보존** |
| V3-1/2 Storm 켬 | S1 PASS, S2 2/2 PASS, S3 6/6 PASS, `stormBlitReady == 1` |
| V3-3 Storm 끔 | 자체시험 로그 **전무** |
| V3-4 Mmap 켬 | cdev major 1 등록, 매핑 OK, 자기검사 0 bad, 가드 2종 정확히 거부 |
| V3-5 Mmap 끔 | `S4a` 로그 **전무**, `character major = 4294967295`, `open failed errno=6`(ENXIO) |

### 7-1. 계획이 틀렸던 곳 — `displayAccessoryHolder`가 아니었다

계획의 C1-1은 스위치를 뷰 49(콘텐츠 뷰)에 상자 48과 **나란히** 두려 했다.
그렇게 하니 **모드 UI는 보이는데 스위치는 보이지 않았다.**

원인: `IODisplayInspector`는 이 nib의 콘텐츠 뷰 전체를 쓰지 않는다. **`displayMode`
상자(48) 하나만** 꺼내 기반 인스펙터의 액세서리 영역에 설치하고 나머지는 버린다.
따라서 그 상자 **바깥**에 놓인 컨트롤은 화면에 올라갈 통로가 없다.

수정: 상자 48과 그 콘텐츠 뷰 52를 66 → 136으로 키우고 기존 자식(54, 55)을 위로
올린 뒤, 스위치와 안내문을 상자 **안**에 넣었다. 그래서 늘어난 것은 창이 아니라
상자다.

`displayAccessoryHolder`(oid 54)는 끝내 쓰지 않았다 — `data.classes` 어디에도
선언되지 않은 `SwitchView` 클래스의 CustomView이고, 362×19이며, Box 55 밑에
깔려 있어 무엇도 보여주지 못하는 자리다.

### 7-2. 두 스위치는 서로 독립적이다 (V3-4에서 부수적으로 확인)

`Storm 2D Test`를 끄고 `VRAM Mmap`만 켠 부팅에서, mmap 프로브의 채우기 항목이
`-702`(`IO_R_RESOURCE`)로 거부됐다. 이는 실패가 아니라 정확한 동작이다 —
채우기는 mmap 등록 **과** `stormBlitReady`를 둘 다 요구하는데 후자가 0이었다.
두 플래그가 서로 간섭하지 않고 각자의 경로만 여닫는다는 증거다.
