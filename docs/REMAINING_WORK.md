# 드라이버 잔여 작업

기준일: 2026-08-19
상태: 디스플레이 드라이버 자체는 **실기에서 완성 동작**. 아래는 다음 stage(3D)로
넘어가기 전에 남겨두는 잔여 항목이며, **어느 것도 현재 동작을 막지 않는다.**

정본 참조: 현재 상태는 `TEST_STATUS.md`, 인수인계는 `../HANDOFF.md`,
드라이버 사용법은 `../OpenStepMGAReplacementDisplay/README.md`.

## 0. 왜 지금 멈추는가

남은 항목은 전부 (a) 하드웨어가 없어 확인 불가, (b) 이득이 측정되지 않아
착수 근거가 없음, (c) 순수 정리 중 하나다. 3D 작업이 드라이버 구조를 크게
바꿀 가능성이 있으므로, 지금 손대면 두 번 하게 되는 것들이 섞여 있다.

## 1. 기능 — 미착수, 근거 있음

### 1-1. 두 번째 head (dual head)
G450은 dual head지만 secondary CRTC/DAC 경로를 한 번도 건드리지 않았다.
`docs/R6_G400_G450_REGISTER_DIVERGENCE.md`에 레지스터 차이가 일부 정리돼
있으나 secondary 계열은 조사 자체가 미완이다.
- **막는 것**: 없음. 순수 미착수.
- **필요 조건**: 두 번째 모니터 실물.

### 1-2. VRAM 자동 검출
`MGA Memory Size`는 보수적 고정 16 MiB다. 실제 보드는 32 MiB일 가능성이
있으나(`R2` 감사의 `0d43` → G45FMDVP32DSF 후보) **한 번도 실측하지 않았다.**
- **막는 것**: 없음. 16 MiB는 안전 하한.
- **주의**: 오프스크린 VRAM 창(S4a)이 이 값에 의존한다. 늘리려면 S4a의
  가드 범위도 함께 재검증해야 한다.

### 1-3. DDC/EDID 런타임 사용
`edid/OpenStepMGAEDID.c`는 빌드에 들어가 있으나 모드 선택에 쓰이지 않는다.
모드는 config-table 문자열로만 정해진다.
- **막는 것**: 없음. 고정 모드 목록으로 충분히 동작한다.
- **가치**: 낮음. 5×5 조합이 이미 Configure에서 선택 가능하다.

### 1-4. G400 지원
`chipIsG450`는 PCI revision ≥ `MGA_G450_MIN_REVISION`로만 갈린다. G400은
미착수이고 실물도 없다. PLL 계열이 다르므로 코드 분기가 필요하다.

## 2. 성능 — 이득이 측정되지 않았다

### 2-1. 2D 가속을 실제 화면에 쓰는 길
Storm 엔진은 동작이 실증됐지만(S1/S2/S3a) **평상시에는 한 번도 쓰이지
않는다.** WindowServer가 `IODisplayDoBlit`을 보내지 않기 때문이다(측정으로
확정, `S3B_PREP_INSTRUMENTATION_PLAN.md`).
- 우리가 가시 프레임버퍼에 직접 블릿하면 그려지기는 하나, damage 장부에
  없으므로 WindowServer가 리페인트할 때 지워진다.
- **결론**: 창 안 가속에는 협조 프로토콜이 없다. 전체화면 소유 구조에서만
  의미가 있으며, 그것은 3D stage의 주제다.

### 2-2. 커서 하드웨어 가속
G450에는 하드웨어 커서가 있으나 OPENSTEP의 커서는 **소프트웨어 커서**이고
(IDA로 확인: 프레임버퍼 대상 CPU 복사 루프), 그 락은 `@private`이라 공유할 수
없다. 하드웨어 커서로 바꾸려면 커널 쪽 커서 경로를 우회해야 한다.
- **막는 것**: 구조적. 이득도 측정되지 않았다.

## 3. 정리 — 미룬 것

### 3-1. 폐기 산출물 미삭제
`protocol/`, `profile/`, `recovery/`, `test/openstep-mga-lease-*`,
`test/openstep-mga-*mig*` 등 P2 MiG/R1 복구 매트릭스 시절 산출물이 남아 있다.
현재 드라이버는 이들을 링크하지 않는다(빌드 대상은 `OpenStepMGAManualConfig.c`
+ `OpenStepMGAEDID.c`뿐).
- **삭제하지 않은 이유**: 여러 `docs/` 문서가 이들을 참조하므로 지우면 링크가
  깨진다. 대신 `TEST_STATUS.md` 머리말과 `HANDOFF.md`에 "폐기된 경로"임을
  명시해 두었다.
- **하려면**: 문서 링크를 먼저 정리한 뒤 한 번에.

### 3-2. 초기 계획 문서군의 시제
`P0`~`P3`, `R1`~`R4`, `G1` 문서들은 당시 계획을 현재형으로 서술한다. 개별
수정 대신 상위 문서에 "역사적 기록" 표시만 달아 두었다.

### 3-3. 빌드 스크립트의 `MOUNTPT`
`/ndrv`가 wedge되면 `nx-mount.sh`가 `/ndrv2`로 폴백하므로 매번
`MOUNTPT=/ndrv2`를 넘겨야 한다. 자동 감지로 바꿀 수 있다. 사소함.

### 3-4. `gcdsd` 부팅 자동 기동
재부팅할 때마다 `tools/nx-daemon.sh start`와 `nx-mount.sh`를 손으로 돌린다.
이 저장소가 아니라 GCDS 쪽 사안.

### 3-5. `AR3` — 조사 완료. 남은 범위는 ILOAD 계열뿐

X.Org DDX 색상 확장 경로의 `OUTREG(MGAREG_AR3, 0); /* crashes occasionally
without this */`(`mga_storm.c:1768`, `:1813`)를 보고 우리도 방어적으로
0을 쓰려 했으나, 조사 결과 **그럴 필요가 없고 쓰지 않는 것이 맞다.**

`AR3`는 **BITBLT/ILOAD 계열의 소스 주소지정 레지스터**다. DDX의 복사 경로가
값을 넣고(`:1390`, `:1398`, `:1466`, `:1479`, `:1491`), 사다리꼴 경로는 전혀
건드리지 않으며(`:1566`~`:1590`), 둘은 같은 가속 테이블에 등록돼 X가 수시로
번갈아 부른다(`:647`, `:656`). 즉 **stale `AR3`로 사다리꼴을 그리는 것이
출하 동작**이다. 0으로 미는 곳은 색상 확장/ILOAD 계열뿐이다.

원본 OPENSTEP `MatroxMGA`는 참고가 되지 않았다 — 드로잉 엔진을 아예 쓰지
않는다(`AR3`·`FXBNDRY` 참조 0건, 검색 방법은 CRTC/DAC 대조군으로 검증).

**남은 범위**: 우리가 색상 확장/ILOAD를 구현하게 되면 그때는 `AR3` 정리가
필요하다. 그 전까지는 해당 없음. 정본: `docs/D3_2_SLOPED_TRAPEZOID_PLAN.md`
§1-0-1.

### 3-6. 깊이 값 클램프 (3D 구현 시 필수)

깊이는 16비트이므로 `DR0`의 유효 구간은 `[0, 0x7FFF8000]`이다. 그 밖에서
하드웨어 동작이 **일관되지 않다** — `0x80000000`은 `0xffff`로 포화하는데
`0xFFFFFFFF`는 0을 준다(실측, `D3_3_DEPTH_PLAN.md` §10-3).

**드라이버가 직접 자른다.** Mesa에서 온 깊이를 `[0, 0xFFFF]`로 클램프한 뒤
`<< 15`한다. 하드웨어에 맡기지 않는다.

- **막는 것**: 없음(아직 Mesa 연결 전).
- **닫는 조건**: 3D 경로에 클램프가 들어가면 끝.

### 3-7. 임시로 꺼둔 프로브 (되돌릴 것)

syslog가 버스트를 삼켜 D3-3b 첫 실행 데이터를 통째로 잃었다. 대응으로:

- **저장소의** `OpenStepMGAReplacementDisplay/Instance0.table`과
  `Default.table`에서 `Storm 2D Test`/`DMA Ring Test`/`WARP Test`를 `No`로
  바꿨다.

  실기 파일만 고쳤더니 `install-matrox-driver.sh`가 매번 저장소 원본으로
  덮어써서(`tools/install-matrox-driver.sh:20`) 다음 설치에 되살아났고,
  그 탓에 D3-4b 결과를 한 번 더 잃었다. **실기가 아니라 저장소를 고쳐야
  한다.**
- `enterLinearMode`에서 `runRasterInterpolationTest`와
  `runDstorgOriginTest` 호출을 주석 처리했다.

둘 다 **검증이 끝나 문서에 기록된** 프로브다. 3D 브링업이 끝나면 되돌린다.

- **닫는 조건**: 두 호출의 주석 해제 + config 스위치 복원.
- **함께 확인할 것**: 그 사이에 `osmgaDmaIndex`가 `SECADDRESS`/`SECEND`/
  `SETUPADDRESS`/`SETUPEND`를 거부하도록 바뀌었다
  (`M1_MESA_BRIDGE_PLAN.md` §4-4). D1 링 시험이 여전히 통과하는지 본다.

### 3-8. cdevsw 매핑의 언로드/재로드 수명 — **미해결**

`VRAM Mmap`이 켜지면 cdevsw 항목을 등록하고, M1-0부터는 DMA 링 64 KiB를
`IOMallocLow`로 잡아 **해제하지 않는다**.

- 드라이버를 다시 로드할 때마다 컨벤셔널 메모리가 샌다.
- **cdevsw 항목이 물리 메모리보다 오래 살면** 열린 매핑이 재사용된 메모리를
  가리킬 수 있다. `addToCdevswFromDescription`이 언로드 때 제거되는지
  확인한 적이 없다.

이 환경에서 언로드/재로드는 검증되지 않았다([[openstep-zero-byte-bundle]]의
kern_loader 함정 참조).

- **막는 것**: 없음. 개발 스위치이고 기본값은 `No`다.
- **닫는 조건**: 언로드 후 `/dev` 항목이 사라지는지 확인하거나, 언로드를
  지원하지 않는다고 명시한다. 정본: `M1_MESA_BRIDGE_PLAN.md` §9-4.

## 4. 검증 — 회귀를 자동화하지 않았다

지금까지의 실기 검증은 전부 **수동 + 재부팅**이다. 부팅 로그 grep과 두 개의
유저스페이스 프로브(`openstep-mga-stats-probe.m`,
`openstep-mga-vram-mmap-probe.m`)가 사실상의 회귀 도구다.
- 모드 5×5 = 25개 조합 중 실제로 화면을 띄워 본 것은 일부다. 나머지는
  operator 판단으로 생략했다(2026-08-19: "다른 해상도는 굳이 검토하지 않아도
  괜찮을거 같습니다").
- **하려면**: 부팅 1회당 조합 1개라 비용이 크다. 이득 대비 낮음.

## 5. 다음 stage로 넘길 때의 전제

3D 작업(`S5_HW3D_DMA_FEASIBILITY.md`)이 이 드라이버에서 이어받는 것:

| 이어받는 것 | 상태 |
| --- | --- |
| BAR0/BAR1 매핑 | 동작 |
| Storm 2D 엔진 구동 시퀀스 | 실증(S1/S2) |
| 유저스페이스 → 커널 RPC 경로 | 실증(S3a, `getIntValues`/`setIntValues`) |
| 오프스크린 VRAM의 유저 태스크 매핑 | 실증(S4a, cdevsw `d_mmap`) |
| 계측 19종 | 동작 |
| 개발 스위치 2종 + Configure 인스펙터 | 동작(C1) |
| **연속 물리 메모리(DMA 링용)** | **`IOMallocLow` 64 KiB — S5 §3-2에서 확인** |

3D가 새로 만들어야 하는 것은 `S5_HW3D_DMA_FEASIBILITY.md` §5에 있다.

## 6. 이 문서가 결정하지 않은 것

- 위 항목 중 무엇을 언제 할지(operator 판단)
- 3D stage가 드라이버 구조를 어떻게 바꿀지 — 바뀐 뒤에 3-1을 하는 편이
  낫다고 보지만 확정은 아니다
