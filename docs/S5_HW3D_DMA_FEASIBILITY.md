# S5 — 하드웨어 3D(WARP) 및 DMA 실현 가능성 조사

기준일: 2026-08-19 (§3은 같은 날 정정됨 — §3-2 참조)
상태: **조사만 완료. 착수하지 않음.** 구현 전 별도 계획서와 교차검토가 필요하다.

## 0. 이 문서가 답하는 질문

"OpenGL 가속"의 이득이 실제로 어디서 나오는가, 그리고 진짜 이득이 나는
**하드웨어 삼각형 래스터화(WARP 엔진)** 가 이 플랫폼에서 가능한가.

## 1. 가속 이득의 소재 — 냉정한 분석

Mesa 3.4.2의 삼각형 래스터화는 **CPU가 픽셀을 하나씩 쓰는** 소프트웨어 방식이다.
그 대상이 RAM이면 빠르고 **VRAM이면 PCI를 거쳐 훨씬 느리다.**

| 연산 | 하드웨어 가능? | 효과 |
| --- | --- | --- |
| `glClear` | ✅ Storm 단색채우기(S1 실증) | **큰 이득** — 프레임마다 전체 화면 |
| 화면 전송(presentation) | ✅ Storm BITBLT(S2 실증) | **이득** — CPU 복사 제거 |
| 삼각형 래스터화 | ⚠️ **WARP 엔진 필요** | **진짜 이득은 여기** |
| 스팬 픽셀 쓰기(CPU) | ❌ | VRAM 대상이면 **손해** |

→ **렌더 타깃을 VRAM으로 옮기는 것 자체는 이득이 아니다.** WARP 없이는
`glClear`와 presentation만 이득이고, 스팬 쓰기는 오히려 느려질 수 있다.
순이득 여부는 워크로드에 달렸으며 **측정 없이는 알 수 없다.**

## 2. 프레젠테이션의 구조적 제약

렌더 결과를 화면에 올리면 WindowServer 영역과 충돌한다. S3b-prep에서 실제로
관측했듯, 우리가 가시 프레임버퍼에 직접 블릿하면 나타나기는 하지만 **WindowServer가
리페인트할 때 지워진다**(damage 장부에 없으므로).

- **창 안(windowed) 가속 GL**: 협조 프로토콜이 없다. `IODisplayDoBlit`은
  사문화됐고(S3b), DRI 같은 것도 없다. **사실상 불가능.**
- **전체화면 가속 GL**: 우리가 화면을 소유하므로 정당하게 성립한다.
  데모·게임형 용도라면 이쪽이 현실적이다.

## 3. OPENSTEP의 DMA 지원 실태 (실측)

### 3-1. 있는 것

| 항목 | 근거 |
| --- | --- |
| `_kmem_alloc_wired` (0x173d1c) | 커널 전역 export — **wired 할당 가능** |
| `_IOPhysicalFromVirtual` (0x1a9334) | 공개 API(`driverkit/kernelDriver.h`) — 물리주소 조회 |
| PCI 버스마스터 활성 | H1 S0 실측 `command=0x0007`(bit2 = bus master) |

### 3-2. 정정 (2026-08-19) — 물리 연속 할당자는 **있다**

초판은 "물리적 연속 할당자 없음(`nm /mach_kernel | grep -i contig` → 0건)"이라고
적었다. **이 결론은 틀렸고, 원인은 검증 방법에 있었다**: `contig`로 grep하면
`IOMallocLow`라는 이름의 심볼은 절대 걸리지 않는다. 앞서 기록한 `grep "A\|B"`
함정과 같은 부류의 실수다.

실기 커널로 다시 확인한 사실:

| 항목 | 근거 |
| --- | --- |
| `_IOMallocLow` / `_IOFreeLow` 존재 | 타깃 `nm /mach_kernel` → `001c87e0 T _IOMallocLow` |
| 공식 API다 | `/NextDeveloper/Headers/driverkit/i386/kernelDriver.h:21` (타깃에도 설치돼 있음) |
| 헤더의 약속 | "low 16 megabytes ... 24 bits of address" — **연속성은 언급하지 않는다** |

IDA로 구현을 따라간 결과(헤더 문구보다 좁고, 우리 목적에는 오히려 낫다):

```
IOMallocLow(size)              0x1c87e0
  └ dma_buf_alloc(buf, size)   0x18980c   size > 0x10000 이면 실패
      ├ 풀 2개: dma_buf_sm(≤ page_size) / dma_buf_lg(> page_size)
      └ 풀이 비면 생성자 호출
          └ sub_1898EC          0x1898ec   alloc_cnvmem(0x10000, 0x10000) + bzero
              └ alloc_cnvmem    0x18ad9c   고정 아레나 위의 **범프 할당자**
                  (정렬 → 한계 검사 → 포인터 전진)
```

아레나 경계는 `sub_18ACF8`(0x18acf8)이 정한다:
`dword_1E7610 = page_align(MEMORY[0x11138])`, `dword_1E7614 = page_align(cnvmem << 10)`.
즉 **conventional memory(640 KiB 미만)** 구간이다.

→ **범프 할당자가 선형 아레나를 잘라 주므로 물리적으로 연속이다.** 다만
헤더가 말하는 "low 16 MB"보다 훨씬 좁은 conventional memory이고, 아레나는
ISA DMA를 쓰는 다른 모든 것과 공유한다.

### 3-3. 판정: MGA primary DMA 링에는 충분하다

| 제약 | 값 |
| --- | --- |
| 1회 할당 상한 | **64 KiB** (`dma_buf_alloc`이 `> 0x10000` 거부) |
| 연속성 | 보장됨(범프 할당자) |
| 위치 | conventional memory(<640 KiB), 전체 아레나는 그보다 작고 공유됨 |
| 정렬 | `alloc_cnvmem(size, align)`이 요청 정렬 보장(대형 풀은 64 KiB 정렬) |

MGA primary DMA는 `PRIMADDRESS`~`PRIMEND`로 연속 물리 영역을 요구하는데,
64 KiB = 16384 dword면 명령 링으로 넉넉하다. G450은 32비트 PCI 마스터이므로
저역 주소도 문제가 없다.

**따라서 "연속 물리 메모리를 못 구해서 DMA가 막힌다"는 초판의 우려는 해소됐다.**
`IOPhysicalFromVirtual`(0x1a9334, 공개 API)로 장치에 넘길 물리주소를 얻는다.

### 3-4. 여전히 없는 것

| 항목 | 확인 결과 |
| --- | --- |
| DriverKit PCI DMA 헬퍼 | `IOPCIDirectDevice`는 **config space 접근만** 제공 |
| 범용 DMA API | `IOEISADMABuffer`/`startDMAForBuffer:` 등은 전부 **ISA/EISA 8237 채널용** — PCI 버스마스터와 무관 |
| AGP | PCI 카드이며 OPENSTEP에 AGP 개념 자체가 없다 |

이는 결함이 아니라 구조다. PCI 버스마스터 장치는 자체 DMA 엔진을 갖고, 각
드라이버가 디스크립터를 만들어 장치 레지스터에 물리주소를 써 넣는 방식이
당대의 정상 패턴이었다 — OPENSTEP 정식 드라이버 중 DEC 21040/21140,
AMD PCnet32, 3Com 3C90x, Intel PIIX IDE가 모두 그렇게 했다.

출처는 **저장소 루트의 `openstep_pci_bus_master_driver_research.md`** 이다.
특정 드라이버에 종속되는 내용이 아니라 플랫폼 전반의 사실이므로 루트에 두는
것이 의도된 배치다 — 이 프로젝트 폴더 안으로 옮기지 말 것.

단, 그 문서가 `IOMallocLow`를 "low 16MB physically contiguous"라고 적은 것은
헤더 주석을 옮긴 것이고, 실제 아레나는 위에서 확인한 대로 conventional
memory다. 연속성이라는 **결론은 맞고 범위 서술이 넓다.**

## 4. DMA보다 큰 장벽 — 3D 소프트웨어 스택이 통째로 없다

### 4-1. 정정 — AGP 전제는 **DDX에만** 해당한다

초판은 "X.Org의 3D 경로는 전부 AGP 전제"라고 적었다. DDX(`mga_dri.c`)에
대해서는 맞다 — WARP 마이크로코드·primary DMA·버퍼·텍스처를 `DRM_AGP`로
매핑하고, PCI면 `agpSize`를 0으로 강제하라고만 적어 둔다(`mga_dri.c:297`).

**그러나 DRM에는 PCI 전용 경로가 따로 있다.** legacy DRM의
`mga_do_pci_dma_bootstrap`이 그것이고, 끝에서 `dev_priv->dma_access = 0`으로
두고 `"Initialized card for PCI DMA."`를 찍는다(`mga_dma.c:699` 부근).
AGP 경로는 대비되게 `MGA_PAGPXFER`를 넣는다(`:596`).

즉 **PCI 버스마스터 primary DMA는 당시 출하된 구성**이며, 우리가 없는 길을
내는 것이 아니다. 초판이 "PCI 전용 경로는 우리 참조 트리에 없다"고 한 것도
사실이었으나, 그것은 트리를 받지 않았기 때문이지 존재하지 않아서가 아니다.
자세한 것은 `D1_PRIMARY_DMA_RING_PLAN.md`.

### 4-2. DDX는 3D를 실행하지 않는다
`mga_dri.c`는 AGP 매핑을 잡아 offset을 DRM에 넘기는 것이 전부다
(`drmCommandWrite(DRM_MGA_INIT, ...)`). 실제 WARP 구동·상태 emit은 Linux 커널
DRM(`mga_warp.c`, `mga_state.c`)에 있으며 **우리에게 없다.**

### 4-3. 결정적 — Mesa 3.4.2에 MGA 드라이버가 없다
우리 `opennstep-mesa342/upstream/Mesa-3.4.2/src/`에는 FX(3dfx)·S3·SVGA·GGI·
D3D·Allegro·DOS·BeOS·Windows·X·OSmesa만 있다. **MGA 없음.**
MGA용 Mesa 하드웨어 드라이버는 DRI 트리(Mesa 3.5+)에 있고 **DRM ioctl·SAREA·
DRI 락에 의존**한다 — OPENSTEP에 전부 없다. 따라서 **포팅이 아니라 신규 작성**이다.

### 4-4. 쓸 수 있는 자산 (2026-08-19 재확인)

초판이 이 항목을 적을 때 근거로 삼은 트리는 **워크스페이스에 없었다**
(`scratch/`가 gitignore라 이전 세션의 다운로드가 사라졌고 git 이력에도 없다).
즉 한동안 **재확인 불가능한 주장**이었다. 다시 받아 확인한 결과 **주장 자체는
사실이었다**:

| 자산 | 확인 |
| --- | --- |
| `xf86-video-mga-2.0.0/src/mga_ucode.h` | **11,610줄**, `Permission is hereby granted, free of charge...`, ⓒ1999 Matrox Graphics Inc. → MIT 계열, 사용 가능 |
| legacy MGA DRM(`mga_drv.h`/`mga_dma.c`/`mga_state.c`/`mga_warp.c`/`mga_irq.c`) | Precision Insight/VA Linux, **MIT**(GPL 아님). primary DMA 레지스터·명령 인코딩·PCI 경로의 정본 |

현재 위치는 `scratch/mga-drm/`과 `scratch/xf86-video-mga-2.0.0/`이며
**`scratch/`는 gitignore라 다시 사라질 수 있다.** 착수 시 안정된 위치로
옮기거나 재취득 절차를 문서화할 것. 취득 URL은 `refs/SOURCES.md`에 이미 있다.

## 5. 옵션 3의 실제 작업 목록

"DMA를 붙이는 일"이 아니라 다음을 **전부 새로 만드는 일**이다:

1. ~~PCI 버스마스터 DMA 링~~ — **완료(2026-08-19). `D1_PRIMARY_DMA_RING_PLAN.md` §7**
2. WARP 마이크로코드 업로드 경로
3. 3D 상태·정점 명령 스트림 생성기
4. DMA 완료 동기화(인터럽트 또는 bounded 폴링)
5. VRAM 텍스처 관리
6. **Mesa 하드웨어 드라이버 신규 작성**(DRI판 포팅 불가 → `dd_function_table`
   훅을 Mesa 3.4.2 내장 FX/Glide 드라이버 구조를 참고해 직접)

S1~S4a가 각각 하루 이내 규모였다면 이것은 **차원이 다른 규모**다.

## 6. 착수한다면 첫 단계 (권고)

**DMA 링 하나만 먼저 실증한다**: `IOMallocLow`로 64 KiB를 잡고
`IOPhysicalFromVirtual`로 물리주소를 얻어 `PRIMADDRESS`/`PRIMEND`에 써 넣은 뒤,
카드가 그것을 버스마스터로 읽어 **이미 검증된 2D 명령**(단색채우기)을
실행하는지 확인한다. §3-2 정정으로 이 단계의 전제(연속 물리 메모리)는
이미 확보돼 있다.

- 성립하면 → 나머지 5개 항목이 의미를 갖는다
- 성립하지 않으면 → **거기서 멈춘다.** 3D 스택을 만들 이유가 없다

이 방식이 지금까지의 원칙(가장 큰 미지수를 가장 싸게 먼저 제거)과 일치한다.
S1이 "엔진이 반응하는가"를 물었듯, 이 단계는 "카드가 시스템 메모리를 읽는가"를
묻는다.

> **✅ 실행됨(2026-08-19): 성립한다.** 카드가 목록을 끝까지 가져가 실행하고
> SOFTRAP까지 도달했으며, 결과 픽셀은 S1이 MMIO로 낸 것과 동일했다
> (checksum `0xDBEEF000`, 불일치 0). 정본 `D1_PRIMARY_DMA_RING_PLAN.md` §7.
> **따라서 "거기서 멈춘다"는 조건은 발동하지 않았다.**

## 7. 이 문서가 결정하지 않은 것

- 옵션 3을 실제로 할지 여부(operator 판단)
- 전체화면 전용으로 갈지, 창 안 가속을 다른 방법으로 시도할지
- `glClear`+presentation만으로 얻는 실제 이득 수치(**측정된 바 없음**)
