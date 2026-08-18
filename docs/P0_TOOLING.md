# P0.4 — Target Tooling Protocol

## 재사용 도구

이 workspace의 root `tools/`는 `openstep-intel1000` driver 개발에서 검증된
도구를 재사용한다. Matrox 작업을 위해 복사본을 만들지 않는다.

| 도구 | 사용 시점 | 역할 | 실패 시 fallback |
| --- | --- | --- | --- |
| `nxrun.sh` | 전 단계 | telnet/csh command, `kl_util`, 상태 회수 | 재접속; GUI daemon에 의존하지 않음 |
| `nx.sh` | build/GUI test | gcdsd 경유 command, get/put, GUI smoke | `nxrun.sh`로 상태 확인 |
| `nx-mount.sh` | source 변경 뒤, P1+ | `/ndrv` NFS mount/remount, `noac` cache policy | telnet으로 mount 상태 조사 |
| `nx-logcatch.sh` | 모든 LKS load 전 | `/usr/adm/messages`를 NFS에 즉시 기록 | 마지막 NFS log를 보존하고 재부팅 |
| `nx-install-driver.sh` | bundle build 후 | target `/tmp` build, reloc size 검증, 안전 설치 | install 전 build-only `-n` 수행 |
| `check-env.csh` | 새 boot/dev env | DriverKit headers, tools, compiler 점검 | 결과를 P1 report에 첨부 |

## NFS 운용

현재 gnfsd 수정 후에는 NFS를 source와 log evidence의 기본 경로로 쓴다.

- build 전 host source 변경이 target에 보이지 않으면 `nx-mount.sh`로 remount한다.
- `noac` mount는 source freshness를 우선한다. 대량 build가 느려질 수 있으므로
  불필요한 full-tree scan은 피한다.
- LKS hang 직전 log도 회수할 수 있도록 `nx-logcatch.sh start`를 load보다 먼저
  실행한다.
- gnfsd/NFS가 일시적으로 실패해도 kernel driver 시험을 반복하지 않는다. 먼저
  telnet으로 target 상태를 확인하고 NFS를 복구한다.

## GCD 운용

- GCD는 GUI smoke test와 target file transfer에 사용한다.
- P1/P2의 kernel probe는 GCD availability와 무관해야 한다. 상태와 kernel
  loader 제어의 기준 경로는 telnet이다.
- GCD daemon이 재부팅 뒤 사라졌을 때는 target terminal에서 `gcdsd`를 시작한
  뒤에만 `nx.sh` GUI command를 사용한다.

## LKS 설치 규칙

1. source는 NFS `/ndrv`에서 읽고, target build는 `/tmp/<bundle>`에서 한다.
2. 최초에는 `nx-install-driver.sh <bundle> -n`으로 build output만 확인한다.
3. install 전 kernel log collector를 시작한다.
4. build/installed `*_reloc` byte size가 일치하지 않으면 설치본을 사용하지
   않는다.
5. driverLoader automatic registration과 `InstanceN.table` 생성은 P1의
   read-only path가 안정된 뒤에 별도 결정한다.

`MatroxMGA`는 현재 화면을 담당하므로 이 도구로 unload, reinstall, replace하지
않는다. `OpenStepMGAProbe`와 `OpenStepMGAService`만 독립 bundle로 다룬다.
