# C3 — 번들 이름을 줄인다, 설치가 되도록 (2026-08-29, 코딩 전)

## 1. 증상과 원인 — 재현했다

운영자가 `Installer.app` 에서 드라이버 패키지를 골랐더니 **아무 반응 없이
멈춘다**(선택지 2).  구조는 멀쩡하다 — `.bom` 23 항목 경로 정확, `.info`
정상, `pre_install` 실행 비트 있음, `VERIFY_DRIVER_PKG=PASS`.

문제는 **경로 길이**다.  `installer_tar` 는 100 자를 넘는 경로를 담지 못하고,
드라이버 번들의 nib 이 그것을 넘는다:

```
103  ./private/Drivers/i386/OpenStepMGAReplacementDisplay.config/.../data.nib
107                                                              .../data.classes
110                                                              .../data.dependency
```

그래서 `build-driver-pkg.sh` 는 **첫 패키징 커밋부터** 페이로드를
`installer_bigtar` 로 다시 만들어 왔다.  그 산출물은:

```
installer_bigtar tf   정상 (12 파일, nib 3 개)
시스템 tar tf         directory checksum error, 0 파일
installer_tar tf      끝나지 않는다 (180 초 timeout, 두 번 재현)
```

**Installer 가 추출에 `installer_tar` 를 쓰면 그 행이 곧 "안 열림" 이다.**
그리고 이것은 1.1 의 회귀가 아니다 — v1.0 도 같은 형식이었다.  지금까지
설치는 언제나 `tools/install-matrox-driver.sh` 로 했으므로 드러나지 않았다.

## 2. 그러므로 이름을 줄인다

경로에서 줄일 수 있는 것은 번들 이름뿐이다(`./private/Drivers/i386/` 은 OS
가 정하고, 디스플레이 인스펙터 nib 은 반드시 `DisplayInspector.nib` 이어야
한다).

```
이름                            자   최장   여유
OpenStepMGAReplacementDisplay   29   110   초과
OSMGAReplaceDisplay             19   100    +0
OSMGAReplaceDisp                16    97    +3
OSMGAG450Display                16    97    +3
OSMGADisplay                    12    93    +7
```

운영자 제안은 `OSMGAReplaceDisplay` 인데 **정확히 100 자, 여유 0** 이다.
한계가 포함인지 미만인지를 재려 했으나 `installer_tar` 가 그 실험에서도
행이라 답을 못 얻었다 — **답이 필요 없게 만드는 편이 낫다.**

**제안: `OSMGADisplay`(12 자, 여유 7).**  가장 긴 것이 `data.dependency`
인데 nib 에 파일이 하나 더 생기거나 이름이 길어지면 다시 넘으므로, 여유는
사치가 아니라 이 결함이 재발하지 않게 하는 값이다.  그리고 `OSMGA` 는 이
프로젝트의 C 심볼이 이미 전부 쓰는 접두사다.

## 3. 같은 규칙을 나머지에도

운영자 지시: 남겨 두면 부채가 된다.

```
OpenStepMGAReplacementDisplay  -> OSMGADisplay        살아 있는 드라이버 번들
OpenStepMGAMesaAccel           -> OSMGAMesaAccel      패키지 이름
OpenStepMGAProbe               -> OSMGAProbe          죽은 번들 (P1/P2 sidecar)
OpenStepMGAService             -> OSMGAService        죽은 번들 (같은 경로)
```

`OpenStepMesa342DemosMGA` 는 **바꾸지 않는다** — Mesa 포트의 이름이고 그
저장소가 정한다.

## 4. 무엇이 따라 바뀌나 — 세어 봤다

```
OpenStepMGAReplacementDisplay   98 파일
OpenStepMGAMesaAccel            18 파일
```

그중 위험한 것은 파일 개수가 아니라 **부팅 경로에 있는 것들**이다:

```
Instance0.table 의 "Class Names" / "Server Name" / "Driver Name"
System.config 의 Active Drivers 항목          <- 여기가 틀리면 화면이 안 나온다
설치 경로 /private/Drivers/i386/<이름>.config
<이름>_reloc 바이너리 이름과 kl_ld 의 -n 인자
```

## 5. 소스 파일 이름은 이 변경에 넣지 않는다

`OpenStepMGAHW3D.c`(93 파일), `OpenStepMGAEDID.c`(32), `OpenStepMGAWarpUcode.c`
(9) 등은 **경로 한계와 무관**하고, 부팅 경로도 아니다.  같은 규칙을 적용할
값어치는 있으나 **이 변경에 섞으면 부팅을 깨뜨릴 수 있는 diff 를 검토
불가능하게 만든다.**  별도 변경으로 미룬다 — 부채로 적어 두고 지운다.

## 6. 순서와 되돌리기

```
1  이름을 바꾸고 빌드한다 (아직 설치하지 않는다)
2  패키지를 만들고, installer_tar 가 페이로드를 읽는지 확인한다
   -- 이것이 이 변경의 합격 기준이다
3  install-matrox-driver.sh 로 설치
4  Active Drivers 를 새 이름으로 바꾼다 (Configure.app)
5  재부팅
```

4 를 빠뜨리면 기계가 **디스플레이 드라이버 없이** 뜬다.  복구는 문서에 이미
있다: 부팅 프롬프트에서 `config=Default`.

## 7. codex 에 물을 것

1. `OSMGADisplay` 로 충분한가, 아니면 `Class Names` 같은 곳에서 이름이
   충돌하거나 예약된 형태인가?
2. 번들 이름과 Objective-C 클래스 이름이 **달라도** 되나?  지금은 같다.
   클래스까지 바꾸면 diff 가 커지고, 안 바꾸면 둘이 어긋난다
3. `Active Drivers` 를 바꾸는 안전한 순서는?  새 이름을 **추가**하고 재부팅
   뒤 옛 것을 지우는 편이 안전한가, 아니면 한 번에 바꿔야 하나
4. 죽은 번들 둘을 개명하면 빌드가 깨지나 — 아무도 안 부르는데 Makefile 이
   여전히 참조하나?
5. 이 변경이 **되돌릴 수 있나**.  옛 번들을 지우지 않고 두면 두 이름이 다
   설치돼 있는 상태가 되는데, 그게 안전한가 위험한가
6. 빠뜨린 것

---

## 8. codex 교차검토 판정

| codex 주장 | 내 검증 | 결과 |
|---|---|---|
| **Objective-C 클래스는 바꾸지 마라.** `Class Names` 는 relocatable 안에 컴파일된 클래스를 가리켜야 한다.  번들 이름과 클래스 이름은 달라도 된다 — 이 워크스페이스의 `SpaceSaver2Mouse` 가 이미 `Class Names = SS2TrackPoint` 다 | `../openstep-spacesaver2ps2/SpaceSaver2Mouse/Instance0.table:7` 확인.  `SpaceSaver2Keyboard` 는 클래스를 **둘** 선언한다 | ✅ **채택.  diff 가 절반이 된다** |
| **검증기가 `installer_bigtar` 로 읽는다** — 이름만 바꾸면 Installer 를 멈추게 하는 그 형식을 계속 내보낸다 | `verify-driver-pkg.sh:29` — `BIGTAR=...installer_bigtar`, 주석까지 "SAME archiver the payload was written with" | ✅ **사실이고 이 결함의 핵심이다.**  **포장이 자기 자신과만 일치했다** — 잘못된 도구로 쓰고 같은 도구로 검증했으니 통과했다.  이름 변경은 *가능하게* 할 뿐, **고치는 것은 bigtar 경로를 지우는 것**이다 |
| 합격 기준을 `tf` 보다 강하게: `package` 의 기본 페이로드만 쓰고, **실제 `.tar.Z` 를 `installer_tar` 로 빈 디렉터리에 풀어** bom 과 대조하고, **100 자 이상 경로를 전부 거절**하라 | 논리 | ✅ 채택 |
| `Localizable.strings` 의 첫 키는 `Server Name` 을 따른다 | 파일 확인: 첫 키가 `"OpenStepMGAReplacementDisplay"` 다 | ✅ 채택 |
| **Active Drivers 는 한 번에 바꿔라.** 겹쳐 두면 같은 PCI 를 둘이 주장하고 소유가 순서에 달린다 | 논리 + 두 `+probe:` 가 다 G450 을 받는다 | ✅ **채택.  내 §6 이 위험했다** |
| 옛 번들은 `.config` 로 남기지 말고 `.prev` 로 옮겨라 — 설치돼 있는 것만으로는 안전하나 **발견 가능한 채로** 두면 위험하다 | 논리 | ✅ 채택 |
| **인스턴스 표 이관이 빠져 있다.** 새 경로는 첫 설치로 취급되어 `Location` 이 빈 출하 표를 받는다 | `install-matrox-driver.sh` 가 "instance tables preserved" 라고 말하는 것은 **같은 경로**일 때다 | ✅ **채택.  내 계획에 없던 단계다** |
| 죽은 번들은 디렉터리만 바꾸면 자기 시험이 깨진다 — `NAME`·`TOOLS`·reloc 하위프로젝트·표·시험 경로를 함께 | Makefile 확인: 자기 이름을 내부에 담고 있다 | ✅ 채택 |
| `OpenStepMGAMesaAccel` 개명은 **경로 한계와 무관한 정체성 이관**이다.  부팅 경로 커밋과 **분리하라** | 논리 | ✅ 채택 |

## 9. 그래서 계획이 바뀐다

**이름 변경은 고치는 것이 아니라 가능하게 하는 것이다.  고치는 것은 이것이다:**

```
build-driver-pkg.sh   bigtar 재작성 경로를 지운다
verify-driver-pkg.sh  bigtar 로 읽는 것을 지우고 installer_tar 로 푼다
                      + 스테이지의 100 자 이상 경로를 전부 거절한다
```

이름은 그 두 가지를 **가능하게** 만든다.  순서:

```
1  이름 바꾸고 (클래스는 그대로), Localizable 첫 키도
2  bigtar 경로 삭제, 검증기를 installer_tar 로
3  빌드 -> 패키지 -> installer_tar 로 실제 추출 (합격 기준)
4  인스턴스 표 이관: 알려진 정상 표에서 이름만 바꾼 것을 새 경로에
5  Active Drivers 를 한 번에 교체, 옛 번들은 .prev 로
6  재부팅
```

별도 커밋: `OSMGAMesaAccel` 정체성 이관, 죽은 번들 둘.

## 10. 1~3 단계 시행 결과

```
번들        OpenStepMGAReplacementDisplay -> OSMGADisplay
클래스      OpenStepMGAReplacementDisplay  (그대로 -- Class Names 가 가리킨다)
Driver Name / Server Name / Localizable 첫 키    -> OSMGADisplay
```

빌드 `BUILD_EXIT=0`, `OSMGADisplay.config/OSMGADisplay_reloc` 537,564 바이트.

포장에서 **bigtar 를 지웠다.**  이제 페이로드는 `package` 자신의
`installer_tar` 가 쓰고, 빌드가 100 자 이상 경로를 **규칙으로** 거절한다
(오늘 긴 세 개를 나열하는 것이 아니라 — 다음에 길어질 것은 그 셋이 아니다).
빌드에 "file name too long" 경고가 **하나도** 없다.

그리고 검증기가 **Installer 가 쓰는 도구로** 읽는다.  아홉 항목이 전부
나온다:

```
OSMGADisplay_reloc, OSMGADisplay, Default.table, Instance0.table,
Display.modes, English.lproj/Localizable.strings,
nib/data.classes, nib/data.dependency, nib/data.nib
```

`VERIFY_DRIVER_PKG=PASS`.

도중에 검증기의 `kill -0` 대기가 **정상 종료한 작업에서 "No such process" 를
찍어** 깨끗한 실행을 깨진 것처럼 보이게 했다.  표식 파일로 바꿨다.

## 11. 4~5 단계 시행 — 실기 상태를 바꿨다

```
백업        /private/Devices/System.config/Instance0.table.pre-osmga  (358 바이트)
설치        OSMGADisplay.config,  reloc 537,564 바이트
이관        살아 있던 표에서 두 줄만 바꾼 것을 얹었다
            Driver Name / Server Name -> OSMGADisplay
            Class Names / Location / Display Mode / Gray Levels /
            Mesa Acceleration / WARP 3D / VRAM Mmap / MGA Memory Size  전부 보존
교체        "Active Drivers" = "... OSMGADisplay"   한 번에, 겹침 없이
격리        옛 번들 -> OpenStepMGAReplacementDisplay.prev  (.config 가 아니므로 안 보인다)
```

이관이 **필요했다는 증거**: 설치 스크립트가 `first install: the built
bundle's instance table is in use` 라고 말했고, 그 표의 `Location` 은
비어 있었다.  얹지 않았으면 카드를 못 찾았을 것이다.

그리고 첫 시도가 조용히 실패했다 — 긴 한 줄을 원격 셸에 보냈다가 터미널이
엉켰고, `Location` 이 여전히 비어 있는 것을 보고서야 알았다.  **표를 읽어
확인하지 않았으면 그대로 재부팅했을 것이다.**

## 12. 재부팅 후 — 개명이 통했다

```
Aug 29 18:30:58  OpenStepMGAReplacementDisplay: linear mode ACTIVE 1600x1200 RGB:888/32
Aug 29 18:30:58  offscreen window OPENED 9322496..29360128 (19568 KiB); full-screen GL
Aug 29 18:30:53  M1-3a: Mesa acceleration switch is Yes
Aug 29 18:30:53  C2: WARP 3D preference is Yes
```

**로그 접두사가 아직 옛 이름인 것은 옳다** — 그 문자열은 클래스 이름에서
오고, 클래스는 일부러 안 바꿨다.  번들은 `OSMGADisplay` 이고 클래스는
`OpenStepMGAReplacementDisplay` 이며, `Class Names` 가 그 클래스를 가리킨다.

이관한 표가 그대로 읽혔다: 가속 켜짐, WARP 선호 켜짐, 1600x1200 복귀.
teapot 이 **환경변수 없이 128 제출** — 체크박스 설정이 이관을 건너 살아남았다.

```
빠른 회귀            PROBLEM 0 건
build-driver-pkg     PASS (payload by package/installer_tar)
verify-driver-pkg    PASS
check-bom-overlap    PASS
```

한 가지 자책할 것: 처음 로그를 볼 때 `grep "a|b|c"` 를 썼는데 이 `grep` 은
BRE 라 아무것도 안 나왔고, 잠깐 드라이버가 안 떴다고 생각했다.  `tail` 로
직접 보고서야 멀쩡한 것을 알았다.  **도구가 답을 못 찾은 것과 답이 없는
것은 다르다.**

## 13. 원인은 tar 가 아니었다 — `DiskName` 이었다

운영자가 남긴 오류:

```
Can't open package /private/tmp/pkgout/OSMGADisplay.pkg
(file OSMGADisplay.info contains no DiskName field).
```

**Installer 는 페이로드를 읽는 데까지 가지도 않았다.**  `.info` 에
`DiskName` 이 없어 거절한 것이고, 워크스페이스의 다른 모든 패키지에는
그 키가 있다 — Mesa 넷, 가속 하나.  드라이버 것만 없었고, 첫 패키징
커밋부터 그랬다.

그리고 **나는 그것을 보고도 지나쳤다.**  증상을 좁히던 중 두 `.info` 를
나란히 놓고 키 차이를 세었고, 가속 쪽에만 `DiskName` 이 있는 것을 적어
놓고서 "둘 다 그럴듯하다" 며 넘어갔다.  그 줄이 답이었다.

### 그러면 bigtar 작업은 헛일이었나 — 아니다, 그러나 원인은 아니었다

두 가지를 갈라 적는다:

```
증상의 원인          .info 에 DiskName 이 없다        <- 이것이 "안 열림"
따로 있던 결함       포장이 installer_bigtar 로 쓰고
                     검증기가 같은 도구로 읽었다.
                     Installer 의 installer_tar 는 그것에서 행한다
```

뒤엣것은 실재한다 — 재현했다(180 초 timeout, 두 번).  다만 그것은 **열린
뒤 설치할 때** 부딪혔을 결함이고, 운영자는 거기까지 가지 못했다.
`DiskName` 만 넣었으면 열렸을 것이고, **그 다음에 행이 났을 것이다.**

내가 잘못한 것은 고친 내용이 아니라 **인과를 확인하지 않고 단정한 것**이다.
"Installer 가 installer_tar 를 쓰면 그 행이 곧 안 열림이다" 라고 적었는데,
그 앞의 "쓰면" 을 검증한 적이 없다.

### 재발 방지

검증기가 `.info` 의 필수 키를 이름으로 검사한다:

```
info keys
  ok   Title / Version / Description / DefaultLocation / DiskName
```

파일이 그럴듯해 보이는 것과 Installer 가 받아들이는 것은 다르고,
그 차이를 사람 눈에 맡겼던 것이 이 결함이 오래 산 이유다.

## 14. 설치가 성공했고, 그러면서 **두 번째 결함**이 드러났다

패키지가 정상적으로 열렸고 운영자가 둘 다 설치했다.  가속 쪽은 온전하다:

```
/LocalDeveloper/Libraries/libGL_mga.a    2,103,924 바이트
/LocalDeveloper/Headers/                 OpenStepMGAHW3D.h, MesaBuffer.h, MesaHook.h
설치본으로 링크한 teapot                  표면이 엔진의 것, WARP 128 제출, 카드 100%
```

그러나 드라이버 설치가 **인스턴스 표를 출하본으로 덮었고**, 그 출하본이
아직 **옛 이름**을 담고 있었다:

```
설치 직후    "Driver Name" = "OpenStepMGAReplacementDisplay";   <- 낡음
             "Location"    = "";
             "Display Mode" = 1024x768,  WARP 3D No, Mesa No, Mmap No
Active Drivers = "... OSMGADisplay"
```

**그 상태로 재부팅했으면 디스플레이 드라이버가 뜨지 않았을 것이다** —
활성 목록은 `OSMGADisplay` 를 부르는데 그 번들의 표는 자기가
`OpenStepMGAReplacementDisplay` 라고 말한다.  화면이 멀쩡했던 것은 이미
메모리에 올라온 드라이버 덕분이지 디스크 상태 덕분이 아니다.

`pkg/Instance0.release.table` 이 내 개명에서 빠진 파일이었다.  고쳤고,
살아 있는 표는 이관본으로 되살렸다(28 줄, 1600x1200, 가속·WARP·mmap Yes).

### 검증기가 이제 이것을 잡는다

```
ok   Driver Name is OSMGADisplay
ok   Server Name is OSMGADisplay
ok   Class Names still names the compiled class
```

이름 셋을 **각각** 검사한다 — 둘은 번들을 따라야 하고 하나는 따라서는 안
된다.  "출하 표인지" 만 보던 기존 검사는 이 어긋남을 통과시켰다.

### 그리고 남는 문제 하나 (아직 안 고쳤다)

**패키지 설치가 운영자의 설정을 지운다.**  디스플레이 드라이버에서 그것은
`Location` 이 비워진다는 뜻이고, 곧 카드를 못 찾는다는 뜻이다.  이번에는
두 번 다 손으로 되살렸다.  설치 스크립트에는 보존 규칙이 있지만
**Installer 경로에는 없다** — `post_install` 이 기존 표를 병합하게 하는
것이 답일 텐데, 그것은 이 변경에 섞지 않고 따로 다룬다.
