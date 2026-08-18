# R6 — H1 기준 mode-transaction 입력의 정직한 구성

기준일: 2026-08-18
결정: operator — "H1로 정책층 정직하게 개작".

`OSMGABeginModeTransaction`의 preflight은 `OSMGAR2PhysicalProfile`(→R3 mode
review 경유)과 `OSMGARecoveryMatrix`가 유효할 것을 요구한다. 두 검증기는
attestation 방식(caller가 `*_verified` 플래그/참조를 세우면 신뢰)이며 원래
계획(물리 VRAM 실측 + Installer.app atomic 워크플로) 기준으로 설계됐다. H1
방법론에서는 증거의 성격이 다르므로, 아래 매핑대로 **진실된 값·참조**로 구성한다.
플래그를 세운다는 것은 "H1에서 이에 상응하는 증거가 존재함"을 뜻하며, 참조 문자열은
그 증거가 물리 실측이 아니라 operator-directed 보수 배치임을 그대로 밝힌다.

## R2 physical profile (operator-directed 16 MiB 보수 배치)

| 필드 | 값 | 참조(진실된 근거) |
| --- | --- | --- |
| evidence_mask BOARD_ID | set | PCI 102b:0525 rev85 실측(S0 probe, `H1_HARDWARE_INTERROGATION_DECISION.md`) |
| board_evidence_reference | (문자열) | "PCI 102b:0525 G450, func 04:00.0, S0 probe; physical P/N marking 미대조" |
| evidence_mask CROSSCHECK | set | `R2.1` 독립 자료(FreeBSD DRM, X.Org MGA 2.0.0, Linux UAPI) |
| crosscheck_evidence_reference | (문자열) | "R2.1 BSD/Linux/X.Org family cross-check; G400/G450 family envelope" |
| evidence_mask VRAM_TYPE | set | G450 family = DDR(공개 자료). 이 보드 물리 확인 아님 |
| vram_evidence_reference | (문자열) | "G450 family DDR envelope (R2 catalogue); not physical board measurement" |
| vram_type | DDR_SDRAM | family 특성 |
| evidence_mask VRAM_SIZE | set | **operator-directed 16 MiB** floor. FB aperture 32 MiB(S1)로 ≥16 MiB 주소공간 확인. S3 실측 보류 |
| physical_vram_bytes | 16*1024*1024 | 보수 배치값(측정 아님) |
| evidence_mask RAMDAC_LIMIT | set | family 360/230 MHz envelope(R2.1) |
| ramdac_evidence_reference | (문자열) | "G450 family RAMDAC envelope ~360 MHz (R2.1); board-exact limit 미확정" |
| applicable_ramdac_khz | 360000 | family envelope 상한(보수적으로 mode 162 MHz는 그 안) |

정직성: 참조 문자열이 "operator-directed / family envelope / not physical
measurement"임을 명시한다. 이는 `TEST_STATUS`의 G2 상태(operator 16M floor
pass, 물리 미측정)와 모순되지 않는다 — 배치 입력이지 물리 증거 상향이 아니다.

## Recovery matrix (VGA 기준선, config-edit 활성화)

matrix의 후보수(original 1/0, recovery 0/1, failure 1/0)는 H1에 그대로 맞는다:
- **original** snapshot = VGA가 display owner인 현재 부팅 설정 (VGA=1, replacement=0)
- **recovery** snapshot = 우리 드라이버가 active인 활성화 설정 (VGA=0, replacement=1)
- **failure** snapshot = VGA로 되돌린 복구 설정 (VGA=1, replacement=0)

boolean 플래그의 H1 의미(코드 주석·이 문서로 문서화):

| 필드 | H1에서의 진실된 의미 | 근거 |
| --- | --- | --- |
| snapshot.bundle_verified | 각 설정의 bundle/미존재가 확인됨 | build artifact + Active Drivers 설정 |
| snapshot.instance_table_verified | System.config `Active Drivers` 상태가 확인됨 | 기준선 캡처(`R5_VGA_RECOVERY_REHEARSAL_RUN_SHEET.md`) |
| snapshot.rollback_instructions_verified | telnet 복구 편집 절차가 문서화됨 | 런시트 복구 절차 |
| atomic_install_verified | 활성화가 **단일 원자적 table 편집**(Active Drivers 한 줄)임 | config-edit 모델. Installer.app 아님(H1) |
| installer_rollback_verified | **config-edit 되돌림**(Active Drivers→VGA)이 리허설됨 | `R5-VGA-20260818-A` PASS |
| failure_original_boot_verified | VGA cold-boot 복구가 실증됨 | `R5-VGA-20260818-A` PASS(telnet ≤60s, Workspace GUI) |
| independent_recovery_channel_verified | telnet(Pro1000) 복구 채널 | `R5-VGA-20260818-A`. 단 "깨진 display 부팅서 telnet 생존"은 가정 |

정직성: 필드 이름이 "installer"를 말하지만 H1 활성화는 config-edit이다. 이
문서와 드라이버 코드 주석이 그 remap을 명시하므로, 플래그는 "H1에서 상응 복구
증거가 존재함(단 Installer가 아니라 config-edit + telnet + VGA)"을 진실되게
attest한다. `atomic_install`은 "설치의 원자성"을 "단일 table-edit의 원자성"으로
읽는다.

## 하드웨어 안전장치는 불변

이 입력 개작은 transaction의 하드웨어 시퀀싱/안전을 바꾸지 않는다: bounded PLL
lock poll, CRTC readback 검증, 4-stage rollback(display/PLL/VGA-safe/superclass)은
그대로 강제된다. 개작은 preflight의 "원래계획 전용 증거 요구"를 H1의 상응 증거로
정직하게 충족시키는 것에 한정된다.

## 코드 배치

드라이버(`OpenStepMGAReplacementDisplay`)에 `osmgaBuildH1R2Profile()`와
`osmgaBuildH1RecoveryMatrix()` 헬퍼를 두고, 각 헬퍼 상단에 위 매핑을 주석으로
복기한다. 참조 문자열은 위 표의 문자열을 그대로 사용한다.
