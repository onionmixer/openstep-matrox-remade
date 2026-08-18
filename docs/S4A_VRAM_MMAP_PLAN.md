# S4a — 오프스크린 VRAM을 유저 태스크에 매핑 (`cdevsw` mmap)

기준일: 2026-08-19
상태: codex 교차검토(§8) 반영해 **대폭 개정**. 구현 미시작.
**차단 요건 2건(캐시 속성·언로드 수명)이 해결되기 전에는 Mesa 연결 불가.**
선행: [S3a](S3_IODISPLAY_DO_BLIT_PLAN.md) PASS(엔진 구동 경로 완성),
[S3b-prep](S3B_PREP_INSTRUMENTATION_PLAN.md) §14(관문 조사).

## 0. 왜 이것이 S4의 전제인가

**Storm 엔진은 VRAM만 조작할 수 있다.** Mesa가 지금처럼 일반 RAM에 렌더하면
(OSMesa 방식) 엔진은 그 버퍼를 읽지도 쓰지도 못한다 — `Clear`를 하드웨어로 돌릴
수도, 화면으로 blit할 수도 없다.

따라서 **Mesa의 렌더 타깃이 VRAM에 있어야만** 가속이 성립한다. 유저 프로세스가
VRAM을 자기 주소공간에 매핑받는 것이 S4의 선택이 아니라 **전제**다.

## 1. 확정된 커널 계약 (IDA 정독)

### 1-1. `cdevsw` mmap 규약

`_smmap`(0x106e58) 정독:
```c
v5 = funcs_106FAE[11 * major];      /* funcs_106FAE = cdevsw + 0x20
                                       = 엔트리당 11개 포인터 중 인덱스 8 */
if (v5 == nulldev || v5 == nodev || !v5)  → EINVAL

/* 요청 범위의 모든 페이지를 먼저 검증한다 */
off = 0;
while ( v5(dev, off + fileOffset, prot) != -1 ) {
    off += page_size;
    if (len <= off) break;
}                                   /* 하나라도 -1이면 EINVAL, 매핑 안 함 */

obj = vm_object_special(dev, v5, prot, fileOffset, size);
vm_map_find(target_task, obj, 0, &address, size, 0);
```

`vm_object_special`(0x17c17c) 정독:
```c
v10 = a2(dev, offset + (i << page_shift), prot);   /* 페이지마다 1회 */
*v9 = v10 << page_shift;                           /* 물리주소 = 반환값 << PAGE_SHIFT */
vm_page_insert(...);
```

→ **확정된 규약**
| 항목 | 값 |
| --- | --- |
| 시그니처 | `int mmap(dev_t dev, off_t offset, int prot)` |
| 반환 | **페이지 프레임 번호(PFN)**. 커널이 `<< PAGE_SHIFT`로 물리주소를 만든다 |
| 거부 | **`-1`** |
| 호출 횟수 | **페이지당 최소 2회** — `_smmap` 검증 루프에서 1회, `vm_object_special` 채우기에서 1회 |
| 인덱스 | `cdevsw` 엔트리의 **8번**(`open,close,read,write,ioctl,stop,reset,select,**mmap**,getc,putc`) |

**페이지 단위로 호출되므로 페이지별 범위 검증이 자연스럽다.**

### 1-3. ⚠ 치명적 — `vm_object_special`은 `-1`을 검사하지 않는다

내 최초 서술("페이지마다 1회")은 **틀렸다.** 같은 오프셋에 대해 **최소 두 번**
호출된다(검증 1회 + 채우기 1회). 그리고 `vm_object_special`의 2차 호출에는
**거부 검사가 없다**:

```c
v10 = a2(dev, offset + (i << page_shift), prot);
*v9 = v10 << page_shift;      /* ← -1 검사 없음 */
vm_page_insert(...);
```

→ 1차(검증)에서 통과했다가 2차(채우기)에서 `-1`을 반환하면
`-1 << PAGE_SHIFT = 0xFFFFF000`이 **물리주소로 그대로 쓰인다.**

**따라서 핸들러는 완전히 결정적이어야 한다**: 같은 `(dev, offset, prot)`에 대해
항상 같은 답. 판정에 쓰는 상태는 **cdevsw 등록 수명 동안 불변**이어야 한다.
`vramWindowOpen` 같은 **가변 플래그를 판정에 쓰면 안 된다**(1차 통과 후 2차 전에
꺼지면 위 경로로 들어간다).

### 1-2. 등록 경로 — DriverKit 정식 메서드

`+[IODevice addToCdevswFromDescription:open:close:read:write:ioctl:stop:reset:
select:mmap:getc:putc:]`(0x1a44a8) 정독:
- config table의 문자열 키를 읽어 10진수로 파싱 → 원하는 major, 없으면 `-1`
- `IOAddToCdevswAt(major, ...)` 호출
- 성공 시 `[self setCharacterMajor:major]`

→ **유저스페이스가 major 번호를 알 수 있다**: `IODevice.h`의
`IO_CHARACTER_MAJOR "IOCharacterMajor"`를 `IODeviceMaster getIntValues`로 조회.
우리가 이미 실증한 경로(§S3b-prep 9-2)다.

커널 심볼은 전역 export 확인됨: `_IOAddToCdevsw`(0x1a9d30),
`_IOAddToCdevswAt`(0x1a9c6c), `_IORemoveFromCdevsw`(0x1a9dec).

## 2. 하는 일

### 2-1. 문자 디바이스 등록

`initFromDeviceDescription:` 말미에서(플래그로 게이팅) 문자 디바이스를 등록한다.
`open`/`close`/`mmap`만 제공하고 나머지는 `nodev` 상당으로 둔다.

### 2-2. `mmap` 핸들러 — 결정적·불변·방어적

§1-3 때문에 핸들러는 **순수 산술 + 불변 상태**만 쓴다. codex가 제시한 하드닝을
반영한다(부호 오버플로, minor 검증, PFN 표현 가능성).

```c
static int
osmgaDevMmap(dev_t dev, off_t offset, int prot)
{
    unsigned long off, phys;

    /* 모두 등록 시점에 고정된 불변 값으로만 판정한다 */
    if (minor(dev) != 0 || offset < 0)                 return -1;
    if (prot != (PROT_READ | PROT_WRITE))              return -1;

    off = (unsigned long)offset;
    if (gWindowStart > gWindowEnd ||
        gWindowEnd - gWindowStart < PAGE_SIZE ||
        off < gWindowStart ||
        off > gWindowEnd - PAGE_SIZE)                  return -1;  /* 오버플로 없음 */

    if (gFbPhysical > ULONG_MAX - off)                 return -1;
    phys = gFbPhysical + off;
    if ((phys & (PAGE_SIZE - 1)) != 0)                 return -1;
    if ((phys >> PAGE_SHIFT) > INT_MAX)                return -1;
    return (int)(phys >> PAGE_SHIFT);
}
```

- `gWindowStart/gWindowEnd/gFbPhysical`은 **등록 시 한 번 기록하고 이후 불변**.
  게이팅은 "등록을 아예 하지 않는 것"으로 하지, 런타임 플래그로 하지 않는다.
- `prot`는 `PROT_READ|PROT_WRITE`만 허용(`PROT_EXEC`·0·미지 비트 거부).
  클라이언트는 `MAP_SHARED`를 써야 한다(콜백은 공유 플래그를 못 본다).
- `gWindowStart`는 **실제 프로그램된 스캔아웃 원점·pitch·height**에서 유도한다
  (명목 width×height가 아니라). 우리 드라이버는 원점 0·pitch=width가 증명돼
  있으므로(S1 §4-0) 동일하지만, 유도 경로를 명시한다.

### 2-3. 유저스페이스 테스트

1. `IODeviceMaster`로 `Display0` 찾기 → `IOCharacterMajor` 조회
2. `/dev` 노드 확보(없으면 `mknod`로 생성; major는 위에서 얻은 값, minor 0)
3. `open()` → `mmap()`으로 오프스크린 창 일부를 매핑
4. **왕복 검증**: 유저스페이스에서 위치 인코딩 패턴을 쓰고, 드라이버 쪽
   `OSMGAProbeBlit`으로 그 영역을 다른 오프스크린 블록에 복사한 뒤, 다시
   유저스페이스에서 읽어 확인
   → **유저 매핑과 엔진이 같은 메모리를 본다**는 것이 증명된다. 이것이 S4의 요체다.
5. 창 밖 오프셋 `mmap` 시도 → **실패해야 정상**

## 3. 안전 분석

| 위험 | 완화 |
| --- | --- |
| 클라이언트가 가시 스캔아웃을 매핑 | mmap 핸들러가 창 밖을 **전부 거부**. 창은 오프스크린 한 구간뿐 |
| 미탑재 VRAM 매핑 | 창 끝을 실증 7 MiB 이내로 제한(S1 이래 동일 원칙) |
| 임의 프로세스가 VRAM 접근 | config 플래그로만 열림(기본 OFF). 디바이스 노드 권한으로 추가 통제 |
| cdevsw 슬롯 고갈/충돌 | `IOAddToCdevswAt(-1,...)`로 빈 슬롯 자동 할당. 실패 시 로그 남기고 **디바이스 없이 계속 동작**(디스플레이 기능은 영향 없음) |
| 언로드 시 dangling 엔트리 | `free`에서 `IORemoveFromCdevsw` 호출 |
| 캐시 일관성 | 유저 매핑의 캐시 속성은 미확정 → **S1 이래의 원칙대로 uncached 별칭으로 교차검증**하고, 왕복 테스트로 실제 일관성을 확인 |
| 커널 문맥 오해 | mmap 핸들러는 **순수 산술만** 한다. 락·할당·로그·하드웨어 접근 없음 |

**이 단계는 엔진을 새로 건드리지 않는다.** 위험은 "잘못된 물리 페이지를 유저에게
노출"에 국한되며, 창 검증이 그것을 막는다.

## 4. 검증 방법

### 4-1. 하드웨어 이전 (호스트)
- 창 산술(시작/끝/가드)이 가시영역과 겹치지 않고 실증 VRAM 안쪽인지 모드별 확인
- 경계값: 창 시작 직전/직후, 창 끝 직전/직후, 음수, 비정렬 오프셋이 각각
  기대대로 허용/거부되는지

### 4-2. 타깃 빌드
클린 빌드, 경고 0, `nm -u`에 새 미해결 심볼이 `_IOAddToCdevsw*` 계열만 추가되는지 확인.

### 4-3. 실기 부팅
1. 부팅 로그에 major 번호 등록 성공
2. 유저스페이스에서 `IOCharacterMajor` 조회 성공
3. `mmap` 성공, 창 밖 `mmap` 실패
4. **왕복 검증 통과**(유저가 쓴 패턴을 엔진이 복사하고 유저가 다시 읽음)
5. 디스플레이 정상, telnet 생존

### 4-4. 복구
config 플래그 off 또는 재부팅.

## 5. 이 단계가 증명하지 않는 것

- Mesa 연결(S4b) — 이 단계는 매핑 경로만 증명한다
- 성능, 다중 클라이언트 할당(현재는 창 하나를 단일 클라이언트가 쓰는 전제)
- 유저 매핑의 캐시 속성(왕복 테스트로 *동작*은 확인하되 속성 자체는 미확정)

## 6. 자체 확인 완료 항목 (codex 답변과 대조할 것)

### Q2 — 등록 경로: **완전 자답**

`+[IODevice addToCdevswFromDescription:...]` 디스어셈블(`0x1a44b1`):
```
push offset aCharacterMajor ; "Character Major"
... [[deviceDescription configTable] valueForStringKey:"Character Major"]
```
→ config 키는 **`"Character Major"`**(문자열 `0x1d6cec`). 없으면 `-1`을 넘긴다.

`IOAddToCdevswAt`(`0x1a9c6c`) 디컴파일:
```c
if ( a1 == -1 ) {                    /* 자동 할당 */
    for (i = 0; i < nchrdev; v13 += 11)
        if (!memcmp(v13, &unk_1E5100, 0x2C)) break;   /* 빈 슬롯 탐색 */
        ++i;
}
if (i < 0 || nchrdev <= i || memcmp(&cdevsw + 11*i, &unk_1E5100, 0x2C))
    return -1;                       /* 슬롯이 비어있지 않으면 실패 */
... 11개 포인터 기록 ...
return i;                            /* 할당된 major */
```
→ **`IOAddToCdevswAt(-1, ...)`는 빈 슬롯을 자동 할당한다.** 명시 major를 주면 그
슬롯이 **비어 있을 때만** 성공. `IOAddToCdevsw`(At 없음)는 항상 자동 할당이다.

→ 채택: **config에 `"Character Major"`를 두지 않고**(즉 `-1`) 자동 할당을 쓴다.
major는 부팅마다 달라질 수 있으므로 유저스페이스는 반드시 `IOCharacterMajor`를
조회해서 알아내야 한다(하드코딩 금지). 이는 `Display0` object number가 부팅마다
바뀌었던 것과 같은 교훈이다.

## 7. ⛔ 차단 요건 — 해결 전 Mesa 연결 금지

codex가 두 건을 **차단 요건**으로 지적했고 타당하다. "왕복 테스트가 통과했으니
괜찮다"로 넘기지 않고 **명시적 go/no-go 게이트**로 둔다.

### 7-1. 캐시 속성 — 미해결

`d_mmap`이 PFN을 반환하는 이 경로에는 **uncached나 write-combining을 요청할
공개 수단이 없다.** `prot`은 접근 보호이지 캐시 타입이 아니다. 커널이 쓰는
`IOMapPhysicalIntoIOTask` 별칭은 **IO 태스크** 매핑이라 이후 유저 special-object
매핑의 캐시 속성을 정하지 않는다. `mapFrameBufferAtPhysicalAddress:`에서 본
`IO_DISPLAY_CACHE_*` 플래그도 이 경로에 자동 적용되지 않는다.

유저 매핑이 write-back 캐시라면:
- CPU 쓰기가 캐시에 남은 채 Storm이 VRAM을 읽을 수 있다
- Storm 쓰기 후 CPU 캐시에 낡은 라인이 남을 수 있다
- 유저스페이스에 일반적인 캐시 플러시 API가 없다

→ **전용 stale-cache 테스트를 게이트로 둔다**(단순 왕복은 불충분):
1. 유저스페이스에서 목적지 값을 **읽어 캐시에 올린다**
2. Storm이 그 영역을 덮어쓴다
3. 유저스페이스에서 **다시 읽어** 새 값이 보이는지 확인 → 안 보이면 캐시 문제 확정
4. 반대 방향: CPU가 쓴 소스를 Storm이 제대로 읽는지 별도 확인

결과에 따라 (a) 실제 PTE/MTRR 메모리 타입 확인, (b) 명시적 fence 규약,
(c) 이 경로 포기 중 택일한다.

### 7-2. 매핑 수명 — 미해결

`IORemoveFromCdevsw`는 **이후 디스패치만** 막는다. 이미 열린 fd도, 이미 만들어진
VM 매핑도 되돌리지 않는다. `close()`해도 매핑은 남는다. special VM object와
클라이언트 PTE가 **드라이버보다 오래 살 수 있고**, 그 페이지는 계속 BAR 물리주소를
가리킨다 — 이후 하드웨어 리셋/재구성 시 위험하다. 등록 해제와 진행 중 `mmap`의
경합도 있다.

→ **이 단계에서는 매핑 기능이 켜지면 드라이버를 언로드 불가로 두고, 해제는 재부팅으로
한다.** open-count는 불충분하다(매핑이 `close()`보다 오래 산다).

## 8. codex 교차검토 결과 (2026-08-19)

### 8-1. 채택(중대) — 호출 횟수와 결정성
"페이지마다 1회"라는 내 서술이 틀렸다. **최소 2회**이고, 2차 경로에는 `-1` 검사가
없어 가변 상태로 판정하면 `0xFFFFF000`이 물리주소가 된다. 내 디컴파일로 재확인함
(§1-3). → 핸들러를 불변 상태 기반으로 재작성.

### 8-2. 채택 — 등록 경로, 공개 헤더로 교차확인
`IODevice.h:77` 주석이 codex 주장을 정확히 확인: 키는 **`"Character Major"`**와
`"Device Major"`, 없으면 **첫 번째 가용 major**, `+characterMajor`로 조회.
내 IDA 분석과 공개 문서가 독립적으로 일치한다.
→ 직접 `IOAddToCdevswAt`가 아니라 **`+addToCdevswFromDescription:...` 클래스
메서드**를 쓴다(config·`IOCharacterMajor` 통합이 문서화돼 있다).
**클래스당 1회 등록**이며 인스턴스마다가 아니다(메서드가 `+`이고 주석도 "for this
class").

### 8-3. 채택 — `/dev` 노드
OPENSTEP 4.2에 devfs가 없고 cdevsw 등록이 노드를 만들지 않는다. **root가 `mknod`**로
만든다(드라이버가 파일시스템 노드를 만들지 않는다). 동적 major이므로 특권 헬퍼가
로드 후 `IOCharacterMajor`를 조회해 노드를 생성·갱신한다.
권한은 **`0600`, root 소유**로 시작한다.

### 8-4. 채택 — 호출 문맥
인터럽트 문맥은 아니고 호출 프로세스의 `mmap` 시스템콜에서 온다. 다만 VM 객체
생성 중이라 VM 락이 잡혀 있을 수 있고 반복 호출된다. → 할당·수면·I/O·`IOLog`·
유저 복사·락 **전부 금지**, 순수 산술만.

### 8-5. 채택 — `prot` 검사와 산술 하드닝
`prot`는 요청일 뿐이며 최종 PTE 보호를 증명하지 않는다. `PROT_READ|PROT_WRITE`만
허용. `offset + PAGE_SIZE > end`는 부호 오버플로 가능 → `off > end - PAGE_SIZE`
형태로. minor·정렬·PFN 표현가능성도 검증.

### 8-6. 채택 — 잘못된 PFN의 결과
`-1`이 아닌 값은 **그대로 신뢰된다**(§1-3). 임의 물리 RAM(커널 데이터 포함),
MMIO(디바이스 부작용), 미존재 물리공간(폴트·행)을 유저 프로세스에 매핑할 수 있다.
커널 측 검증이 없으므로 **전적으로 우리 책임**이다.

### 8-7. 채택 — 고정 창의 위치
고정 창은 **단일 클라이언트 스모크 테스트**로만 적절하다. Mesa용 소유권 모델이
아니다. `d_mmap`은 `dev`/offset/prot만 받고 **RPC 클라이언트 신원이나 file-private
상태를 받지 못한다.** 확장하려면 **검증된 minor 번호 등에 인코딩된 per-client
핸들**이 필요하고, 콜백은 그 클라이언트에 할당된 구간만 허용해야 한다.
→ 고정 창으로 **물리 매핑·캐시 거동을 먼저 증명**하고, S4b 전에 할당자
(`protocol/OpenStepMGAOffscreenAllocator`)와 실제 소유권 모델을 도입한다.
