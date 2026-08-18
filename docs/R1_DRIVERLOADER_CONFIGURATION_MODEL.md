# R1 — OPENSTEP Driver-Loader Configuration Model

기준일: 2026-08-18

## 결론

replacement display driver의 sole-owner recovery boot는 `Default.table` 파일을
하나 더 만드는 것으로 성립하지 않는다. OPENSTEP에서 boot-time candidate를
결정하는 것은 Configure/`driverLoader`가 생성한 `InstanceN.table` 상태다.
따라서 이 프로젝트의 “boot profile”은 다음 세 요소를 함께 보존·review한
**configuration snapshot**을 뜻한다.

1. installed bundle과 complete relocatable artifact
2. 해당 bundle의 `Default.table` 및 `InstanceN.table`
3. Configure가 만든 old/new instance selection과 driver-loader result

R1의 목적은 snapshot 두 개를 동시에 active로 만들지 않는 것이다. production
snapshot은 `MatroxMGA`만, future recovery snapshot은 replacement만 exact MGA PCI
function의 display candidate가 되어야 한다. `Default.table` matching precedence나
`kl_util -l` result에 의존해서 이 invariant를 대신할 수 없다.

## 원전/실기 근거

| 근거 | 확인된 사실 | R1 적용 |
| --- | --- | --- |
| local `doc/driverkit.md` §실제 활성화 구조 | 별도 OPENSTEP 실기에서 `driverLoader a`와 active `Instance0.table` 관계를 관찰 | target-specific startup path가 확인되기 전까지 일반적 model로만 사용 |
| 동일 문서 | `Default.table`은 device catalogue, `InstanceN.table`은 actual configured instance; `+probe:`는 `driverLoader`가 호출 | replacement `Default.table`만으로 G1 통과 불가 |
| 동일 문서의 실기 관찰 | `/private/Devices`는 `/private/Drivers/i386`와 같은 directory의 link | 두 path를 별 storage로 취급하거나 한쪽만 검사하면 안 됨 |
| official QVision DriverHelp | display adapter를 바꾸기 전 Configure에서 Default VGA Adapter로 변경하도록 안내 | replacement cutover는 GUI/Configure recovery 절차와 분리 불가 |
| R0/R1 target read-only evidence | current owner는 `MatroxMGA`; replacement artifact는 두 production path에 없음 | current production snapshot은 안전하지만 recovery snapshot은 미작성 |

target-native file collector는 `/etc/rc`의 guarded startup block에서
`/usr/etc/driverLoader a >/dev/console 2>&1`를 확인했다. 따라서 이 target에서도
boot-time configuration은 `driverLoader a`를 거친다는 근거가 있다. 앞선
`not-found` output은 구형 OPENSTEP `egrep`가 POSIX `[[:space:]]` class를
해석하지 못한 script portability 문제였고, collector는 literal `grep`으로
교정했다.

추가 target-native table snapshot(`R1-20260818-B`)은 original bundle의
`Default.table`이 legacy `0x0519102B` generic entry인 반면, 실제 active
`Instance0.table`은 `0x0525102B`, `Dev:0 Func:0 Bus:4`,
`MatroxMGAG400_16MB`를 명시함을 보였다. 즉 `Default.table`은 installed bundle의
catalogue이고 `Instance0.table`은 current function/mode selection이라는 이 문서의
model을 실기 상태가 뒷받침한다. future replacement review는 둘 중 하나만 복제하거나
편집하는 방식으로 진행할 수 없다.

## configuration snapshot contract

| snapshot | MGA display candidate | required instance state | 금지 상태 |
| --- | --- | --- | --- |
| P-original | `MatroxMGA` only | existing `MatroxMGA.config/Instance0.table` preserved | replacement `InstanceN.table` active |
| P-recovery | replacement only | old Matrox instance is explicitly excluded; replacement instance is the only candidate | Matrox and replacement instances coexisting or loader order dependency |
| P-failure | original `MatroxMGA` only | P-original restoration verified | replacement instance left active after failure |

P-recovery는 G1/G2/G3/G4 및 operator 승인 전에는 만들지 않는다. 이 문서는
snapshot contract만 정의하며 current production filesystem을 수정하지 않는다.

이 three-snapshot contract의 candidate-count/evidence completeness rule은
`R1_RECOVERY_MATRIX_POLICY.md`의 pure-C validator에도 고정되어 있다. 현재
target에 적용할 입력값은 없으며, synthetic test pass가 G1을 통과시키지 않는다.

## future Configure run의 required evidence

future operator run에서, action 전후 각각 다음 정보를 raw address 없이 기록한다.

| check | P-original | P-recovery | P-failure |
| --- | --- | --- | --- |
| configured display bundle name | `MatroxMGA` | replacement | `MatroxMGA` |
| relevant `InstanceN.table` names/count | expected old only | expected new only | expected old only |
| old/new matching candidate count | 1 / 0 | 0 / 1 | 1 / 0 |
| `driverLoader`/boot verdict | original display | replacement result | original display restored |
| screen + independent recovery | stable | separately evaluated | stable |

한 쪽의 candidate count가 `unknown`이면 verdict는 `pending`이다. zero-byte or
partial bundle, manual `cp`, direct instance-file editing, runtime `MatroxMGA`
unload는 허용하지 않는다. complete package installation과 Configure-mediated
instance selection이 모두 review된 뒤에만 P-recovery의 실기 작업을 별도로
승인할 수 있다.

## G1에 아직 필요한 것

1. Configure의 display-driver replacement workflow를 original-only rehearsal로
   확인한다. 이때 replacement bundle을 설치하지 않는다.
2. P-original recovery path를 cold reboot로 재현한다(R5 run sheet).
3. G2/G3가 통과한 뒤에만 replacement bundle의 exact matching table와
   Configure-visible metadata를 설계한다.
4. package installer의 atomicity/rollback evidence를 review한다. direct target
   directory copy는 허용하지 않는다.

이 문서는 G1을 통과시키지 않는다. 특히 “Default VGA Adapter” 변경은 화면
ownership에 영향을 줄 수 있으므로 operator의 명시적 실행 승인 없이는 시도하지
않는다.
