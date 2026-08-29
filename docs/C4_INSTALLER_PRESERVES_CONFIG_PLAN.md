# C4 — Installer 경로도 인스턴스 표를 보존해야 한다 (2026-08-29, 코딩 전)

## 1. 사실

운영자가 `Installer.app` 으로 드라이버 패키지를 설치했더니 인스턴스 표가
출하본으로 덮였다:

```
설치 전 (이관본)   Location "Dev:0 Func:0 Bus:4", 1600x1200,
                   Mesa Yes, WARP 3D Yes, VRAM Mmap Yes
설치 직후          Location "",  1024x768,  셋 다 No
```

**디스플레이 드라이버에서 `Location` 이 비면 카드를 못 찾는다.**  이번에는
화면이 멀쩡해 보였는데, 그건 드라이버가 이미 메모리에 있었기 때문이지
디스크가 옳아서가 아니다.  재부팅했으면 드러났을 것이다.

`tools/install-matrox-driver.sh` 에는 보존 규칙이 있다.  **Installer 경로에는
없다.**  같은 패키지를 두 길로 설치할 수 있는데 한쪽만 설정을 지킨다.

## 2. 규칙은 이미 정해져 있다 — 새로 만들지 않는다

설치 스크립트가 정책과 그 이유를 적어 두었다:

> 기계의 인스턴스 표 **집합**이 upgrade 의 정본이다.  빌드의 것을 먼저
> 지우고 설치된 것을 복사한다.  그래서 `Instance1.table` 만 있는 기계가
> 조용히 `Instance0` 을 얻지 않는다.
>
> **바이트 단위로 보존한다.  병합하지 않고, 새 릴리스가 추가한 키로 채우지도
> 않는다** — 드라이버는 키가 없으면 알아서 기본값을 쓰고, **부재가 의미를
> 가질 수 있다**: `ColorSpace: BW:4` 인데 `Gray Levels` 가 없는 표는 legacy
> 4 단계 설정이고, 키를 채우면 그것을 조용히 256 으로 바꾼다.

Installer 경로에 옮길 것은 이 규칙 그대로다.

## 3. 설계

`pre_install` 은 압축 해제 **전에**, `post_install` 은 **후에** 돈다.
`$argv[2]` 가 설치 대상이다(가속 패키지와 Mesa 포트가 같은 관례를 쓴다).

```
pre_install    STASH 를 비우고, 설치된 번들에 Instance*.table 이 있으면
               거기에 복사한다.  없으면 아무것도 안 한다 (첫 설치)

post_install   STASH 에 표가 있으면
                 1) 페이로드가 쓴 Instance*.table 을 **전부** 지우고
                 2) 보관한 것을 되돌린다
               STASH 를 지운다
```

첫 설치에서는 STASH 가 비어 있고 출하 표가 그대로 남는다 — 새 기계가
시작 표를 얻는 길이 그것이므로 옳다.

## 4. 실패해도 표 없는 번들을 남기지 않는다

**지우기 전에 되돌릴 것이 읽히는지 먼저 확인한다.**  순서가 반대면
`post_install` 이 중간에 죽었을 때 인스턴스 표가 하나도 없는 번들이 남고,
그것은 부팅 후보가 아니다 — 화면 없이 뜬다.

```
STASH 의 표를 전부 읽어 본다 (cat > /dev/null)
  실패하면  아무것도 지우지 않고 출하 표를 그대로 둔다 + 경고
  성공해야  비로소 페이로드의 표를 지우고 되돌린다
```

모드도 되돌린다: 번들 안의 표는 `444` 다.

## 5. 취소된 설치가 남긴 STASH

`pre_install` 이 돌고 설치가 취소되면 STASH 가 남는다.  다음 설치의
`pre_install` 이 **먼저 비우므로** 낡은 것이 되살아나지는 않는다.  그러나
`post_install` 만 단독으로 도는 길은 없으므로 그것으로 충분하다.

## 6. codex 에 물을 것

1. `pre_install` 과 `post_install` 이 **같은 설치 세션**에서 반드시 짝으로
   도나?  `pre` 가 성공하고 `post` 가 안 도는 경우가 있나 (사용자 취소,
   디스크 부족)?  있다면 그때 기계는 어떤 상태로 남나
2. STASH 를 `/tmp` 에 두어도 되나 — Installer 가 격리된 환경에서 도나?
3. `$argv[2]` 가 `/` 일 때 경로 조립이 `//private/...` 이 되는데 문제 없나
4. 바이트 보존이 옳은가, 아니면 **새 키만 더하는** 병합이 나은가?  설치
   스크립트는 부재가 의미를 가진다는 이유로 보존을 골랐다.  그 이유가
   Installer 경로에서도 같은가
5. 이 스크립트가 실패하면 Installer 는 무엇을 하나 — 설치를 되돌리나,
   아니면 반쯤 된 채로 두나?  후자면 `post_install` 은 **절대 실패하면
   안 되고** 경고만 하고 0 을 반환해야 하나
6. 빠뜨린 것

---

## 7. 먼저 — 이 계획은 `R5` 의 규칙이 두 번째 경로에 닿는 것이다

`docs/R5_INSTALLER_KEEPS_THE_CONFIGURATION_PLAN.md` 가 **같은 결함을 스크립트
경로에서** 이미 다뤘고, 지금의 보존 규칙이 거기서 나왔다.  그 문서의 한 줄이
정본이다:

> **The installer installs the BUNDLE.  It does not install the CONFIGURATION.**

C4 는 새 규칙이 아니라 **그 규칙을 Installer 경로에도 적용하는 것**이다.
같은 패키지를 두 길로 설치할 수 있는데 한쪽만 규칙을 지키고 있었다.

## 8. codex 교차검토 판정

| codex 주장 | 내 검증 | 결과 |
|---|---|---|
| **드라이버 패키저는 `post_install` 을 담지도 않는다** — `:129` 가 `pre_install` 만 복사한다 | 확인.  가속 패키저는 `:175-177` 에서 **둘 다** 복사한다 | ✅ **사실.  빌더도 고쳐야 한다** |
| 두 훅이 **짝으로 돈다고 가정하지 마라.** 취소·크래시·디스크 부족이 `pre` 뒤 `post` 전에 올 수 있다 | 논리 | ✅ 채택 |
| **`/tmp` 는 나쁜 자리다** — 손상이 드러나는 바로 그 부팅에 지워진다 | 논리.  그리고 이 기계는 `/tmp` 가 부팅마다 비는 것을 이미 여러 번 겪었다 | ✅ **채택.  설치 대상의 부모(`/private/Drivers/i386/`)에 둔다** |
| **낡은 STASH 를 무조건 지우지 마라** — 그것이 중단된 설치의 유일한 사본일 수 있다 | 논리 | ✅ **채택.  내 §5 가 위험했다** |
| 지우기 전에 **복사하고 `cmp` 로** 확인하라.  root 에서는 `-r` 만으로 부족하다 | 논리 | ✅ 채택 |
| **실패했는데 0 을 반환하지 마라** — 못 뜨는 드라이버를 "성공" 으로 만든다 | 논리 | ✅ **채택.  내 §6-5 의 유혹을 접는다** |
| csh 의 glob 은 no-match 에서 **스크립트를 죽인다** | 이 저장소가 csh 함정을 여러 번 겪었다 | ✅ 채택 |
| 설치된 **집합**을 보존하라 — 페이로드에 없는 이름까지 | `R5` 의 규칙 그대로 | ✅ 채택 |
| `//private/...` 를 정규화하라 | 논리 | ✅ 채택 |
| 바이트 보존이 옳다, 병합하지 마라 | `R5` 가 같은 결론 | ✅ 일치 |
| Installer 의 취소·롤백 의미를 **탐침 패키지로 측정하라** | — | ⚖️ **부분 채택** (§9) |

## 9. 측정 캠페인은 하지 않는다 — 대신 주장을 좁힌다

codex 의 마지막 지적은 옳다: Installer 가 살아 있는 번들을 비원자적으로
바꾸고 중간에 끊길 수 있다면, **어떤 pre/post 쌍도 부팅을 보장할 수 없다.**

그러나 탐침 패키지로 취소·롤백 의미를 다 재는 것은 지금 얻을 것에 비해
크다.  그리고 재지 않아도 이 변경이 **엄밀히 개선**임은 말할 수 있다:

```
지금        설치가 설정을 지운다.  중단돼도 지운다.  사본이 없다
이 변경 뒤  정상 설치에서 설정이 남는다
            중단돼도 설정이 STASH 에 남아 복구할 수 있다
```

그러므로 **"부팅 안전을 보장한다" 고 말하지 않는다.**  말하는 것은
"정상 설치에서 설정이 보존되고, 비정상 종료에서도 사본이 남는다" 이다.
보장이 필요하면 완성된 번들을 통째로 원자 교체하는 설치 방식이어야 하고,
그것은 이 변경이 아니다.

## 10. 시행 결과

```
an upgrade keeps the machine's table
  ok   Location survived the install
  ok   the operator's switch survived
  ok   the stash is gone after a clean restore
without the hooks the value IS lost -- so the test is not vacuous
  ok   extraction alone loses Location, as it did on the machine
a first install keeps the shipped table
  ok   nothing to preserve, so the package's own table stays
an interrupted install leaves the configuration recoverable
  ok   the stash still holds the machine's table
  ok   a later install keeps it rather than overwriting it

CHECK_INSTALL_HOOKS=PASS
```

시험은 **모래상자**에서 돈다 — 훅이 대상을 `argv[2]` 로 받으므로 버리는
트리를 가리키면 실기 설정을 걸지 않고 진짜 스크립트를 돌릴 수 있다.
그리고 **공허 방지 팔**이 있다: 훅 없이 추출만 하면 `Location` 이 실제로
사라지는 것을 보인다.  그것이 없으면 "보존됐다" 가 "애초에 안 지워졌다" 와
구별되지 않는다.

검증기도 두 훅을 본다:

```
ok   pre_install is present and executable
ok   post_install is present and executable
ok   post_install restores the preserved instance tables
```

마지막 줄이 필요한 이유는 `post_install` 이 **아예 포장되지 않고 있었기**
때문이다 — 빌더가 `pre_install` 만 복사했다.  쓰고도 실리지 않는 훅은
이 패키지가 이미 두 번 겪은 결함의 모양이다.
