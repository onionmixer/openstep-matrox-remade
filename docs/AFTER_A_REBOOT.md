# 재부팅 뒤에 하는 일

`/tmp` 는 `/etc/rc:148` 이 부팅마다 비운다.  그래서 예전에는 재부팅 한 번마다
드라이버 번들과 시험 바이너리 일곱 개를 손으로 다시 짓고, 다시 안전한 곳으로
복사해야 했다.  이제 그러지 않는다.

## 두 줄이면 된다

```sh
./tools/nx-mount.sh                                     # NFS 다시 붙이기
./tools/nxrun.sh 'sh /ndrv/tools/build-matrox-tests.sh' # 필요할 때만
```

두 번째 줄조차 **소스를 고쳤을 때만** 필요하다.  바이너리는 살아남는다.

## 무엇이 어디에 사는가

| 대상 | 경로 | 재부팅 |
|---|---|---|
| 시험 바이너리 7 개 | `/usr/local/nxbuild/bin/` | 살아남는다 |
| 드라이버 빌드 트리 | `/usr/local/nxbuild/OpenStepMGAReplacementDisplay/` | 살아남는다 |
| 설치된 드라이버 | `/private/Drivers/i386/…` | 살아남는다 |
| Mesa 스테이징 | `/usr/local/mesastage/OpenStepMesa342/` | 살아남는다 |
| 가속 libGL | `openstep-matrox-remade/build/mesa/` (NFS) | 살아남는다 |
| NFS 마운트 `/ndrv` | — | **사라진다** (`fstab` 에 항목 없음) |

경로는 `NXBUILD` 로 바꿀 수 있고 기본값은 `etc/site.conf` 기구를 따른다
(`tools/site.sh`).  대상 쪽 스크립트들은 같은 기본값을 각자 들고 있다 —
그쪽에서는 리눅스 환경을 볼 수 없기 때문이다.

## 시험 바이너리

| 이름 | 무엇 | 하드웨어 |
|---|---|---|
| `reach` | 커널 탐침 §1…§65 | **필요** |
| `texdraw` | GL 장면(`dump`, `tile`, `tilebnd`, `perspfar` …) | **필요** |
| `tv` | 검증기 단언 | 불필요 |
| `trh` | 40000 배치 차등 시험 | 불필요 |
| `tcost` | 검증기 비용 | 불필요 |
| `tr` | 빌더 도달범위 하네스 | 불필요 |
| `tc` | 빌더 좌표 하네스 | 불필요 |

## 다시 지어야 할 때

```sh
# 시험만 (몇 초)
./tools/nxrun.sh 'sh /ndrv/tools/build-matrox-tests.sh'

# 드라이버 (재부팅해야 살아난다)
./tools/nxrun.sh 'sh /ndrv/tools/build-matrox-driver.sh'
./tools/nxrun.sh 'sh /ndrv/tools/install-matrox-driver.sh'

# 가속 libGL (백엔드를 고쳤을 때만, 15 분쯤)
./tools/nxrun.sh 'csh /ndrv/openstep-matrox-remade/tools/build-matrox-mesa.csh'
```

Mesa 스테이징은 이제 로컬 디스크에 있으므로 **다시 스테이징할 일이 없다.**
정말 필요하면:

```sh
./tools/nxrun.sh 'csh /ndrv/opennstep-mesa342/build/stage-openstep-mesa342.csh /ndrv'
```

## 경로를 옮기고 확인한 것

빌드 위치를 바꾼 것이 산출물을 바꾸지 않았음을 `cmp` 로 확인했다 — 새 경로에서
지은 드라이버가 설치본과 **바이트 단위로 동일**하다.  유저랜드 시험 셋도 새
위치에서 그대로 통과한다.
