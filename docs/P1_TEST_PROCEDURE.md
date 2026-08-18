# P1 — Read-Only Probe Test Procedure

## Preconditions

1. `P0_TARGET_INVENTORY.md`, `P0_BINARY_BEHAVIOUR.md`,
   `P0_REFERENCE_MATRIX.md`, `P0_TOOLING.md`를 검토한다.
2. 수정된 gnfsd가 실행 중이고 `/ndrv` mount가 정상이어야 한다.
3. `tools/nx-logcatch.sh start`로 NFS kernel logging을 시작한다.
4. `MatroxMGA`가 loaded 상태인지 `kl_util -s MatroxMGA`로 확인한다.
5. GCD 상태는 필수가 아니다. P1의 control path는 telnet이다.

## Build

```
./tools/nx-install-driver.sh openstep-matrox-remade/OpenStepMGAProbe -n
```

build-only가 성공하면 target `/tmp/OpenStepMGAProbe`의 bundle을 유지한다.
P1은 `/private/Devices` installation 또는 `driverLoader` registration이
필요하지 않다.

## Load and evidence

target csh에서 다음을 실행한다.

```
csh -f /ndrv/openstep-matrox-remade/test/run-p1-probe.csh
```

성공 출력은 다음 semantic record를 포함해야 한다.

- `MGA-PROBE begin` 및 `end`.
- `04:00.0`, `vid=102b`, `did=0525`.
- PCI command, IRQ/pin, BAR0/1/2 값.

## Pass criteria

- kernel log가 complete begin/end record를 포함한다.
- `MatroxMGA`가 load된 상태로 유지된다.
- display, telnet, NFS가 모두 정상이다.
- `OpenStepMGAProbe` unload/deregister 후 `kl_util -s`에 남지 않는다.

## 2026-08-18 실행 결과

통과했다.

- target compiler `cc 2.7.2`로 `/tmp/OpenStepMGAProbe` bundle을 build했다.
- target에서 `run-p1-probe.csh`로 load, status 확인, unload, deregister를
  수행했다.
- kernel log에는 `begin`부터 `end`까지의 record가 남았고, device는
  `04:00.0 102b:0525 rev 85`, IRQ 11/pin A로 확인됐다.
- raw BAR 값은 `f8000008 e8200000 e8800000`이었다.
- 기존 `MatroxMGA`는 계속 loaded였고, display/telnet/NFS 이상은 관찰되지
  않았다.

P1은 이 결과로 종료한다. P1 bundle을 `/private/Devices` 또는
`driverLoader`에 등록하지 않았으며, P2 이전에 BAR mapping 시험을 추가하지
않는다. BAR1이 과거 `pcils` 기록과 다르므로 `P0_TARGET_INVENTORY.md`의
재검증 조건이 해소될 때까지 주소 기반 후속 시험은 금지한다.

## Stop criteria

P1 code는 MGA register write를 하지 않으므로 화면 변화가 발생하면 즉시
시험을 중단한다. 재시도하기 전에 NFS log와 `/usr/adm/messages` 마지막
record를 보존하고, 원인을 문서화한다.
