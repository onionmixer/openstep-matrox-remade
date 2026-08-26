# OpenStep Matrox MGA Remade

OPENSTEP 4.2의 Matrox G450 PCI VGA를 위한 새 DriverKit 디스플레이
드라이버와, 그 위에 얹은 Mesa 3.4.2 하드웨어 3D 가속이다. 기존 바이너리
display driver를 고친 것이 아니라, 공개 하드웨어 자료와 공개 MGA 구현을
근거로 새로 만든 `IOFrameBufferDisplay` 서브클래스다.

**v1.0 릴리스**: [releases/tag/v1.0](https://github.com/onionmixer/openstep-matrox-remade/releases/tag/v1.0)
— 설치용 `.pkg` 세 개와 SHA256SUMS. 설치와 복구 절차는
[release-packaging/INSTALL.md](release-packaging/INSTALL.md)에 있다.

## 현재 상태 (2026-08-27)

**`OpenStepMGAReplacementDisplay`가 실기에서 동작한다.** 기존 `MatroxMGA`를
대체하는 완전한 `IOFrameBufferDisplay` 서브클래스로, G450을 다음 조합으로
구동한다:

- **해상도** 5종(640×480 ~ 1600×1200, VESA DMT 타이밍)
- **픽셀 포맷** 4종: RGB:888/32, RGB:555/16, RGB:256/8(PseudoColor,
  `setTransferTable:count:`로 윈도서버 컬러맵 반영), BW:8(grayscale)
- **회색 단계**는 포맷이 아니라 별도 키 `Gray Levels` — 256/16/4/2.
  네 값 모두 같은 8bpp 스캔아웃·같은 `IODisplayInfo`이고 DAC 램프만 다르다.
  진짜 2bpp linear 프레임버퍼는 G450 스캔아웃 엔진의 한계로 불가능함을
  원본 바이너리로 확정했다(순정 VGA의 `BW:2`는 `rowBytes = width/4`인
  플래너 VGA 경로이지 리니어 스캔아웃이 아니다)
- Configure.app의 `Display.modes`로 20개 조합(5×4) 선택 가능하고, 회색
  단계는 같은 인스펙터의 라디오 매트릭스로 고른다.  config-table의
  `Display Mode`/`Gray Levels` 문자열로 부팅 시 반영

G450 PLL(픽셀 클럭 M/N/P 후보 탐색)과 15bpp RAMDAC 팔레트 인덱싱은 원본
`MatroxMGA` 바이너리를 IDA로 완전 해독하고 codex와 교차검토해 충실히
포팅했다 — 자세한 내용과 실기 검증 결과는
[docs/R6_G450_PIXEL_PLL_ALGORITHM.md](docs/R6_G450_PIXEL_PLL_ALGORITHM.md)에,
전체 검증 이력은 [docs/TEST_STATUS.md](docs/TEST_STATUS.md)에 있다.

모드셋 이후에는 **2D 엔진과 메모리 경로**를 실기에서 실증했다: Storm 2D
단색채우기·BITBLT(S1/S2), 유저스페이스→커널 RPC로 엔진 구동(S3a, self-test
6/6), 계측 19종 + 커서 오버라이드(S3b-prep), 오프스크린 VRAM의 유저 태스크
매핑(S4a). 그 과정에서 **`IODisplayDoBlit`은 OPENSTEP 4.2에서 사문화됐음**을
측정으로 확정했다(WindowServer가 보내지 않는다).

### 3D — Mesa 3.4.2가 카드에서 돈다

OSMesa 백엔드(`libGL_mga.a`)가 삼각형을 G450 엔진으로 보내고, 표현할 수 없는
것만 Mesa의 래스터라이저로 내려보낸다. 실기 측정:

- **1600×1200×32 전체화면** — 오프스크린 창 20,037,632 바이트, teapot 삼각형
  17,292 개 중 **97% 를 카드가 그리고 커널 거부 0**. 텍스처 아레나
  8,511,488 바이트가 남고, 세 값 모두 python 계산과 바이트 단위로 일치한다.
- **800×600 창에서 실시간** — 회전하는 teapot이 **47.6 fps**. 같은 소스를
  stock Mesa로 링크한 쪽은 12.8 fps.

그 3.7 배가 어디서 오는지는 짚어둘 값어치가 있다. **래스터화가 아니다** —
stock 쪽 draw 단계가 8.58 ms 로 오히려 더 짧다. 차이는 전부 **화면 전달**이다:
드라이버의 VRAM→VRAM blit 3.70 ms 대 AppKit 경로 65.90 ms.

### VRAM 선언

보드 메모리를 `MGA Memory Size` 로 **8 / 16 / 32** MiB 중에 고른다. 32 는 세
가지 독립된 방법으로 실증했고(BAR sizing, 브리지 윈도우, 2,048 페이지 증명),
8 도 실기에서 확인했다 — 1600×1200×32 에 8 을 선언하면 가시 프레임버퍼만
7,680,000 바이트라 오프스크린 창이 들어갈 자리가 없고, 드라이버가 그걸
알아채고 이유를 남기며 OpenGL 을 끈다. **8 을 선언하면 8 만 쓴다.**

8 은 아직 없는 G400 지원을 내다본 것이다. 지금 `:3714` 는 G450 이 아니면
모드 프로그래밍을 거부한다.

### 데모

Mesa Demos 변종 패키지가 데모 **두 쌍**을 싣는다. 각각 소스 하나를 두
바이너리로 빌드하고, 소스·빌드 스크립트·자체 README 를 함께 넣어서 패키지만
보고도 어떻게 만들어졌는지 알 수 있게 했다.

| | |
|---|---|
| `Examples/Mesa342/Teapot` | `teapot_sw` / `teapot_hybrid` — 파일로 쓴다 |
| `Examples/Mesa342/GLWindow` | `glwin_sw` / `glwin_hybrid` — 창에서 돌고 제목에 fps 를 찍는다 |

각 쌍의 `_sw` 는 stock Mesa 만 링크해 Matrox 코드가 **하나도** 없고, 포장
단계가 그걸 심볼로 확인한다. 다만 `glwin_hybrid` 는 `teapot_hybrid` 와 달리
그리기가 아니라 **전달**에 드라이버가 필요해서, 드라이버가 없으면 창을 열고
그렇다고 말한 뒤 비어 있는다.

현재 사실의 정본은 [docs/REMAINING_WORK.md](docs/REMAINING_WORK.md)이고,
초기 단계의 검증 이력은 [docs/TEST_STATUS.md](docs/TEST_STATUS.md), 인수인계는
[HANDOFF.md](HANDOFF.md), 드라이버 사용법은
[OpenStepMGAReplacementDisplay/README.md](OpenStepMGAReplacementDisplay/README.md)다.
3D 착수 전의 실현 가능성 조사는
[docs/S5_HW3D_DMA_FEASIBILITY.md](docs/S5_HW3D_DMA_FEASIBILITY.md)에 남아 있다.

아래 "## 범위" 이후 문서 끝까지는 이 결과에 이르기 전, 2026-08-18
기준으로 세운 **초기 설계 방향(폐기됨)** — `MatroxMGA`와 공존하는 별도
Mesa/3D sidecar 서비스 — 의 기록이다. 실제로는 H1 방법론 전환(운영자가 target을
generic SVGA 단독 소유로 재부팅해 MGA native 자원을 무소유 상태로 만듦) 이후
`MatroxMGA`를 완전히 대체하는 display driver 자체를 새로 만드는 방향으로
바뀌었고, 그 결과가 위 "현재 상태"다. 아래 내용은 탐색 과정의 근거·결정 기록으로
남겨두되, 현재 아키텍처를 나타내지는 않는다. Mesa/3D 가속 경로는 그 뒤 이
replacement driver 위에서 다시 설계됐고, 위 "3D" 절이 그 결과다. 아래 문단들이
현재형으로 서술하는 16 MiB cap 같은 값도 그때의 기록이지 지금 정책이 아니다.
이후 문단들이 현재형으로 서술하는
P1/P2 MiG control plane, lease, sidecar service는 **모두 폐기된 경로**이며
현재 드라이버는 그 코드를 링크하지 않는다.

## 범위 (2026-08-18 초기 설계, 폐기됨)

- 대상: Matrox MGA G400/G450 계열, PCI, i386 OPENSTEP 4.2.
- 3D 목표: Mesa 3.4.2와 연결 가능한 고정 기능 OpenGL 1.2 가속 경로.
- 1차 표시 목표: offscreen VRAM 렌더링 결과를 SDL2의 기존 AppKit
  표시 경로로 전달한다.
- 제외: AGP/GART, GL 1.3 이상 확장, shader, 다중 head, WindowServer
  교체, 기존 `MatroxMGA` 바이너리의 수정.

## 초기 설계 결론 (2026-08-18, 폐기됨)

OPENSTEP DriverKit은 PCI/MMIO mapping, DMA 물리 주소 변환, 인터럽트,
loadable kernel server, MiG 통신을 제공하므로 native DRM-유사 서비스는
기술적으로 가능하다. 그러나 OPENSTEP은 X11이 아니므로 Linux/FreeBSD의
DRI/DRM ABI를 포팅하지 않는다. `OpenStepMGA`라는 별도 kernel service와
Mesa backend를 설계한다.

실기의 기존 `MatroxMGA` display driver가 현재 화면을 소유한다. 따라서
처음부터 같은 PCI 장치를 claim하거나 mode setting을 수행하지 않는다.
안전한 공존과 offscreen VRAM 사용이 검증되기 전에는 read-only probe만
허용한다.

현재 `MatroxMGA` configuration의 16 MiB profile과 PCI subsystem `102b:0d43`의
공개 32 Mb board catalogue 표기는 서로 다르다. release deployment는 운영자가
확정한 보수적 PCI G450 16 MiB cap만 사용하며, 32 MiB candidate로 넓히지 않는다.
이는 R3 fixed 1600×1200@60 record에는 충분하지만, P3 BAR mapping, offscreen
allocation, GPU command submission에는 existing scanout/cursor/hidden allocation과
mapping compatibility evidence가 추가로 필요하다. 대조와 경계는
[docs/P1_SUBSYSTEM_MEMORY_RECONCILIATION.md](docs/P1_SUBSYSTEM_MEMORY_RECONCILIATION.md)에
기록한다.

자세한 근거는 [ANALYSIS.md](ANALYSIS.md), OPENSTEP 원전 문서 대조는
[docs/P0_OPENSTEP_DOCUMENTS.md](docs/P0_OPENSTEP_DOCUMENTS.md), 단계별 실행
기준은 [PLAN.md](PLAN.md), 출처와 라이선스 경계는
[refs/SOURCES.md](refs/SOURCES.md)에 기록한다.

G400/G450 DDC/EDID는 기술적으로 가능하지만, 현재 display driver와 공존하는
3D sidecar에는 넣지 않는다. 새 display driver가 단독 소유할 때의 optional
boot-time 기능으로 분리하며, DDC 실패 시 수동 `Display Mode`를 유지하는
정책은 [docs/P0_DDC_EDID_FEASIBILITY.md](docs/P0_DDC_EDID_FEASIBILITY.md)에
기록한다.

그 정책의 D0 pure-C EDID base-block parser와 fixed-mode intersection test는
[docs/D0_EDID_PARSER_POLICY.md](docs/D0_EDID_PARSER_POLICY.md)에 기록한다.
OPENSTEP user-process loader 진단용 target-only harness의 경계는
[docs/D0_TARGET_HARNESS.md](docs/D0_TARGET_HARNESS.md)에 기록한다.

기존 `MatroxMGA`와 공존하지 않는 replacement display driver의 lifecycle,
manual-mode/EDID 우선순위, recovery admission gate는
[docs/D1_REPLACEMENT_DISPLAY_OWNERSHIP.md](docs/D1_REPLACEMENT_DISPLAY_OWNERSHIP.md)에
고정한다. 이 문서는 현재 hardware access 권한을 부여하지 않는다.
recovery boot에서의 실제 후속 단계, 각 gate의 evidence, failure rollback은
[docs/RECOVERY_REPLACEMENT_DRIVER_EXECUTION_PLAN.md](docs/RECOVERY_REPLACEMENT_DRIVER_EXECUTION_PLAN.md)에
분리해 두었다.
그 중 현재 실행 순서(R0 fingerprint comparator → R5 original-only recovery
rehearsal 및 별도 R2 physical evidence)는
[docs/NEXT_STEP_EXECUTION_PLAN.md](docs/NEXT_STEP_EXECUTION_PLAN.md)에 구체화했다.
R2 physical evidence가 없는 값의 code 유입을 거부하는 pure-C admission contract는
[docs/R2_PROFILE_ADMISSION_POLICY.md](docs/R2_PROFILE_ADMISSION_POLICY.md)에 기록한다.
target original binary의 configuration override 및 hardware-sensitive count path를
분리한 static audit은
[docs/R2_ORIGINAL_BINARY_CONFIGURATION_AUDIT.md](docs/R2_ORIGINAL_BINARY_CONFIGURATION_AUDIT.md)에 기록한다.
future recovery profile의 three-snapshot sole-owner invariant는
[docs/R1_RECOVERY_MATRIX_POLICY.md](docs/R1_RECOVERY_MATRIX_POLICY.md)에 고정한다.
P-recovery/P-failure Configure 및 Installer rollback 실기에서 사용할 기록 양식은
[docs/reports/R1_RECOVERY_CONFIGURATION_RUN_SHEET.md](docs/reports/R1_RECOVERY_CONFIGURATION_RUN_SHEET.md)에
고정한다.
P-recovery table의 exact PCI/mode/16MiB admission contract는
[docs/R1_G450_RECOVERY_CONFIG_POLICY.md](docs/R1_G450_RECOVERY_CONFIG_POLICY.md)에
기록한다.
G1의 hardware-precondition 완료 범위와 별도로 남은 Configure/rollback evidence는
[docs/G1_HARDWARE_PRECONDITION_STATUS.md](docs/G1_HARDWARE_PRECONDITION_STATUS.md)에
기록한다.
G2 이후 단 하나의 manual mode를 evidence와 산술로 검토하는 fail-closed policy는
[docs/R3_MANUAL_MODE_REVIEW_POLICY.md](docs/R3_MANUAL_MODE_REVIEW_POLICY.md)에 있다.
operator-provided 16 MiB working assumption과 현재 실기 1600×1200×32 geometry로 준비한 offline
candidate와 그 증거 경계는
[docs/R3_16M_WORKING_ASSUMPTION.md](docs/R3_16M_WORKING_ASSUMPTION.md)에 분리했다.
그 geometry와 비교할 1600×1200@60 DMT timing shape의 offline verifier/audit은
[docs/R3_DMT_TIMING_CANDIDATE_AUDIT.md](docs/R3_DMT_TIMING_CANDIDATE_AUDIT.md)에 있다.
16 MiB 가정에서 display-only와 Mesa color/depth layout을 구분하는 byte budget은
[docs/R3_16M_RENDER_BUDGET.md](docs/R3_16M_RENDER_BUDGET.md)에 기록한다.
Mesa의 고정 1024×768 render target과 current desktop으로의 CPU-reference
presentation 경계는
[docs/P3_1024_RENDER_TARGET_PRESENTATION.md](docs/P3_1024_RENDER_TARGET_PRESENTATION.md)에 있다.
Mesa 3.4.2 software fallback과 future hardware candidate를 분리하는 selector는
[docs/P3_MESA_BACKEND_FALLBACK.md](docs/P3_MESA_BACKEND_FALLBACK.md)에,
code-only regression의 aggregate contract는
[docs/NO_HARDWARE_REGRESSION_MATRIX.md](docs/NO_HARDWARE_REGRESSION_MATRIX.md)에 있다.
그 계획의 R4 fail-closed `IOFrameBufferDisplay` skeleton과 source/target-build
검토 결과는 [docs/R4_SKELETON_REVIEW.md](docs/R4_SKELETON_REVIEW.md)에 있다.
future recovery-only framebuffer mapping API의 local DriverKit audit과 unresolved
range boundary는 [docs/R6_DRIVERKIT_MAPPING_AUDIT.md](docs/R6_DRIVERKIT_MAPPING_AUDIT.md)에
기록한다.
future Storm 2D bring-up의 public-source analysis와 staged safety boundary는
[docs/R6_STORM_2D_SOURCE_AUDIT.md](docs/R6_STORM_2D_SOURCE_AUDIT.md)에 기록한다.
G450 PLL/DAC mode transition의 one-head/one-candidate/rollback boundary는
[docs/R6_G450_PLL_MODE_SOURCE_AUDIT.md](docs/R6_G450_PLL_MODE_SOURCE_AUDIT.md)에 기록한다.
그 중 reviewed single-plan M/N/P byte-image의 source/license boundary와 C89
encoder는 [docs/R6_G450_PLL_ENCODING_POLICY.md](docs/R6_G450_PLL_ENCODING_POLICY.md)에
기록한다.
현재 original G450 16 MiB bundle가 노출하는 one-mode selection의 read-only
audit은 [docs/R6_ORIGINAL_MODE_LIST_AUDIT.md](docs/R6_ORIGINAL_MODE_LIST_AUDIT.md)에
기록한다.
original static-analysis evidence를 independent three-range recovery data plan으로
재구성한 boundary는 [docs/R6_G450_RANGE_PUBLICATION_PLAN.md](docs/R6_G450_RANGE_PUBLICATION_PLAN.md)에
기록한다.
checked 1600×1200×32 primary-head CRTC byte image의 offline boundary는
[docs/R6_G450_PRIMARY_CRTC_IMAGE_POLICY.md](docs/R6_G450_PRIMARY_CRTC_IMAGE_POLICY.md)에
기록한다.
PLL lock 및 Storm 2D FIFO/idle에 공통으로 적용할 timeout/stability policy는
[docs/R6_BOUNDED_POLL_POLICY.md](docs/R6_BOUNDED_POLL_POLICY.md)에 기록한다.
R6 preflight·PLL lock·linear result·rollback을 결합하는 one-mode transaction
policy는 [docs/R6_MODE_TRANSACTION_POLICY.md](docs/R6_MODE_TRANSACTION_POLICY.md)에 기록한다.
first 2D clear/copy의 active-transaction/offscreen-only admission은
[docs/R6_OFFSCREEN_2D_ADMISSION.md](docs/R6_OFFSCREEN_2D_ADMISSION.md)에 기록한다.
그 admission에 전달할 address-free surface ID/footprint ledger의 monotonic
allocation boundary는
[docs/R6_OFFSCREEN_ALLOCATOR_POLICY.md](docs/R6_OFFSCREEN_ALLOCATOR_POLICY.md)에 기록한다.
R6 policy의 OPENSTEP target compiler validation 결과는
[docs/R6_TARGET_PURE_C_VALIDATION.md](docs/R6_TARGET_PURE_C_VALIDATION.md)에 기록한다.

현재 부팅의 independent PCI config cross-check는
[docs/P1_PCIL_RECHECK.md](docs/P1_PCIL_RECHECK.md)에 기록한다.

현재 MiG control plane은 P2.6까지 target에서 검증되었다. client는
`query_capabilities`로 단일 software lease와 명시적 `hardware_ready=0` 상태를
확인할 수 있으며, 이는 PCI/MMIO를 포함한 실제 MGA 자원에 접근하지 않는다는
계약이다. protocol, client-death recovery, two-client contention, raw MiG
negative path를 포함한 full regression 결과는
[docs/P2_P26_CONTROL_REGRESSION_REPORT.md](docs/P2_P26_CONTROL_REGRESSION_REPORT.md)에
기록한다.

P2 bundle에는 hardware API·device binding·resource table·`START/WIRE`의 유입을
거부하는 host-side static gate도 있다. 실행 방법과 한계는
[docs/P2_STATIC_SAFETY_GATE.md](docs/P2_STATIC_SAFETY_GATE.md)에 기록한다.
target-native relocatable의 import table도 service load 전에 검사하며, 근거와
실행 방법은 [docs/P2_BINARY_IMPORT_GATE.md](docs/P2_BINARY_IMPORT_GATE.md)에
기록한다.

NFS source에서 temporary clean build 후 control-plane suite를 재현하는 P2.9
runner와 target 검증 상태는
[docs/P2_CLEAN_REPRODUCIBILITY.md](docs/P2_CLEAN_REPRODUCIBILITY.md)에 기록한다.

현재 target/host 검증 상태와 P3 차단 조건의 요약은
[docs/TEST_STATUS.md](docs/TEST_STATUS.md)에 기록한다.

P3 첫 clear/readback test의 hardware-independent expected-pixel, checksum 및
mismatch oracle은 [docs/P3_REFERENCE_ORACLE.md](docs/P3_REFERENCE_ORACLE.md)에
있다. 이 oracle은 MGA에 접근하지 않는다.

future P3 clear/triangle request가 raw VRAM address를 받지 않도록 하는 geometry-only
command envelope은 [docs/P3_COMMAND_ENVELOPE.md](docs/P3_COMMAND_ENVELOPE.md)에
정의한다. 현재 P2 MiG ABI에는 아직 연결하지 않는다.
